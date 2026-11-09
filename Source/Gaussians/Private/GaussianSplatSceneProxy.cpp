#include "GaussianSplatSceneProxy.h"

#include "GaussiansRenderer.h"
#include "GaussianSplatComponent.h"
#include "GaussianSplatResource.h"
#include "GaussianSplatVertexFactory.h"

#include "GSWorldSubsystem.h"
#include "GSViewExtension.h"


FGaussianSplatSceneProxy::FGaussianSplatSceneProxy(const UGaussianSplatComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
	, ScaleMultiplier(InComponent->SplatScaleMultiplier)
	, ClippingTags(InComponent->ClippingTags)
	, bClippingTagsExactMatchOnly(InComponent->bClippingTagsExactMatchOnly)
	, VertexFactory(GMaxRHIFeatureLevel)
{
	if (InComponent->GaussianSplatAsset)
	{
		// Get GPU resources from asset
		// This pointer can be assigned from game thread but NOT dereferenced
		GaussianSplatResource_RenderThread = InComponent->GaussianSplatAsset->GetOrCreateResource();
	}

	ShadowMaterialProxy = InComponent->ShadowMaterial ? InComponent->ShadowMaterial->GetRenderProxy() : nullptr;

	// Get view extension from world subsystem
	if (UWorld* World = InComponent->GetWorld())
	{
		if (UGSWorldSubsystem* Subsystem = World->GetSubsystem<UGSWorldSubsystem>())
		{
			ViewExtension = Subsystem->GetViewExtension();
		}
	}

	bSupportsRuntimeVirtualTexture = true;
	bIsStaticMesh = false;
}

FGaussianSplatSceneProxy::~FGaussianSplatSceneProxy()
{
	// All rendering resources are released in DestroyRenderThreadResources
}

void FGaussianSplatSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
	check(IsInRenderingThread());
	FPrimitiveSceneProxy::CreateRenderThreadResources(RHICmdList);

	if (GaussianSplatResource_RenderThread)
	{
		CreateIndexBufferResources(RHICmdList);

		// Create the primitive data uniform buffer to be used by the vertex factory (ownership is passed to vertex factory)
		FGaussianSplatPrimitiveShaderResources Parameters;
		Parameters.SplatIndexBuffer = IndexBufferSRV;
		Parameters.SplatIndexCountBuffer = IndexCountBufferSRV;
		FUniformBufferRHIRef SplatPrimitiveUniformBuffer = TUniformBufferRef<FGaussianSplatPrimitiveShaderResources>::CreateUniformBufferImmediate(
			Parameters, UniformBuffer_MultiFrame);

		VertexFactory.InitResource(RHICmdList);
		VertexFactory.SetGSplatDataUniformBuffer(GaussianSplatResource_RenderThread->GetResource()->GetUniformBuffer());
		VertexFactory.SetGSplatPrimitiveDataUniformBuffer(MoveTemp(SplatPrimitiveUniformBuffer));

		if (auto ViewExt = ViewExtension.Pin())
		{
			ViewExt->RegisterProxy(this);
		}
	}
}

void FGaussianSplatSceneProxy::DestroyRenderThreadResources()
{
	check(IsInRenderingThread());
	FPrimitiveSceneProxy::DestroyRenderThreadResources();

	if (GaussianSplatResource_RenderThread)
	{
		if (auto ViewExt = ViewExtension.Pin())
		{
			ViewExt->UnregisterProxy(this);
		}

		VertexFactory.ReleaseResource();

		{
			IndexCountBufferSRV.SafeRelease();
			IndexCountBufferUAV.SafeRelease();
			IndexCountBuffer.SafeRelease();

			IndexBufferSRV.SafeRelease();
			IndexBufferUAV.SafeRelease();
			IndexBuffer.SafeRelease();
		}

		GaussianSplatResource_RenderThread.Reset();
	}
}

SIZE_T FGaussianSplatSceneProxy::GetTypeHash() const
{
	static SIZE_T UniquePtr;
	return reinterpret_cast<SIZE_T>(&UniquePtr);
}

uint32 FGaussianSplatSceneProxy::GetMemoryFootprint() const
{
	return sizeof(*this) + GetAllocatedSize();
}

FPrimitiveViewRelevance FGaussianSplatSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;

	Result.bDrawRelevance = IsShown(View) & !View->bIsGameView & !View->Family->EngineShowFlags.Editor & !View->Family->EngineShowFlags.Game;

#ifdef WITH_EDITOR
		Result.bDrawRelevance |= View->Family->EngineShowFlags.HitProxies;
#endif
	
	//Result.bDrawRelevance = View->Family->EngineShowFlags.HitProxies; //IsShown(View); //false;// ; // to draw only shadows

	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bRenderInDepthPass = ShouldRenderInDepthPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow; 

	Result.bShadowRelevance = IsShadowCast(View);
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
	
	Result.bDynamicRelevance = true;

	return Result;
}

void FGaussianSplatSceneProxy::GetDynamicMeshElements(
	const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily, 
	uint32 VisibilityMap, 
	FMeshElementCollector& Collector) const
{
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

			if (IsSelected())
			{
				RenderBounds(PDI, ViewFamily.EngineShowFlags, GetBounds(), true);
			}

			if (VertexFactory.IsInitialized() && ShadowMaterialProxy && Gaussians::IsShadowsEnabled())
			{
				FMeshBatch& Batch = Collector.AllocateMesh();

				Batch.Type = PT_TriangleList;
				Batch.bUseForDepthPass = true;
				Batch.CastShadow = true;
				 
				Batch.CastRayTracedShadow = false;
				Batch.bRenderToVirtualTexture = true;
				Batch.bUseAsOccluder = true;
				Batch.ReverseCulling = IsLocalToWorldDeterminantNegative();
				Batch.bDisableBackfaceCulling = true;
				Batch.DepthPriorityGroup = GetDepthPriorityGroup(Views[ViewIndex]);
				Batch.bCanApplyViewModeOverrides = true;
				Batch.SegmentIndex = 0;

				Batch.MaterialRenderProxy = ShadowMaterialProxy;

				Batch.VertexFactory = &VertexFactory;

				FMeshBatchElement& MeshElement = Batch.Elements[0];
				MeshElement.IndexBuffer = nullptr;
				MeshElement.FirstIndex = 0;

				// TODO: Use indirect arguments to draw shadow (draw indirect args are already populated and stored in IndexCountBuffer)
				// One quad per splat -> 2 triangles per quad
				MeshElement.NumPrimitives = 2 * GaussianSplatResource_RenderThread->GetResource()->GetSplatCount();
				MeshElement.NumInstances = 1;
				MeshElement.MinVertexIndex = 0;
				MeshElement.MaxVertexIndex = 0;

				Collector.AddMesh(ViewIndex, Batch);
			}
		}
	}
}

FGaussianSplatResource* FGaussianSplatSceneProxy::GetResource_RenderThread() const
{
	check(IsInRenderingThread());
	return GaussianSplatResource_RenderThread->GetResource();
}

FRHIShaderResourceView* FGaussianSplatSceneProxy::GetIndexBufferSRV() const
{
	check(IsInRenderingThread());
	return IndexBufferSRV;
}

FRHIUnorderedAccessView* FGaussianSplatSceneProxy::GetIndexBufferUAV() const
{
	check(IsInRenderingThread());
	return IndexBufferUAV;
}

FRHIBuffer* FGaussianSplatSceneProxy::GetIndexCountBuffer() const
{
	check(IsInRenderingThread());
	return IndexCountBuffer;
}

FRHIShaderResourceView* FGaussianSplatSceneProxy::GetIndexCountBufferSRV() const
{
	check(IsInRenderingThread());
	return IndexCountBufferSRV;
}

FRHIUnorderedAccessView* FGaussianSplatSceneProxy::GetIndexCountBufferUAV() const
{
	check(IsInRenderingThread());
	return IndexCountBufferUAV;
}

bool FGaussianSplatSceneProxy::RequiresNewCullingPass() const
{
	// A cloud with levels of detail is cut again every frame: what it draws
	// follows the eye.
	const FGaussianSplatResource* Resource = GetResource_RenderThread();
	return bRequiresIndexBufferUpdate || (Resource != nullptr && Resource->GetLodLevels() > 0);
}

void FGaussianSplatSceneProxy::MarkIndexBufferUpdated()
{
	bRequiresIndexBufferUpdate = false;
}

bool FGaussianSplatSceneProxy::SupportsRelighting() const
{
	check(IsInRenderingThread());
	return GaussianSplatResource_RenderThread->GetResource()->SupportsRelighting();
}

bool FGaussianSplatSceneProxy::HasAnyClippingTags(const struct FGameplayTagContainer& Tags, bool bExactMatch) const
{
	return (bClippingTagsExactMatchOnly || bExactMatch) ? ClippingTags.HasAnyExact(Tags) : ClippingTags.HasAny(Tags);
}

void FGaussianSplatSceneProxy::CreateIndexBufferResources(FRHICommandListBase& RHICmdList)
{
	check(IsInRenderingThread());

	uint32 NumSplats = GaussianSplatResource_RenderThread->GetResource()->GetSplatCount();

	FRHIBufferCreateDesc CreateDesc = FRHIBufferCreateDesc::Create(
		TEXT("GSplatProxyIndexBuffer"),
		sizeof(uint32) * NumSplats,
		sizeof(uint32),
		EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess
	).DetermineInitialState();
	IndexBuffer = RHICmdList.CreateBuffer(CreateDesc);

	IndexBufferSRV = RHICmdList.CreateShaderResourceView(
		IndexBuffer,
		FRHIViewDesc::CreateBufferSRV()
		.SetTypeFromBuffer(IndexBuffer)
		.SetFormat(PF_R32_UINT)
	);

	IndexBufferUAV = RHICmdList.CreateUnorderedAccessView(
		IndexBuffer,
		FRHIViewDesc::CreateBufferUAV()
		.SetTypeFromBuffer(IndexBuffer)
		.SetFormat(PF_R32_UINT)
	);

	CreateDesc = FRHIBufferCreateDesc::Create(
		TEXT("GSplatProxyIndexCountBuffer"),
		sizeof(FRHIDrawIndirectParameters),
		sizeof(uint32),
		EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::DrawIndirect
	).DetermineInitialState()
		.SetInitActionInitializer();

	FRHIBufferInitializer Initializer = RHICmdList.CreateBufferInitializer(CreateDesc);
	FRHIDrawIndirectParameters DrawParameters{ 0, 1, 0, 0 };
	Initializer.WriteData(&DrawParameters, sizeof(DrawParameters));
	IndexCountBuffer = Initializer.Finalize();

	IndexCountBufferSRV = RHICmdList.CreateShaderResourceView(
		IndexCountBuffer,
		FRHIViewDesc::CreateBufferSRV()
		.SetTypeFromBuffer(IndexCountBuffer)
		.SetFormat(PF_R32_UINT)
	);

	IndexCountBufferUAV = RHICmdList.CreateUnorderedAccessView(
		IndexCountBuffer,
		FRHIViewDesc::CreateBufferUAV()
		.SetTypeFromBuffer(IndexCountBuffer)
		.SetFormat(PF_R32_UINT)
	);
}
