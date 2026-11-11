#pragma once


#ifdef __cplusplus
#include "HLSLTypeAliases.h"

namespace UE::HLSL
{
#endif

struct FGaussians_ClippingVolume
{
	float3 Origin;
	uint ModeAndShape;

	float4 AxisX_HalfExtentX;
	float4 AxisY_HalfExtentY;
	float4 AxisZ_HalfExtentZ;

	void PackModeAndShape(uint Mode, uint Shape)
	{
		ModeAndShape = (Mode << 16) | (Shape & 0xFFFF);
	}

	uint GetMode()
	{
		return ModeAndShape >> 16;
	}

	uint GetShape()
	{
		return ModeAndShape & 0xFFFF;
	}
};

#define GAUSSIANS_CLIPPING_MODE_CLIP_INSIDE_ALL  0
#define GAUSSIANS_CLIPPING_MODE_CLIP_INSIDE_ANY  1
#define GAUSSIANS_CLIPPING_MODE_CLIP_OUTSIDE_ALL 2
#define GAUSSIANS_CLIPPING_MODE_CLIP_OUTSIDE_ANY 3

// athenea: the most lamp groups a cloud's layers are drawn for.
#define GS_MAX_LIGHT_LAYERS 16
// athenea: the most local lights a TX cloud is lit by in a view.
#define GS_MAX_LOCAL_LIGHTS 8

#define GAUSSIANS_CLIPPING_SHAPE_BOX	0
#define GAUSSIANS_CLIPPING_SHAPE_SPHERE 1

#ifdef __cplusplus
}

using FGaussians_ClippingVolume = UE::HLSL::FGaussians_ClippingVolume;

#endif
