#include "GSDispatchHelper.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"

class FGSSetupIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSSetupIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FGSSetupIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(Buffer<uint>, CountBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint3>, RWIndirectArgs)

		SHADER_PARAMETER(uint32, GroupDivisor)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FGSSetupIndirectArgsCS, "/Plugin/StarLightGS/GSDispatchHelper.usf", "GSSetupIndirectArgsCS", SF_Compute);


void Gaussians::AddSetupDispatchIndirectArgsPass(
	FRDGBuilder& GraphBuilder, 
	FRHIShaderResourceView* CountSRV, 
	FRDGBuffer* IndirectArgsBuffer, 
	uint32 ThreadGroupSize1D
)
{
	check(IsInRenderingThread());

	FGSSetupIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGSSetupIndirectArgsCS::FParameters>();
	PassParameters->CountBuffer = CountSRV;
	PassParameters->RWIndirectArgs = GraphBuilder.CreateUAV(IndirectArgsBuffer, PF_R32_UINT);
	PassParameters->GroupDivisor = ThreadGroupSize1D;

	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FGSSetupIndirectArgsCS> SetupIndirectArgsCS(ShaderMap);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("GSCopyIndirectArgs"),
		ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
		SetupIndirectArgsCS,
		PassParameters,
		FIntVector(1, 1, 1)
	);
}
