#pragma once

#include "RenderResource.h"
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

	// Optional buffers
	SHADER_PARAMETER_SRV(StructuredBuffer<float3>, Normals)
	SHADER_PARAMETER_SRV(StructuredBuffer<float>, HighOrderSH)
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
	SHADER_PARAMETER(float, FileUnitsToCm)
	SHADER_PARAMETER(float, GlassIor)

	// athenea: a .athc's levels of detail, cut every frame (GSplatCulling.usf).
	SHADER_PARAMETER(uint32, LodLevels)
	SHADER_PARAMETER(uint32, LodFinestLevel)
	SHADER_PARAMETER(uint32, LodFinestBase)
	SHADER_PARAMETER(float, LodExtent)
	SHADER_PARAMETER(FVector3f, LodBoundsLo)
	SHADER_PARAMETER(float, LodUnitsToCm)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, LodWords)

	// athenea: a medium of gaussians (FGaussianSplatData::bVolume): sigma and
	// temperature a gaussian, and the medium's constants (GSCompute.usf).
	SHADER_PARAMETER(uint32, VolumeOn)
	SHADER_PARAMETER(float, VolumeDensityScale)
	SHADER_PARAMETER(float, VolumeEmissionScale)
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
	bool IsVolume() const { return bVolume; }
	// Checks from CPU copy of splat data if this resource supports relighting
	bool SupportsRelighting() const { return SplatData.SupportsRelighting(); }
	// athenea: whether the colour is an albedo a transfer lights
	bool HasTransfer() const { return SplatData.bHasTransfer; }
	bool HasCurvature() const { return SplatData.bHasTransfer && (!SplatData.TransferCurvature.IsEmpty() || (SplatData.bPacked && SplatData.bPackedCurvature)); }

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
	// athenea: a .athc's levels of detail, two words a gaussian.
	FBufferResources LodWordsBuffer;
	FBufferResources VolumeBuffer;
	bool bVolume = false;
	uint32 LodLevels = 0;
};
