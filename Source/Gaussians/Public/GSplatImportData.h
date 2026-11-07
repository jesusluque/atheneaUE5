#pragma once

#include "CoreMinimal.h"
#include "EditorFramework/AssetImportData.h"

#include "GSplatImportData.generated.h"


UENUM()
enum class EGSplatSwizzleAxis : uint8
{
	X = 0,
	Y,
	Z,
	Xn,
	Yn,
	Zn,

	Max
};


/**
 * Cached with the GSplat asset so if it is reimported, the import settings can be recovered
 */
UCLASS()
class UGSplatImportData : public UAssetImportData
{
	GENERATED_BODY()

public:

	// Number of splats found in the file
	UPROPERTY()
	int32 NumSplats = 0;

	// Optional property support
	UPROPERTY()
	bool bHasNormals = false;
	UPROPERTY()
	bool bHasHighOrderSH = false;
	UPROPERTY()
	bool bHasMetallicRoughness = false;

	UPROPERTY()
	bool bImportNormals = false;
	UPROPERTY()
	bool bImportHighOrderSH = true;
	UPROPERTY()
	bool bImportMetallicRoughness = false;

	UPROPERTY()
	bool bPopulateMRWithDefault = false;

	UPROPERTY()
	float MetallicDefaultValue = 0.0f;
	UPROPERTY()
	float RoughnessDefaultValue = 1.0f;

	// Transformation on import
	UPROPERTY()
	float ScaleFactor = 100.0f;

	// True if the base colour in the original PLY file is already in RGB (to know when to avoid transforming from SH representation to RGB)
	UPROPERTY()
	bool bBaseColorIsRGB = false;

	UPROPERTY()
	bool bApplyGammaCorrection = false;

	UPROPERTY()
	bool bOpacityAsAlpha = false;

	// Swizzling
	UPROPERTY()
	TArray<EGSplatSwizzleAxis> Swizzle{ EGSplatSwizzleAxis::Z, EGSplatSwizzleAxis::X, EGSplatSwizzleAxis::Yn };
};
