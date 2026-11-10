#include "GSGPUStats.h"

#include "RenderGraph.h"
#include "RenderGraphResources.h"

#include "GSViewExtension.h"
#include "GaussiansRenderer.h"
#include "HAL/IConsoleManager.h"

// The splats each frame sends to the rasteriser, in the log: what the levels of
// detail are checked by (r.Gaussians.Athenea.LodPixels changes it).
static TAutoConsoleVariable<int32> CVarAtheneaLogSplats(
	TEXT("r.Gaussians.Athenea.LogSplats"), 0,
	TEXT("1 logs the splats drawn in each frame whose count has come back from the GPU."),
	ECVF_RenderThreadSafe);


void FGSGPUStats::PollStats(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread());

	TStaticArray<uint32, Stat_Count> StatValues{ 0 };

	// Poll stats in flight to see if any are complete
	// StatsReadback is only read/written in render thread - no need for mutex
	while (FGPUStat* Stat = StatsReadback.Peek())
	{
		// Stats are inserted chronologically - so front of queue should be completed first
		// Therefore when we find an incomplete sample we do not need to check the rest of the queue
		if (!Stat->Fence->Poll())
			break;

		uint32* StatData = static_cast<uint32*>(RHICmdList.LockStagingBuffer(Stat->StagingBuffer, Stat->Fence, 0, Stat_Count * sizeof(uint32)));
		FMemory::Memcpy(StatValues.GetData(), StatData, Stat_Count * sizeof(uint32));

		RHICmdList.UnlockStagingBuffer(Stat->StagingBuffer);

		// Sample has been processed
		StatsReadback.Pop();
	}

	if (CVarAtheneaLogSplats.GetValueOnRenderThread() != 0 && (StatValues[Stat_LitSplatsRendered] > 0 || StatValues[Stat_UnlitSplatsRendered] > 0))
	{
		UE_LOG(LogGaussiansRenderer, Display, TEXT("athenea: splats drawn %u lit, %u unlit, LodPixels x%.2f"),
			StatValues[Stat_LitSplatsRendered], StatValues[Stat_UnlitSplatsRendered],
			FMath::Max(1.0f, *reinterpret_cast<const float*>(&StatValues[Stat_LodBudgetScale])));
	}

	// Update stats
	if (StatValues[Stat_LitSplatsRendered] > 0)
	{
		SET_DWORD_STAT(STAT_SplatsRenderedLit, StatValues[Stat_LitSplatsRendered]);
	}
	if (StatValues[Stat_UnlitSplatsRendered] > 0)
	{
		SET_DWORD_STAT(STAT_SplatsRenderedUnlit, StatValues[Stat_UnlitSplatsRendered]);
	}
}

void FGSGPUStats::ProcessBeginRender(FRDGBuilder& GraphBuilder)
{
	check(IsInRenderingThread());
	checkf(!StatsTransientBuffer, TEXT("Transient buffer has already been allocated - did you call ProcessBeginRender twice in the same frame?"));

	// Create a new buffer to hold transient stats
	StatsTransientBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), Stat_Count),
		TEXT("Gaussians.TransientStatsBuffer")
	);

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StatsTransientBuffer, PF_R32_UINT), 0);
}

void FGSGPUStats::ProcessEndRender(FRDGBuilder& GraphBuilder)
{
	check(IsInRenderingThread());
	checkf(StatsTransientBuffer, TEXT("Transient buffer has not been allocated - did you call ProcessBeginRender at the start of the frame?"));

	// Queue stats for reading back in future
	FGPUStat Stats;
	Stats.StagingBuffer = RHICreateStagingBuffer();
	Stats.Fence = RHICreateGPUFence(TEXT("GaussiansStatsReadback"));
	Stats.Fence->Clear();

	AddReadbackBufferPass(
		GraphBuilder,
		RDG_EVENT_NAME("GaussiansStatsReadback"),
		StatsTransientBuffer,
		[StatsBuffer = StatsTransientBuffer, StagingBuffer = Stats.StagingBuffer, Fence = Stats.Fence](FRDGAsyncTask, FRHICommandList& CommandList)
		{
			CommandList.CopyToStagingBuffer(StatsBuffer->GetRHI(), StagingBuffer, 0, Stat_Count * sizeof(uint32));
			CommandList.WriteGPUFence(Fence);
		});

	// StatsReadback is only read/written in render thread - no need for mutex
	StatsReadback.Enqueue(Stats);

	// Do not use after this point
	StatsTransientBuffer = nullptr;
}

void FGSGPUStats::SetStatFromGPU(FRDGBuilder& GraphBuilder, EStatID Stat, FRDGBufferRef SourceData, uint32 OffsetInBytes) const
{
	check(IsInRenderingThread());
	checkf(StatsTransientBuffer, TEXT("Transient buffer has not been allocated - did you call ProcessBeginRender at the start of the frame?"));

	uint64 DestOffset = static_cast<uint64>(Stat) * sizeof(uint32);
	AddCopyBufferPass(GraphBuilder, StatsTransientBuffer, DestOffset, SourceData, OffsetInBytes, sizeof(uint32));
}
