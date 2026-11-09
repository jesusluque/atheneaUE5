#pragma once

#include "RHIShaderPlatform.h"
#include "RenderGraphFwd.h"
#include "RHIFeatureLevel.h"
#include "RHIFwd.h"


namespace GPURadixSort
{

	// GPU-driven radix sort (number of elements to sort is supplied by previous GPU work)
	// Requires two buffers to operate (ping-pong)
	// Can optionally sort arrays of values along with the keys
	// BufferIndex specifies the buffer containing unsorted data initially
	// Counter offset is the index (in num uints - not num bytes) into the buffer containing the counter to use for creating dispatch indirect args
	// Returns index of buffer containing sorted result
	// Implemented in RadixSort.cpp
	uint32 SortBufferIndirect(
		FRDGBuilder& GraphBuilder, 
		TArrayView<FRDGBufferRef> InKeyBuffers,
		/* Optional */ TArrayView<FRDGBufferRef> InValueBuffers,
		int32 BufferIndex, 
		FRDGBufferRef Counter, 
		uint32 CounterOffset, 
		uint32 KeyMask, 
		ERHIFeatureLevel::Type FeatureLevel,
		bool bDescending = false
	);

	// Overload with finer-grained control, for example in a situation with sub-allocating out of buffers
	// Can specify the region of the buffer to be sorted manually with the SRV/UAV
	uint32 SortBufferIndirect(
		FRDGBuilder& GraphBuilder,
		TArrayView<FRDGBufferSRVRef> InKeySRVs,
		TArrayView<FRDGBufferUAVRef> InKeyUAVs,
		/* Optional */ TArrayView<FRDGBufferSRVRef> InValueSRVs,
		/* Optional */ TArrayView<FRDGBufferUAVRef> InValueUAVs,
		int32 BufferIndex,
		FRDGBufferRef Counter,
		uint32 CounterOffset,
		uint32 KeyMask,
		ERHIFeatureLevel::Type FeatureLevel,
		bool bDescending = false
	);

}
