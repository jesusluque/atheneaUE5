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
	PackedFormat = Other->PackedFormat;
	PackedBytes = Other->PackedBytes;
	PackedStreams = Other->PackedStreams;
	PackedSpz = Other->PackedSpz;

	bHasTransfer = Other->bHasTransfer;
	TransferDirectCount = Other->TransferDirectCount;
	Transfer = Other->Transfer;
	TransferMetallic = Other->TransferMetallic;
	TransferSurface = Other->TransferSurface;
	CellWords = Other->CellWords;
	TransferCells = Other->TransferCells;
	TransferField = Other->TransferField;
	bPackedTransferField = Other->bPackedTransferField;
	TransferCurvature = Other->TransferCurvature;
	bPackedCurvature = Other->bPackedCurvature;
	FileUnitsToCm = Other->FileUnitsToCm;
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
	PackedFormat = 0;
	PackedBytes.Empty();
	PackedStreams.Empty();
	PackedSpz = FIntVector(0, 0, 0);

	bHasTransfer = false;
	TransferDirectCount = 0;
	Transfer.Empty();
	TransferMetallic.Empty();
	TransferSurface.Empty();
	CellWords = 0;
	TransferCells.Empty();
	TransferField.Empty();
	bPackedTransferField = false;
	TransferCurvature.Empty();
	bPackedCurvature = false;
	FileUnitsToCm = 100.0f;
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
