// Copyright (c) 2026 jesus luque.
#pragma once

#include "CoreMinimal.h"

class UTextureRenderTarget2D;
class UTexture2D;

namespace Gaussians
{
	// athenea: how far a frame is from its reference, in 8-bit sRGB steps of
	// each pixel's largest channel difference (athenea's compareImages:
	// p99, max and the share over 2): computed on the device
	// (GSCompare.usf), only the histogram read back.
	struct FFrameCompare
	{
		bool bValid = false;
		double Mean = 0.0;     // steps
		uint32 P99 = 0;        // steps
		uint32 Max = 0;        // steps
		double Over2 = 0.0;    // share of pixels more than 2 steps apart
		uint64 Pixels = 0;     // compared: those the reference's alpha keeps
	};

	// Game thread; waits for the GPU. The two of the same size.
	GAUSSIANSRENDERER_API FFrameCompare CompareFrames(UTextureRenderTarget2D* Frame, UTexture2D* Reference);
}
