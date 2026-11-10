#pragma once


#include "CoreMinimal.h"
#include "RHIFwd.h"

class FSceneView;

class FGaussianSplatResource;


// Describes functionality of a renderable gaussian splat across module boundaries
// Implemented by GaussianSplatSceneProxy
class GAUSSIANSRENDERER_API IGaussianSplatInterface
{
public:
	virtual ~IGaussianSplatInterface() = default;

	virtual FGaussianSplatResource* GetResource_RenderThread() const = 0;
	virtual FRHIUniformBuffer* GetProxyUniformBuffer() const = 0;

	virtual const FBoxSphereBounds& GetSplatBounds() const = 0;
	virtual bool IsSplatShown(const FSceneView* InView) const = 0;

	virtual FRHIShaderResourceView* GetIndexBufferSRV() const = 0;
	virtual FRHIUnorderedAccessView* GetIndexBufferUAV() const = 0;

	virtual FRHIBuffer* GetIndexCountBuffer() const = 0;
	virtual FRHIShaderResourceView* GetIndexCountBufferSRV() const = 0;
	virtual FRHIUnorderedAccessView* GetIndexCountBufferUAV() const = 0;

	virtual bool RequiresNewCullingPass() const = 0;
	// athenea: where the cloud stands, for a level of detail cut each frame.
	virtual FMatrix GetSplatLocalToWorld() const = 0;
	virtual void MarkIndexBufferUpdated() = 0;

	virtual bool SupportsRelighting() const = 0;

	virtual float GetScaleMultiplier() const = 0;
	// athenea: whether the cloud casts shadows: its culled index list is what the
	// shadow passes draw too, so a cut by the view's frustum is theirs as well.
	virtual bool CastsSplatShadow() const = 0;
	virtual bool HasAnyClippingTags(const struct FGameplayTagContainer& Tags, bool bExactMatch) const = 0;
};
