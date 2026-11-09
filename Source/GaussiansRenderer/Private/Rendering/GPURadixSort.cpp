#include "GPURadixSort.h"

#include "ShaderParameterStruct.h"
#include "GlobalShader.h"
#include "RenderGraph.h"
#include "ShaderCompilerCore.h"


/*-----------------------------------------------------
	This is based on GPUSort.h/cpp implemented in Unreal Engine
	However, there are two critical differences:
	1.	This implementation supports sorting an array of keys, rather than two linked arrays of key-value pairs
	2.	This sort should be GPU-driven - i.e., the number of items to sort is not known to the CPU
		but instead determined by prior GPU work.
-----------------------------------------------------*/

// --- Global State --- //

#define GPUSORT_BITCOUNT 32
#define RADIX_BITS 4
#define DIGIT_COUNT (1 << RADIX_BITS)
#define KEYS_PER_LOOP 8
#define THREAD_COUNT 128
#define TILE_SIZE (THREAD_COUNT * KEYS_PER_LOOP)
#define MAX_GROUP_COUNT 64
#define MAX_PASS_COUNT (32 / RADIX_BITS)


void SetRadixSortShaderCompilerEnvironment1(FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("RADIX_BITS"), RADIX_BITS);
	OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), THREAD_COUNT);
	OutEnvironment.SetDefine(TEXT("KEYS_PER_LOOP"), KEYS_PER_LOOP);
	OutEnvironment.SetDefine(TEXT("MAX_GROUP_COUNT"), MAX_GROUP_COUNT);
	OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
}

// --- Required Structures --- //

// This is populated on the GPU prior to the sort beginning
struct FRadixSortParameters
{
	uint32 TilesPerGroup;
	uint32 ExtraTileCount;
	uint32 ExtraKeyCount;
	uint32 GroupCount;
};


// --- Kernels --- //

class FGPURadixSortPopulateParametersCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGPURadixSortPopulateParametersCS);
	SHADER_USE_PARAMETER_STRUCT(FGPURadixSortPopulateParametersCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, Counter)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FRadixSortParameters>, RWRadixSortParameterBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWIndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("RADIX_SORT_POPULATE_PARAMETERS"), 1);
		SetRadixSortShaderCompilerEnvironment1(OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGPURadixSortPopulateParametersCS, "/Plugin/StarLightGS/RadixSort.usf", "RadixSortPopulateParametersCS", SF_Compute)

class FGPURadixSortClearOffsetsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGPURadixSortClearOffsetsCS);
	SHADER_USE_PARAMETER_STRUCT(FGPURadixSortClearOffsetsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutOffsets)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("RADIX_SORT_CLEAR_OFFSETS"), 1);
		SetRadixSortShaderCompilerEnvironment1(OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGPURadixSortClearOffsetsCS, "/Plugin/StarLightGS/RadixSort.usf", "RadixSortClearOffsetsCS", SF_Compute)

class FGPURadixSortUpsweepCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGPURadixSortUpsweepCS);
	SHADER_USE_PARAMETER_STRUCT(FGPURadixSortUpsweepCS, FGlobalShader);

	class FRadixSortDescending : SHADER_PERMUTATION_BOOL("RADIX_SORT_DESCENDING");
	using FPermutationDomain = TShaderPermutationDomain<FRadixSortDescending>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, RadixShift)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FRadixSortParameters>, RadixSortParameterBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, InKeys)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutOffsets)

		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("RADIX_SORT_UPSWEEP"), 1);
		SetRadixSortShaderCompilerEnvironment1(OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGPURadixSortUpsweepCS, "/Plugin/StarLightGS/RadixSort.usf", "RadixSortUpsweepCS", SF_Compute)

class FGPURadixSortSpineCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGPURadixSortSpineCS);
	SHADER_USE_PARAMETER_STRUCT(FGPURadixSortSpineCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, InOffsets)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutOffsets)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("RADIX_SORT_SPINE"), 1);
		SetRadixSortShaderCompilerEnvironment1(OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGPURadixSortSpineCS, "/Plugin/StarLightGS/RadixSort.usf", "RadixSortSpineCS", SF_Compute)

class FGPURadixSortDownsweepCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGPURadixSortDownsweepCS);
	SHADER_USE_PARAMETER_STRUCT(FGPURadixSortDownsweepCS, FGlobalShader);

	class FRadixSortValues : SHADER_PERMUTATION_BOOL("RADIX_SORT_VALUES");
	class FRadixSortDescending : SHADER_PERMUTATION_BOOL("RADIX_SORT_DESCENDING");
	using FPermutationDomain = TShaderPermutationDomain<FRadixSortValues, FRadixSortDescending>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, RadixShift)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FRadixSortParameters>, RadixSortParameterBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, InKeys)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutKeys)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, InOffsets)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, InValues)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutValues)

		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("RADIX_SORT_DOWNSWEEP"), 1);
		SetRadixSortShaderCompilerEnvironment1(OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGPURadixSortDownsweepCS, "/Plugin/StarLightGS/RadixSort.usf", "RadixSortDownsweepCS", SF_Compute)


// --- Public interface --- //

uint32 GPURadixSort::SortBufferIndirect(
	FRDGBuilder& GraphBuilder, 
	TArrayView<FRDGBufferRef> InKeyBuffers,
	/* Optional */ TArrayView<FRDGBufferRef> InValueBuffers,
	int32 BufferIndex, 
	FRDGBufferRef Counter, 
	uint32 CounterOffset, 
	uint32 KeyMask, 
	ERHIFeatureLevel::Type FeatureLevel,
	bool bDescending)
{
	check(InKeyBuffers.Num() >= 2); // Only element 0 and 1 will ever be used, but it's not invalid to have a larger array
	check(InKeyBuffers[0] && InKeyBuffers[1]);
	check(BufferIndex >= 0 && BufferIndex < 2);

	bool bSortValues = !InValueBuffers.IsEmpty();
	if (bSortValues)
	{
		check(InValueBuffers.Num() >= 2);
		check(InValueBuffers[0] && InValueBuffers[1]);
	}

	TStaticArray<FRDGBufferSRVRef, 2> KeysSRV	= { GraphBuilder.CreateSRV(InKeyBuffers[0], PF_R32_UINT), GraphBuilder.CreateSRV(InKeyBuffers[1], PF_R32_UINT) };
	TStaticArray<FRDGBufferUAVRef, 2> KeysUAV	= { GraphBuilder.CreateUAV(InKeyBuffers[0], PF_R32_UINT), GraphBuilder.CreateUAV(InKeyBuffers[1], PF_R32_UINT) };
	TStaticArray<FRDGBufferSRVRef, 2> ValuesSRV{};
	TStaticArray<FRDGBufferUAVRef, 2> ValuesUAV{};
	if (bSortValues)
	{
		ValuesSRV = { GraphBuilder.CreateSRV(InValueBuffers[0], PF_R32_UINT), GraphBuilder.CreateSRV(InValueBuffers[1], PF_R32_UINT) };
		ValuesUAV = { GraphBuilder.CreateUAV(InValueBuffers[0], PF_R32_UINT), GraphBuilder.CreateUAV(InValueBuffers[1], PF_R32_UINT) };
	}

	return SortBufferIndirect(
		GraphBuilder,
		KeysSRV,
		KeysUAV,
		bSortValues ? ValuesSRV : TArrayView<FRDGBufferSRVRef>{},
		bSortValues ? ValuesUAV : TArrayView<FRDGBufferUAVRef>{},
		BufferIndex,
		Counter,
		CounterOffset,
		KeyMask,
		FeatureLevel,
		bDescending
	);
}

uint32 GPURadixSort::SortBufferIndirect(
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
	bool bDescending
)
{
	check(BufferIndex >= 0 && BufferIndex < 2);

	// Only element 0 and 1 will ever be used, but it's not invalid to have a larger array
	check(InKeySRVs.Num() >= 2); 
	check(InKeySRVs[0] && InKeySRVs[1]);
	check(InKeyUAVs.Num() >= 2);
	check(InKeyUAVs[0] && InKeyUAVs[1]);

	bool bSortValues = !InValueSRVs.IsEmpty();
	if (bSortValues)
	{
		check(InValueSRVs.Num() >= 2);
		check(InValueSRVs[0] && InValueSRVs[1]);
		check(InValueUAVs.Num() >= 2);
		check(InValueUAVs[0] && InValueUAVs[1]);
	}

	auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FGPURadixSortPopulateParametersCS> PopulateParametersCS(ShaderMap);
	TShaderMapRef<FGPURadixSortClearOffsetsCS> ClearOffsetsCS(ShaderMap);

	FGPURadixSortUpsweepCS::FPermutationDomain UpsweepPermutation;
	UpsweepPermutation.Set<FGPURadixSortUpsweepCS::FRadixSortDescending>(bDescending);
	TShaderMapRef<FGPURadixSortUpsweepCS> UpsweepCS(ShaderMap, UpsweepPermutation);

	TShaderMapRef<FGPURadixSortSpineCS> SpineCS(ShaderMap);

	FGPURadixSortDownsweepCS::FPermutationDomain DownsweepPermutation;
	DownsweepPermutation.Set<FGPURadixSortDownsweepCS::FRadixSortValues>(bSortValues);
	DownsweepPermutation.Set<FGPURadixSortDownsweepCS::FRadixSortDescending>(bDescending);
	TShaderMapRef<FGPURadixSortDownsweepCS> DownsweepCS(ShaderMap, DownsweepPermutation);

	// Create parameter and indirect args buffers
	FRDGBufferRef ParameterBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(FRadixSortParameters), 1), TEXT("GPURadixSort.Parameters")
	);
	FRDGBufferRef IndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(), TEXT("GPURadixSort.IndirectArgs"));
	{
		FGPURadixSortPopulateParametersCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGPURadixSortPopulateParametersCS::FParameters>();

		auto desc = FRDGBufferSRVDesc{ Counter, static_cast<uint32>(CounterOffset * sizeof(uint32)), 1 };
		desc.Format = PF_R32_UINT;

		PassParameters->Counter = GraphBuilder.CreateSRV(desc);
		PassParameters->RWRadixSortParameterBuffer = GraphBuilder.CreateUAV(ParameterBuffer);
		PassParameters->RWIndirectArgs = GraphBuilder.CreateUAV(IndirectArgs);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SetupParameters"),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			PopulateParametersCS,
			PassParameters,
			FIntVector(1, 1, 1)
		);
	}

	// Allocate transients for offsets
	TStaticArray<FRDGBufferRef, 2> OffsetBuffers;
	auto OffsetBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), DIGIT_COUNT * MAX_GROUP_COUNT);
	for (uint32 Buffer = 0; Buffer < 2; Buffer++)
	{
		OffsetBuffers[Buffer] = GraphBuilder.CreateBuffer(OffsetBufferDesc, Buffer == 0 ? TEXT("GPURadixSort.OffsetBuffer0") : TEXT("GPURadixSort.OffsetBuffer1"));
	}

	// Determine how many passes are required.
	constexpr int32 BitCount = GPUSORT_BITCOUNT;
	constexpr int32 PassCount = BitCount / RADIX_BITS;

	// Execute sort passes
	uint32 RadixShift = 0;
	uint32 PassBits = DIGIT_COUNT - 1;
	for (int32 PassIndex = 0; PassIndex < PassCount; PassIndex++)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "RadixSort(Pass=%d)", PassIndex);

		if ((PassBits & KeyMask) != 0)
		{
			// Step 1: Clear offsets
			{
				FGPURadixSortClearOffsetsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGPURadixSortClearOffsetsCS::FParameters>();
				PassParameters->OutOffsets = GraphBuilder.CreateUAV(OffsetBuffers[0], PF_R32_UINT);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ClearOffsets"),
					ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
					ClearOffsetsCS,
					PassParameters,
					FIntVector{ 1, 1, 1}
				);
			}
			
			// Step 2: Upsweep
			{
				FGPURadixSortUpsweepCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGPURadixSortUpsweepCS::FParameters>();
				PassParameters->RadixShift = RadixShift;
				PassParameters->RadixSortParameterBuffer = GraphBuilder.CreateSRV(ParameterBuffer);

				PassParameters->InKeys = InKeySRVs[BufferIndex];
				PassParameters->OutOffsets = GraphBuilder.CreateUAV(OffsetBuffers[0], PF_R32_UINT);

				PassParameters->IndirectArgs = IndirectArgs;

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Upsweep"),
					PassParameters,
					ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
					[PassParameters, UpsweepCS, IndirectArgs](FRHICommandList& RHICmdList)
					{
						FComputeShaderUtils::DispatchIndirect(
							RHICmdList,
							UpsweepCS,
							*PassParameters,
							IndirectArgs,
							0
						);
					});
			}

			// Step 3: Spine
			{
				FGPURadixSortSpineCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGPURadixSortSpineCS::FParameters>();
				PassParameters->InOffsets = GraphBuilder.CreateSRV(OffsetBuffers[0], PF_R32_UINT);
				PassParameters->OutOffsets = GraphBuilder.CreateUAV(OffsetBuffers[1], PF_R32_UINT);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Spine"),
					ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
					SpineCS,
					PassParameters,
					FIntVector{1, 1, 1}
				);
			}

			// Step 4: Downsweep
			{
				FGPURadixSortDownsweepCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGPURadixSortDownsweepCS::FParameters>();
				PassParameters->RadixShift = RadixShift;
				PassParameters->RadixSortParameterBuffer = GraphBuilder.CreateSRV(ParameterBuffer);

				PassParameters->InKeys = InKeySRVs[BufferIndex];
				PassParameters->OutKeys = InKeyUAVs[BufferIndex ^ 0x1];

				PassParameters->InOffsets = GraphBuilder.CreateSRV(OffsetBuffers[1], PF_R32_UINT);

				if (bSortValues)
				{
					PassParameters->InValues = InValueSRVs[BufferIndex];
					PassParameters->OutValues = InValueUAVs[BufferIndex ^ 0x1];
				}

				PassParameters->IndirectArgs = IndirectArgs;

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Downsweep"),
					PassParameters,
					ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
					[PassParameters, DownsweepCS, IndirectArgs](FRHICommandList& RHICmdList)
					{
						FComputeShaderUtils::DispatchIndirect(
							RHICmdList,
							DownsweepCS,	
							*PassParameters,
							IndirectArgs,
							0
						);
					});
			}
			
			BufferIndex ^= 0x1;
		}

		// Update the radix shift for the next pass
		RadixShift += RADIX_BITS;
		PassBits <<= RADIX_BITS;
	}

	// Return the buffer containing the sorted results
	return BufferIndex;
}
