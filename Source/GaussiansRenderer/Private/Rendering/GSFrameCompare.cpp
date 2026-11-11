// Copyright (c) 2026 jesus luque.
#include "GSFrameCompare.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

class FGSCompareCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSCompareCS);
	SHADER_USE_PARAMETER_STRUCT(FGSCompareCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D<float4>, Frame)
		SHADER_PARAMETER_TEXTURE(Texture2D<float4>, Reference)
		SHADER_PARAMETER(FUintVector2, Size)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, Histogram)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSCompareCS, "/Plugin/StarLightGS/GSCompare.usf", "GSCompareCS", SF_Compute);

Gaussians::FFrameCompare Gaussians::CompareFrames(UTextureRenderTarget2D* Frame, UTexture2D* Reference)
{
	FFrameCompare Result;
	if (!Frame || !Reference || !Frame->GetResource() || !Reference->GetResource()
		|| Frame->SizeX != Reference->GetSizeX() || Frame->SizeY != Reference->GetSizeY())
	{
		return Result;
	}
	FlushRenderingCommands();
	TArray<uint32> Histogram;
	Histogram.SetNumZeroed(257);
	const FTextureResource* FrameResource = Frame->GetResource();
	const FTextureResource* ReferenceResource = Reference->GetResource();
	const FUintVector2 Size(Frame->SizeX, Frame->SizeY);
	ENQUEUE_RENDER_COMMAND(GSCompareFrames)([FrameResource, ReferenceResource, Size, &Histogram](FRHICommandListImmediate& RHICmdList)
	{
		FRHIGPUBufferReadback Readback(TEXT("Gaussians.CompareReadback"));
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 257), TEXT("Gaussians.CompareHistogram"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Buffer), 0u);
			FGSCompareCS::FParameters* Parameters = GraphBuilder.AllocParameters<FGSCompareCS::FParameters>();
			Parameters->Frame = FrameResource->TextureRHI;
			Parameters->Reference = ReferenceResource->TextureRHI;
			Parameters->Size = Size;
			Parameters->Histogram = GraphBuilder.CreateUAV(Buffer);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Gaussians.CompareFrames"),
				TShaderMapRef<FGSCompareCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), Parameters,
				FComputeShaderUtils::GetGroupCount(FIntPoint(Size.X, Size.Y), 8));
			AddEnqueueCopyPass(GraphBuilder, &Readback, Buffer, 257 * sizeof(uint32));
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		const uint32* Words = static_cast<const uint32*>(Readback.Lock(257 * sizeof(uint32)));
		FMemory::Memcpy(Histogram.GetData(), Words, 257 * sizeof(uint32));
		Readback.Unlock();
	});
	FlushRenderingCommands();

	// The pixels compared: all but those the reference's alpha leaves out.
	uint64 Pixels = 0;
	for (uint32 s = 0; s < 256; s++)
	{
		Pixels += Histogram[s];
	}
	// p99: the least step that 99 % of the pixels are within.
	const uint64 Within99 = (Pixels * 99 + 99) / 100;
	uint64 Seen = 0, Over2 = 0;
	bool bP99 = false;
	for (uint32 s = 0; s < 256; s++)
	{
		const uint32 n = Histogram[s];
		Result.Max = n > 0 ? s : Result.Max;
		Seen += n;
		Over2 += s > 2 ? n : 0;
		if (!bP99 && Seen >= Within99)
		{
			Result.P99 = s;
			bP99 = true;
		}
	}
	Result.bValid = Pixels > 0;
	Result.Pixels = Pixels;
	Result.Mean = double(Histogram[256]) / double(FMath::Max<uint64>(Pixels, 1));
	Result.Over2 = double(Over2) / double(FMath::Max<uint64>(Pixels, 1));
	return Result;
}
