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
	Emission = Other->Emission;
	bPackedEmission = Other->bPackedEmission;
	LightLayerNames = Other->LightLayerNames;
	LightLayerDefaults = Other->LightLayerDefaults;
	LightLayers = Other->LightLayers;
	Edit = Other->Edit;
	bTransferZonal = Other->bTransferZonal;
	bShadowCatcher = Other->bShadowCatcher;
	bLightCatcher = Other->bLightCatcher;
	SkinJoints = Other->SkinJoints;
	SkinPerSplat = Other->SkinPerSplat;
	bSkinFromDecode = Other->bSkinFromDecode;
	SkinInfluences = Other->SkinInfluences;
	SkinGradients = Other->SkinGradients;
	SkinTimes = Other->SkinTimes;
	SkinXforms = Other->SkinXforms;
	VisibilityParts = Other->VisibilityParts;
	VisibilityTexels = Other->VisibilityTexels;
	VisibilityAmbient = Other->VisibilityAmbient;
	VisibilityPartOf = Other->VisibilityPartOf;
	CryptoIds = Other->CryptoIds;
	Hardness = Other->Hardness;
	CryptoNames = Other->CryptoNames;
	CryptoHashes = Other->CryptoHashes;
	FileUnitsToCm = Other->FileUnitsToCm;
	GlassIor = Other->GlassIor;
	LodWords = Other->LodWords;
	LodLevels = Other->LodLevels;
	LodFinestLevel = Other->LodFinestLevel;
	LodFinestBase = Other->LodFinestBase;
	LodLevelRuns = Other->LodLevelRuns;
	LodBoundsLo = Other->LodBoundsLo;
	LodExtent = Other->LodExtent;
	Volume = Other->Volume;
	bVolume = Other->bVolume;
	VolumeDensityScale = Other->VolumeDensityScale;
	VolumeAlbedo = Other->VolumeAlbedo;
	VolumeEmission = Other->VolumeEmission;
	VolumeEmissionScale = Other->VolumeEmissionScale;
	VolumeAnisotropy = Other->VolumeAnisotropy;
	VolumeTemperatureScale = Other->VolumeTemperatureScale;
	VolumeTemperatureOffset = Other->VolumeTemperatureOffset;
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
	Emission.Empty();
	bPackedEmission = false;
	LightLayerNames.Empty();
	LightLayerDefaults.Empty();
	LightLayers.Empty();
	Edit = FGSSplatEdit();
	bTransferZonal = false;
	bShadowCatcher = false;
	bLightCatcher = false;
	SkinJoints = 0;
	SkinPerSplat = 0;
	bSkinFromDecode = false;
	SkinInfluences.Empty();
	SkinGradients.Empty();
	SkinTimes.Empty();
	SkinXforms.Empty();
	VisibilityParts.Empty();
	VisibilityTexels.Empty();
	VisibilityAmbient.Empty();
	VisibilityPartOf.Empty();
	CryptoIds.Empty();
	Hardness.Empty();
	CryptoNames.Empty();
	CryptoHashes.Empty();
	FileUnitsToCm = 100.0f;
	GlassIor = 0.0f;
	LodWords.Empty();
	LodLevels = 0;
	LodFinestLevel = 0;
	LodFinestBase = 0;
	LodLevelRuns.Empty();
	LodBoundsLo = FVector3f::ZeroVector;
	LodExtent = 0.0f;
	Volume.Empty();
	bVolume = false;
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
