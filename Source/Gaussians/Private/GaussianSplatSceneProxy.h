#pragma once

#include "GameplayTagContainer.h"
#include "GaussianSplatInterface.h"
#include "GaussianSplatVertexFactory.h"

#include "PrimitiveSceneProxy.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "MeshBatch.h"
#include "Materials/MaterialInterface.h"


class UGaussianSplatComponent;
class FGaussianSplatResource;
class FGaussianSplatResourceWrapper;
class FGSViewExtension;


class FGaussianSplatSceneProxy : public FPrimitiveSceneProxy, public IGaussianSplatInterface
{
public:
	FGaussianSplatSceneProxy(const UGaussianSplatComponent* InComponent);
	virtual ~FGaussianSplatSceneProxy();

	//~ Begin FPrimitiveSceneProxy Interface
	virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;
	virtual void DestroyRenderThreadResources() override;

	virtual SIZE_T GetTypeHash() const override;
	virtual uint32 GetMemoryFootprint() const override;

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views, 
		const FSceneViewFamily& ViewFamily, 
		uint32 VisibilityMap, 
		FMeshElementCollector& Collector) const override;
	//~ End FPrimitiveSceneProxy Interface

	//~ Begin IGaussianSplatInterface
	virtual FGaussianSplatResource* GetResource_RenderThread() const override;
	virtual inline FRHIUniformBuffer* GetProxyUniformBuffer() const override { return GetUniformBuffer(); }

	virtual inline const FBoxSphereBounds& GetSplatBounds() const override { return GetBounds(); };
	virtual inline bool IsSplatShown(const FSceneView* InView) const override { return IsShown(InView); };

	virtual FRHIShaderResourceView* GetIndexBufferSRV() const override;
	virtual FRHIUnorderedAccessView* GetIndexBufferUAV() const override;

	virtual FRHIBuffer* GetIndexCountBuffer() const override;
	virtual FRHIShaderResourceView* GetIndexCountBufferSRV() const override;
	virtual FRHIUnorderedAccessView* GetIndexCountBufferUAV() const override;

	virtual bool RequiresNewCullingPass() const override;
	virtual void MarkIndexBufferUpdated() override;

	virtual bool SupportsRelighting() const override;

	virtual inline float GetScaleMultiplier() const override { return ScaleMultiplier; }
	virtual bool HasAnyClippingTags(const FGameplayTagContainer& Tags, bool bExactMatch) const override;
	//~ End IGaussianSplatInterface

private:

	void CreateIndexBufferResources(FRHICommandListBase& RHICmdList);

private:
	// GPU Resources for the asset pointed to by the component
	// It is NOT safe to dereference this pointer on the game thread
	TSharedPtr<FGaussianSplatResourceWrapper> GaussianSplatResource_RenderThread;

	FBufferRHIRef IndexBuffer = nullptr;
	FShaderResourceViewRHIRef IndexBufferSRV = nullptr;
	FUnorderedAccessViewRHIRef IndexBufferUAV = nullptr;

	FBufferRHIRef IndexCountBuffer = nullptr;
	FShaderResourceViewRHIRef IndexCountBufferSRV = nullptr;
	FUnorderedAccessViewRHIRef IndexCountBufferUAV = nullptr;

	bool bRequiresIndexBufferUpdate = true;

	TWeakPtr<FGSViewExtension> ViewExtension;

	// Per-instance properties
	float ScaleMultiplier = 1.0f;

	FGameplayTagContainer ClippingTags;
	bool bClippingTagsExactMatchOnly = false;

	bool bDrawVFInMainPass = false;

	//Proxy vertex factory
	FGaussianSplatVertexFactory VertexFactory;
	FMaterialRenderProxy* ShadowMaterialProxy;
};
