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

	// The prim's own transform is not applied: athenea writes its clouds at
	// /World/Splats with none, and a rotation would have to turn the harmonics.
	const pxr::GfMatrix4d LocalToWorld = pxr::UsdGeomXformable(Field.GetPrim()).ComputeLocalToWorldTransform(At);
	if (!pxr::GfIsClose(LocalToWorld, pxr::GfMatrix4d(1.0), 1e-6))
	{
		UE_LOG(LogGSplatEditor, Warning, TEXT("%s: the ParticleField's transform is not applied; place the actor instead."), *Filename);
	}

	Splats.SplatCount = uint32(Count);
	Splats.RecalculateBounds();
	UE_LOG(LogGSplatEditor, Display, TEXT("%s: %llu gaussians, degree %d, %s colour."),
		*Filename, uint64(Count), Degree, bLinear ? TEXT("linear") : TEXT("sRGB"));
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
