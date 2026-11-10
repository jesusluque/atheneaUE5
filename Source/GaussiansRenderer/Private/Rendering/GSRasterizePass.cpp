#include "GaussiansRenderer.h"
#include "GSViewExtension.h"
#include "GaussianSplatResource.h"
#include "GaussianSplatInterface.h"
#include "GSGPUStats.h"

#include "GaussiansHZB.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RendererInterface.h"
#include "RendererClasses.h"
#include "ScreenPass.h"
#include "ScenePrivate.h"

#include "Rendering/GPURadixSort.h"

#include "GaussiansDefinitions.h"
#include "GSDispatchHelper.h"
#include "ScenePrivate.h"
#include "SceneProxies/SkyLightSceneProxy.h"
#include "SystemTextures.h"
#include "PixelShaderUtils.h"

// athenea: each stage of a splat frame timed apart, so a profile says where the
// time goes.
DECLARE_GPU_STAT_NAMED(GaussiansPrePass, TEXT("Gaussians: PrePass"));
DECLARE_GPU_STAT_NAMED(GaussiansSky, TEXT("Gaussians: Sky"));
DECLARE_GPU_STAT_NAMED(GaussiansMediumShadow, TEXT("Gaussians: Medium shadow"));
DECLARE_GPU_STAT_NAMED(GaussiansSort, TEXT("Gaussians: Radix Sort"));
DECLARE_GPU_STAT_NAMED(GaussiansRaster, TEXT("Gaussians: Rasterization"));


static TAutoConsoleVariable<int32> CVarGaussiansMaxSplats(
	TEXT("r.Gaussians.MaxSplats"),
	5'000'000,
	TEXT("The maximum number of splats that can be rendered in a single frame."),
	ECVF_RenderThreadSafe
);

// athenea: what a TX cloud draws in place of its body, to check a stage of it:
// 0 the body; 1 the sky's mean radiance as projected (SkySH[0] C0); 2-5 the
// projected sky along the file's +y, -y, +z, -z; 6 which sharp-lobe inputs
// are on; 7 the albedo as decoded; 100-115 a projected sky coefficient.
static TAutoConsoleVariable<int32> CVarGaussiansAtheneaDebug(
	TEXT("r.Gaussians.Athenea.Debug"),
	0,
	TEXT("A TX cloud draws: 0 its body; 1 the projected sky's mean radiance; 7 its albedo as decoded."),
	ECVF_RenderThreadSafe
);

// athenea: r.Gaussians.Athenea.LogSky 1 reads a TX cloud's projected sky back
// once and logs its 16 coefficients, then sets itself back to 0.
static TAutoConsoleVariable<int32> CVarGaussiansAtheneaResidual(
	TEXT("r.Gaussians.Athenea.Residual"),
	1,
	TEXT("1: a TX cloud reflects the sky without its sun (a residual cube prefiltered by roughness) and the sun analytically; 0: the sky light's own cube, sun included."),
	ECVF_RenderThreadSafe
);

// The residual sky is the world's, the same for every TX cloud: kept from the
// frame that built it while the sky light's cube and colour stay what they were.
static TRefCountPtr<IPooledRenderTarget> GAtheneaResidual;
static FRHITexture* GAtheneaResidualSky = nullptr;
static FLinearColor GAtheneaResidualColor = FLinearColor::Black;

static TAutoConsoleVariable<int32> CVarGaussiansAtheneaLogSky(
	TEXT("r.Gaussians.Athenea.LogSky"),
	0,
	TEXT("1: log the next TX cloud's projected sky (16 rgb coefficients and its sun), once."),
	ECVF_RenderThreadSafe
);
static TUniquePtr<FRHIGPUBufferReadback> GAtheneaSkyReadback;

static void AtheneaLogSkyIfReady()
{
	if (GAtheneaSkyReadback && GAtheneaSkyReadback->IsReady())
	{
		const FVector4f* Values = static_cast<const FVector4f*>(GAtheneaSkyReadback->Lock(18 * sizeof(FVector4f)));
		FString Line;
		for (int32 k = 0; k < 16; k++)
		{
			Line += FString::Printf(TEXT(" %d:(%.5f %.5f %.5f)"), k, Values[k].X, Values[k].Y, Values[k].Z);
		}
		Line += FString::Printf(TEXT(" sun:(%.4f %.4f %.4f cone %.4f) irradiance:(%.4f %.4f %.4f) threshold %.4f"),
			Values[16].X, Values[16].Y, Values[16].Z, Values[16].W, Values[17].X, Values[17].Y, Values[17].Z, Values[17].W);
		GAtheneaSkyReadback->Unlock();
		GAtheneaSkyReadback.Reset();
		UE_LOG(LogTemp, Display, TEXT("athenea sky:%s"), *Line);
	}
}

static TAutoConsoleVariable<float> CVarGaussiansOpacityThreshold(
	TEXT("r.Gaussians.OpacityThreshold"),
	0.0f,
	TEXT("Only render splats with an opacity greater than this threshold."),
	ECVF_RenderThreadSafe
);


class FPermutationEnableRelighting : SHADER_PERMUTATION_BOOL("ENABLE_RELIGHTING");
using FSplattingPermutationDomain = TShaderPermutationDomain<FPermutationEnableRelighting>;

class FGSPrepassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSPrepassCS)
	SHADER_USE_PARAMETER_STRUCT(FGSPrepassCS, FGlobalShader);

	using FPermutationDomain = FSplattingPermutationDomain;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)

		SHADER_PARAMETER_STRUCT_REF(FGaussianSplatShaderResources, GSResource)
		SHADER_PARAMETER_STRUCT_REF(FPrimitiveUniformShaderParameters, Primitive)

		SHADER_PARAMETER_SRV(Buffer<uint>, SplatIndexBuffer)
		SHADER_PARAMETER_SRV(Buffer<uint>, SplatIndexCounter)

		SHADER_PARAMETER(float, SplatScaleMultiplier)
		SHADER_PARAMETER(float, SplatOpacityThreshold)

		// athenea: the sky on the transfer's harmonics, for a TX cloud
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, SkySH)
		SHADER_PARAMETER(uint32, AtheneaDebug)
		SHADER_PARAMETER_TEXTURE(TextureCube, SkyCubemap)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkyCubemapSampler)
		SHADER_PARAMETER(float, SkyMips)
		SHADER_PARAMETER_RDG_TEXTURE(TextureCube, SkyResidual)
		SHADER_PARAMETER(float, SkyResidualMips)
		// athenea: a medium's light reaching each gaussian (GSVolumeShadow.usf)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, VolumeShadow)

		SHADER_PARAMETER_STRUCT_INCLUDE(FGaussiansHZBParameters, HZB)

		// Out
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint32>, Counter)
		// athenea: the view's splat budget, what this frame wanted
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, LodBudgetOut)
		SHADER_PARAMETER(uint32, LodBudgetCap)
		SHADER_PARAMETER(FVector3f, LodEyeLocal)
		SHADER_PARAMETER(float, LodPixelsPerUnit)
		SHADER_PARAMETER(float, LodThreshold)
		SHADER_PARAMETER(float, LodDissolve)
		SHADER_PARAMETER(float, LodMorph)
		SHADER_PARAMETER(float, LodMorphSplats)
		SHADER_PARAMETER(float, LodThin)
		SHADER_PARAMETER(float, LodThinLeast)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float2>, OutSSPositions)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, OutSSScale)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float3>, OutConic)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, OutColorOpacity)
		// athenea: a TX splat's sharp lobes, read per pixel by the raster
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>, OutSharpA)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>, OutSharpB)
		SHADER_PARAMETER(uint32, SharpOut)

		// Only used when relighting is enabled
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutNormal)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutMetallicRoughness)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, OutSSVelocity)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutDepth)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutGaussianIDX)

		SHADER_PARAMETER(uint32, BufferCapacity)
		SHADER_PARAMETER(uint32, TightQuads)
		SHADER_PARAMETER(uint32, MipPay)
		SHADER_PARAMETER(float, MipDilation)

		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), GetThreadGroupSize1D());
	}

	static uint32 GetThreadGroupSize1D() { return 128; }
};

IMPLEMENT_GLOBAL_SHADER(FGSPrepassCS, "/Plugin/StarLightGS/GSCompute.usf", "GSPrepass", SF_Compute);

// athenea: a faint splat's quad sized by its opacity instead of three sigmas.
static TAutoConsoleVariable<int32> CVarGaussiansAtheneaTightQuads(
	TEXT("r.Gaussians.Athenea.TightQuads"), 1,
	TEXT("1: a splat of opacity a is drawn out to sqrt(2 ln 255a) sigmas (where it falls under 1/255), not three; 0: three sigmas."),
	ECVF_RenderThreadSafe);

// athenea: the 0.3 px spread paid back from a gaussian's opacity (Mip-Splatting).
static TAutoConsoleVariable<int32> CVarGaussiansAtheneaMipPaid(
	TEXT("r.Gaussians.Athenea.MipPaid"), 1,
	TEXT("1: a gaussian's opacity times sqrt(det before / det after) the 0.3 px spread, as athenea draws it (Mip-Splatting); 0: the spread at full opacity."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarGaussiansAtheneaMediumShadow(
	TEXT("r.Gaussians.Athenea.MediumShadow"), 1,
	TEXT("1: a medium of gaussians is shadowed by itself (a 64^3 grid each frame); 0: every gaussian sees all of each light."),
	ECVF_RenderThreadSafe);

// athenea: a medium's shadow on itself (GSVolumeShadow.usf): its extinction
// into a grid, then the light reaching each gaussian through it.
BEGIN_SHADER_PARAMETER_STRUCT(FGSVolumeShadowParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT_REF(FGaussianSplatShaderResources, GSResource)
	SHADER_PARAMETER_STRUCT_REF(FPrimitiveUniformShaderParameters, Primitive)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, Grid)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, GridIn)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, OutShadow)
END_SHADER_PARAMETER_STRUCT()

class FGSVolumeDepositCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSVolumeDepositCS)
	SHADER_USE_PARAMETER_STRUCT(FGSVolumeDepositCS, FGlobalShader);
	using FParameters = FGSVolumeShadowParameters;
};
class FGSVolumeShadowCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSVolumeShadowCS)
	SHADER_USE_PARAMETER_STRUCT(FGSVolumeShadowCS, FGlobalShader);
	using FParameters = FGSVolumeShadowParameters;
};
IMPLEMENT_GLOBAL_SHADER(FGSVolumeDepositCS, "/Plugin/StarLightGS/GSVolumeShadow.usf", "GSVolumeDepositCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FGSVolumeShadowCS, "/Plugin/StarLightGS/GSVolumeShadow.usf", "GSVolumeShadowCS", SF_Compute);

// athenea: the sky light projected onto a TX transfer's harmonics, in a
// primitive's file frame (GSSkySH.usf).
class FGSSkySHCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSSkySHCS)
	SHADER_USE_PARAMETER_STRUCT(FGSSkySHCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_TEXTURE(TextureCube, SkyCubemap)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkyCubemapSampler)
		SHADER_PARAMETER(float, SkyMip)
		SHADER_PARAMETER_STRUCT_REF(FGaussianSplatShaderResources, GSResource)
		SHADER_PARAMETER_STRUCT_REF(FPrimitiveUniformShaderParameters, Primitive)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutSkySH)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSSkySHCS, "/Plugin/StarLightGS/GSSkySH.usf", "GSSkySHCS", SF_Compute);

// athenea: the sky without its sun, prefiltered by roughness (GSSkyResidual.usf):
// the cut cube's level 0 and its box mips, then the residual's mips from it.
BEGIN_SHADER_PARAMETER_STRUCT(FGSSkyResidualParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_TEXTURE(TextureCube, SkyCubemap)
	SHADER_PARAMETER_SAMPLER(SamplerState, SkyCubemapSampler)
	SHADER_PARAMETER(float, SkySourceMips)
	SHADER_PARAMETER(float, SkySourceSize)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, SkySH)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(TextureCube, CutCube)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, CutAbove)
	SHADER_PARAMETER(float, CutMips)
	SHADER_PARAMETER(uint32, Mip)
	SHADER_PARAMETER(uint32, Mips)
	SHADER_PARAMETER(uint32, Size)
	SHADER_PARAMETER_STRUCT_REF(FGaussianSplatShaderResources, GSResource)
	SHADER_PARAMETER_STRUCT_REF(FPrimitiveUniformShaderParameters, Primitive)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float4>, OutResidual)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutSunWorld)
END_SHADER_PARAMETER_STRUCT()

#define GS_SKY_RESIDUAL_SHADER(Name) \
class Name : public FGlobalShader \
{ \
	DECLARE_GLOBAL_SHADER(Name) \
	SHADER_USE_PARAMETER_STRUCT(Name, FGlobalShader); \
	using FParameters = FGSSkyResidualParameters; \
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) \
	{ \
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); \
	} \
};
GS_SKY_RESIDUAL_SHADER(FGSSkyCutCS)
GS_SKY_RESIDUAL_SHADER(FGSSkyDownCS)
GS_SKY_RESIDUAL_SHADER(FGSSkyResidualCS)
GS_SKY_RESIDUAL_SHADER(FGSSunWorldCS)
IMPLEMENT_GLOBAL_SHADER(FGSSunWorldCS, "/Plugin/StarLightGS/GSSkyResidual.usf", "GSSunWorldCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FGSSkyCutCS, "/Plugin/StarLightGS/GSSkyResidual.usf", "GSSkyCutCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FGSSkyDownCS, "/Plugin/StarLightGS/GSSkyResidual.usf", "GSSkyDownCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FGSSkyResidualCS, "/Plugin/StarLightGS/GSSkyResidual.usf", "GSSkyResidualCS", SF_Compute);


class FSplattingPassVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSplattingPassVS)

	using FPermutationDomain = FSplattingPermutationDomain;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float2>, VS_SSPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, VS_SSScale)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float3>, VS_Conic)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, VS_ColorOpacity)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint4>, VS_SharpA)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint4>, VS_SharpB)
		SHADER_PARAMETER(uint32, SharpIn)

		// Only used when relighting is enabled
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, VS_Normal) // Octahedron encoding 16bpc
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, VS_MetallicRoughness)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, VS_SSVelocity)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, VS_Depth)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, VS_GaussianIDX)

		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	SHADER_USE_PARAMETER_STRUCT(FSplattingPassVS, FGlobalShader);
};

IMPLEMENT_GLOBAL_SHADER(FSplattingPassVS, "/Plugin/StarLightGS/GSCompute.usf", "GSSplatting_VS", SF_Vertex);

class FSplattingPassPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSplattingPassPS)

	using FPermutationDomain = FSplattingPermutationDomain;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		// athenea: what the sharp lobes are read with, per pixel
		SHADER_PARAMETER_RDG_TEXTURE(TextureCube, SkyResidual)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkyCubemapSampler)
		SHADER_PARAMETER(float, SkyResidualMips)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, SunWorld)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	SHADER_USE_PARAMETER_STRUCT(FSplattingPassPS, FGlobalShader);
};

IMPLEMENT_GLOBAL_SHADER(FSplattingPassPS, "/Plugin/StarLightGS/GSCompute.usf", "GSSplatting_PS", SF_Pixel);


BEGIN_SHADER_PARAMETER_STRUCT(FSplattingPassPipelineParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FSplattingPassVS::FParameters, VertexShader)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSplattingPassPS::FParameters, PixelShader)
END_SHADER_PARAMETER_STRUCT()


static TAutoConsoleVariable<float> CVarGaussiansAtheneaDilation(
	TEXT("r.Gaussians.Athenea.Dilation"), 0.3f,
	TEXT("athenea: the low-pass every gaussian's image is spread by, in pixels squared (0.3: the 3DGS reference's and athenea's). Wider calms the shimmer of a far cloud's sub-pixel gaussians at the cost of sharpness; with MipPaid the spread is paid back from the opacity, so the brightness holds."),
	ECVF_RenderThreadSafe);

// athenea: the splats front to back (GSFront.usf).
static TAutoConsoleVariable<int32> CVarGaussiansAtheneaFrontToBack(
	TEXT("r.Gaussians.Athenea.FrontToBack"), 8,
	TEXT("athenea: unlit splats drawn front to back into a buffer of their own, in this many batches (each twice the one before, at most 16), the pixels already opaque dropping what later batches draw behind them; the buffer then goes over the scene, and where the view has a temporal anti-aliasing the nearest showing splat's velocity with it. 0: back to front, as before."),
	ECVF_RenderThreadSafe);

class FSplattingFrontVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSplattingFrontVS)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSplattingPassVS::FParameters, Base)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, FrontBatches)
		SHADER_PARAMETER(uint32, FrontBatch)
	END_SHADER_PARAMETER_STRUCT()

	SHADER_USE_PARAMETER_STRUCT(FSplattingFrontVS, FGlobalShader);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("GS_FRONT_TO_BACK"), 1);
		OutEnvironment.SetDefine(TEXT("ENABLE_RELIGHTING"), 0);
	}
};
IMPLEMENT_GLOBAL_SHADER(FSplattingFrontVS, "/Plugin/StarLightGS/GSCompute.usf", "GSSplatting_VS", SF_Vertex);

class FSplattingFrontPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSplattingFrontPS)
	using FParameters = FSplattingPassPS::FParameters;
	SHADER_USE_PARAMETER_STRUCT(FSplattingFrontPS, FGlobalShader);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("GS_FRONT_TO_BACK"), 1);
		OutEnvironment.SetDefine(TEXT("ENABLE_RELIGHTING"), 0);
	}
};
IMPLEMENT_GLOBAL_SHADER(FSplattingFrontPS, "/Plugin/StarLightGS/GSCompute.usf", "GSSplatting_PS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FSplattingFrontPipelineParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FSplattingFrontVS::FParameters, VertexShader)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSplattingPassPS::FParameters, PixelShader)
END_SHADER_PARAMETER_STRUCT()

class FGSFrontBatchesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSFrontBatchesCS)
	SHADER_USE_PARAMETER_STRUCT(FGSFrontBatchesCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, FrontCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutFrontArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutFrontBatches)
		SHADER_PARAMETER(uint32, FrontBatchCount)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FGSFrontBatchesCS, "/Plugin/StarLightGS/GSFront.usf", "GSFrontBatchesCS", SF_Compute);

class FGSFrontSaturatePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSFrontSaturatePS)
	SHADER_USE_PARAMETER_STRUCT(FGSFrontSaturatePS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, FrontColor)
		SHADER_PARAMETER(float, FrontOpaque)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FGSFrontSaturatePS, "/Plugin/StarLightGS/GSFront.usf", "GSFrontSaturatePS", SF_Pixel);

class FGSFrontCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSFrontCompositePS)
	SHADER_USE_PARAMETER_STRUCT(FGSFrontCompositePS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, FrontColor)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FGSFrontCompositePS, "/Plugin/StarLightGS/GSFront.usf", "GSFrontCompositePS", SF_Pixel);

class FGSFrontVelocityPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSFrontVelocityPS)
	SHADER_USE_PARAMETER_STRUCT(FGSFrontVelocityPS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FrontNearest)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FGSFrontVelocityPS, "/Plugin/StarLightGS/GSFront.usf", "GSFrontVelocityPS", SF_Pixel);

// Static blend / stencil states

using FRelitBlendState = TStaticBlendState<
	/* Color:       */ CW_RGBA, BO_Add, BF_InverseDestAlpha, BF_One, BO_Add, BF_InverseDestAlpha, BF_One,
	/* Normal:      */ CW_RGBA, BO_Add, BF_InverseDestAlpha, BF_One, BO_Add, BF_InverseDestAlpha, BF_One,
	/* Depth + PBR: */ CW_RGBA, BO_Add, BF_InverseDestAlpha, BF_One, BO_Add, BF_InverseDestAlpha, BF_One,
	/* Velocity: */	   CW_RGBA, BO_Add, BF_InverseDestAlpha, BF_One, BO_Add, BF_InverseDestAlpha, BF_One
>;
using FUnlitBlendState = TStaticBlendState<
	/* Color:    */ CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha,
	/* Velocity: */ CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero
>;

// Front to back: each splat under what the buffer holds.
using FFrontBlendState = TStaticBlendState<
	/* Color:    */ CW_RGBA, BO_Add, BF_InverseDestAlpha, BF_One, BO_Add, BF_InverseDestAlpha, BF_One,
	/* Nearest:  */ CW_RED, BO_Max, BF_One, BF_One, BO_Max, BF_One, BF_One
>;
using FFrontCompositeBlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>;
using FFrontSaturateDepthState = TStaticDepthStencilState<true, CF_Always>;

using FRelitDepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>;
using FUnlitDepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>;


void FGSViewExtension::GaussianSplattingPass(
	FRDGBuilder& GraphBuilder, 
	const FSceneView& InView,
	const FRenderTargetBindingSlots& RenderTargets,
	const TSet<IGaussianSplatInterface*>& Proxies,
	const FGSGPUStats* GPUStats,
	FRDGBufferRef LodBudget)
{
	// Don't call this when there are no proxies
	check(!Proxies.IsEmpty());
	bool bRelighting = (*Proxies.FindArbitraryElement())->SupportsRelighting() && Gaussians::IsRelightingEnabled_RenderThread();

	check(InView.bIsViewInfo);
	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(InView);

	// Create transient buffers
	int32 Capacity = CVarGaussiansMaxSplats.GetValueOnRenderThread();
	uint32 LodBudgetCap = 0;
	// athenea: no more than the frame's clouds hold -- the transient buffers
	// (about 68 bytes a gaussian) were made at r.Gaussians.MaxSplats every
	// frame, 340 MB at the default, whatever was drawn.
	{
		uint64 Held = 0;
		for (auto Proxy : Proxies)
		{
			if (const FGaussianSplatResource* Resource = Proxy->GetResource_RenderThread())
			{
				Held += Resource->GetSplatCount();
			}
		}
		// The budget counts only where the clouds hold more than the buffers.
		LodBudgetCap = Held > uint64(FMath::Max(Capacity, 1)) ? uint32(FMath::Max(Capacity, 1)) : 0u;
		Capacity = int32(FMath::Clamp<uint64>(Held, 1, uint64(FMath::Max(Capacity, 1))));
	}

	FRDGBufferRef NDCPositionsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FVector2f), Capacity), TEXT("Gaussians.NDCPositions"));
	FRDGBufferRef NDCScalesBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), Capacity), TEXT("Gaussians.NDCScales"));
	FRDGBufferRef ConicsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector3f), Capacity), TEXT("Gaussians.Conics"));
	FRDGBufferRef ColorOpacityBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), Capacity), TEXT("Gaussians.ColorOpacity"));

	FRDGBufferRef GaussianIndexBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), Capacity), TEXT("Gaussians.GaussianIDX"));
	FRDGBufferRef GaussianDepthBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), Capacity), TEXT("Gaussians.Depth"));

	// Optional buffers - only in use when relighting enabled
	FRDGBufferRef NormalBuffer = nullptr;
	FRDGBufferRef MetallicRoughnessBuffer = nullptr;
	FRDGBufferRef ScreenSpaceVelocityBuffer = nullptr;
	if (bRelighting)
	{
		NormalBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), Capacity), TEXT("Gaussians.Normal"));
		MetallicRoughnessBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), Capacity), TEXT("Gaussians.MetallicRoughness"));
		ScreenSpaceVelocityBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FUintVector2), Capacity), TEXT("Gaussians.ScreenSpaceVelocity"));
	}

	// Setup indirect args
	FRDGBufferRef ComputeIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(), TEXT("Gaussians.ComputeIndirectArgs"));
	FRDGBufferRef RasterIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(), TEXT("Gaussians.RasterIndirectArgs"));

	// Clear Indirect ARGS
	// Vertex count per instance will be set by dispatch thread 0
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RasterIndirectArgsBuffer, PF_R32_UINT), 0);

	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(InView.GetShaderPlatform());

	FSplattingPermutationDomain Permutation;
	Permutation.Set<FPermutationEnableRelighting>(bRelighting);

	TShaderMapRef<FGSPrepassCS> PrePassShader(ShaderMap, Permutation);
	TShaderMapRef<FSplattingPassVS> VertexShader(ShaderMap, Permutation);
	TShaderMapRef<FSplattingPassPS> PixelShader(ShaderMap, Permutation);

	// athenea: unlit splats front to back, where asked and there is a depth
	// buffer to copy (GSFront.usf).
	const int32 FrontBatches = bRelighting || RenderTargets.DepthStencil.GetTexture() == nullptr || RenderTargets[0].GetTexture() == nullptr
		? 0 : FMath::Clamp(CVarGaussiansAtheneaFrontToBack.GetValueOnRenderThread(), 0, 16);

	bool bAnySplatsRendered = false;
	// athenea: the sky light's captured cubemap, which a TX cloud is lit by
	// (a real-time capture's is not read yet).
	FTexture* SkyTexture = nullptr;
	FLinearColor SkyColor = FLinearColor::Black;
	if (const FScene* Scene = InView.Family && InView.Family->Scene ? InView.Family->Scene->GetRenderScene() : nullptr)
	{
		if (Scene->SkyLight && !Scene->SkyLight->bRealTimeCaptureEnabled && Scene->SkyLight->ProcessedTexture
			&& Scene->SkyLight->ProcessedTexture->TextureRHI)
		{
			SkyTexture = Scene->SkyLight->ProcessedTexture;
			SkyColor = Scene->SkyLight->GetEffectiveLightColor();
		}
	}
	// athenea: the sharp lobes are kept where a TX cloud with curvature is
	// drawn under a sky light and its residual (else one element each).
	bool bSharp = false;
	if (SkyTexture && !bRelighting && CVarGaussiansAtheneaResidual.GetValueOnRenderThread() != 0)
	{
		for (auto Proxy : Proxies)
		{
			const FGaussianSplatResource* Resource = Proxy->GetResource_RenderThread();
			bSharp |= Resource && Resource->HasCurvature();
		}
	}
	const uint32 SharpCount = bSharp ? uint32(Capacity) : 1u;
	FRDGBufferRef SharpABuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FUintVector4), SharpCount), TEXT("Gaussians.SharpA"));
	FRDGBufferRef SharpBBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FUintVector4), SharpCount), TEXT("Gaussians.SharpB"));
	FRDGBufferRef SunWorldBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), 2), TEXT("Gaussians.SunWorld"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SunWorldBuffer), 0u);
	if (!bSharp)
	{
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SharpABuffer, PF_R32G32B32A32_UINT), 0u);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SharpBBuffer, PF_R32G32B32A32_UINT), 0u);
	}
	FRDGTextureRef FrameResidual = nullptr;

	for (auto Proxy : Proxies)
	{
		const FBoxSphereBounds& ProxyBounds = Proxy->GetSplatBounds();
		if (!InView.GetCullingFrustum().IntersectBox(ProxyBounds.Origin, ProxyBounds.BoxExtent) | !Proxy->IsSplatShown(&InView))
		{
			continue;
		}
		bAnySplatsRendered = true;

		RDG_EVENT_SCOPE_STAT(GraphBuilder, GaussiansPrePass, "Gaussians: PrePass");

		// Set up indirect args for this splat
		Gaussians::AddSetupDispatchIndirectArgsPass(
			GraphBuilder,
			Proxy->GetIndexCountBufferSRV(),
			ComputeIndirectArgs,
			FGSPrepassCS::GetThreadGroupSize1D()
		);

		FGaussianSplatResource* Resource = Proxy->GetResource_RenderThread();
		auto SplatCount  = Resource->GetSplatCount();

		if (bRelighting)
		{
			INC_DWORD_STAT_BY(STAT_SplatsSubmittedLit, SplatCount)
		}
		else
		{
			INC_DWORD_STAT_BY(STAT_SplatsSubmittedUnlit, SplatCount)
		}

		TUniformBufferRef<FGaussianSplatShaderResources> ResourceUniformBuffer = TUniformBufferRef<FGaussianSplatShaderResources>{ Resource->GetUniformBuffer() };
		TUniformBufferRef<FPrimitiveUniformShaderParameters> PrimitiveUniformBuffer = TUniformBufferRef<FPrimitiveUniformShaderParameters>{ Proxy->GetProxyUniformBuffer() };

		// athenea: a TX cloud's sky, projected in its own frame this frame; a
		// cloud with no transfer reads none of it.
		FRDGBufferRef SkySHBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), 18), TEXT("Gaussians.SkySH"));
		FRDGTextureRef SkyResidual = nullptr;
		constexpr uint32 ResidualSize = 256, ResidualMips = 9;
		{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, GaussiansSky, "Gaussians: Sky");
		// A medium of gaussians reads its mean and its sun as well.
		if ((Resource->HasTransfer() || Resource->IsVolume()) && SkyTexture)
		{
			FGSSkySHCS::FParameters* SkyParameters = GraphBuilder.AllocParameters<FGSSkySHCS::FParameters>();
			SkyParameters->View = InView.ViewUniformBuffer;
			SkyParameters->SkyCubemap = SkyTexture->TextureRHI;
			SkyParameters->SkyCubemapSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			// A mip of about 64 texels a face, as fine as 16384 directions read.
			const int32 Mips = SkyTexture->TextureRHI->GetDesc().NumMips;
			SkyParameters->SkyMip = float(FMath::Max(0, Mips - 7));
			SkyParameters->GSResource = ResourceUniformBuffer;
			SkyParameters->Primitive = PrimitiveUniformBuffer;
			SkyParameters->OutSkySH = GraphBuilder.CreateUAV(SkySHBuffer);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("GSSkySH"),
				TShaderMapRef<FGSSkySHCS>(ShaderMap), SkyParameters, FIntVector(1, 1, 1));
			AtheneaLogSkyIfReady();
			if (CVarGaussiansAtheneaLogSky.GetValueOnRenderThread() == 1 && !GAtheneaSkyReadback)
			{
				GAtheneaSkyReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("Gaussians.SkySHReadback"));
				AddEnqueueCopyPass(GraphBuilder, GAtheneaSkyReadback.Get(), SkySHBuffer, 18 * sizeof(FVector4f));
				CVarGaussiansAtheneaLogSky->Set(0, ECVF_SetByCode);
			}
		}
		else
		{
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SkySHBuffer), 0u);
		}

		// The sky without its sun, for the reflections, where the sun is taken
		// analytically (GSSkyResidual.usf).
		const bool bResidual = Resource->HasTransfer() && SkyTexture && CVarGaussiansAtheneaResidual.GetValueOnRenderThread() != 0;
		if (bResidual && GAtheneaResidual.IsValid() && GAtheneaResidualSky == SkyTexture->TextureRHI.GetReference() && GAtheneaResidualColor == SkyColor)
		{
			SkyResidual = GraphBuilder.RegisterExternalTexture(GAtheneaResidual, TEXT("Gaussians.SkyResidual"));
		}
		else if (bResidual)
		{
			const FRDGTextureDesc CubeDesc = FRDGTextureDesc::CreateCube(ResidualSize, PF_FloatRGBA, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV, ResidualMips);
			FRDGTextureRef Cut = GraphBuilder.CreateTexture(CubeDesc, TEXT("Gaussians.SkyCut"));
			SkyResidual = GraphBuilder.CreateTexture(CubeDesc, TEXT("Gaussians.SkyResidual"));
			const FRDGBufferSRVRef SkySHSRV = GraphBuilder.CreateSRV(SkySHBuffer);
			const FRHITextureDesc& SourceDesc = SkyTexture->TextureRHI->GetDesc();
			auto Common = [&](uint32 MipIndex, FRDGTextureRef Out) -> FGSSkyResidualParameters*
			{
				FGSSkyResidualParameters* P = GraphBuilder.AllocParameters<FGSSkyResidualParameters>();
				P->View = InView.ViewUniformBuffer;
				P->SkyCubemap = SkyTexture->TextureRHI;
				P->SkyCubemapSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				P->SkySourceMips = float(SourceDesc.NumMips);
				P->SkySourceSize = float(SourceDesc.Extent.X);
				P->SkySH = SkySHSRV;
				P->CutMips = float(ResidualMips);
				P->Mip = MipIndex;
				P->Mips = ResidualMips;
				P->Size = FMath::Max(ResidualSize >> MipIndex, 1u);
				P->GSResource = ResourceUniformBuffer;
				P->Primitive = PrimitiveUniformBuffer;
				P->OutResidual = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Out, uint8(MipIndex)));
				return P;
			};
			auto Groups = [&](uint32 MipIndex)
			{
				const uint32 Size = FMath::Max(ResidualSize >> MipIndex, 1u);
				return FIntVector(FMath::DivideAndRoundUp(Size, 8u), FMath::DivideAndRoundUp(Size, 8u), 6);
			};
			// The cut: level 0 from the sky, then each level the mean of the one above.
			{
				FGSSkyResidualParameters* P = Common(0, Cut);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("GSSkyCut"), TShaderMapRef<FGSSkyCutCS>(ShaderMap), P, Groups(0));
			}
			for (uint32 MipIndex = 1; MipIndex < ResidualMips; MipIndex++)
			{
				FGSSkyResidualParameters* P = Common(MipIndex, Cut);
				FRDGTextureSRVDesc Above = FRDGTextureSRVDesc::CreateForMipLevel(Cut, int32(MipIndex) - 1);
				Above.DimensionOverride = ETextureDimension::Texture2DArray;
				P->CutAbove = GraphBuilder.CreateSRV(Above);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("GSSkyDown mip %u", MipIndex), TShaderMapRef<FGSSkyDownCS>(ShaderMap), P, Groups(MipIndex));
			}
			// The residual's levels, from the whole cut.
			const FRDGTextureSRVRef CutSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Cut));
			for (uint32 MipIndex = 0; MipIndex < ResidualMips; MipIndex++)
			{
				FGSSkyResidualParameters* P = Common(MipIndex, SkyResidual);
				P->CutCube = CutSRV;
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("GSSkyResidual mip %u", MipIndex),
					TShaderMapRef<FGSSkyResidualCS>(ShaderMap), P, Groups(MipIndex));
			}
			GraphBuilder.QueueTextureExtraction(SkyResidual, &GAtheneaResidual);
			GAtheneaResidualSky = SkyTexture->TextureRHI.GetReference();
			GAtheneaResidualColor = SkyColor;
		}

		// The sun in the world, for the raster's sharp lobes (the same for every TX cloud).
		if (SkyResidual)
		{
			FrameResidual = SkyResidual;
			FGSSkyResidualParameters* P = GraphBuilder.AllocParameters<FGSSkyResidualParameters>();
			P->View = InView.ViewUniformBuffer;
			P->SkySH = GraphBuilder.CreateSRV(SkySHBuffer);
			P->GSResource = ResourceUniformBuffer;
			P->Primitive = PrimitiveUniformBuffer;
			P->OutSunWorld = GraphBuilder.CreateUAV(SunWorldBuffer);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("GSSunWorld"), TShaderMapRef<FGSSunWorldCS>(ShaderMap), P, FIntVector(1, 1, 1));
		}
		}

		// athenea: a medium's shadow on itself, from UE's directional light and
		// the sky above, a pair a gaussian; one pair for any other cloud.
		FRDGBufferRef VolumeShadow = nullptr;
		if (Resource->IsVolume() && CVarGaussiansAtheneaMediumShadow.GetValueOnRenderThread() == 0)
		{
			// Unshadowed: every gaussian sees all of each light (1.0f's bits).
			VolumeShadow = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), FMath::Max(SplatCount, 1u)), TEXT("Gaussians.MediumShadowOff"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(VolumeShadow), 0x3F800000u);
		}
		else if (Resource->IsVolume())
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, GaussiansMediumShadow, "Gaussians: Medium shadow");
			const uint32 Count = FMath::Max(SplatCount, 1u);
			constexpr uint32 GridCells = 64u * 64u * 64u;
			FRDGBufferRef Grid = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), GridCells), TEXT("Gaussians.MediumGrid"));
			VolumeShadow = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), Count), TEXT("Gaussians.MediumShadow"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Grid), 0u);
			auto Common = [&]()
			{
				FGSVolumeShadowParameters* P = GraphBuilder.AllocParameters<FGSVolumeShadowParameters>();
				P->View = InView.ViewUniformBuffer;
				P->GSResource = ResourceUniformBuffer;
				P->Primitive = PrimitiveUniformBuffer;
				return P;
			};
			// A one-element buffer, cleared, for the binding a pass does not use.
			auto Dummy = [&](const TCHAR* Name)
			{
				FRDGBufferRef B = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), Name);
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(B), 0u);
				return B;
			};
			const FIntVector Groups(FMath::DivideAndRoundUp(Count, 64u), 1, 1);
			{
				FGSVolumeShadowParameters* P = Common();
				P->Grid = GraphBuilder.CreateUAV(Grid);
				P->OutShadow = GraphBuilder.CreateUAV(VolumeShadow);
				P->GridIn = GraphBuilder.CreateSRV(Dummy(TEXT("Gaussians.MediumGridDummy")));
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("GSMediumDeposit"), TShaderMapRef<FGSVolumeDepositCS>(ShaderMap), P, Groups);
			}
			{
				FGSVolumeShadowParameters* P = Common();
				P->Grid = GraphBuilder.CreateUAV(Dummy(TEXT("Gaussians.MediumGridDummyOut")));
				P->GridIn = GraphBuilder.CreateSRV(Grid);
				P->OutShadow = GraphBuilder.CreateUAV(VolumeShadow);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("GSMediumShadow"), TShaderMapRef<FGSVolumeShadowCS>(ShaderMap), P, Groups);
			}
		}
		else
		{
			VolumeShadow = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), 1), TEXT("Gaussians.MediumShadowNone"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(VolumeShadow), 0u);
		}

		FGSPrepassCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGSPrepassCS::FParameters>();
		PassParameters->SkySH = GraphBuilder.CreateSRV(SkySHBuffer);
		PassParameters->VolumeShadow = GraphBuilder.CreateSRV(VolumeShadow);
		PassParameters->AtheneaDebug = uint32(FMath::Max(0, CVarGaussiansAtheneaDebug.GetValueOnRenderThread()));
		PassParameters->SkyCubemap = SkyTexture ? SkyTexture->TextureRHI : GBlackTextureCube->TextureRHI;
		PassParameters->SkyCubemapSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->SkyMips = SkyTexture ? float(SkyTexture->TextureRHI->GetDesc().NumMips) : 1.0f;
		PassParameters->SkyResidual = SkyResidual ? SkyResidual : GSystemTextures.GetCubeBlackDummy(GraphBuilder);
		PassParameters->SkyResidualMips = SkyResidual ? float(ResidualMips) : 0.0f;
		PassParameters->View = InView.ViewUniformBuffer;

		PassParameters->GSResource = ResourceUniformBuffer;
		PassParameters->Primitive = PrimitiveUniformBuffer;

		PassParameters->SplatIndexBuffer = Proxy->GetIndexBufferSRV();
		PassParameters->SplatIndexCounter = Proxy->GetIndexCountBufferSRV();

		PassParameters->SplatScaleMultiplier = Proxy->GetScaleMultiplier();
		PassParameters->SplatOpacityThreshold = CVarGaussiansOpacityThreshold.GetValueOnRenderThread();

		PassParameters->HZB = Gaussians::GetHZBParameters(GraphBuilder, ViewInfo, Gaussians::EHZBType::FurthestHZB);

		PassParameters->Counter = GraphBuilder.CreateUAV(RasterIndirectArgsBuffer, PF_R32_UINT);
		PassParameters->LodBudgetOut = GraphBuilder.CreateUAV(LodBudget, PF_R32_UINT);
		PassParameters->LodBudgetCap = LodBudgetCap;
		{
			// The cut's own numbers, for its morph (GSLod.ush).
			const FGSLodCut Cut = GSLodCutOf(*Proxy, ViewInfo);
			PassParameters->LodEyeLocal = Cut.EyeLocal;
			PassParameters->LodPixelsPerUnit = Cut.PixelsPerUnit;
			PassParameters->LodThreshold = Cut.Threshold;
			PassParameters->LodDissolve = Cut.Dissolve;
			PassParameters->LodMorph = Cut.Morph;
			PassParameters->LodMorphSplats = Cut.MorphSplats;
			PassParameters->LodThin = Cut.Thin;
			PassParameters->LodThinLeast = Cut.ThinLeast;
		}

		PassParameters->OutSSPositions = GraphBuilder.CreateUAV(NDCPositionsBuffer, PF_G32R32F);
		PassParameters->OutSSScale = GraphBuilder.CreateUAV(NDCScalesBuffer, PF_A32B32G32R32F);
		PassParameters->OutConic = GraphBuilder.CreateUAV(ConicsBuffer);
		PassParameters->OutColorOpacity = GraphBuilder.CreateUAV(ColorOpacityBuffer, PF_A32B32G32R32F);
		PassParameters->OutSharpA = GraphBuilder.CreateUAV(SharpABuffer, PF_R32G32B32A32_UINT);
		PassParameters->OutSharpB = GraphBuilder.CreateUAV(SharpBBuffer, PF_R32G32B32A32_UINT);
		PassParameters->SharpOut = !bSharp ? 0u : (SkyResidual && Resource->HasCurvature()) ? 1u : 2u;
		PassParameters->OutGaussianIDX = GraphBuilder.CreateUAV(GaussianIndexBuffer, PF_R32_UINT);
		PassParameters->OutDepth = GraphBuilder.CreateUAV(GaussianDepthBuffer, PF_R32_UINT);

		if (bRelighting)
		{
			PassParameters->OutNormal = GraphBuilder.CreateUAV(NormalBuffer, PF_R32_UINT);
			PassParameters->OutMetallicRoughness = GraphBuilder.CreateUAV(MetallicRoughnessBuffer, PF_R32_UINT);
			PassParameters->OutSSVelocity = GraphBuilder.CreateUAV(ScreenSpaceVelocityBuffer, PF_R32G32_UINT);
		}

		PassParameters->BufferCapacity = Capacity;
		PassParameters->TightQuads = CVarGaussiansAtheneaTightQuads.GetValueOnRenderThread() != 0 ? 1u : 0u;
		PassParameters->MipPay = CVarGaussiansAtheneaMipPaid.GetValueOnRenderThread() != 0 ? 1u : 0u;
		PassParameters->MipDilation = FMath::Clamp(CVarGaussiansAtheneaDilation.GetValueOnRenderThread(), 0.0f, 16.0f);

		PassParameters->IndirectArgs = ComputeIndirectArgs;

		FComputeShaderUtils::AddPass<FGSPrepassCS>(
			GraphBuilder,
			RDG_EVENT_NAME("GSPrepass"),
			PrePassShader,
			PassParameters,
			ComputeIndirectArgs,
			/*IndirectArgsOffset:*/ 0
		);
	}

	// If no splats survived culling then no further work required
	if (!bAnySplatsRendered)
	{
		return;
	}

	// Sort all projected screens-space splats along view axis
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, GaussiansSort, "Gaussians: Radix Sort");

		TArray<FRDGBufferRef> InKeys   = { GaussianDepthBuffer, GraphBuilder.CreateBuffer(GaussianDepthBuffer->Desc, TEXT("Gaussians.PingPongDepth")) };
		TArray<FRDGBufferRef> InValues = { GaussianIndexBuffer, GraphBuilder.CreateBuffer(GaussianIndexBuffer->Desc, TEXT("Gaussians.PingPongIDX")) };

		uint32 Index = GPURadixSort::SortBufferIndirect(
			GraphBuilder, 
			InKeys,
			InValues, 
			0,
			RasterIndirectArgsBuffer,
			1,
			UINT32_MAX, 
			GMaxRHIFeatureLevel,
			!bRelighting && FrontBatches == 0  // Unlit splats must be sorted back-to-front, unless drawn front to back
		);

		GaussianIndexBuffer = InValues[Index];
		GaussianDepthBuffer = InKeys[Index];
	}

	// Rasterize splats into the render target
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, GaussiansRaster, "Gaussians: Rasterization");

		auto PassParameters = GraphBuilder.AllocParameters<FSplattingPassPipelineParameters>();

		PassParameters->VertexShader.View = InView.ViewUniformBuffer;
		PassParameters->VertexShader.VS_SSPositions = GraphBuilder.CreateSRV(NDCPositionsBuffer, PF_G32R32F);
		PassParameters->VertexShader.VS_SSScale = GraphBuilder.CreateSRV(NDCScalesBuffer, PF_A32B32G32R32F);
		PassParameters->VertexShader.VS_Conic = GraphBuilder.CreateSRV(ConicsBuffer);
		PassParameters->VertexShader.VS_ColorOpacity = GraphBuilder.CreateSRV(ColorOpacityBuffer, PF_A32B32G32R32F);
		PassParameters->VertexShader.VS_SharpA = GraphBuilder.CreateSRV(SharpABuffer, PF_R32G32B32A32_UINT);
		PassParameters->VertexShader.VS_SharpB = GraphBuilder.CreateSRV(SharpBBuffer, PF_R32G32B32A32_UINT);
		PassParameters->VertexShader.SharpIn = (bSharp && FrameResidual) ? 1u : 0u;
		PassParameters->VertexShader.VS_GaussianIDX = GraphBuilder.CreateSRV(GaussianIndexBuffer, PF_R32_UINT);
		PassParameters->VertexShader.VS_Depth = GraphBuilder.CreateSRV(GaussianDepthBuffer, PF_R32_UINT);

		if (bRelighting)
		{
			PassParameters->VertexShader.VS_Normal = GraphBuilder.CreateSRV(NormalBuffer, PF_R32_UINT);
			PassParameters->VertexShader.VS_MetallicRoughness = GraphBuilder.CreateSRV(MetallicRoughnessBuffer, PF_R32_UINT);
			PassParameters->VertexShader.VS_SSVelocity = GraphBuilder.CreateSRV(ScreenSpaceVelocityBuffer, PF_R32G32_UINT);
		}

		PassParameters->VertexShader.IndirectArgs = RasterIndirectArgsBuffer;

		PassParameters->PixelShader.View = InView.ViewUniformBuffer;
		PassParameters->PixelShader.SkyResidual = FrameResidual ? FrameResidual : GSystemTextures.GetCubeBlackDummy(GraphBuilder);
		PassParameters->PixelShader.SkyCubemapSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->PixelShader.SkyResidualMips = FrameResidual ? 9.0f : 0.0f;
		PassParameters->PixelShader.SunWorld = GraphBuilder.CreateSRV(SunWorldBuffer);
		// TODO: Verify quantity of render targets based on whether relighting is enabled or not
		PassParameters->PixelShader.RenderTargets = RenderTargets;

		if (FrontBatches > 0)
		{
			FRDGTextureRef SceneColor = RenderTargets[0].GetTexture();
			FRDGTextureRef SceneDepth = RenderTargets.DepthStencil.GetTexture();
			FRDGTextureRef Front = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(SceneColor->Desc.Extent, PF_FloatRGBA,
				FClearValueBinding::Transparent, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("Gaussians.Front"));
			// The nearest showing splat's depth, for the scene's velocity (TSR reads it).
			// Only a temporal method reads it, and a second target costs a
			// sixth of the pass: without one none is written.
			FRDGTextureRef SceneVelocity = IsTemporalAccumulationBasedMethod(ViewInfo.AntiAliasingMethod) ? RenderTargets[1].GetTexture() : nullptr;
			FRDGTextureRef FrontVelocity = SceneVelocity == nullptr ? nullptr : GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(SceneColor->Desc.Extent, PF_R32_FLOAT,
				FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("Gaussians.FrontNearest"));
			// A depth of the splats' own: the scene's, then the opaque pixels brought near.
			FRDGTextureRef FrontDepth = GraphBuilder.CreateTexture(SceneDepth->Desc, TEXT("Gaussians.FrontDepth"));
			AddCopyTexturePass(GraphBuilder, SceneDepth, FrontDepth);

			FRDGBufferRef FrontArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(FrontBatches), TEXT("Gaussians.FrontArgs"));
			FRDGBufferRef FrontBases = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), FrontBatches), TEXT("Gaussians.FrontBatches"));
			{
				FGSFrontBatchesCS::FParameters* Batches = GraphBuilder.AllocParameters<FGSFrontBatchesCS::FParameters>();
				Batches->FrontCount = GraphBuilder.CreateSRV(RasterIndirectArgsBuffer, PF_R32_UINT);
				Batches->OutFrontArgs = GraphBuilder.CreateUAV(FrontArgs, PF_R32_UINT);
				Batches->OutFrontBatches = GraphBuilder.CreateUAV(FrontBases, PF_R32_UINT);
				Batches->FrontBatchCount = uint32(FrontBatches);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("GSFrontBatches"), TShaderMapRef<FGSFrontBatchesCS>(ShaderMap), Batches, FIntVector(1, 1, 1));
			}
			TShaderMapRef<FSplattingFrontVS> FrontVS(ShaderMap);
			TShaderMapRef<FSplattingFrontPS> FrontPS(ShaderMap);
			for (int32 Batch = 0; Batch < FrontBatches; Batch++)
			{
				auto* P = GraphBuilder.AllocParameters<FSplattingFrontPipelineParameters>();
				P->VertexShader.Base = PassParameters->VertexShader;
				P->VertexShader.Base.IndirectArgs = FrontArgs;
				P->VertexShader.FrontBatches = GraphBuilder.CreateSRV(FrontBases, PF_R32_UINT);
				P->VertexShader.FrontBatch = uint32(Batch);
				P->PixelShader = PassParameters->PixelShader;
				P->PixelShader.RenderTargets = FRenderTargetBindingSlots();
				P->PixelShader.RenderTargets[0] = FRenderTargetBinding(Front, Batch == 0 ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
				if (FrontVelocity != nullptr)
				{
					P->PixelShader.RenderTargets[1] = FRenderTargetBinding(FrontVelocity, Batch == 0 ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
				}
				P->PixelShader.RenderTargets.DepthStencil = FDepthStencilBinding(FrontDepth, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilNop);
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("GSRasterizeFront %d", Batch),
					P,
					ERDGPassFlags::Raster,
					[P, FrontVS, FrontPS, Batch, ViewRect = ViewInfo.ViewRect](FRHICommandList& RHICmdList)
					{
						RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
						FGraphicsPipelineStateInitializer GraphicsPSOInit;
						RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
						GraphicsPSOInit.BlendState = FFrontBlendState::GetRHI();
						GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
						GraphicsPSOInit.DepthStencilState = FUnlitDepthStencilState::GetRHI();
						GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
						GraphicsPSOInit.BoundShaderState.VertexShaderRHI = FrontVS.GetVertexShader();
						GraphicsPSOInit.BoundShaderState.PixelShaderRHI = FrontPS.GetPixelShader();
						GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;
						SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
						SetShaderParameters(RHICmdList, FrontVS, FrontVS.GetVertexShader(), P->VertexShader);
						SetShaderParameters(RHICmdList, FrontPS, FrontPS.GetPixelShader(), P->PixelShader);
						RHICmdList.DrawPrimitiveIndirect(P->VertexShader.Base.IndirectArgs->GetIndirectRHICallBuffer(), uint32(Batch) * sizeof(FRHIDrawIndirectParameters));
					}
				);
				if (Batch + 1 < FrontBatches)
				{
					FGSFrontSaturatePS::FParameters* Saturate = GraphBuilder.AllocParameters<FGSFrontSaturatePS::FParameters>();
					Saturate->FrontColor = Front;
					Saturate->FrontOpaque = 0.996f;
					Saturate->RenderTargets.DepthStencil = FDepthStencilBinding(FrontDepth, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop);
					FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("GSFrontSaturate %d", Batch),
						TShaderMapRef<FGSFrontSaturatePS>(ShaderMap), Saturate, ViewInfo.ViewRect, nullptr, nullptr, FFrontSaturateDepthState::GetRHI());
				}
			}
			FGSFrontCompositePS::FParameters* Composite = GraphBuilder.AllocParameters<FGSFrontCompositePS::FParameters>();
			Composite->FrontColor = Front;
			Composite->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
			FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("GSFrontComposite"),
				TShaderMapRef<FGSFrontCompositePS>(ShaderMap), Composite, ViewInfo.ViewRect, FFrontCompositeBlendState::GetRHI());
			if (FrontVelocity != nullptr)
			{
				FGSFrontVelocityPS::FParameters* Velocity = GraphBuilder.AllocParameters<FGSFrontVelocityPS::FParameters>();
				Velocity->View = InView.ViewUniformBuffer;
				Velocity->FrontNearest = FrontVelocity;
				Velocity->RenderTargets[0] = FRenderTargetBinding(SceneVelocity, ERenderTargetLoadAction::ELoad);
				FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("GSFrontVelocity"),
					TShaderMapRef<FGSFrontVelocityPS>(ShaderMap), Velocity, ViewInfo.ViewRect);
			}
		}
		else
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("GSRasterizePass"),
			PassParameters,
			ERDGPassFlags::Raster,
			[bRelighting, PassParameters, VertexShader, PixelShader, ViewRect = ViewInfo.ViewRect](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

				// Initialize the Pipeline State
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				// Set up blend/depth/rasterizer states
				GraphicsPSOInit.BlendState = bRelighting ? FRelitBlendState::GetRHI() : FUnlitBlendState::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.DepthStencilState = bRelighting ? FRelitDepthStencilState::GetRHI() : FUnlitDepthStencilState::GetRHI();

				// Set shaders
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI; //using SV_VertexID
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

				// Set shader parameters
				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VertexShader);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PixelShader);

				// Execute the Indirect Draw
				// The RHI needs the actual underlying FRHIBuffer for the indirect call
				FRHIBuffer* IndirectArgsRHIBuffer = PassParameters->VertexShader.IndirectArgs->GetIndirectRHICallBuffer();
				uint32 ArgumentOffset = 0; // Offset in bytes if your buffer contains multiple draw args

				RHICmdList.DrawPrimitiveIndirect(IndirectArgsRHIBuffer, ArgumentOffset);
			}
		);
	}

	// Copy into transient stats buffer
	if (GPUStats)
	{
		// The num instances set in the indirect args tells how many splats were actually sent for rasterization
		GPUStats->SetStatFromGPU(
			GraphBuilder, 
			bRelighting ? FGSGPUStats::Stat_LitSplatsRendered : FGSGPUStats::Stat_UnlitSplatsRendered,
			RasterIndirectArgsBuffer,
			sizeof(uint32)
		);
		GPUStats->SetStatFromGPU(GraphBuilder, FGSGPUStats::Stat_LodBudgetScale, LodBudget, 0);
	}
}
