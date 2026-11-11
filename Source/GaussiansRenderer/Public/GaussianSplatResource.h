#pragma once

#include "RenderResource.h"
#include "RenderGraphFwd.h"
#include "ShaderParameterMacros.h"
#include "UniformBuffer.h"
#include "GaussianData.h"


BEGIN_UNIFORM_BUFFER_STRUCT(FGaussianSplatShaderResources, GAUSSIANSRENDERER_API)
	SHADER_PARAMETER(int32, SplatCount)

	// Required buffers
	SHADER_PARAMETER_SRV(StructuredBuffer<float3>, Positions)
	SHADER_PARAMETER_SRV(StructuredBuffer<float3>, Scales)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, Orientations)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SHAndOpacities)
	// athenea: a .athc's packed arrays kept resident in place of the decoded
	// ones (GSSplatData.ush): positions and opacity, four shape words a splat.
	SHADER_PARAMETER(uint32, PackedResident)
	SHADER_PARAMETER(float, PackedUnitsToCm)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, PackedPositions)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, PackedShape)

	// Optional buffers
	SHADER_PARAMETER_SRV(StructuredBuffer<float3>, Normals)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, HighOrderSH)   // halves, 46 a splat (GSColor.ush)
	SHADER_PARAMETER_SRV(StructuredBuffer<float2>, MetallicRoughness)

	// athenea: how the colour is read (GS_COLOR_* in GSCompute.usf), and the
	// file's axes in local space, which the harmonics are expanded in.
	SHADER_PARAMETER(uint32, ColorFlags)
	SHADER_PARAMETER(FVector3f, FileAxisX)
	SHADER_PARAMETER(FVector3f, FileAxisY)
	SHADER_PARAMETER(FVector3f, FileAxisZ)

	// athenea: a TX cloud's transfer and metallic (GS_COLOR_TRANSFER).
	SHADER_PARAMETER(uint32, TransferDirectCount)
	SHADER_PARAMETER_SRV(StructuredBuffer<float>, Transfer)
	SHADER_PARAMETER_SRV(StructuredBuffer<float>, TransferMetallic)
	SHADER_PARAMETER_SRV(StructuredBuffer<float>, TransferSurface)
	SHADER_PARAMETER(uint32, CellWords)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, TransferCells)
	SHADER_PARAMETER(uint32, HasField)
	SHADER_PARAMETER_SRV(StructuredBuffer<float>, TransferField)
	SHADER_PARAMETER(uint32, HasCurvature)
	SHADER_PARAMETER_SRV(StructuredBuffer<float>, TransferCurvature)
	SHADER_PARAMETER(uint32, TransferZonal)
	SHADER_PARAMETER(uint32, Catcher)
	SHADER_PARAMETER(uint32, LightCatcher)
	SHADER_PARAMETER(float, ShadowCut)
	SHADER_PARAMETER(float, ShadowClip)
	SHADER_PARAMETER(float, ShadowPush)
	SHADER_PARAMETER(uint32, HasCrypto)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, CryptoIds)
	SHADER_PARAMETER(uint32, HasVisibility)
	SHADER_PARAMETER_SRV(StructuredBuffer<float2>, Visibility)
	SHADER_PARAMETER(uint32, HasEmission)
	SHADER_PARAMETER_SRV(StructuredBuffer<float>, Emission)
	SHADER_PARAMETER(uint32, HasHardness)
	SHADER_PARAMETER_SRV(StructuredBuffer<float>, Hardness)
	SHADER_PARAMETER(uint32, LightLayerCount)
	SHADER_PARAMETER(uint32, LightLayerSplats)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, LightLayers)
	SHADER_PARAMETER(float, FileUnitsToCm)
	SHADER_PARAMETER(float, GlassIor)

	// athenea: a .athc's levels of detail, cut every frame (GSplatCulling.usf).
	SHADER_PARAMETER(uint32, LodLevels)
	SHADER_PARAMETER(uint32, LodFinestLevel)
	SHADER_PARAMETER(uint32, LodFinestBase)
	SHADER_PARAMETER_ARRAY(FUintVector4, LodLevelRuns, [12])
	SHADER_PARAMETER(float, LodExtent)
	SHADER_PARAMETER(FVector3f, LodBoundsLo)
	SHADER_PARAMETER(float, LodUnitsToCm)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, LodWords)
	// athenea: where each finest-level group's splats begin (the splats are in
	// the groups' order), the first group element, and the finest groups' count.
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, LodGroupStarts)
	SHADER_PARAMETER(uint32, LodSplatsEnd)
	SHADER_PARAMETER(uint32, LodFinestGroups)
	SHADER_PARAMETER(uint32, LodGroupRuns)

	// athenea: a medium of gaussians (FGaussianSplatData::bVolume): sigma and
	// temperature a gaussian, and the medium's constants (GSCompute.usf).
	SHADER_PARAMETER(uint32, VolumeOn)
	SHADER_PARAMETER(float, VolumeDensityScale)
	SHADER_PARAMETER(float, VolumeEmissionScale)
	SHADER_PARAMETER(float, VolumeAnisotropy)
	SHADER_PARAMETER(float, VolumeTemperatureScale)
	SHADER_PARAMETER(float, VolumeTemperatureOffset)
	SHADER_PARAMETER(FVector3f, VolumeAlbedo)
	SHADER_PARAMETER(FVector3f, VolumeEmission)
	SHADER_PARAMETER_SRV(StructuredBuffer<float2>, Volume)
END_UNIFORM_BUFFER_STRUCT()

BEGIN_UNIFORM_BUFFER_STRUCT(FGaussianSplatPrimitiveShaderResources, GAUSSIANSRENDERER_API)
	SHADER_PARAMETER_SRV(Buffer<uint>, SplatIndexBuffer)
	SHADER_PARAMETER_SRV(Buffer<uint>, SplatIndexCountBuffer)
END_UNIFORM_BUFFER_STRUCT()


struct FGaussianSplatData;


/**
 * Container of all GPU resources required to render a gaussian splat asset.
 * This takes a Structure of Arrays approach, where each property has its own array.
 *
 * All GPU resources are packaged into a uniform buffer.
 * This uniform buffer can be consumed by rendering pipelines.
 * 
 * Not all arrays must exist - some properties are optional and will only be created
 * when the imported Gaussian splat asset supplied data.
 *
 * While any missing properties will still have a valid SRV in the uniform buffer,
 * they should not be accessed in shaders.
 */
class GAUSSIANSRENDERER_API FGaussianSplatResource : public FRenderResource
{
public:
	// Makes a copy of the splat data that will be read on the render thread when RHI is initialized
	// TODO: Could potentially release CPU splat data once copied to GPU
	FGaussianSplatResource(FGaussianSplatData InSplatData);
	virtual ~FGaussianSplatResource();

	//~ Begin FRenderResource Interface
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;

	virtual FString GetFriendlyName() const override { return TEXT("GaussianSplatResource"); }
	//~ End FRenderResource Interface


	// Call this when splats have changed on CPU. It will copy splats over to the GPU.
	// Call this on the rendering thread only (with a copy of the splat data)
	void UpdateSplats(FRHICommandListBase& RHICmdList, FGaussianSplatData InSplatData);

	// The uniform buffer collects all shader parameters related to this splat model
	// Can be nullptr if resources have not been created or this resource has no data
	// Check it is not nullptr before use!
	FRHIUniformBuffer* GetUniformBuffer() { return UniformBuffer; }

	// Gets splat count from CPU copy of splat data
	uint32 GetSplatCount() const { return SplatData.SplatCount; }
	uint32 GetLodLevels() const { return LodLevels; }
	// athenea: the finest merged level's groups (0: no levels, or a paged tree).
	uint32 GetLodFinestGroups() const { return LodFinestGroups; }
	// athenea: the finest groups whose splats are one run each, in order (0: not so, or not known).
	uint32 GetLodGroupRuns() const { return LodGroupRuns == LodFinestGroups ? LodGroupRuns : 0u; }
	uint32 GetLodSplatsEnd() const { return LodSplatsEnd; }
	bool IsVolume() const { return bVolume; }
	// athenea: whether some of its gaussians are surfels (a scale of zero: a
	// .athc's merged levels, a flat conversion), drawn from the pixel's ray.
	bool HasSurfels() const { return bSurfels || LodLevels > 0; }
	// Checks from CPU copy of splat data if this resource supports relighting
	bool SupportsRelighting() const { return SplatData.SupportsRelighting(); }
	// athenea: whether the colour is an albedo a transfer lights
	bool HasTransfer() const { return SplatData.bHasTransfer; }
	bool HasCurvature() const { return SplatData.bHasTransfer && (!SplatData.TransferCurvature.IsEmpty() || (SplatData.bPacked && SplatData.bPackedCurvature)); }

	// athenea: whether a skeleton carries the cloud, and its poses' span in seconds.
	bool IsSkinned() const { return bSkinned; }
	float GetSkinDuration() const { return SplatData.SkinTimes.IsEmpty() ? 0.0f : SplatData.SkinTimes.Last(); }
	// The cloud posed at a time (seconds into its poses, blended between the
	// two sampled poses around it) into the buffers every pass reads. A pose
	// already drawn is not posed again. Several components sharing a skinned
	// asset share its pose: the last one asked is drawn by all.
	// LocalToWorld and SunToLight (world, w 1 where there is a sun) say where
	// the sun is in the cloud's frame, for its parts' baked visibility.
	void Skin_RenderThread(FRDGBuilder& GraphBuilder, float Seconds, const FMatrix& LocalToWorld, const FVector4f& SunToLight);

private:
	// CPU copy of data that this resource will copy to GPU
	FGaussianSplatData SplatData;

	TUniformBufferRef<FGaussianSplatShaderResources> UniformBuffer{ };

	struct FBufferResources
	{
		FBufferRHIRef Buffer;
		FShaderResourceViewRHIRef SRV;

		void SafeRelease()
		{
			SRV.SafeRelease();
			Buffer.SafeRelease();
		}
	};

	// Required buffers
	FBufferResources PositionsBuffer;
	FBufferResources ScalesBuffer;
	FBufferResources OrientationsBuffer;
	FBufferResources SHAndOpacitiesBuffer;

	// Optional buffers
	FBufferResources NormalsBuffer;
	FBufferResources HighOrderSHBuffer;
	FBufferResources MetallicRoughnessBuffer;

	// athenea: a TX cloud's transfer
	FBufferResources TransferBuffer;
	FBufferResources TransferMetallicBuffer;
	FBufferResources TransferSurfaceBuffer;
	FBufferResources TransferCellsBuffer;
	FBufferResources TransferFieldBuffer;
	// athenea: a .athc v3 TX cloud's transfer buffers, written by the device.
	FBufferResources PackedTransfer;
	FBufferResources PackedMetallic;
	FBufferResources PackedSurface;
	FBufferResources PackedCells;
	FBufferResources PackedField;
	FBufferResources PackedCurvature;
	FBufferResources CurvatureBuffer;
	FBufferResources PackedEmission;
	FBufferResources EmissionBuffer;
	FBufferResources HardnessBuffer;
	bool bSurfels = false;
	FBufferResources LightLayersBuffer;
	FBufferResources CryptoBuffer;
	// athenea: a skinned cloud's bind pose, influences and gradients, and the
	// views its posed buffers are written through.
	bool bSkinned = false;
	float SkinShown = -1.0f;
	FBufferResources SkinRestPositionsBuffer;
	FBufferResources SkinRestRotationsBuffer;
	FBufferResources SkinRestScalesBuffer;
	FBufferResources SkinInfluencesBuffer;
	FBufferResources SkinGradientsBuffer;
	FUnorderedAccessViewRHIRef SkinPositionsUAV;
	FUnorderedAccessViewRHIRef SkinScalesUAV;
	FUnorderedAccessViewRHIRef SkinOrientationsUAV;
	FBufferResources VisPartsBuffer;
	FBufferResources VisPartOfBuffer;
	FBufferResources VisTexelsBuffer;
	FBufferResources VisAmbientBuffer;
	FBufferResources VisibilityBuffer;
	FUnorderedAccessViewRHIRef VisibilityUAV;
	uint32 VisParts = 0;
	FVector4f SkinSunShown = FVector4f(0, 0, 0, -1);
	// athenea: a .athc's levels of detail, two words a gaussian.
	FBufferResources LodWordsBuffer;
	FBufferResources LodGroupStartsBuffer;
	uint32 LodGroupRuns = 0;
	uint32 LodSplatsEnd = 0;
	FBufferResources PackedPositionsBuffer;
	FBufferResources PackedShapeBuffer;
	bool bPackedResident = false;
	FBufferResources VolumeBuffer;
	bool bVolume = false;
	uint32 LodLevels = 0;
	uint32 LodFinestGroups = 0;
};
