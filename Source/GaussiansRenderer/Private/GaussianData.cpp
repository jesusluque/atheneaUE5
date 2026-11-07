#include "GaussianData.h"


void FGaussianSplatData::CopyFrom(const FGaussianSplatData* Other)
{
	SplatCount = Other->SplatCount;

	Positions = Other->Positions;
	Orientations = Other->Orientations;
	Scales = Other->Scales;
	ZeroOrderHarmonicsCoefficientsAndOpacities = Other->ZeroOrderHarmonicsCoefficientsAndOpacities;

	Normals = Other->Normals;
	HighOrderHarmonicsCoefficients = Other->HighOrderHarmonicsCoefficients;
	MetallicRoughness = Other->MetallicRoughness;

	FileAxisX = Other->FileAxisX;
	FileAxisY = Other->FileAxisY;
	FileAxisZ = Other->FileAxisZ;
	bLinearizeColor = Other->bLinearizeColor;

	bPacked = Other->bPacked;
	PackedPositions = Other->PackedPositions;
	PackedShape = Other->PackedShape;
	PackedSH = Other->PackedSH;
	PackedSHWords = Other->PackedSHWords;
	PackedRestPerColour = Other->PackedRestPerColour;
	PackedUnitsToCm = Other->PackedUnitsToCm;
	Bounds = Other->Bounds;
}

void FGaussianSplatData::Empty()
{
	SplatCount = 0;

	Positions.Empty();
	Orientations.Empty();
	Scales.Empty();
	ZeroOrderHarmonicsCoefficientsAndOpacities.Empty();

	Normals.Empty();
	HighOrderHarmonicsCoefficients.Empty();
	MetallicRoughness.Empty();

	bPacked = false;
	PackedPositions.Empty();
	PackedShape.Empty();
	PackedSH.Empty();
}

void FGaussianSplatData::Resize(uint32 Num)
{
	SplatCount = Num;

	Positions.Init(FVector4f(0, 0, 0, 0), SplatCount);
	Orientations.Init(FQuat4f::Identity, SplatCount);
	Scales.Init(FVector4f(0, 0, 0, 0), SplatCount);
	ZeroOrderHarmonicsCoefficientsAndOpacities.Init(FVector4f(0, 0, 0, 0), SplatCount);

	if (bHasNormals)
	{
		Normals.Init(FVector4f(0, 0, 0, 0), SplatCount);
	}

	if (bHasHighOrderSH)
	{
		HighOrderHarmonicsCoefficients.Init(0.f, SplatCount * NumHighOrderSH);
	}

	if (bHasMetallicRoughness)
	{
		MetallicRoughness.Init(FVector2f(0, 1), SplatCount);
	}
}

void FGaussianSplatData::RecalculateBounds()
{
	FBoxSphereBounds::Builder Builder{ };

	for (const auto& Point : Positions)
	{
		// TODO: Scale point and add min/max to take into account size of splat
		Builder += static_cast<FVector>(Point);
	}

	Bounds = Builder;
}
