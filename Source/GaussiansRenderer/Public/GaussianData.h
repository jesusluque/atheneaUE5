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

	// athenea: which packing a packed asset holds, and for a file's own
	// packing its bytes as the file has them (GSDecode.usf has a decoder for
	// each):
	//   0  athenea's device packing (the arrays above)
	//   1  antimatter15's .splat: 32-byte records
	//   2  Niantic's .spz: its six streams one after another, each on a word;
	//      PackedStreams gives where each starts (positions, alphas, colours,
	//      scales, rotations, harmonics) and PackedSpz its fixed point's
	//      fractional bits, whether rotations are smallest-three, and the
	//      harmonics' coefficients a colour
	//   3  PlayCanvas's .sog: its images' texels, ranges and codebooks
	//      (GSplatSogImporter.cpp says where each lies)
	//   4  glTF with KHR_gaussian_splatting: the file's buffers and a table of
	//      accessor descriptors at PackedStreams[0] (GSplatGltfImporter.cpp)
	//   5  athenea's .athc version 3 (the web's): its chunks' CORE and SHRS
	//      sections as stored, after the gunzip, and a block table
	//      (GSplatAthcImporter.cpp)
	UPROPERTY()
	uint8 PackedFormat = 0;
	UPROPERTY()
	TArray<uint8> PackedBytes;
	UPROPERTY()
	TArray<uint32> PackedStreams;
	UPROPERTY()
	FIntVector PackedSpz = FIntVector(0, 0, 0);

	// athenea: a TX cloud's transfer (docs/decisions.md in athenea, "A transfer
	// right under any sky and any light"). The colour is then an albedo, and a
	// frame lights it: albedo (1 - metallic) <T_direct + T_indirect, sky>, the
	// sky projected onto the same harmonics in the file's frame.
	//   Transfer          TransferDirectCount (9 or 16) direct values, then
	//                     3 x that many indirect ones (rgb per coefficient);
	//                     4 x TransferDirectCount a splat
	//   TransferMetallic  one a splat
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	bool bHasTransfer = false;
	UPROPERTY()
	uint32 TransferDirectCount = 0;
	UPROPERTY()
	TArray<float> Transfer;
	UPROPERTY()
	TArray<float> TransferMetallic;

	// athenea: what a TX cloud's polish reflects with, GS_SURFACE_STRIDE floats a
	// splat (the layout is GSColor.ush's): the shading normal in the file's
	// frame, roughness, the specular lobe's weight, colour and index, the
	// coat's weight, roughness, index and darkening, the sheen's colour and
	// roughness, and whether the metal is Schlick's.
	UPROPERTY()
	TArray<float> TransferSurface;
	// Its open directions: CellWords words a splat, one bit a cell of a 16 x 16
	// (8 words) or 32 x 32 (32 words) octahedral grid over the whole sphere in
	// the file's frame; none where the cloud keeps no cells.
	UPROPERTY()
	uint32 CellWords = 0;
	UPROPERTY()
	TArray<uint32> TransferCells;

	// The reflected field (task TX, step 2): 16 rgb coefficients a splat of the
	// radiance arriving from what stands around it under a white sky, in the
	// file's frame; empty where the cloud keeps none.
	UPROPERTY()
	TArray<float> TransferField;

	// A packed cloud (PackedFormat 5) whose transfer the device decodes: the
	// arrays above stay empty, and this says whether its field is there.
	UPROPERTY()
	bool bPackedTransferField = false;

	// The surface's shape operator under each gaussian (athenea's
	// primvars:athenea:splat:curvature): 3 floats a splat, (uu, uv, vv) in the
	// gaussian's own first two axes, in one over the file's units; what the
	// sharp lobes turn by across a splat, drawn per pixel. Empty where the
	// cloud keeps none, or where the device decodes it (bPackedCurvature).
	UPROPERTY()
	TArray<float> TransferCurvature;
	UPROPERTY()
	bool bPackedCurvature = false;
	// Centimetres a unit of the file the cloud came from: what the curvature's
	// units are taken from.
	UPROPERTY()
	float FileUnitsToCm = 100.0f;

	// The cloud's index of refraction (`primvars:athenea:splat:ior`): over 1,
	// its transmitting gaussians are a solid glass the lens draws (GSColor.ush).
	// A .athc does not keep it; set it here for one.
	UPROPERTY(EditAnywhere, Category = "Gaussians")
	float GlassIor = 0.0f;

	static constexpr uint32 SurfaceStride = 20;


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
