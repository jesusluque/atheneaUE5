#pragma once

#include "RenderGraphFwd.h"
#include "RHIFwd.h"


namespace Gaussians
{
	void AddSetupDispatchIndirectArgsPass(
		FRDGBuilder& GraphBuilder,
		FRHIShaderResourceView* CountSRV,
		FRDGBuffer* IndirectArgsBuffer,
		uint32 ThreadGroupSize1D
	);
}
