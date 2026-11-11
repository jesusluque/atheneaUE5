// Copyright (c) 2026 jesus luque.
//
// A gaussian cloud from a USD stage: the first UsdVolParticleField3DGaussianSplat
// on it, read through the OpenUSD UE bundles (USDCore). It reads what athenea's
// own reader reads (modules/usd/src/Export.cpp, readParticleFieldRecords):
// linear scales and opacities, (degree + 1)^2 harmonic coefficients a gaussian
// with the constant term first, sRGB colour unless primvars:athenea:splat:linear
// says the stage holds light, and the stage's up axis and metres per unit.

#include "GSplatImporter.h"
#include "GSplatImportData.h"

#if WITH_EDITOR

#include "GaussianData.h"
#include "GaussianSplatInterface.h"
#include "Async/ParallelFor.h"
#include "GaussiansEditorModule.h"
#include "Misc/Paths.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/primvarsAPI.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/half.h"
#include "pxr/base/gf/vec3h.h"
#include "pxr/usd/usdVol/particleField3DGaussianSplat.h"
#include "USDIncludesEnd.h"
#endif

bool GSplatImporter::IsUsdFile(const FString& Filename)
{
	const FString Extension = FPaths::GetExtension(Filename).ToLower();
	return Extension == TEXT("usd") || Extension == TEXT("usda") || Extension == TEXT("usdc");
}

#if USE_USD_SDK

namespace
{
	pxr::UsdVolParticleField3DGaussianSplat FirstSplats(const pxr::UsdStageRefPtr& Stage)
	{
		for (const pxr::UsdPrim& Prim : Stage->Traverse())
		{
			if (Prim.IsA<pxr::UsdVolParticleField3DGaussianSplat>())
			{
				return pxr::UsdVolParticleField3DGaussianSplat(Prim);
			}
		}
		return pxr::UsdVolParticleField3DGaussianSplat();
	}

	pxr::UsdStageRefPtr OpenStage(const FString& Filename)
	{
		return pxr::UsdStage::Open(std::string(TCHAR_TO_UTF8(*Filename)), pxr::UsdStage::LoadAll);
	}
}

// LIGHT IN ONE WORD (athenea's common/packing.slang, packRgb9e5): three 9-bit
// mantissas under one 5-bit exponent; negative and not-a-number are nothing,
// past the top is the top.
static uint32 PackRgb9e5(const FVector3f& Rgb)
{
	const float Top = 65408.0f;
	const FVector3f c(Rgb.X > 0.0f ? FMath::Min(Rgb.X, Top) : 0.0f, Rgb.Y > 0.0f ? FMath::Min(Rgb.Y, Top) : 0.0f,
		Rgb.Z > 0.0f ? FMath::Min(Rgb.Z, Top) : 0.0f);
	const float Largest = FMath::Max3(c.X, c.Y, c.Z);
	if (!(Largest >= 5.9604645e-8f))
	{
		return 0u;
	}
	int32 e = FMath::Max(-16, FMath::FloorToInt(FMath::Log2(Largest))) + 16;
	float Step = FMath::Exp2(float(e - 24));
	if (uint32(FMath::FloorToFloat(Largest / Step + 0.5f)) >= 512u)
	{
		e += 1;
		Step *= 2.0f;
	}
	const uint32 r = FMath::Min(uint32(FMath::FloorToFloat(c.X / Step + 0.5f)), 511u);
	const uint32 g = FMath::Min(uint32(FMath::FloorToFloat(c.Y / Step + 0.5f)), 511u);
	const uint32 b = FMath::Min(uint32(FMath::FloorToFloat(c.Z / Step + 0.5f)), 511u);
	return r | (g << 9) | (b << 18) | (uint32(e) << 27);
}

bool GSplatImporter::StageHasSplats(const FString& Filename)
{
	const pxr::UsdStageRefPtr Stage = OpenStage(Filename);
	return Stage && FirstSplats(Stage);
}

bool GSplatImporter::ImportUsd(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats)
{
	const pxr::UsdStageRefPtr Stage = OpenStage(Filename);
	if (!Stage)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("Failed to open USD stage: %s"), *Filename);
		return false;
	}
	const pxr::UsdVolParticleField3DGaussianSplat Field = FirstSplats(Stage);
	if (!Field)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s has no gaussian ParticleField."), *Filename);
		return false;
	}

	// The earliest value: a cloud whose arrays are sampled has no default.
	const pxr::UsdTimeCode At = pxr::UsdTimeCode::EarliestTime();
	pxr::VtVec3fArray Positions;
	pxr::VtQuatfArray Orientations;
	pxr::VtQuathArray OrientationsHalf;
	pxr::VtVec3fArray Scales;
	pxr::VtFloatArray Opacities;
	pxr::VtVec3fArray Coefficients;
	int Degree = 0;
	Field.GetPositionsAttr().Get(&Positions, At);
	if (!Field.GetOrientationsAttr().Get(&Orientations, At) && Field.GetOrientationsAttr().Get(&OrientationsHalf, At))
	{
		Orientations.reserve(OrientationsHalf.size());
		for (const pxr::GfQuath& Q : OrientationsHalf)
		{
			Orientations.push_back(pxr::GfQuatf(Q));
		}
	}
	Field.GetScalesAttr().Get(&Scales, At);
	Field.GetOpacitiesAttr().Get(&Opacities, At);
	Field.GetRadianceSphericalHarmonicsDegreeAttr().Get(&Degree, At);
	Field.GetRadianceSphericalHarmonicsCoefficientsAttr().Get(&Coefficients, At);

	const size_t Count = Positions.size();
	if (Count == 0 || Orientations.size() != Count || Scales.size() != Count || Opacities.size() != Count)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: a ParticleField needs one position, orientation, scale and opacity a gaussian."), *Filename);
		return false;
	}
	Degree = FMath::Clamp(Degree, 0, 3);
	const size_t PerSplat = size_t(Degree + 1) * size_t(Degree + 1);
	if (Coefficients.size() != Count * PerSplat)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: %llu harmonic coefficients for %llu gaussians at degree %d."),
			*Filename, uint64(Coefficients.size()), uint64(Count), Degree);
		return false;
	}

	// UE is left-handed and z up, in centimetres. A y-up stage comes in as
	// (x, z, y) and a z-up one as (x, -y, z): each a mirror, as a change of
	// handedness needs.
	const bool bZUp = pxr::UsdGeomGetStageUpAxis(Stage) == pxr::UsdGeomTokens->z;
	ImportData.Swizzle = bZUp
		? TArray<EGSplatSwizzleAxis>{ EGSplatSwizzleAxis::X, EGSplatSwizzleAxis::Yn, EGSplatSwizzleAxis::Z }
		: TArray<EGSplatSwizzleAxis>{ EGSplatSwizzleAxis::X, EGSplatSwizzleAxis::Z, EGSplatSwizzleAxis::Y };
	ImportData.ScaleFactor = float(pxr::UsdGeomGetStageMetersPerUnit(Stage) * 100.0);

	bool bLinear = false;
	const pxr::UsdAttribute SaidLinear = Field.GetPrim().GetAttribute(pxr::TfToken("primvars:athenea:splat:linear"));
	bLinear = SaidLinear && SaidLinear.Get(&bLinear) && bLinear;
	ImportData.bApplyGammaCorrection = !bLinear;
	ImportData.bBaseColorIsRGB = false;
	ImportData.bOpacityAsAlpha = true;

	pxr::VtVec3fArray Normals;
	{
		const pxr::UsdGeomPrimvar Primvar = pxr::UsdGeomPrimvarsAPI(Field.GetPrim()).GetPrimvar(pxr::TfToken("athenea:splat:normal"));
		if (!(Primvar && Primvar.Get(&Normals, At) && Normals.size() == Count))
		{
			Normals.clear();
		}
	}

	// A TX cloud's transfer: the direct half (9 or 16 values a gaussian), the
	// indirect one (three times that) where it was kept, and the metallic the
	// body is scaled by. The reflected field and the open directions are not
	// read yet.
	pxr::VtFloatArray TransferDirect, TransferIndirect, Metallic;
	uint32 TransferCount = 0;
	bool bZonal = false;
	{
		const pxr::UsdGeomPrimvarsAPI Primvars(Field.GetPrim());
		const pxr::UsdGeomPrimvar Direct = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:transferDirect"));
		if (Direct && Direct.Get(&TransferDirect, At))
		{
			const int Element = Direct.GetElementSize();
			if ((Element == 9 || Element == 16) && TransferDirect.size() == Count * size_t(Element))
			{
				TransferCount = uint32(Element);
			}
		}
		// A zonal transfer (two lobes in each gaussian's frame, ten values) is
		// read where there is no transferDirect: kept as records of nine, its
		// ten values at the start, which the shader turns each frame.
		const pxr::UsdGeomPrimvar Zonal = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:transferZonal"));
		pxr::VtFloatArray ZonalValues;
		if (TransferCount == 0 && Zonal && Zonal.Get(&ZonalValues, At) && ZonalValues.size() == Count * 10)
		{
			TransferCount = 9;
			bZonal = true;
			TransferDirect.resize(Count * 9);
			TransferIndirect.assign(Count * 27, 0.0f);
			for (size_t i = 0; i < Count; ++i)
			{
				// Nine here, the tenth as the first indirect value.
				for (int v = 0; v < 9; ++v)
				{
					TransferDirect[i * 9 + v] = ZonalValues[i * 10 + v];
				}
				TransferIndirect[i * 27] = ZonalValues[i * 10 + 9];
			}
		}
		const pxr::UsdGeomPrimvar Indirect = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:transferIndirect"));
		if (!bZonal && !(TransferCount > 0 && Indirect && Indirect.Get(&TransferIndirect, At)
			&& TransferIndirect.size() == Count * 3 * size_t(TransferCount)))
		{
			TransferIndirect.clear();
		}
		const pxr::UsdGeomPrimvar Metal = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:metallic"));
		if (!(Metal && Metal.Get(&Metallic, At) && Metallic.size() == Count))
		{
			Metallic.clear();
		}
	}

	// What a TX cloud's polish reflects with, where the cloud carries it: each
	// primvar one value (or one triple) a gaussian; a missing one takes the
	// value athenea gives it (plainLobes).
	const pxr::UsdGeomPrimvarsAPI Primvars(Field.GetPrim());
	auto Floats = [&](const char* Name) {
		pxr::VtFloatArray Out;
		const pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken(Name));
		if (!(P && P.Get(&Out, At) && Out.size() == Count))
		{
			Out.clear();
		}
		return Out;
	};
	auto Triples = [&](const char* Name) {
		pxr::VtVec3fArray Out;
		const pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken(Name));
		if (!(P && P.Get(&Out, At) && Out.size() == Count))
		{
			Out.clear();
		}
		return Out;
	};
	const pxr::VtFloatArray Roughness = Floats("athenea:splat:roughness");
	const pxr::VtFloatArray SpecularWeight = Floats("athenea:splat:specularWeight");
	const pxr::VtVec3fArray SpecularColor = Triples("athenea:splat:specularColor");
	const pxr::VtFloatArray SpecularIor = Floats("athenea:splat:specularIor");
	const pxr::VtFloatArray CoatWeight = Floats("athenea:splat:coatWeight");
	const pxr::VtFloatArray CoatRoughness = Floats("athenea:splat:coatRoughness");
	const pxr::VtFloatArray CoatIor = Floats("athenea:splat:coatIor");
	const pxr::VtFloatArray CoatDarkening = Floats("athenea:splat:coatDarkening");
	const pxr::VtVec3fArray SheenColor = Triples("athenea:splat:sheenColor");
	const pxr::VtFloatArray SheenRoughness = Floats("athenea:splat:sheenRoughness");
	const pxr::VtFloatArray MaterialRoughness = Floats("athenea:splat:materialRoughness");
	const pxr::VtFloatArray Transmission = Floats("athenea:splat:transmission");
	pxr::VtIntArray ThinWalled;
	{
		const pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:thinWalled"));
		if (!(P && P.Get(&ThinWalled, At) && ThinWalled.size() == Count))
		{
			ThinWalled.clear();
		}
	}
	float GlassIor = 0.0f;
	{
		const pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:ior"));
		if (P && P.GetInterpolation() == pxr::UsdGeomTokens->constant)
		{
			P.Get(&GlassIor, At);
		}
	}
	// The shape operator is three values a gaussian (elementSize 3).
	pxr::VtFloatArray Curvature;
	{
		const pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:curvature"));
		if (!(P && P.Get(&Curvature, At) && Curvature.size() == Count * 3))
		{
			Curvature.clear();
		}
	}
	// What each gaussian gives off, linear rgb (athenea's Export writes color3f[]).
	pxr::VtVec3fArray Emission;
	{
		const pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:emission"));
		if (!(P && P.Get(&Emission, At) && Emission.size() == Count))
		{
			Emission.clear();
		}
	}
	pxr::VtFloatArray Reflected;
	{
		// 48 values a gaussian, beside the indirect half only (transfer_layout.slang).
		const pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:transferReflected"));
		if (!(P && P.Get(&Reflected, At) && Reflected.size() == Count * 48))
		{
			Reflected.clear();
		}
	}
	pxr::VtIntArray SchlickMetal, ShadowBits;
	{
		const pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:schlickMetal"));
		if (!(P && P.Get(&SchlickMetal, At) && SchlickMetal.size() == Count))
		{
			SchlickMetal.clear();
		}
	}
	uint32 CellWords = 0;
	{
		// 8 or 32 words a gaussian: a TX transfer's cells over the whole sphere.
		// The first transfer's 2 words cover the half a gaussian faces and are
		// not read.
		const pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:shadowBits"));
		if (P && P.Get(&ShadowBits, At))
		{
			const int Element = P.GetElementSize();
			if ((Element == 8 || Element == 32) && ShadowBits.size() == Count * size_t(Element))
			{
				CellWords = uint32(Element);
			}
		}
	}

	ImportData.NumSplats = int32(Count);
	ImportData.bHasNormals = !Normals.empty();
	ImportData.bHasHighOrderSH = Degree > 0;
	// athenea: THE RELIT ROUTE'S MATERIAL -- a gaussian's metallic and
	// roughness (primvars:athenea:splat:metallic, :roughness), what the
	// GBuffer route lights it with beside its normal.
	pxr::VtFloatArray RelitMetallic, RelitRoughness;
	{
		const pxr::UsdGeomPrimvarsAPI Pv(Field.GetPrim());
		const pxr::UsdGeomPrimvar M = Pv.GetPrimvar(pxr::TfToken("athenea:splat:metallic"));
		const pxr::UsdGeomPrimvar R = Pv.GetPrimvar(pxr::TfToken("athenea:splat:roughness"));
		if (!(M && R && M.Get(&RelitMetallic, At) && R.Get(&RelitRoughness, At) && RelitMetallic.size() == Count && RelitRoughness.size() == Count))
		{
			RelitMetallic.clear();
			RelitRoughness.clear();
		}
	}
	ImportData.bHasMetallicRoughness = !RelitMetallic.empty();

	Splats.bHasNormals = ImportData.bHasNormals && ImportData.bImportNormals;
	Splats.bHasHighOrderSH = ImportData.bHasHighOrderSH && ImportData.bImportHighOrderSH;
	Splats.bHasMetallicRoughness = ImportData.bHasMetallicRoughness && ImportData.bImportMetallicRoughness;
	Splats.Resize(uint32(Count));

	const TArray<EGSplatSwizzleAxis>& Swizzle = ImportData.Swizzle;
	const float ScaleFactor = ImportData.ScaleFactor;
	const uint32 NumSH = FGaussianSplatData::NumHighOrderSH;
	for (size_t i = 0; i < Count; i++)
	{
		const pxr::GfVec3f& P = Positions[i];
		Splats.Positions[i] = SwizzleVector(P[0], P[1], P[2], Swizzle) * ScaleFactor;

		const pxr::GfVec3f& S = Scales[i];
		// Scales stay in metres, as the PLY importer leaves them: the shaders
		// take them to centimetres themselves.
		Splats.Scales[i] = SwizzleVectorAbs(S[0], S[1], S[2], Swizzle) * (ScaleFactor / 100.0f);

		const pxr::GfQuatf& Q = Orientations[i];
		const pxr::GfVec3f& Im = Q.GetImaginary();
		const FVector4f R = SwizzleQuaternion(Im[0], Im[1], Im[2], Q.GetReal(), Swizzle);
		Splats.Orientations[i] = FQuat4f(R.X, R.Y, R.Z, R.W).GetNormalized();

		// The constant term as the PLY importer stores f_dc: times C0, plus a
		// half. No clamp and no gamma: the GPU does both after the sum.
		const pxr::GfVec3f& Dc = Coefficients[i * PerSplat];
		Splats.ZeroOrderHarmonicsCoefficientsAndOpacities[i] = FVector4f(
			Dc[0] * 0.28209479177387814f + 0.5f,
			Dc[1] * 0.28209479177387814f + 0.5f,
			Dc[2] * 0.28209479177387814f + 0.5f,
			Opacities[i]);

		if (Splats.bHasHighOrderSH)
		{
			// Coefficient k of channel c goes where f_rest puts it: c * 15 + k - 1.
			// A lower degree leaves the higher bands at zero.
			for (size_t k = 1; k < PerSplat; k++)
			{
				const pxr::GfVec3f& C = Coefficients[i * PerSplat + k];
				for (uint32 c = 0; c < 3; c++)
				{
					Splats.HighOrderHarmonicsCoefficients[i * NumSH + c * 15 + (k - 1)] = C[c];
				}
			}
		}

		if (Splats.bHasNormals)
		{
			const pxr::GfVec3f& N = Normals[i];
			Splats.Normals[i] = SwizzleVector(N[0], N[1], N[2], Swizzle).GetSafeNormal();
		}
		if (Splats.bHasMetallicRoughness && Splats.MetallicRoughness.Num() == int32(Count))
		{
			Splats.MetallicRoughness[i] = FVector2f(FMath::Clamp(RelitMetallic[i], 0.0f, 1.0f), FMath::Clamp(RelitRoughness[i], 0.0f, 1.0f));
		}
	}

	// athenea: a medium of gaussians (athenea vdb2splat): its sigma and
	// temperature a gaussian, its albedo as the colour's constant term, the
	// medium's constants from the field's primvars. The file's opacities are
	// not read (athenea reads none either).
	{
		bool bVolume = false;
		const pxr::UsdAttribute SaidVolume = Field.GetPrim().GetAttribute(pxr::TfToken("primvars:athenea:splat:volume"));
		bVolume = SaidVolume && SaidVolume.Get(&bVolume, At) && bVolume;
		auto PerGaussian = [&](const char* Name, pxr::VtFloatArray& Out) {
			const pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken(Name));
			pxr::VtHalfArray Half;
			if (P && P.Get(&Out, At) && Out.size() == Count)
			{
				return true;
			}
			Out.clear();
			if (P && P.Get(&Half, At) && Half.size() == Count)
			{
				Out.reserve(Count);
				for (const pxr::GfHalf& H : Half)
				{
					Out.push_back(float(H));
				}
				return true;
			}
			return false;
		};
		auto Constant = [&](const char* Name, float Default) {
			const pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken(Name));
			float Value = Default;
			return P && P.Get(&Value, At) ? Value : Default;
		};
		auto Colour = [&](const char* Name, const FVector3f& Default) {
			const pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken(Name));
			pxr::GfVec3f Value;
			return P && P.Get(&Value, At) ? FVector3f(Value[0], Value[1], Value[2]) : Default;
		};
		pxr::VtFloatArray Sigma, Temperature;
		if (bVolume && PerGaussian("athenea:volume:sigma", Sigma))
		{
			PerGaussian("athenea:volume:temperature", Temperature);
			pxr::VtVec3fArray Albedo;
			{
				const pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken("athenea:volume:albedo"));
				pxr::VtVec3hArray Half;
				if (!(P && P.Get(&Albedo, At) && Albedo.size() == Count))
				{
					Albedo.clear();
					if (P && P.Get(&Half, At) && Half.size() == Count)
					{
						for (const pxr::GfVec3h& H : Half)
						{
							Albedo.push_back(pxr::GfVec3f(H));
						}
					}
				}
			}
			Splats.bVolume = true;
			Splats.Volume.SetNumUninitialized(int32(Count));
			for (size_t i = 0; i < Count; i++)
			{
				Splats.Volume[int32(i)] = FVector2f(Sigma[i], Temperature.empty() ? 0.0f : Temperature[i]);
				const pxr::GfVec3f A = Albedo.empty() ? pxr::GfVec3f(1, 1, 1) : Albedo[i];
				Splats.ZeroOrderHarmonicsCoefficientsAndOpacities[int32(i)] = FVector4f(A[0], A[1], A[2], 1.0f);
			}
			Splats.bHasHighOrderSH = false;
			ImportData.bApplyGammaCorrection = false;
			Splats.FileUnitsToCm = ImportData.ScaleFactor;
			Splats.VolumeDensityScale = Constant("athenea:densityScale", 1.0f);
			Splats.VolumeAlbedo = Colour("athenea:albedo", FVector3f(0.8f, 0.8f, 0.8f));
			Splats.VolumeEmission = Colour("athenea:volume:emission", FVector3f::ZeroVector);
			Splats.VolumeEmissionScale = Constant("athenea:volume:emissionScale", 0.0f);
			Splats.VolumeAnisotropy = FMath::Clamp(Constant("athenea:anisotropy", 0.0f), -0.99f, 0.99f);
			Splats.VolumeTemperatureScale = Constant("athenea:volume:temperatureScale", 1.0f);
			Splats.VolumeTemperatureOffset = Constant("athenea:volume:temperatureOffset", 0.0f);
			UE_LOG(LogGSplatEditor, Display, TEXT("%s: a medium of %llu gaussians (density scale %g, emission scale %g, temperature x%g + %g K)."),
				*Filename, uint64(Count), Splats.VolumeDensityScale, Splats.VolumeEmissionScale,
				Splats.VolumeTemperatureScale, Splats.VolumeTemperatureOffset);
		}
	}

	// The harmonics are expanded in the stage's frame.
	Splats.FileAxisX = SwizzleVector(1.0f, 0.0f, 0.0f, Swizzle);
	Splats.FileAxisY = SwizzleVector(0.0f, 1.0f, 0.0f, Swizzle);
	Splats.FileAxisZ = SwizzleVector(0.0f, 0.0f, 1.0f, Swizzle);
	Splats.bLinearizeColor = ImportData.bApplyGammaCorrection;

	if (TransferCount > 0)
	{
		const size_t Stride = 4 * size_t(TransferCount);
		Splats.bHasTransfer = true;
		Splats.TransferDirectCount = TransferCount;
		Splats.bTransferZonal = bZonal;
		{
			bool bCatcher = false;
			const pxr::UsdAttribute A = Field.GetPrim().GetAttribute(pxr::TfToken("primvars:athenea:splat:catcher"));
			Splats.bShadowCatcher = A && A.Get(&bCatcher, At) && bCatcher;
		}
		Splats.Transfer.SetNumZeroed(int32(Count * Stride));
		Splats.TransferMetallic.SetNumZeroed(int32(Count));
		for (size_t i = 0; i < Count; i++)
		{
			float* Out = &Splats.Transfer[i * Stride];
			FMemory::Memcpy(Out, &TransferDirect[i * TransferCount], TransferCount * sizeof(float));
			if (!TransferIndirect.empty())
			{
				FMemory::Memcpy(Out + TransferCount, &TransferIndirect[i * 3 * TransferCount], 3 * TransferCount * sizeof(float));
			}
			Splats.TransferMetallic[i] = Metallic.empty() ? 0.0f : Metallic[i];
		}

		// The surface, GS_SURFACE_* in GSColor.ush: normal (3), roughness,
		// specular weight, colour (3) and index, coat weight, roughness, index
		// and darkening, sheen colour (3) and roughness, Schlick's metal.
		const uint32 SS = FGaussianSplatData::SurfaceStride;
		Splats.TransferSurface.SetNumZeroed(int32(Count * SS));
		for (size_t i = 0; i < Count; i++)
		{
			float* S = &Splats.TransferSurface[i * SS];
			const pxr::GfVec3f N = Normals.empty() ? pxr::GfVec3f(0, 0, 1) : Normals[i];
			S[0] = N[0]; S[1] = N[1]; S[2] = N[2];
			S[3] = Roughness.empty() ? 0.5f : Roughness[i];
			S[4] = SpecularWeight.empty() ? 1.0f : SpecularWeight[i];
			const pxr::GfVec3f C = SpecularColor.empty() ? pxr::GfVec3f(1, 1, 1) : SpecularColor[i];
			S[5] = C[0]; S[6] = C[1]; S[7] = C[2];
			S[8] = SpecularIor.empty() ? 1.5f : SpecularIor[i];
			S[9] = CoatWeight.empty() ? 0.0f : CoatWeight[i];
			S[10] = CoatRoughness.empty() ? 0.0f : CoatRoughness[i];
			S[11] = CoatIor.empty() ? 1.5f : CoatIor[i];
			S[12] = CoatDarkening.empty() ? 0.0f : CoatDarkening[i];
			const pxr::GfVec3f Sh = SheenColor.empty() ? pxr::GfVec3f(0, 0, 0) : SheenColor[i];
			S[13] = Sh[0]; S[14] = Sh[1]; S[15] = Sh[2];
			S[16] = SheenRoughness.empty() ? 0.3f : SheenRoughness[i];
			S[17] = SchlickMetal.empty() ? 0.0f : float(SchlickMetal[i] != 0);
			S[18] = MaterialRoughness.empty() ? S[3] : MaterialRoughness[i];
			// What of the body passes, plus 2 for a thin wall (GSPolish.ush).
			S[19] = (Transmission.empty() ? 0.0f : FMath::Clamp(Transmission[i], 0.0f, 1.0f))
				+ (!ThinWalled.empty() && ThinWalled[i] != 0 ? 2.0f : 0.0f);
		}

		if (!Reflected.empty() && !TransferIndirect.empty())
		{
			Splats.TransferField.SetNumUninitialized(int32(Count * 48));
			FMemory::Memcpy(Splats.TransferField.GetData(), Reflected.cdata(), Count * 48 * sizeof(float));
		}

		// The shape operator, three a splat, kept as the file has it.
		if (Curvature.size() == Count * 3)
		{
			Splats.TransferCurvature.SetNumUninitialized(int32(Count * 3));
			FMemory::Memcpy(Splats.TransferCurvature.GetData(), Curvature.cdata(), Count * 3 * sizeof(float));
		}
		if (Emission.size() == Count)
		{
			Splats.Emission.SetNumUninitialized(int32(Count * 3));
			FMemory::Memcpy(Splats.Emission.GetData(), Emission.cdata(), Count * 3 * sizeof(float));
		}
		Splats.FileUnitsToCm = ImportData.ScaleFactor;
		Splats.GlassIor = GlassIor;

		Splats.CellWords = CellWords;
		if (CellWords > 0)
		{
			Splats.TransferCells.SetNumUninitialized(int32(Count * CellWords));
			FMemory::Memcpy(Splats.TransferCells.GetData(), ShadowBits.cdata(), Count * CellWords * sizeof(uint32));
		}
	}

	// THE EDIT over the cloud (AtheneaSplatEditAPI on its prim or the nearest
	// prim above it that carries one), in the cloud's own space, brought to the
	// component's: the stage's axes as the positions are (Swizzle), in centimetres.
	for (pxr::UsdPrim Prim = Field.GetPrim(); Prim && !Prim.IsPseudoRoot(); Prim = Prim.GetParent())
	{
		const pxr::UsdAttribute ActiveAttr = Prim.GetAttribute(pxr::TfToken("primvars:athenea:edit:active"));
		bool bActive = false;
		if (!(ActiveAttr && ActiveAttr.Get(&bActive, At)))
		{
			continue;
		}
		auto Get = [&](const char* Name, auto Default) {
			auto Out = Default;
			const pxr::UsdAttribute A = Prim.GetAttribute(pxr::TfToken(std::string("primvars:athenea:edit:") + Name));
			if (A)
			{
				A.Get(&Out, At);
			}
			return Out;
		};
		const float Cm = ImportData.ScaleFactor;
		auto ToLocal = [&](const pxr::GfVec3f& v) {
			return bZUp ? FVector3f(v[0], -v[1], v[2]) * Cm : FVector3f(v[0], v[2], v[1]) * Cm;
		};
		FGSSplatEdit& Edit = Splats.Edit;
		Edit.bActive = bActive;
		Edit.bSphere = Get("shape", pxr::TfToken("box")) == pxr::TfToken("sphere");
		const pxr::TfToken Mode = Get("mode", pxr::TfToken("grade"));
		Edit.Mode = Mode == pxr::TfToken("keep") ? EGSSplatEditMode::Keep : Mode == pxr::TfToken("remove") ? EGSSplatEditMode::Remove : EGSSplatEditMode::Grade;
		Edit.bInvert = Get("invert", false);
		Edit.Centre = ToLocal(Get("centre", pxr::GfVec3f(0.0f)));
		const pxr::GfVec3f Size = Get("size", pxr::GfVec3f(1.0f));
		// A sphere's radius is its x; a box's half extents follow the axes, unsigned.
		Edit.Size = Edit.bSphere ? FVector3f(Size[0] * Cm) : ToLocal(Size).GetAbs();
		const pxr::GfVec3f Tint = Get("tint", pxr::GfVec3f(1.0f));
		Edit.Tint = FLinearColor(Tint[0], Tint[1], Tint[2], 1.0f);
		Edit.Saturation = Get("saturation", 1.0f);
		Edit.Brightness = Get("brightness", 1.0f);
		Edit.Opacity = Get("opacity", 1.0f);
		Edit.MinOpacity = Get("minOpacity", 0.0f);
		Edit.MaxScale = Get("maxScale", 0.0f) * Cm;
		UE_LOG(LogGSplatEditor, Display, TEXT("%s: the stage's edit over the cloud (%s, %s)."), *Filename,
			bActive ? TEXT("active") : TEXT("inactive"), UTF8_TO_TCHAR(Mode.GetText()));
		break;
	}

	// THE SKELETON THAT CARRIES THE CLOUD (athenea mesh2splat --skinned): the
	// influences under UsdSkel's names (or athenea's own), the bind transform,
	// the weights' gradients and the joints' transforms at each authored time
	// code. Kept in the file's space; GSSkin.usf poses it each frame.
	{
		auto PrimvarOf = [&](const char* A, const char* B) {
			pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken(A));
			return P ? P : Primvars.GetPrimvar(pxr::TfToken(B));
		};
		pxr::VtIntArray JointIndices;
		pxr::VtFloatArray JointWeights;
		const pxr::UsdGeomPrimvar IndicesPv = PrimvarOf("skel:jointIndices", "athenea:splat:jointIndices");
		const pxr::UsdGeomPrimvar WeightsPv = PrimvarOf("skel:jointWeights", "athenea:splat:jointWeights");
		const pxr::UsdGeomPrimvar XformsPv = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:skinningXforms"));
		std::vector<double> Times;
		if (IndicesPv && WeightsPv && XformsPv && IndicesPv.Get(&JointIndices, At) && WeightsPv.Get(&JointWeights, At)
			&& JointIndices.size() == JointWeights.size() && JointIndices.size() % Count == 0 && XformsPv.GetAttr().GetTimeSamples(&Times))
		{
			const uint32 JointsPerSplat = uint32(JointIndices.size() / Count);
			pxr::GfMatrix4d Bind(1.0);
			for (const char* Name : { "primvars:skel:geomBindTransform", "primvars:athenea:splat:geomBindTransform" })
			{
				const pxr::UsdAttribute A = Field.GetPrim().GetAttribute(pxr::TfToken(Name));
				if (A && A.Get(&Bind, At))
				{
					break;
				}
			}
			pxr::VtMatrix4dArray First;
			if (Times.empty())
			{
				Times.push_back(pxr::UsdTimeCode::Default().GetValue());
			}
			XformsPv.Get(&First, pxr::UsdTimeCode(Times[0]));
			const uint32 Joints = uint32(First.size());
			if (Joints > 0 && JointsPerSplat > 0)
			{
				Splats.SkinJoints = Joints;
				Splats.SkinPerSplat = JointsPerSplat;
				Splats.FileUnitsToCm = ImportData.ScaleFactor;
				// A word an influence, as athenea packs them: the joint in the low
				// 16 bits, the weight as unorm16 in the high.
				Splats.SkinInfluences.SetNumUninitialized(int32(Count * JointsPerSplat));
				for (size_t k = 0; k < Count * JointsPerSplat; ++k)
				{
					const uint32 Joint = uint32(FMath::Clamp(JointIndices[k], 0, 65535));
					const uint32 Weight = uint32(FMath::RoundToInt(FMath::Clamp(JointWeights[k], 0.0f, 1.0f) * 65535.0f));
					Splats.SkinInfluences[k] = Joint | (Weight << 16);
				}
				// Two halves a word, low first: a VtHalfArray's own bytes.
				pxr::VtHalfArray Gradients;
				const pxr::UsdGeomPrimvar GradientsPv = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:jointWeightGradients"));
				if (JointsPerSplat > 1 && GradientsPv && GradientsPv.Get(&Gradients, At) && Gradients.size() == Count * (JointsPerSplat - 1) * 2)
				{
					Splats.SkinGradients.SetNumUninitialized(int32(Count * (JointsPerSplat - 1)));
					FMemory::Memcpy(Splats.SkinGradients.GetData(), Gradients.cdata(), Gradients.size() * sizeof(uint16));
				}
				// The joints at each time code: C_j = X_j G, three rows a joint,
				// for column vectors. USD's matrices are for row vectors (p' = p M,
				// the translation in the last row), so the kernel's are their
				// transposes, as athenea's engine and SplatSkinner turn them:
				// C[r][c] = sum_k X[k][r] G[c][k].
				const double Start = Times.front();
				const double Rate = FMath::Max(Stage->GetTimeCodesPerSecond(), 1.0e-6);
				for (const double T : Times)
				{
					pxr::VtMatrix4dArray Xforms;
					if (!XformsPv.Get(&Xforms, pxr::UsdTimeCode(T)) || Xforms.size() != Joints)
					{
						continue;
					}
					Splats.SkinTimes.Add(float((T - Start) / Rate));
					for (uint32 j = 0; j < Joints; ++j)
					{
						const pxr::GfMatrix4d& X = Xforms[j];
						for (int r = 0; r < 3; ++r)
						{
							double Row[4];
							for (int c = 0; c < 4; ++c)
							{
								Row[c] = X[0][r] * Bind[c][0] + X[1][r] * Bind[c][1] + X[2][r] * Bind[c][2] + X[3][r] * Bind[c][3];
							}
							Splats.SkinXforms.Add(FVector4f(float(Row[0]), float(Row[1]), float(Row[2]), float(Row[3])));
						}
					}
				}
				// The parts' baked visibility, where athenea visibility wrote it.
				{
					pxr::VtFloatArray VisParts;
					pxr::VtIntArray VisTexels, VisAmbient, VisPartOf;
					auto GetPv = [&](const char* Name, auto& Out) {
						const pxr::UsdGeomPrimvar P = Primvars.GetPrimvar(pxr::TfToken(Name));
						return P && P.Get(&Out, At);
					};
					if (GetPv("athenea:splat:visibilityParts", VisParts) && VisParts.size() % 12 == 0 && !VisParts.empty()
						&& GetPv("athenea:splat:visibilityTexels", VisTexels) && GetPv("athenea:splat:visibilityAmbient", VisAmbient)
						&& GetPv("athenea:splat:visibilityPartOf", VisPartOf) && VisPartOf.size() == Count)
					{
						Splats.VisibilityParts = TArray<float>(VisParts.cdata(), int32(VisParts.size()));
						Splats.VisibilityTexels = TArray<uint32>(reinterpret_cast<const uint32*>(VisTexels.cdata()), int32(VisTexels.size()));
						Splats.VisibilityAmbient = TArray<uint32>(reinterpret_cast<const uint32*>(VisAmbient.cdata()), int32(VisAmbient.size()));
						Splats.VisibilityPartOf = TArray<uint32>(reinterpret_cast<const uint32*>(VisPartOf.cdata()), int32(VisPartOf.size()));
						UE_LOG(LogGSplatEditor, Display, TEXT("%s: %d parts' baked visibility."), *Filename, int32(VisParts.size() / 12));
					}
				}
				UE_LOG(LogGSplatEditor, Display, TEXT("%s: carried by %u joints, %u influences a gaussian%s, %d poses over %.2f s."), *Filename,
					Joints, JointsPerSplat, Splats.SkinGradients.IsEmpty() ? TEXT("") : TEXT(" with their gradients"), Splats.SkinTimes.Num(),
					Splats.SkinTimes.IsEmpty() ? 0.0f : Splats.SkinTimes.Last());
			}
		}
	}

	{
		bool bLightCatcher = false;
		const pxr::UsdAttribute A = Field.GetPrim().GetAttribute(pxr::TfToken("primvars:athenea:splat:lightCatcher"));
		Splats.bLightCatcher = A && A.Get(&bLightCatcher, At) && bLightCatcher;
	}
	// A HARD SURFEL's p, one a gaussian (2 the gaussian), kept only where one
	// is harder.
	{
		pxr::VtFloatArray P;
		const pxr::UsdGeomPrimvar Pv = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:hardness"));
		if (Pv && Pv.Get(&P, At) && P.size() == Count)
		{
			bool bHard = false;
			for (const float p : P)
			{
				bHard |= p > 2.001f;
			}
			if (bHard)
			{
				Splats.Hardness = TArray<float>(P.cdata(), int32(P.size()));
			}
		}
	}
	// CRYPTOMATTE: an id a gaussian (the prim it came from, hashed) and the
	// manifest naming them, {"<path>":"<eight hex digits>", ...}.
	{
		pxr::VtIntArray Ids;
		const pxr::UsdGeomPrimvar IdsPv = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:cryptoObject"));
		if (IdsPv && IdsPv.Get(&Ids, At) && Ids.size() == Count)
		{
			Splats.CryptoIds = TArray<uint32>(reinterpret_cast<const uint32*>(Ids.cdata()), int32(Ids.size()));
			std::string Manifest;
			const pxr::UsdGeomPrimvar ManifestPv = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:cryptoManifest"));
			pxr::VtValue Held;
			if (ManifestPv && ManifestPv.Get(&Held, At))
			{
				Manifest = Held.IsHolding<std::string>() ? Held.UncheckedGet<std::string>()
					: Held.IsHolding<pxr::TfToken>() ? Held.UncheckedGet<pxr::TfToken>().GetString() : std::string();
			}
			// "key":"hex" pairs, read without a JSON parser (the manifest holds nothing else).
			const FString Text = UTF8_TO_TCHAR(Manifest.c_str());
			int32 At0 = 0;
			while (true)
			{
				const int32 K0 = Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, At0);
				const int32 K1 = K0 >= 0 ? Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, K0 + 1) : -1;
				const int32 V0 = K1 >= 0 ? Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, K1 + 1) : -1;
				const int32 V1 = V0 >= 0 ? Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, V0 + 1) : -1;
				if (V1 < 0)
				{
					break;
				}
				Splats.CryptoNames.Add(Text.Mid(K0 + 1, K1 - K0 - 1));
				Splats.CryptoHashes.Add(uint32(FCString::Strtoui64(*Text.Mid(V0 + 1, V1 - V0 - 1), nullptr, 16)));
				At0 = V1 + 1;
			}
			UE_LOG(LogGSplatEditor, Display, TEXT("%s: Cryptomatte ids, %d prims named."), *Filename, Splats.CryptoNames.Num());
		}
	}
	// THE LAMPS' LAYERS: primvars:athenea:splat:lightLayer:<group> (and
	// :lightLayer:emission:<group>, a group's indirect light at its level 1),
	// color3f[] a gaussian, linear, per unit of the group's radiance; packed as
	// athenea's device does (lightLayerPack): RGB9E5 of the light times 2^14.
	{
		const std::string Prefix = "athenea:splat:lightLayer:";
		TArray<FString> Names;
		TArray<pxr::VtVec3fArray> Layers;
		for (const pxr::UsdGeomPrimvar& P : Primvars.GetPrimvars())
		{
			const std::string Name = P.GetPrimvarName().GetString();
			pxr::VtVec3fArray Light;
			if (Name.rfind(Prefix, 0) == 0 && P.Get(&Light, At) && Light.size() == Count && Names.Num() < GSMaxLightLayers)
			{
				Names.Add(UTF8_TO_TCHAR(Name.substr(Prefix.size()).c_str()));
				Layers.Add(MoveTemp(Light));
			}
		}
		if (Names.Num() > 0)
		{
			// The groups' levels as the stage leaves them: a LightGroups scope's
			// children (athenea:lightGroup:emissionLuminance x :emissionColor
			// where :initialOn; :radiance and :color where those are missing).
			TMap<FString, FLinearColor> Levels;
			for (const pxr::UsdPrim& Prim : Stage->Traverse())
			{
				if (Prim.GetName() != pxr::TfToken("LightGroups"))
				{
					continue;
				}
				for (const pxr::UsdPrim& Group : Prim.GetChildren())
				{
					float Luminance = 1.0f;
					pxr::GfVec3f Colour(1.0f);
					bool bOn = true;
					auto Read = [&](const char* A, const char* B, auto& Out) {
						const pxr::UsdAttribute Attr = Group.GetAttribute(pxr::TfToken(A));
						if (!(Attr && Attr.Get(&Out, At)))
						{
							const pxr::UsdAttribute Other = B ? Group.GetAttribute(pxr::TfToken(B)) : pxr::UsdAttribute();
							if (Other)
							{
								Other.Get(&Out, At);
							}
						}
					};
					Read("athenea:lightGroup:emissionLuminance", "athenea:lightGroup:radiance", Luminance);
					Read("athenea:lightGroup:emissionColor", "athenea:lightGroup:color", Colour);
					Read("athenea:lightGroup:initialOn", nullptr, bOn);
					Levels.Add(UTF8_TO_TCHAR(Group.GetName().GetString().c_str()),
						bOn ? FLinearColor(Colour[0] * Luminance, Colour[1] * Luminance, Colour[2] * Luminance, 1.0f) : FLinearColor::Black);
				}
			}
			const uint32 N = uint32(Names.Num());
			Splats.LightLayerNames = Names;
			Splats.LightLayerDefaults.SetNum(Names.Num());
			for (int32 k = 0; k < Names.Num(); ++k)
			{
				const FString Group = Names[k].StartsWith(TEXT("emission:")) ? Names[k].RightChop(9) : Names[k];
				const FLinearColor* Level = Levels.Find(Group);
				Splats.LightLayerDefaults[k] = Level ? *Level : FLinearColor::Black;
			}
			Splats.LightLayers.SetNumUninitialized(int32(Count * N));
			ParallelFor(int32(Count), [&](int32 i)
			{
				for (uint32 k = 0; k < N; ++k)
				{
					const pxr::GfVec3f& c = Layers[k][i];
					Splats.LightLayers[i * N + k] = PackRgb9e5(FVector3f(c[0], c[1], c[2]) * 16384.0f);
				}
			});
			UE_LOG(LogGSplatEditor, Display, TEXT("%s: %u lamp layers (%s)."), *Filename, N, *FString::Join(Names, TEXT(", ")));
		}
	}

	// The prim's own transform is not applied: athenea writes its clouds at
	// /World/Splats with none, and a rotation would have to turn the harmonics.
	const pxr::GfMatrix4d LocalToWorld = pxr::UsdGeomXformable(Field.GetPrim()).ComputeLocalToWorldTransform(At);
	if (!pxr::GfIsClose(LocalToWorld, pxr::GfMatrix4d(1.0), 1e-6))
	{
		UE_LOG(LogGSplatEditor, Warning, TEXT("%s: the ParticleField's transform is not applied; place the actor instead."), *Filename);
	}

	Splats.SplatCount = uint32(Count);
	Splats.RecalculateBounds();
	UE_LOG(LogGSplatEditor, Display, TEXT("%s: %llu gaussians, degree %d, %s colour, %s."),
		*Filename, uint64(Count), Degree, bLinear ? TEXT("linear") : TEXT("sRGB"),
		TransferCount == 0 ? TEXT("no transfer")
			: TransferIndirect.empty() ? *FString::Printf(TEXT("a transfer of %u direct values"), TransferCount)
			: *FString::Printf(TEXT("a transfer of %u direct and %u indirect values"), TransferCount, 3 * TransferCount));
	return true;
}

#else

bool GSplatImporter::StageHasSplats(const FString& Filename)
{
	return false;
}

bool GSplatImporter::ImportUsd(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats)
{
	UE_LOG(LogGSplatEditor, Error, TEXT("This build has no USD SDK; %s cannot be read."), *Filename);
	return false;
}

#endif // USE_USD_SDK

#endif // WITH_EDITOR
