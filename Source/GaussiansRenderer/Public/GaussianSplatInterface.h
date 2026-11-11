#pragma once


#include "CoreMinimal.h"
#include "RHIFwd.h"

class FSceneView;

class FGaussianSplatResource;

// athenea: the most lamp groups a cloud's light layers are drawn for (the
// shaders' GS_MAX_LIGHT_LAYERS, Shaders/Shared/GaussiansDefinitions.h).
static constexpr int32 GSMaxLightLayers = 16;

// athenea: a splat edit as the prepass reads it (GSCompute.usf's GSEditFate).
struct FGSSplatEditParams
{
	FUintVector4 Flags = FUintVector4(0, 0, 0, 0);   // active, shape (1 sphere), mode, invert
	FVector4f Centre = FVector4f(0, 0, 0, 1);         // xyz, opacity
	FVector4f Size = FVector4f(1, 1, 1, 0);           // xyz, min opacity
	FVector4f Tint = FVector4f(1, 1, 1, 1);           // rgb, saturation
	FVector4f More = FVector4f(1, 0, 0, 0);           // brightness, max scale
};


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
	// athenea: each lamp group's weight this frame (rgb), in the asset's
	// LightLayerNames order; GS_MAX_LIGHT_LAYERS of them.
	virtual const FVector4f* GetLightWeights() const = 0;
	virtual const FGSSplatEditParams& GetSplatEdit() const = 0;
	// athenea: seconds into a skinned cloud's poses this component draws.
	virtual float GetSkinSeconds() const = 0;
	// athenea: the Cryptomatte id this component draws alone (0: all).
	virtual uint32 GetMatteId() const = 0;
	// athenea: whether the cloud casts shadows: its culled index list is what the
	// shadow passes draw too, so a cut by the view's frustum is theirs as well.
	virtual bool CastsSplatShadow() const = 0;
	virtual bool HasAnyClippingTags(const struct FGameplayTagContainer& Tags, bool bExactMatch) const = 0;
};
