#include "GaussiansRenderer.h"
#include "GSViewExtension.h"
#include "GaussianSplatResource.h"
#include "GaussianSplatInterface.h"
#include "GSGPUStats.h"

#include "GaussiansHZB.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RendererInterface.h"
#include "RendererClasses.h"
#include "ScreenPass.h"
#include "ScenePrivate.h"

#include "Rendering/GPURadixSort.h"

#include "GaussiansDefinitions.h"
#include "GSDispatchHelper.h"

// athenea: each stage of a splat frame timed apart, so a profile says where the
// time goes.
DECLARE_GPU_STAT_NAMED(GaussiansPrePass, TEXT("Gaussians: PrePass"));
DECLARE_GPU_STAT_NAMED(GaussiansSort, TEXT("Gaussians: Radix Sort"));
DECLARE_GPU_STAT_NAMED(GaussiansRaster, TEXT("Gaussians: Rasterization"));


static TAutoConsoleVariable<int32> CVarGaussiansMaxSplats(
	TEXT("r.Gaussians.MaxSplats"),
	5'000'000,
	TEXT("The maximum number of splats that can be rendered in a single frame."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarGaussiansOpacityThreshold(
	TEXT("r.Gaussians.OpacityThreshold"),
	0.0f,
	TEXT("Only render splats with an opacity greater than this threshold."),
	ECVF_RenderThreadSafe
);


class FPermutationEnableRelighting : SHADER_PERMUTATION_BOOL("ENABLE_RELIGHTING");
using FSplattingPermutationDomain = TShaderPermutationDomain<FPermutationEnableRelighting>;

class FGSPrepassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSPrepassCS)
	SHADER_USE_PARAMETER_STRUCT(FGSPrepassCS, FGlobalShader);

	using FPermutationDomain = FSplattingPermutationDomain;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)

		SHADER_PARAMETER_STRUCT_REF(FGaussianSplatShaderResources, GSResource)
		SHADER_PARAMETER_STRUCT_REF(FPrimitiveUniformShaderParameters, Primitive)

		SHADER_PARAMETER_SRV(Buffer<uint>, SplatIndexBuffer)
		SHADER_PARAMETER_SRV(Buffer<uint>, SplatIndexCounter)

		SHADER_PARAMETER(float, SplatScaleMultiplier)
		SHADER_PARAMETER(float, SplatOpacityThreshold)

		SHADER_PARAMETER_STRUCT_INCLUDE(FGaussiansHZBParameters, HZB)

		// Out
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint32>, Counter)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float2>, OutSSPositions)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, OutSSScale)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float3>, OutConic)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, OutColorOpacity)

		// Only used when relighting is enabled
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutNormal)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutMetallicRoughness)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, OutSSVelocity)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutDepth)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutGaussianIDX)

		SHADER_PARAMETER(uint32, BufferCapacity)

		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), GetThreadGroupSize1D());
	}

	static uint32 GetThreadGroupSize1D() { return 128; }
};

IMPLEMENT_GLOBAL_SHADER(FGSPrepassCS, "/Plugin/StarLightGS/GSCompute.usf", "GSPrepass", SF_Compute);


class FSplattingPassVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSplattingPassVS)

	using FPermutationDomain = FSplattingPermutationDomain;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float2>, VS_SSPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, VS_SSScale)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float3>, VS_Conic)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, VS_ColorOpacity)

		// Only used when relighting is enabled
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, VS_Normal) // Octahedron encoding 16bpc
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, VS_MetallicRoughness)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, VS_SSVelocity)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, VS_Depth)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, VS_GaussianIDX)

		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	SHADER_USE_PARAMETER_STRUCT(FSplattingPassVS, FGlobalShader);
};

IMPLEMENT_GLOBAL_SHADER(FSplattingPassVS, "/Plugin/StarLightGS/GSCompute.usf", "GSSplatting_VS", SF_Vertex);

class FSplattingPassPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSplattingPassPS)

	using FPermutationDomain = FSplattingPermutationDomain;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	SHADER_USE_PARAMETER_STRUCT(FSplattingPassPS, FGlobalShader);
};

IMPLEMENT_GLOBAL_SHADER(FSplattingPassPS, "/Plugin/StarLightGS/GSCompute.usf", "GSSplatting_PS", SF_Pixel);


BEGIN_SHADER_PARAMETER_STRUCT(FSplattingPassPipelineParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FSplattingPassVS::FParameters, VertexShader)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSplattingPassPS::FParameters, PixelShader)
END_SHADER_PARAMETER_STRUCT()


// Static blend / stencil states

using FRelitBlendState = TStaticBlendState<
	/* Color:       */ CW_RGBA, BO_Add, BF_InverseDestAlpha, BF_One, BO_Add, BF_InverseDestAlpha, BF_One,
	/* Normal:      */ CW_RGBA, BO_Add, BF_InverseDestAlpha, BF_One, BO_Add, BF_InverseDestAlpha, BF_One,
	/* Depth + PBR: */ CW_RGBA, BO_Add, BF_InverseDestAlpha, BF_One, BO_Add, BF_InverseDestAlpha, BF_One,
	/* Velocity: */	   CW_RGBA, BO_Add, BF_InverseDestAlpha, BF_One, BO_Add, BF_InverseDestAlpha, BF_One
>;
using FUnlitBlendState = TStaticBlendState<
	/* Color:    */ CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha,
	/* Velocity: */ CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero
>;

using FRelitDepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>;
using FUnlitDepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>;


void FGSViewExtension::GaussianSplattingPass(
	FRDGBuilder& GraphBuilder, 
	const FSceneView& InView,
	const FRenderTargetBindingSlots& RenderTargets,
	const TSet<IGaussianSplatInterface*>& Proxies,
	const FGSGPUStats* GPUStats)
{
	// Don't call this when there are no proxies
	check(!Proxies.IsEmpty());
	bool bRelighting = (*Proxies.FindArbitraryElement())->SupportsRelighting() && Gaussians::IsRelightingEnabled_RenderThread();

	check(InView.bIsViewInfo);
	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(InView);

	// Create transient buffers
	int32 Capacity = CVarGaussiansMaxSplats.GetValueOnRenderThread();

	FRDGBufferRef NDCPositionsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FVector2f), Capacity), TEXT("Gaussians.NDCPositions"));
	FRDGBufferRef NDCScalesBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), Capacity), TEXT("Gaussians.NDCScales"));
	FRDGBufferRef ConicsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector3f), Capacity), TEXT("Gaussians.Conics"));
	FRDGBufferRef ColorOpacityBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), Capacity), TEXT("Gaussians.ColorOpacity"));

	FRDGBufferRef GaussianIndexBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), Capacity), TEXT("Gaussians.GaussianIDX"));
	FRDGBufferRef GaussianDepthBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), Capacity), TEXT("Gaussians.Depth"));

	// Optional buffers - only in use when relighting enabled
	FRDGBufferRef NormalBuffer = nullptr;
	FRDGBufferRef MetallicRoughnessBuffer = nullptr;
	FRDGBufferRef ScreenSpaceVelocityBuffer = nullptr;
	if (bRelighting)
	{
		NormalBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), Capacity), TEXT("Gaussians.Normal"));
		MetallicRoughnessBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), Capacity), TEXT("Gaussians.MetallicRoughness"));
		ScreenSpaceVelocityBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FUintVector2), Capacity), TEXT("Gaussians.ScreenSpaceVelocity"));
	}

	// Setup indirect args
	FRDGBufferRef ComputeIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(), TEXT("Gaussians.ComputeIndirectArgs"));
	FRDGBufferRef RasterIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(), TEXT("Gaussians.RasterIndirectArgs"));

	// Clear Indirect ARGS
	// Vertex count per instance will be set by dispatch thread 0
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RasterIndirectArgsBuffer, PF_R32_UINT), 0);

	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(InView.GetShaderPlatform());

	FSplattingPermutationDomain Permutation;
	Permutation.Set<FPermutationEnableRelighting>(bRelighting);

	TShaderMapRef<FGSPrepassCS> PrePassShader(ShaderMap, Permutation);
	TShaderMapRef<FSplattingPassVS> VertexShader(ShaderMap, Permutation);
	TShaderMapRef<FSplattingPassPS> PixelShader(ShaderMap, Permutation);

	bool bAnySplatsRendered = false;
	for (auto Proxy : Proxies)
	{
		const FBoxSphereBounds& ProxyBounds = Proxy->GetSplatBounds();
		if (!InView.GetCullingFrustum().IntersectBox(ProxyBounds.Origin, ProxyBounds.BoxExtent) | !Proxy->IsSplatShown(&InView))
		{
			continue;
		}
		bAnySplatsRendered = true;

		RDG_EVENT_SCOPE_STAT(GraphBuilder, GaussiansPrePass, "Gaussians: PrePass");

		// Set up indirect args for this splat
		Gaussians::AddSetupDispatchIndirectArgsPass(
			GraphBuilder,
			Proxy->GetIndexCountBufferSRV(),
			ComputeIndirectArgs,
			FGSPrepassCS::GetThreadGroupSize1D()
		);

		FGaussianSplatResource* Resource = Proxy->GetResource_RenderThread();
		auto SplatCount  = Resource->GetSplatCount();

		if (bRelighting)
		{
			INC_DWORD_STAT_BY(STAT_SplatsSubmittedLit, SplatCount)
		}
		else
		{
			INC_DWORD_STAT_BY(STAT_SplatsSubmittedUnlit, SplatCount)
		}

		TUniformBufferRef<FGaussianSplatShaderResources> ResourceUniformBuffer = TUniformBufferRef<FGaussianSplatShaderResources>{ Resource->GetUniformBuffer() };
		TUniformBufferRef<FPrimitiveUniformShaderParameters> PrimitiveUniformBuffer = TUniformBufferRef<FPrimitiveUniformShaderParameters>{ Proxy->GetProxyUniformBuffer() };

		FGSPrepassCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGSPrepassCS::FParameters>();
		PassParameters->View = InView.ViewUniformBuffer;

		PassParameters->GSResource = ResourceUniformBuffer;
		PassParameters->Primitive = PrimitiveUniformBuffer;

		PassParameters->SplatIndexBuffer = Proxy->GetIndexBufferSRV();
		PassParameters->SplatIndexCounter = Proxy->GetIndexCountBufferSRV();

		PassParameters->SplatScaleMultiplier = Proxy->GetScaleMultiplier();
		PassParameters->SplatOpacityThreshold = CVarGaussiansOpacityThreshold.GetValueOnRenderThread();

		PassParameters->HZB = Gaussians::GetHZBParameters(GraphBuilder, ViewInfo, Gaussians::EHZBType::FurthestHZB);

		PassParameters->Counter = GraphBuilder.CreateUAV(RasterIndirectArgsBuffer, PF_R32_UINT);

		PassParameters->OutSSPositions = GraphBuilder.CreateUAV(NDCPositionsBuffer, PF_G32R32F);
		PassParameters->OutSSScale = GraphBuilder.CreateUAV(NDCScalesBuffer, PF_A32B32G32R32F);
		PassParameters->OutConic = GraphBuilder.CreateUAV(ConicsBuffer);
		PassParameters->OutColorOpacity = GraphBuilder.CreateUAV(ColorOpacityBuffer, PF_A32B32G32R32F);
		PassParameters->OutGaussianIDX = GraphBuilder.CreateUAV(GaussianIndexBuffer, PF_R32_UINT);
		PassParameters->OutDepth = GraphBuilder.CreateUAV(GaussianDepthBuffer, PF_R32_UINT);

		if (bRelighting)
		{
			PassParameters->OutNormal = GraphBuilder.CreateUAV(NormalBuffer, PF_R32_UINT);
			PassParameters->OutMetallicRoughness = GraphBuilder.CreateUAV(MetallicRoughnessBuffer, PF_R32_UINT);
			PassParameters->OutSSVelocity = GraphBuilder.CreateUAV(ScreenSpaceVelocityBuffer, PF_R32G32_UINT);
		}

		PassParameters->BufferCapacity = Capacity;

		PassParameters->IndirectArgs = ComputeIndirectArgs;

		FComputeShaderUtils::AddPass<FGSPrepassCS>(
			GraphBuilder,
			RDG_EVENT_NAME("GSPrepass"),
			PrePassShader,
			PassParameters,
			ComputeIndirectArgs,
			/*IndirectArgsOffset:*/ 0
		);
	}

	// If no splats survived culling then no further work required
	if (!bAnySplatsRendered)
	{
		return;
	}

	// Sort all projected screens-space splats along view axis
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, GaussiansSort, "Gaussians: Radix Sort");

		TArray<FRDGBufferRef> InKeys   = { GaussianDepthBuffer, GraphBuilder.CreateBuffer(GaussianDepthBuffer->Desc, TEXT("Gaussians.PingPongDepth")) };
		TArray<FRDGBufferRef> InValues = { GaussianIndexBuffer, GraphBuilder.CreateBuffer(GaussianIndexBuffer->Desc, TEXT("Gaussians.PingPongIDX")) };

		uint32 Index = GPURadixSort::SortBufferIndirect(
			GraphBuilder, 
			InKeys,
			InValues, 
			0,
			RasterIndirectArgsBuffer,
			1,
			UINT32_MAX, 
			GMaxRHIFeatureLevel,
			!bRelighting  // Unlit splats must be sorted back-to-front
		);

		GaussianIndexBuffer = InValues[Index];
		GaussianDepthBuffer = InKeys[Index];
	}

	// Rasterize splats into the render target
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, GaussiansRaster, "Gaussians: Rasterization");

		auto PassParameters = GraphBuilder.AllocParameters<FSplattingPassPipelineParameters>();

		PassParameters->VertexShader.View = InView.ViewUniformBuffer;
		PassParameters->VertexShader.VS_SSPositions = GraphBuilder.CreateSRV(NDCPositionsBuffer, PF_G32R32F);
		PassParameters->VertexShader.VS_SSScale = GraphBuilder.CreateSRV(NDCScalesBuffer, PF_A32B32G32R32F);
		PassParameters->VertexShader.VS_Conic = GraphBuilder.CreateSRV(ConicsBuffer);
		PassParameters->VertexShader.VS_ColorOpacity = GraphBuilder.CreateSRV(ColorOpacityBuffer, PF_A32B32G32R32F);
		PassParameters->VertexShader.VS_GaussianIDX = GraphBuilder.CreateSRV(GaussianIndexBuffer, PF_R32_UINT);
		PassParameters->VertexShader.VS_Depth = GraphBuilder.CreateSRV(GaussianDepthBuffer, PF_R32_UINT);

		if (bRelighting)
		{
			PassParameters->VertexShader.VS_Normal = GraphBuilder.CreateSRV(NormalBuffer, PF_R32_UINT);
			PassParameters->VertexShader.VS_MetallicRoughness = GraphBuilder.CreateSRV(MetallicRoughnessBuffer, PF_R32_UINT);
			PassParameters->VertexShader.VS_SSVelocity = GraphBuilder.CreateSRV(ScreenSpaceVelocityBuffer, PF_R32G32_UINT);
		}

		PassParameters->VertexShader.IndirectArgs = RasterIndirectArgsBuffer;

		PassParameters->PixelShader.View = InView.ViewUniformBuffer;
		// TODO: Verify quantity of render targets based on whether relighting is enabled or not
		PassParameters->PixelShader.RenderTargets = RenderTargets;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("GSRasterizePass"),
			PassParameters,
			ERDGPassFlags::Raster,
			[bRelighting, PassParameters, VertexShader, PixelShader, ViewRect = ViewInfo.ViewRect](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

				// Initialize the Pipeline State
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				// Set up blend/depth/rasterizer states
				GraphicsPSOInit.BlendState = bRelighting ? FRelitBlendState::GetRHI() : FUnlitBlendState::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.DepthStencilState = bRelighting ? FRelitDepthStencilState::GetRHI() : FUnlitDepthStencilState::GetRHI();

				// Set shaders
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI; //using SV_VertexID
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

				// Set shader parameters
				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VertexShader);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PixelShader);

				// Execute the Indirect Draw
				// The RHI needs the actual underlying FRHIBuffer for the indirect call
				FRHIBuffer* IndirectArgsRHIBuffer = PassParameters->VertexShader.IndirectArgs->GetIndirectRHICallBuffer();
				uint32 ArgumentOffset = 0; // Offset in bytes if your buffer contains multiple draw args

				RHICmdList.DrawPrimitiveIndirect(IndirectArgsRHIBuffer, ArgumentOffset);
			}
		);
	}

	// Copy into transient stats buffer
	if (GPUStats)
	{
		// The num instances set in the indirect args tells how many splats were actually sent for rasterization
		GPUStats->SetStatFromGPU(
			GraphBuilder, 
			bRelighting ? FGSGPUStats::Stat_LitSplatsRendered : FGSGPUStats::Stat_UnlitSplatsRendered,
			RasterIndirectArgsBuffer,
			sizeof(uint32)
		);
	}
}
