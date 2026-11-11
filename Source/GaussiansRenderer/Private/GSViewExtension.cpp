#include "GSViewExtension.h"

#include "GaussiansRenderer.h"
#include "GaussianSplatInterface.h"
#include "GaussianSplatResource.h"

#include "Rendering/GSGPUStats.h"
#include "Rendering/GSplatGBuffer.h"
#include "Rendering/GSExportPass.h"

#include "RenderGraphUtils.h"
#include "PostProcess/PostProcessInputs.h"
#include "ScenePrivate.h"

#include "Subsystems/WorldSubsystem.h"

DEFINE_STAT(STAT_SplatsSubmittedLit)
DEFINE_STAT(STAT_SplatsSubmittedUnlit)
DEFINE_STAT(STAT_SplatsRenderedLit)
DEFINE_STAT(STAT_SplatsRenderedUnlit)

DECLARE_GPU_STAT_NAMED(GaussiansLit, TEXT("Gaussians: Lit"));
// athenea: the unlit splats timed as the lit ones are, so a profile shows them.
DECLARE_GPU_STAT_NAMED(GaussiansUnlit, TEXT("Gaussians: Unlit"));


static TAutoConsoleVariable<bool> CVarGaussiansDisableViewExtension(
	TEXT("r.Gaussians.Debug.DisableViewExtension"),
	false,
	TEXT("Debug tool to disable primary view rendering via view extension."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<bool> CVarGaussiansForceCullEveryFrame(
	TEXT("r.Gaussians.Debug.CullEveryFrame"),
	false,
	TEXT("Forces splat culling (against clipping volumes) to run every frame."),
	ECVF_RenderThreadSafe
);


FGSViewExtension::FGSViewExtension(const FAutoRegister& AutoRegister, UWorldSubsystem* Owner, IGSClippingVolumeContainerInterface* InClippingVolumeContainer)
	: FSceneViewExtensionBase(AutoRegister)
	, OwningSubsystem(Owner)
	, ClippingVolumeContainer(InClippingVolumeContainer)
{
	check(OwningSubsystem && ClippingVolumeContainer);

	GPUStats = MakePimpl<FGSGPUStats>();
}

void FGSViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	// Build collection of clipping volumes and send to render thread
	TArray<FGSClippingVolumeProxy> ClippingVolumeProxies = ClippingVolumeContainer->BuildClippingVolumeProxies();
	Algo::Sort(ClippingVolumeProxies, [](const auto& VolA, const auto& VolB){ return VolA.Priority < VolB.Priority; });

	ENQUEUE_RENDER_COMMAND(CopyGaussianSplatClippingVolumes)([this, TempVolumes = MoveTemp(ClippingVolumeProxies)](FRHICommandListImmediate& RHICommandList)
	{
		ClippingVolumeParameters = TempVolumes;

		GPUStats->PollStats(RHICommandList);
	});
}

void FGSViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	
}

void FGSViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
}

void FGSViewExtension::PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	GPUStats->ProcessBeginRender(GraphBuilder);

	// athenea: skinned clouds posed before anything reads them (shadows, the
	// cut, the prepass), once a family; the first directional light is the sun
	// their parts' baked visibility is read toward.
	FVector4f SunToLight(0, 0, 0, 0);
	if (const FScene* Scene = InViewFamily.Scene ? InViewFamily.Scene->GetRenderScene() : nullptr)
	{
		if (Scene->DirectionalLights.Num() > 0 && Scene->DirectionalLights[0] && Scene->DirectionalLights[0]->Proxy)
		{
			const FVector ToLight = -Scene->DirectionalLights[0]->Proxy->GetDirection();
			SunToLight = FVector4f(FVector3f(ToLight), 1.0f);
		}
	}
	for (IGaussianSplatInterface* Proxy : RelightableProxies.Union(NonRelightableProxies))
	{
		FGaussianSplatResource* Resource = Proxy ? Proxy->GetResource_RenderThread() : nullptr;
		if (Resource && Resource->IsSkinned())
		{
			Resource->Skin_RenderThread(GraphBuilder, Proxy->GetSkinSeconds(), Proxy->GetSplatLocalToWorld(), SunToLight);
		}
	}
}

void FGSViewExtension::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView,
                                                               const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
	RDG_EVENT_SCOPE_STAT(GraphBuilder, GaussiansLit, "Gaussians(Lit)");
	SCOPED_NAMED_EVENT(GaussiansLit, FColor::Purple);

	check(InView.bIsViewInfo);
	FViewInfo& ViewInfo = static_cast<FViewInfo&>(InView);

	// Must force engine to use a velocity texture if nobody else does
	const FSceneTextures& SceneTexturesData = ViewInfo.GetSceneTextures();
	if (!HasBeenProduced(SceneTexturesData.Velocity))
	{
		AddClearRenderTargetPass(GraphBuilder, SceneTexturesData.Velocity);
	}

	// Run culling for all splats that need it
	{
		bool bForceEnable = ClippingVolumeContainer->GetForceClippingEvaluation() || CVarGaussiansForceCullEveryFrame.GetValueOnRenderThread();
		TSet<IGaussianSplatInterface*> AllProxies = RelightableProxies.Union(NonRelightableProxies);

		GSCullingPass(
			GraphBuilder,
			InView,
			AllProxies,
			ClippingVolumeParameters,
			bForceEnable,
			GetLodBudget(GraphBuilder, InView)
		);

		ClippingVolumeContainer->ClearClippingDirtyFlags();
	}

	// Process relightable proxies
	if (!RelightableProxies.IsEmpty() && Gaussians::IsRelightingEnabled_RenderThread())
	{
		FGSplatGBuffer GSplatGBuffer;
		GSplatGBuffer.Init(GraphBuilder, InView);

		{
			RDG_EVENT_SCOPE(GraphBuilder, "Gaussians: Clear GSplatGBuffer");
			GSplatGBuffer.Clear(GraphBuilder);
		}

		GaussianSplattingPass(GraphBuilder, InView, GSplatGBuffer.GetRenderTargetBindings(false, SceneTexturesData.Depth.Target), RelightableProxies, GPUStats.Get(), GetLodBudget(GraphBuilder, InView));
		GSCompositionPass(GraphBuilder, InView, GSplatGBuffer, RenderTargets);

		// This hack is required for the case where the scene contains only splats and no other
		// scene proxies that declare themselves with Substrate view relevance.
		// In those cases, the deferred direct lighting pipeline would otherwise never be executed.
		// By setting this flag we ensure that the deferred direct lighting pipeline will definitely be executed when lit splats are present.
		if (Substrate::IsSubstrateEnabled())
		{
			ViewInfo.SubstrateViewData.UsesTileTypeMask |= ViewInfo.SubstrateViewData.UsesTileTypeMask ? 0u : 1u;
		}
	}
}

void FGSViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView,
	const FPostProcessingInputs& Inputs)
{
	// Render all splats that don't support relighting
	if (!NonRelightableProxies.IsEmpty() || !Gaussians::IsRelightingEnabled_RenderThread())
	{
		check(InView.bIsViewInfo);
		const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(InView);

		RDG_EVENT_SCOPE_STAT(GraphBuilder, GaussiansUnlit, "Gaussians: Unlit");
		const FSceneTextureUniformParameters* SceneTextures = Inputs.SceneTextures->GetContents();
		FRDGTextureRef SceneColorTexture = SceneTextures->SceneColorTexture;
		FRDGTextureRef SceneVelocityTexture = ViewInfo.GetSceneTextures().Velocity;
		FRDGTextureRef SceneDepthTexture = SceneTextures->SceneDepthTexture;

		FRenderTargetBindingSlots RenderTargets{};
		RenderTargets[0] = FRenderTargetBinding{ SceneColorTexture, ERenderTargetLoadAction::ELoad };
		RenderTargets[1] = FRenderTargetBinding{ SceneVelocityTexture, ERenderTargetLoadAction::ELoad };
		RenderTargets.DepthStencil = FDepthStencilBinding{
			SceneDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilNop
		};

		if (!Gaussians::IsRelightingEnabled_RenderThread())
		{
			// If relighting is disabled, then render all relightable gaussians with non-relightable gaussians
			TSet<IGaussianSplatInterface*> AllProxies = RelightableProxies.Union(NonRelightableProxies);
			if (!AllProxies.IsEmpty())
			{
				GaussianSplattingPass(GraphBuilder, InView, RenderTargets, AllProxies, GPUStats.Get(), GetLodBudget(GraphBuilder, InView));
			}
		}
		else
		{
			GaussianSplattingPass(GraphBuilder, InView, RenderTargets, NonRelightableProxies, GPUStats.Get(), GetLodBudget(GraphBuilder, InView));
		}
	}
}

void FGSViewExtension::PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	GPUStats->ProcessEndRender(GraphBuilder);
}

FRDGBufferRef FGSViewExtension::GetLodBudget(FRDGBuilder& GraphBuilder, const FSceneView& InView)
{
	const uint32 Key = InView.State ? InView.State->GetViewKey() : 0u;
	if (TRefCountPtr<FRDGPooledBuffer>* Pooled = LodBudgets.Find(Key))
	{
		return GraphBuilder.RegisterExternalBuffer(*Pooled);
	}
	// A new view starts at the threshold as set: factor 1, nothing wanted.
	FRDGBufferRef Budget = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 4), TEXT("Gaussians.LodBudget"));
	static const uint32 Initial[4] = { 0x3f800000u, 0u, 0u, 0u };
	GraphBuilder.QueueBufferUpload(Budget, Initial, sizeof(Initial), ERDGInitialDataFlags::NoCopy);
	LodBudgets.Add(Key, GraphBuilder.ConvertToExternalBuffer(Budget));
	return Budget;
}

bool FGSViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	//check if main viewport is valid and if the world of the context match the world of the owning subsystem
	return
		(
			Context.GetWorld() == OwningSubsystem->GetWorld()
		) && 
		!CVarGaussiansDisableViewExtension.GetValueOnGameThread();
}

void FGSViewExtension::RegisterProxy(IGaussianSplatInterface* Proxy)
{
	check(IsInRenderingThread());
	if (Proxy->SupportsRelighting())
	{
		check(!RelightableProxies.Contains(Proxy));
		RelightableProxies.Add(Proxy);
	}
	else
	{
		check(!NonRelightableProxies.Contains(Proxy));
		NonRelightableProxies.Add(Proxy);
	}
}

void FGSViewExtension::UnregisterProxy(IGaussianSplatInterface* Proxy)
{
	check(IsInRenderingThread());
	if (Proxy->SupportsRelighting())
	{
		check(RelightableProxies.Contains(Proxy));
		RelightableProxies.Remove(Proxy);
	}
	else
	{
		check(NonRelightableProxies.Contains(Proxy));
		NonRelightableProxies.Remove(Proxy);
	}
}


void FGSViewExtension::CopySplatGPUToCPU(
	IGaussianSplatInterface* Proxy,
	TArray<FVector3f>& Positions, 
	TArray<FVector3f>& Scales,
	TArray<FQuat4f>& Orientations, 
	TArray<FVector4f>& SHAndOpacities, 
	TArray<FVector3f>& Normals,
	TArray<FVector2f>& MetallicRoughness)
{
	check(IsInGameThread());

	ENQUEUE_RENDER_COMMAND(GaussiansExportClippedSplat)([&Positions, &Scales, &Orientations, &SHAndOpacities, &Normals, &MetallicRoughness, Proxy]
	(FRHICommandListImmediate& RHICmdList)
		{
			bool bRelighting = Proxy->SupportsRelighting();

			FRDGBuilder GraphBuilder(RHICmdList);
			FGaussianSplatReadbackData ReadbackData;

			Gaussians::ExportSplatData(
				GraphBuilder,
				Proxy,
				ReadbackData
			);

			GraphBuilder.Execute();

			// Make sure work is submitted to the GPU immediately so that we can wait on it
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread, ERHISubmitFlags::SubmitToGPU | ERHISubmitFlags::FlushRHIThread);

			// Wait for work to complete on the GPU
			ReadbackData.Fence->Wait(RHICmdList, RHICmdList.GetGPUMask());

			// Now take the data from the buffers to populate CPU arrays that will form the asset
			// First read the splat count to know how big to make the arrays
			uint32 SplatCount = *static_cast<uint32*>(RHICmdList.LockStagingBuffer(ReadbackData.SplatCount, ReadbackData.Fence, 0, sizeof(uint32)));
			RHICmdList.UnlockStagingBuffer(ReadbackData.SplatCount);

			auto DoCopyIntoBuffer = [&]<typename T>(FRHIStagingBuffer* StagingBuffer, TArray<T>&DestArray)
			{
				DestArray.SetNumUninitialized(SplatCount);

				void* DataPtr = RHICmdList.LockStagingBuffer(StagingBuffer, ReadbackData.Fence, 0, SplatCount * sizeof(T));
				FMemory::Memcpy(DestArray.GetData(), DataPtr, SplatCount * sizeof(T));
				RHICmdList.UnlockStagingBuffer(StagingBuffer);
			};

			DoCopyIntoBuffer(ReadbackData.Positions, Positions);
			DoCopyIntoBuffer(ReadbackData.Scales, Scales);
			DoCopyIntoBuffer(ReadbackData.Orientations, Orientations);
			DoCopyIntoBuffer(ReadbackData.SHAndOpacities, SHAndOpacities);
			if (bRelighting)
			{
				DoCopyIntoBuffer(ReadbackData.Normals, Normals);
				DoCopyIntoBuffer(ReadbackData.MetallicRoughness, MetallicRoughness);
			}
		});

	FlushRenderingCommands();
}
