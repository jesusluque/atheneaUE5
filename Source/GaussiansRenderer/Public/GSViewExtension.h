#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "RenderGraphResources.h"

#include "GaussianSplatClipping.h"


DECLARE_STATS_GROUP(TEXT("Gaussians"), STATGROUP_Gaussians, STATCAT_Advanced);

DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Splats Submitted: Lit"), STAT_SplatsSubmittedLit, STATGROUP_Gaussians, GAUSSIANSRENDERER_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Splats Submitted: Unlit"), STAT_SplatsSubmittedUnlit, STATGROUP_Gaussians, GAUSSIANSRENDERER_API);

DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Splats Rendered: Lit"), STAT_SplatsRenderedLit, STATGROUP_Gaussians, GAUSSIANSRENDERER_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Splats Rendered: Unlit"), STAT_SplatsRenderedUnlit, STATGROUP_Gaussians, GAUSSIANSRENDERER_API);


class IGaussianSplatInterface;
class FGSGPUStats;
class FGSplatGBuffer;
class FViewInfo;

// athenea: a view's cut of a primitive's levels of detail (GSLod.ush): the
// culling pass draws by it and the prepass morphs by it.
struct FGSLodCut
{
	FVector3f EyeLocal = FVector3f::ZeroVector;
	float PixelsPerUnit = 0.0f;
	float Threshold = 0.0f;
	float Dissolve = 1.0f;
	float Morph = 0.0f;
	float MorphSplats = 0.0f;
	float Thin = 0.0f;
	float ThinLeast = 0.03125f;
	bool bFrustum = false;
	FVector4f Frustum[4] = { FVector4f(0, 0, 0, 0), FVector4f(0, 0, 0, 0), FVector4f(0, 0, 0, 0), FVector4f(0, 0, 0, 0) };
};
FGSLodCut GSLodCutOf(const IGaussianSplatInterface& Proxy, const FViewInfo& ViewInfo);

/**
 * 
 */
class GAUSSIANSRENDERER_API  FGSViewExtension : public FSceneViewExtensionBase
{
public:
	FGSViewExtension(const FAutoRegister& AutoRegister, UWorldSubsystem* Owner, IGSClippingVolumeContainerInterface* ClippingVolumeContainer);

	//~ Begin ISceneViewExtension Interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;

	virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;

	virtual void PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) override;
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs) override;
	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;

	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	//~ End ISceneViewExtension Interface

private:
	// Renders all splats in given proxies into supplied render targets.
	// Will determine from the proxies if relighting is supported (all proxies must have the same support for relighting)
	// If relighting is supported, supply a GSplatGBuffer to render into. Otherwise, supply the scene depth stencil
	static void GaussianSplattingPass(
		FRDGBuilder& GraphBuilder, 
		const FSceneView& InView, 
		const FRenderTargetBindingSlots& RenderTargets, 
		const TSet<IGaussianSplatInterface*>& Proxies,
		const FGSGPUStats* GPUStats,
		FRDGBufferRef LodBudget
	);

	// Composite splat buffer with GBuffer
	static void GSCompositionPass(
		FRDGBuilder& GraphBuilder, 
		const FSceneView& InView, 
		const FGSplatGBuffer& GSplatGBuffer,
		const FRenderTargetBindingSlots& RenderTargets
	);

	// Creates an index buffer of all non-culled splats
	// The index buffer is then stored along with the proxy
	// The index buffer only needs recalculated when splats or culling volumes move
	static void GSCullingPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& InView,
		const TSet<IGaussianSplatInterface*>& Proxies,
		const TArray<FGSClippingVolumeProxy>& ClippingVolumes,
		bool bForceEnable,
		FRDGBufferRef LodBudget
	);

	// athenea: a view's splat budget (GSplatCulling.usf, GSLodBudgetCS), kept
	// from frame to frame on the device: the factor its cut's threshold is
	// raised by so the splats fit the frame's buffers.
	FRDGBufferRef GetLodBudget(FRDGBuilder& GraphBuilder, const FSceneView& InView);
	TMap<uint32, TRefCountPtr<FRDGPooledBuffer>> LodBudgets;

public:

	// DO NOT CALL EVERY FRAME.
	// Blocks game thread AND render thread until GPU work completes.
	static void CopySplatGPUToCPU(
		IGaussianSplatInterface* Proxy,
		TArray<FVector3f>& OutPositions,
		TArray<FVector3f>& OutScales,
		TArray<FQuat4f>&   OutOrientations,
		TArray<FVector4f>& OutSHAndOpacities,
		TArray<FVector3f>& OutNormals,
		TArray<FVector2f>& OutMetallicRoughness
	);

public:
	void RegisterProxy(IGaussianSplatInterface* Proxy);
	void UnregisterProxy(IGaussianSplatInterface* Proxy);

private:
	// Relit and unlit splats are kept separate as they are rendered in different passes
	TSet<IGaussianSplatInterface*> RelightableProxies;
	TSet<IGaussianSplatInterface*> NonRelightableProxies;

	UWorldSubsystem* OwningSubsystem = nullptr;
	IGSClippingVolumeContainerInterface* ClippingVolumeContainer = nullptr;

	TArray<FGSClippingVolumeProxy> ClippingVolumeParameters;

	TPimplPtr<FGSGPUStats> GPUStats = nullptr;

	TAtomic<bool> bShouldRender{ false };
};
