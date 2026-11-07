#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "VertexFactory.h"

/**
 * Gaussian splat factory that fetches splat data form asset uniform and passes it to the mesh pipeline manually
 * used for rendering masked shadow for the gaussian splatting
 */

class FGaussianSplatVertexFactory : public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE_API(FGaussianSplatVertexFactory, GAUSSIANSRENDERER_API)
public:

	GAUSSIANSRENDERER_API FGaussianSplatVertexFactory(ERHIFeatureLevel::Type InFeatureLevel);

	virtual bool RendersPrimitivesAsCameraFacingSprites() const override
	{
		return true;
	};
	
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;

	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	static void GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType, FVertexDeclarationElementList& Elements);
	static void ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors);
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	GAUSSIANSRENDERER_API void SetGSplatDataUniformBuffer(FUniformBufferRHIRef InBuffer) { GSplatDataUniform = MoveTemp(InBuffer); }
	GAUSSIANSRENDERER_API FRHIUniformBuffer* GetGSplatDataUniformBuffer() const { return GSplatDataUniform; }

	GAUSSIANSRENDERER_API void SetGSplatPrimitiveDataUniformBuffer(FUniformBufferRHIRef InBuffer) { GSplatPrimitiveDataUniform = MoveTemp(InBuffer); }
	GAUSSIANSRENDERER_API FRHIUniformBuffer* GetGSplatPrimitiveDataUniformBuffer() const { return GSplatPrimitiveDataUniform; }

private:
	FUniformBufferRHIRef GSplatDataUniform;			 // Data about the gaussian splat model
	FUniformBufferRHIRef GSplatPrimitiveDataUniform; // Data about the gaussian splats that is per-instance
};
