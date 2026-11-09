#pragma once

#include "ShaderParameterMacros.h"
#include "RenderGraphFwd.h"
#include "RenderGraphResources.h"

class FSceneView;


BEGIN_SHADER_PARAMETER_STRUCT(FGSplatGBufferParameters, )
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, GSplatBaseColor)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, GSplatNormal)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, GSplatDepthMetallicRoughness)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, GSplatVelocity)
END_SHADER_PARAMETER_STRUCT()


// Collection of transient textures that make up the GSplat GBuffer
// Which is then composited with the scene renderer's GBuffer to enable relighting
class FGSplatGBuffer
{
public:
	FGSplatGBuffer();

	// Allocates textures
	void Init(FRDGBuilder& GraphBuilder, const FSceneView& InView);

	// Clears all render targets in the GSplatGBuffer to their clear value
	void Clear(FRDGBuilder& GraphBuilder) const;

	// Use this to bind GSplatGBuffer as render targets. Also binds depth-stencil view
	// If bClear = true, render targets will be cleared on load
	// If a depth buffer is supplied, this will be used for a read-only depth test during rasterization
	FRenderTargetBindingSlots GetRenderTargetBindings(bool bClear = false, FRDGTextureRef DepthBuffer = nullptr) const;

	// Use this to bind GSplatGBuffer as collection of shader parameters for reading
	FGSplatGBufferParameters GetShaderParameters(FRDGBuilder& GraphBuilder) const;

private:
	FRDGTextureRef BaseColorTexture;
	FRDGTextureRef NormalTexture;
	FRDGTextureRef DepthMetallicRoughnessTexture;
	FRDGTextureRef VelocityTexture;
};
