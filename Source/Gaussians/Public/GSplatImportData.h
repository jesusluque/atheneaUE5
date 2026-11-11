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
UCLASS(BlueprintType)
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import")
	bool bImportNormals = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import")
	bool bImportHighOrderSH = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import")
	bool bImportMetallicRoughness = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import")
	bool bPopulateMRWithDefault = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import")
	float MetallicDefaultValue = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import")
	float RoughnessDefaultValue = 1.0f;

	// athenea: an .athc or .athz written from a right-handed z-up stage (the
	// files do not record their up axis): mapped to (X, -Y, Z) instead of the
	// y-up (X, Z, Y).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import")
	bool bFileZUp = false;

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
