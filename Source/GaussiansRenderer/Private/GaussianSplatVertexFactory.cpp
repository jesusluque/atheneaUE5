#include "GaussianSplatVertexFactory.h"

#include "GaussianSplatInterface.h"
#include "GaussianSplatResource.h"
#include "MeshMaterialShader.h"
#include "MeshDrawShaderBindings.h"
#include "PrimitiveUniformShaderParameters.h"


IMPLEMENT_VERTEX_FACTORY_TYPE(FGaussianSplatVertexFactory, "/Plugin/StarLightGS/GSVertexFactory.ush",
	  EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsDynamicLighting
	// athenea: no lightmap coordinates are fetched, so no static lighting
	// permutations; the factory only draws shadow depth.
	//| EVertexFactoryFlags::SupportsManualVertexFetch
	| EVertexFactoryFlags::SupportsPositionOnly
	//| EVertexFactoryFlags::SupportsPrecisePrevWorldPos
	//| EVertexFactoryFlags::SupportsCachingMeshDrawCommands
	| EVertexFactoryFlags::SupportsPrimitiveIdStream
	//| EVertexFactoryFlags::SupportsTriangleSorting
	//| EVertexFactoryFlags::SupportsRayTracing
	//| EVertexFactoryFlags::SupportsRayTracingDynamicGeometry
	//| EVertexFactoryFlags::SupportsPSOPrecaching
)

FGaussianSplatVertexFactory::FGaussianSplatVertexFactory(ERHIFeatureLevel::Type InFeatureLevel) : FVertexFactory(InFeatureLevel)
{
	//we dont need declartion as we are fetching data from buffers directly
	//bNeedsDeclaration = false; // it will crash still if you do not create a declaration // this literally appears to do absolutely nothing 
};

class FGaussianSplatVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FGaussianSplatVertexFactoryShaderParameters, NonVirtual);
public:

	void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType VertexStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		FVertexFactoryShaderParameters::GetElementShaderBindings(Scene, View, Shader, VertexStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams);

		const FGaussianSplatVertexFactory* SplatVertexFactory = static_cast<const FGaussianSplatVertexFactory*>(VertexFactory);
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FGaussianSplatShaderResources>(), SplatVertexFactory->GetGSplatDataUniformBuffer());
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FGaussianSplatPrimitiveShaderResources>(), SplatVertexFactory->GetGSplatPrimitiveDataUniformBuffer());
	}
};

IMPLEMENT_TYPE_LAYOUT(FGaussianSplatVertexFactoryShaderParameters);

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FGaussianSplatVertexFactory, SF_Vertex, FGaussianSplatVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FGaussianSplatVertexFactory, SF_Pixel, FGaussianSplatVertexFactoryShaderParameters);
#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FGaussianSplatVertexFactory, SF_Compute, FGaussianSplatVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FGaussianSplatVertexFactory, SF_RayHitGroup, FGaussianSplatVertexFactoryShaderParameters);
#endif // RHI_RAYTRACING

bool FGaussianSplatVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	//if (Parameters.MaterialParameters.bIsMasked || Parameters.MaterialParameters.bIsSpecialEngineMaterial) 
	return true;
}

void FGaussianSplatVertexFactory::GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType,
	FVertexDeclarationElementList& Elements)
{
	/*if (UseGPUScene(GMaxRHIShaderPlatform, GMaxRHIFeatureLevel)
			&& !PlatformGPUSceneUsesUniformBufferView(GMaxRHIShaderPlatform))
	{
		switch (VertexInputStreamType)
		{
		case EVertexInputStreamType::Default:
		{
			Elements.Add(FVertexElement(0, 0, VET_UInt, 0, sizeof(uint32), true));
			break;
		}
		case EVertexInputStreamType::PositionOnly:
		{
			Elements.Add(FVertexElement(0, 0, VET_UInt, 0, sizeof(uint32), true));
			break;
		}
		case EVertexInputStreamType::PositionAndNormalOnly:
		{
			Elements.Add(FVertexElement(0, 0, VET_UInt, 0, sizeof(uint32), true));
			break;
		}
		default:
			checkNoEntry();
		}
	}*/
}

void FGaussianSplatVertexFactory::ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform,
                                                         const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors)
{
	if (Type->SupportsPrimitiveIdStream()
		&& UseGPUScene(Platform)
		&& !IsMobilePlatform(Platform) // On mobile VS may use PrimtiveUB while GPUScene is enabled
		&& ParameterMap.ContainsParameterAllocation(FPrimitiveUniformShaderParameters::FTypeInfo::GetStructMetadata()->GetShaderVariableName()))
	{
		OutErrors.AddUnique(*FString::Printf(TEXT("Shader attempted to bind the Primitive uniform buffer even though Vertex Factory %s computes a PrimitiveId per-instance.  This will break auto-instancing.  Shaders should use GetPrimitiveData(Parameters).Member instead of Primitive.Member."), Type->GetName()));
	}
}

void FGaussianSplatVertexFactory::ModifyCompilationEnvironment(
	const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	if (RHISupportsManualVertexFetch(Parameters.Platform))
	{
		OutEnvironment.SetDefineIfUnset(TEXT("MANUAL_VERTEX_FETCH"), TEXT("1"));
	}

	const bool bVFSupportsPrimtiveSceneData = Parameters.VertexFactoryType->SupportsPrimitiveIdStream() && UseGPUScene(Parameters.Platform);
	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), bVFSupportsPrimtiveSceneData);
}

void FGaussianSplatVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	// Check if this vertex factory has a valid feature level that is supported by the current platform
	check(HasValidFeatureLevel());

	//From the Virtual Heightfield Mesh Vertex Factory, we need to have at least one stream to create the declaration, even if it is not used. 

	FVertexStream NullVertexStream;
	NullVertexStream.VertexBuffer = nullptr;
	NullVertexStream.Stride = 0;
	NullVertexStream.Offset = 0;
	NullVertexStream.VertexStreamUsage = EVertexStreamUsage::ManualFetch;

	check(Streams.Num() == 0);
	Streams.Add(NullVertexStream);

	FVertexDeclarationElementList ElementsDefault;
	FVertexDeclarationElementList ElementsPosOnly;
	FVertexDeclarationElementList ElementsPosNormal;
	//Elements.Add(FVertexElement(0, 0, VET_None, 0, 0, false));

	AddPrimitiveIdStreamElement(EVertexInputStreamType::Default, ElementsDefault, 0, 0);
	AddPrimitiveIdStreamElement(EVertexInputStreamType::PositionOnly, ElementsPosOnly, 0, 0);
	AddPrimitiveIdStreamElement(EVertexInputStreamType::PositionAndNormalOnly, ElementsPosNormal, 0, 0);

	//all declarations are identical

	InitDeclaration(ElementsDefault);
	InitDeclaration(ElementsPosOnly, EVertexInputStreamType::PositionOnly);
	InitDeclaration(ElementsPosNormal, EVertexInputStreamType::PositionAndNormalOnly);
}

void FGaussianSplatVertexFactory::ReleaseRHI()
{
	FVertexFactory::ReleaseRHI();

	GSplatPrimitiveDataUniform.SafeRelease();
	GSplatDataUniform.SafeRelease();
}
