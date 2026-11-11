#pragma once

#include "CoreMinimal.h"

#include "GaussianData.generated.h"


/**
 * Represents parsed data for a single splat, loaded from a regular PLY file.
 */
// athenea: A SPLAT EDIT (athenea's AtheneaSplatEditAPI, common/edit.slang),
// rule for rule: the filters wherever the volume is (opacity below
// MinOpacity, longest scale above MaxScale: gone), then a box of half extents
// Size or a sphere of radius Size.X around Centre, in the cloud's local space
// (centimetres, as the component draws it): Keep drops what is outside,
// Remove what is inside, Grade nothing; what is inside and kept has its
// opacity times Opacity (gone below 1/255) and its colour, in linear light,
// times Tint x Brightness, then lerped from its Rec.709 luminance by
// Saturation.
UENUM(BlueprintType)
enum class EGSSplatEditMode : uint8
{
	Keep = 0,
	Remove = 1,
	Grade = 2,
};

USTRUCT(BlueprintType)
struct GAUSSIANSRENDERER_API FGSSplatEdit
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edit")
	bool bActive = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edit")
	bool bSphere = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edit")
	EGSSplatEditMode Mode = EGSSplatEditMode::Grade;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edit")
	bool bInvert = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edit")
	FVector3f Centre = FVector3f::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edit")
	FVector3f Size = FVector3f(100.0f, 100.0f, 100.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edit")
	FLinearColor Tint = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edit", meta = (ClampMin = "0"))
	float Saturation = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edit", meta = (ClampMin = "0"))
	float Brightness = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edit", meta = (ClampMin = "0"))
	float Opacity = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edit", meta = (ClampMin = "0"))
	float MinOpacity = 0.0f;
	// Centimetres; 0 is off.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edit", meta = (ClampMin = "0"))
	float MaxScale = 0.0f;
};

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
	// A zonal transfer (athenea's transferZonal): the direct half as two lobes
	// in each gaussian's own frame, its ten values at the start of each record
	// of TransferDirectCount 9 (GSColor.ush, GSZonalTransfer); no indirect half.
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	bool bTransferZonal = false;
	// A shadow catcher (primvars:athenea:splat:catcher): a TX patch on the
	// ground drawn black, as opaque as the object's shadow (GSCatcherAlpha).
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	bool bShadowCatcher = false;
	// A light catcher (primvars:athenea:splat:lightCatcher): its lamps' layers
	// alone, added over what is behind it, covering nothing.
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	bool bLightCatcher = false;

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

	// The light each gaussian gives off by itself (athenea's
	// primvars:athenea:splat:emission, a .athc's or .athz's RGB9E5 word): 3
	// linear floats a splat, which a TX cloud's lit body starts from (athenea's
	// relitSplat). Empty where the cloud carries none, or where the device
	// decodes it (bPackedEmission).
	UPROPERTY()
	TArray<float> Emission;
	UPROPERTY()
	bool bPackedEmission = false;

	// athenea: THE LAMPS' LAYERS (primvars:athenea:splat:lightLayer:<group>,
	// docs/decisions.md in athenea, light groups): for each group of lamps, the
	// light each gaussian sends out from that group alone at a radiance of one.
	// A frame adds each layer times its group's weight (the component's
	// LightGroups), so a lamp switched or dimmed is a number. At most
	// GS_MAX_LIGHT_LAYERS groups; LightLayerNames.Num() words a splat in
	// LightLayers, RGB9E5 of the light times 2^14 (athenea's kLightLayerScale).
	// LightLayerDefaults: each group's level as the stage leaves it
	// (emissionLuminance x emissionColor where initially on, else 0).
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	TArray<FString> LightLayerNames;
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	TArray<FLinearColor> LightLayerDefaults;
	UPROPERTY()
	TArray<uint32> LightLayers;

	// athenea: A CLOUD CARRIED BY A SKELETON (mesh2splat --skinned: a USD
	// cloud's skel:jointIndices and skel:jointWeights, skel:geomBindTransform,
	// primvars:athenea:splat:jointWeightGradients and the joints' transforms at
	// each time code, primvars:athenea:splat:skinningXforms; a .athc's SKIN
	// section and ATSK skeleton). SkinPerSplat influences a gaussian, a word
	// each (joint | weight unorm16 << 16), the gradients' words after, and for
	// each sampled instant (SkinTimes, seconds) SkinJoints x 3 rows of the
	// joint's skinning transform after the bind transform, for column vectors
	// (USD's row-vector matrices transposed). The bind pose is the cloud as
	// decoded; GSSkin.usf poses it each frame into Positions, Scales and
	// Orientations.
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	uint32 SkinJoints = 0;
	UPROPERTY()
	uint32 SkinPerSplat = 0;
	UPROPERTY()
	TArray<uint32> SkinInfluences;
	UPROPERTY()
	TArray<uint32> SkinGradients;
	// An .athz's layer K: the influences and gradients are decoded on the
	// device with the splats (GSAthzSkinCS), not held here; the splats are
	// the first SplatCount - groups elements, and the merged levels are not
	// drawn while the cloud is skinned.
	UPROPERTY()
	bool bSkinFromDecode = false;
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	TArray<float> SkinTimes;
	UPROPERTY()
	TArray<FVector4f> SkinXforms;

	// Its parts' baked visibility (athenea visibility: primvars:athenea:splat:
	// visibilityParts, 12 floats a part; visibilityTexels and visibilityAmbient,
	// two halves a word; visibilityPartOf, a part a gaussian).
	UPROPERTY()
	TArray<float> VisibilityParts;
	UPROPERTY()
	TArray<uint32> VisibilityTexels;
	UPROPERTY()
	TArray<uint32> VisibilityAmbient;
	UPROPERTY()
	TArray<uint32> VisibilityPartOf;

	bool IsSkinned() const { return SkinJoints > 0 && SkinTimes.Num() > 0 && SkinXforms.Num() == SkinTimes.Num() * int32(SkinJoints) * 3; }

	// athenea: CRYPTOMATTE (primvars:athenea:splat:cryptoObject, one id a
	// gaussian: the hash of the prim it came from; cryptoManifest names them).
	UPROPERTY()
	TArray<uint32> CryptoIds;

	// athenea: a HARD SURFEL's p (primvars:athenea:splat:hardness, one a
	// gaussian; 2 the gaussian): its disc drawn as exp(-(|u|^p + |v|^p) / 2) in
	// its own axes, its stored scale the moment-matched one. Empty: all 2.
	UPROPERTY()
	TArray<float> Hardness;
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	TArray<FString> CryptoNames;
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	TArray<uint32> CryptoHashes;

	// athenea: the edit the stage put over this cloud (AtheneaSplatEditAPI),
	// in local centimetres; drawn unless the component's own edit is active.
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	FGSSplatEdit Edit;
	// Centimetres a unit of the file the cloud came from: what the curvature's
	// units are taken from.
	UPROPERTY()
	float FileUnitsToCm = 100.0f;

	// The cloud's index of refraction (`primvars:athenea:splat:ior`): over 1,
	// its transmitting gaussians are a solid glass the lens draws (GSColor.ush).
	// A .athc does not keep it; set it here for one.
	UPROPERTY(EditAnywhere, Category = "Gaussians")
	float GlassIor = 0.0f;

	// athenea: a .athc's levels of detail (GSplatAthcImporter.cpp), cut every
	// frame as athenea's CutSelector cuts them (GSplatCulling.usf). The merged
	// levels' groups follow the splats in the arrays above; two words a
	// gaussian: a splat's finest-level group and kLodLeaf, or a group's octree
	// cell code and its level. The octree is the file's (its BoundsLo and
	// Extent, in the file's units and axes); the finest level's groups start
	// at LodFinestBase. LodLevels 0: no levels, every gaussian drawn.
	UPROPERTY()
	TArray<uint32> LodWords;
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	uint32 LodLevels = 0;
	UPROPERTY()
	uint32 LodFinestLevel = 0;
	UPROPERTY()
	uint32 LodFinestBase = 0;
	// Each merged level's groups, by level number: their first index and
	// their count, a pair a level (the morph finds a group's parent in the
	// level above, GSLod.ush). Empty: read from LodWords when the resource is
	// made (an asset saved before the morph).
	UPROPERTY()
	TArray<uint32> LodLevelRuns;
	static constexpr int32 kLodLevelRuns = 12;
	UPROPERTY()
	FVector3f LodBoundsLo = FVector3f::ZeroVector;
	UPROPERTY()
	float LodExtent = 0.0f;
	static constexpr uint32 kLodLeaf = 0x80000000u;
	static constexpr uint32 kLodCoarsest = 0x40000000u;
	// A paged tree's node (version 3): its first word is its parent's index
	// (~0u at a root), and kLodLeaf marks a leaf.
	static constexpr uint32 kLodTree = 0x20000000u;

	// athenea: a MEDIUM OF GAUSSIANS (primvars:athenea:splat:volume, what
	// athenea vdb2splat writes): each gaussian a lump of extinction, drawn
	// with the alpha 1 - exp(-tau) of the optical depth of its pixel's ray
	// (GSCompute.usf). Two values a gaussian: sigma, its peak extinction in the
	// file's density units, and its temperature in the file's units; the
	// colour's constant term holds its albedo. The medium's constants are the
	// field's primvars, athenea's names and defaults.
	UPROPERTY()
	TArray<FVector2f> Volume;
	UPROPERTY(VisibleAnywhere, Category = "Gaussians")
	bool bVolume = false;
	// Density to extinction a file unit of length.
	UPROPERTY(EditAnywhere, Category = "Gaussians|Volume")
	float VolumeDensityScale = 1.0f;
	// The scattering albedo, times each gaussian's.
	UPROPERTY(EditAnywhere, Category = "Gaussians|Volume")
	FVector3f VolumeAlbedo = FVector3f(0.8f, 0.8f, 0.8f);
	// Radiance of the absorbing part: the medium gives off (1 - albedo) times it.
	UPROPERTY(EditAnywhere, Category = "Gaussians|Volume")
	FVector3f VolumeEmission = FVector3f::ZeroVector;
	// athenea: the medium's Henyey-Greenstein g (primvars:athenea:anisotropy):
	// 0 scatters alike every way, toward 1 forward (a cloud's silver lining).
	UPROPERTY(EditAnywhere, Category = "Gaussians|Volume", meta = (ClampMin = "-0.99", ClampMax = "0.99"))
	float VolumeAnisotropy = 0.0f;
	// A gaussian glows as a black body at its temperature times this (0: no fire).
	UPROPERTY(EditAnywhere, Category = "Gaussians|Volume")
	float VolumeEmissionScale = 0.0f;
	// Kelvin a unit of the file's temperature, and kelvin added to it.
	UPROPERTY(EditAnywhere, Category = "Gaussians|Volume")
	float VolumeTemperatureScale = 1.0f;
	UPROPERTY(EditAnywhere, Category = "Gaussians|Volume")
	float VolumeTemperatureOffset = 0.0f;

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
