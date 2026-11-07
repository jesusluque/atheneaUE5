#pragma once

#include "CoreMinimal.h"

#include "GaussianData.generated.h"


/**
 * Represents parsed data for a single splat, loaded from a regular PLY file.
 */
USTRUCT()
struct GAUSSIANSRENDERER_API FGaussianSplatData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	uint32 SplatCount = 0;

	// Required properties:

	// Splat position (x, y, z)
	UPROPERTY()
	TArray<FVector3f> Positions;

	// Splat orientation coming as wxyz from PLY (rot_0, rot_1, rot_2, rot_3)
	UPROPERTY()
	TArray<FQuat4f> Orientations;

	// Splat scale (scale_0, scale_1, scale_2)
	UPROPERTY()
	TArray<FVector3f> Scales;

	// Spherical Harmonics coefficients - Zero order (f_dc_0, f_dc_1, f_dc_2)
	// TODO: We now convert from SH representation to RGB on import. This field should be renamed to reflect that
	UPROPERTY()
	TArray<FVector4f> ZeroOrderHarmonicsCoefficientsAndOpacities;


	// Optional properties:

	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	bool bHasNormals = false;

	UPROPERTY(EditAnywhere, Category = "Gaussians", meta=(EditCondition="bHasNormals", ToolTip="If normals are available, overrides whether normals will be used for rendering or not."))
	bool bUseNormals = true;

	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	bool bHasHighOrderSH = false;

	UPROPERTY(EditAnywhere, Category = "Gaussians", meta = (EditCondition = "bHasHighOrderSH", ToolTip = "If higher-order SH are available, overrides whether higher-order SH will be used for rendering or not."))
	bool bUseHighOrderSH = true;

	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	bool bHasMetallicRoughness = false;

	UPROPERTY(EditAnywhere, Category = "Gaussians", meta = (EditCondition = "bHasNormals", ToolTip = "If Metallic/Roughness is available, overrides whether Metallic/Roughness will be used for rendering or not."))
	bool bUseMetallicRoughness = true;

	bool UseNormals() const { return bHasNormals && bUseNormals; }
	bool UseHighOrderSH() const { return bHasHighOrderSH && bUseHighOrderSH; }
	bool UseMetallicRoughness() const { return bHasMetallicRoughness && bUseMetallicRoughness; }

	// Normal vectors [optional] (nx, ny, nz)
	UPROPERTY()
	TArray<FVector3f> Normals;

	// Spherical Harmonics coefficients [optional] - High order (f_rest_0, ..., f_rest_44)
	UPROPERTY()
	TArray<float> HighOrderHarmonicsCoefficients;

	// PBR properties (metallic, roughness)
	UPROPERTY()
	TArray<FVector2f> MetallicRoughness;

	// athenea: the file's x, y and z axes in the asset's local space. The
	// harmonics are expanded in the file's frame, so the view direction is
	// taken back into it before they are evaluated.
	UPROPERTY()
	FVector3f FileAxisX = FVector3f(1, 0, 0);
	UPROPERTY()
	FVector3f FileAxisY = FVector3f(0, 1, 0);
	UPROPERTY()
	FVector3f FileAxisZ = FVector3f(0, 0, 1);

	// athenea: the colour is display-referred sRGB (its harmonics sum in that
	// space), so the GPU linearises it after the sum.
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	bool bLinearizeColor = false;

	// athenea: a cloud kept as athenea's device packing (common/packing.slang)
	// rather than as the float arrays above, which stay empty. The GPU decodes
	// it into the same buffers when the resource is made (GSDecode.usf).
	//   PackedPositions  xyz in the file's units, linear opacity
	//   PackedShape      4 words a splat: rotation (smallest three), f16 log
	//                    scales, f16 base colour (0.5 + C0 dc)
	//   PackedSH         PackedSHWords words a splat: f16 rest coefficients,
	//                    rgb per basis, PackedRestPerColour of them
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	bool bPacked = false;
	UPROPERTY()
	TArray<FVector4f> PackedPositions;
	UPROPERTY()
	TArray<uint32> PackedShape;
	UPROPERTY()
	TArray<uint32> PackedSH;
	UPROPERTY()
	uint32 PackedSHWords = 0;
	UPROPERTY()
	uint32 PackedRestPerColour = 0;
	// File units to centimetres, for positions; scales take it over 100, as
	// the shaders keep scales in metres.
	UPROPERTY()
	float PackedUnitsToCm = 100.0f;


protected:
	// Bounds of all splats in the model. Used for hit proxies, wireframes, culling
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	FBoxSphereBounds Bounds = FBoxSphereBounds(ForceInit);


public:

	bool SupportsRelighting() const
	{
		return UseNormals() && UseMetallicRoughness();
	}

public:
	FGaussianSplatData()
	{
		Resize(SplatCount);
	}
	
	FGaussianSplatData(uint32 Num)
	{
		Resize(Num);
	}

	void CopyFrom(const FGaussianSplatData* Other);

	void Empty();
	void Resize(uint32 Num);

	void RecalculateBounds();
	const FBoxSphereBounds& GetBounds() const { return Bounds; }
	// athenea: a packed cloud's bounds come from its file, not its positions.
	void SetBounds(const FBoxSphereBounds& InBounds) { Bounds = InBounds; }

	static constexpr uint32 NumHighOrderSH = 45;
};
