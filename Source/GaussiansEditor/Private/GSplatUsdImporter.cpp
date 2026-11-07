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
		const pxr::UsdGeomPrimvar Indirect = Primvars.GetPrimvar(pxr::TfToken("athenea:splat:transferIndirect"));
		if (!(TransferCount > 0 && Indirect && Indirect.Get(&TransferIndirect, At)
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
	ImportData.bHasMetallicRoughness = false;

	Splats.bHasNormals = ImportData.bHasNormals && ImportData.bImportNormals;
	Splats.bHasHighOrderSH = ImportData.bHasHighOrderSH && ImportData.bImportHighOrderSH;
	Splats.bHasMetallicRoughness = false;
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
		Splats.FileUnitsToCm = ImportData.ScaleFactor;
		Splats.GlassIor = GlassIor;

		Splats.CellWords = CellWords;
		if (CellWords > 0)
		{
			Splats.TransferCells.SetNumUninitialized(int32(Count * CellWords));
			FMemory::Memcpy(Splats.TransferCells.GetData(), ShadowBits.cdata(), Count * CellWords * sizeof(uint32));
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
