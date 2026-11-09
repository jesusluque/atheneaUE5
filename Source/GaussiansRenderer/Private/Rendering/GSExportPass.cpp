#include "GSExportPass.h"

#include "GaussianSplatResource.h"
#include "GaussianSplatInterface.h"
#include "GSDispatchHelper.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterStruct.h"


class FGSExportPassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSExportPassCS)
	SHADER_USE_PARAMETER_STRUCT(FGSExportPassCS, FGlobalShader);

	class FEnableRelighting : SHADER_PERMUTATION_BOOL("ENABLE_RELIGHTING");
	using FPermutationDomain = TShaderPermutationDomain<FEnableRelighting>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Splat data
		SHADER_PARAMETER_STRUCT_REF(FGaussianSplatShaderResources, GSResources)
		SHADER_PARAMETER_SRV(Buffer<uint>, IndexCountBuffer)
		SHADER_PARAMETER_SRV(Buffer<uint>, IndexBuffer)

		// Output buffers
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float3>, RWPositions)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float3>, RWScales)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, RWOrientations)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, RWSHAndOpacities)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float3>, RWNormals)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, RWMetallicRoughness)

		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), GetThreadGroupSize1D());
	}

	static uint32 GetThreadGroupSize1D() { return 128; }
};

IMPLEMENT_GLOBAL_SHADER(FGSExportPassCS, "/Plugin/StarLightGS/GSExport.usf", "GSExportPassCS", SF_Compute)


BEGIN_SHADER_PARAMETER_STRUCT(FGSExportReadbackPassParameters, )
	RDG_BUFFER_ACCESS(Positions, ERHIAccess::CopySrc)
	RDG_BUFFER_ACCESS(Scales, ERHIAccess::CopySrc)
	RDG_BUFFER_ACCESS(Orientations, ERHIAccess::CopySrc)
	RDG_BUFFER_ACCESS(SHAndOpacities, ERHIAccess::CopySrc)
	RDG_BUFFER_ACCESS(Normals, ERHIAccess::CopySrc)
	RDG_BUFFER_ACCESS(MetallicRoughness, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()


void Gaussians::ExportSplatData(
	FRDGBuilder& GraphBuilder, 
	const IGaussianSplatInterface* Proxy,
	FGaussianSplatReadbackData& OutReadbackData
)
{
	check(IsInRenderingThread());

	bool bRelighting = Proxy->SupportsRelighting();
	FGaussianSplatResource* Resource = Proxy->GetResource_RenderThread();
	uint32 SplatCount = Resource->GetSplatCount();

	TUniformBufferRef<FGaussianSplatShaderResources> ResourceUniformBuffer = TUniformBufferRef<FGaussianSplatShaderResources>{ Resource->GetUniformBuffer() };

	// Allocate buffers to hold intermediate copy
	FRDGBufferRef TempPositions = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(
		sizeof(FVector3f), SplatCount),
		TEXT("TempPositions"));
	FRDGBufferRef TempScales = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(
		sizeof(FVector3f), SplatCount),
		TEXT("TempScales"));
	FRDGBufferRef TempOrientations = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(
		sizeof(FVector4f), SplatCount),
		TEXT("TempOrientations"));
	FRDGBufferRef TempSHAndOpacities = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(
		sizeof(FVector4f), SplatCount),
		TEXT("TempSHAndOpacities"));

	FRDGBufferRef TempNormals = nullptr;
	FRDGBufferRef TempMetallicRoughness = nullptr;
	if (bRelighting)
	{
		TempNormals = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(
			sizeof(FVector3f), SplatCount),
			TEXT("TempNormals"));
		TempMetallicRoughness = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(
			sizeof(FVector2f), SplatCount),
			TEXT("TempMetallicRoughness"));
	}

	// Allocate readback buffers
	OutReadbackData.SplatCount = RHICreateStagingBuffer();

	OutReadbackData.Positions = RHICreateStagingBuffer();
	OutReadbackData.Scales = RHICreateStagingBuffer();
	OutReadbackData.Orientations = RHICreateStagingBuffer();
	OutReadbackData.SHAndOpacities = RHICreateStagingBuffer();
	OutReadbackData.Normals = bRelighting ? RHICreateStagingBuffer() : nullptr;
	OutReadbackData.MetallicRoughness = bRelighting ? RHICreateStagingBuffer() : nullptr;

	OutReadbackData.Fence = RHICreateGPUFence("SplatReadbackCompleteFence");

	// First, read splat count back
	{
		FRHIBuffer* CountBuffer = Proxy->GetIndexCountBuffer();

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("GSExportPass.ReadSplatCount"),
			ERDGPassFlags::NeverCull,
			[&OutReadbackData, CountBuffer](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.CopyToStagingBuffer(CountBuffer, OutReadbackData.SplatCount, 0, sizeof(uint32));
			});
	}

	// Create indirect args
	FRDGBufferRef ComputeIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(), TEXT("GSExportPass.IndirectArgs"));

	Gaussians::AddSetupDispatchIndirectArgsPass(
		GraphBuilder,
		Proxy->GetIndexCountBufferSRV(),
		ComputeIndirectArgs,
		FGSExportPassCS::GetThreadGroupSize1D()
	);

	// Run compute shader to dereference index buffer and copy splat data into contiguous buffers
	{
		FGSExportPassCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGSExportPassCS::FParameters>();
		PassParameters->GSResources = ResourceUniformBuffer;
		PassParameters->IndexCountBuffer = Proxy->GetIndexCountBufferSRV();
		PassParameters->IndexBuffer = Proxy->GetIndexBufferSRV();

		PassParameters->RWPositions = GraphBuilder.CreateUAV(TempPositions);
		PassParameters->RWScales = GraphBuilder.CreateUAV(TempScales);
		PassParameters->RWOrientations = GraphBuilder.CreateUAV(TempOrientations);
		PassParameters->RWSHAndOpacities = GraphBuilder.CreateUAV(TempSHAndOpacities);

		if (bRelighting)
		{
			PassParameters->RWNormals = GraphBuilder.CreateUAV(TempNormals);
			PassParameters->RWMetallicRoughness = GraphBuilder.CreateUAV(TempMetallicRoughness);
		}

		PassParameters->IndirectArgs = ComputeIndirectArgs;

		FGSExportPassCS::FPermutationDomain Permutation;
		Permutation.Set<FGSExportPassCS::FEnableRelighting>(bRelighting);

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FGSExportPassCS> ComputeShader(ShaderMap, Permutation);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("GSExportPass"),
			ERDGPassFlags::Compute,
			ComputeShader,
			PassParameters,
			ComputeIndirectArgs,
			0
		);
	}

	// Copy all data back into the readback buffers
	{
		FGSExportReadbackPassParameters* PassParameters = GraphBuilder.AllocParameters<FGSExportReadbackPassParameters>();
		PassParameters->Positions = TempPositions;
		PassParameters->Scales = TempScales;
		PassParameters->Orientations = TempOrientations;
		PassParameters->SHAndOpacities = TempSHAndOpacities;

		if (bRelighting)
		{
			PassParameters->Normals = TempNormals;
			PassParameters->MetallicRoughness = TempMetallicRoughness;
		}

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("GSExportReadbackPass"),
			PassParameters,
			ERDGPassFlags::Readback,
			[&OutReadbackData, TempPositions, TempScales, TempOrientations, TempSHAndOpacities, TempNormals, TempMetallicRoughness, SplatCount, bRelighting]
			(FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.CopyToStagingBuffer(TempPositions->GetRHI(), OutReadbackData.Positions, 0, SplatCount * sizeof(FVector3f));
				RHICmdList.CopyToStagingBuffer(TempScales->GetRHI(), OutReadbackData.Scales, 0, SplatCount * sizeof(FVector3f));
				RHICmdList.CopyToStagingBuffer(TempOrientations->GetRHI(), OutReadbackData.Orientations, 0, SplatCount * sizeof(FVector4f));
				RHICmdList.CopyToStagingBuffer(TempSHAndOpacities->GetRHI(), OutReadbackData.SHAndOpacities, 0, SplatCount * sizeof(FVector4f));

				if (bRelighting)
				{
					RHICmdList.CopyToStagingBuffer(TempNormals->GetRHI(), OutReadbackData.Normals, 0, SplatCount * sizeof(FVector3f));
					RHICmdList.CopyToStagingBuffer(TempMetallicRoughness->GetRHI(), OutReadbackData.MetallicRoughness, 0, SplatCount * sizeof(FVector2f));
				}

				// Signal the fence that work has been completed
				RHICmdList.WriteGPUFence(OutReadbackData.Fence);
			}
		);	
	}
}
