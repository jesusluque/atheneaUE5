#include "GaussianSplatResource.h"
#include "HAL/IConsoleManager.h"

#include "GaussianData.h"
#include "GaussianSplatInterface.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIResourceUtils.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "DataDrivenShaderPlatformInfo.h"

// athenea: athenea's device packing decoded into the float buffers below, once
// (GSDecode.usf).
class FGSDecodeAtheneaCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSDecodeAtheneaCS);
	SHADER_USE_PARAMETER_STRUCT(FGSDecodeAtheneaCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, Offset)
		SHADER_PARAMETER(uint32, Count)
		SHADER_PARAMETER(uint32, SHWords)
		SHADER_PARAMETER(uint32, RestPerColour)
		SHADER_PARAMETER(float, UnitsToCm)
		SHADER_PARAMETER(FVector3f, FileToLocalX)
		SHADER_PARAMETER(FVector3f, FileToLocalY)
		SHADER_PARAMETER(FVector3f, FileToLocalZ)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, InPositions)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, InShape)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, InSH)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float3>, OutPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float3>, OutScales)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, OutOrientations)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, OutSHAndOpacities)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, OutHighOrderSH)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FGSDecodeAtheneaCS, "/Plugin/StarLightGS/GSDecode.usf", "GSDecodeAtheneaCS", SF_Compute);

// athenea: a file's own packing (.splat, .spz) decoded from its bytes.
// athenea: when a .athc keeps its packed arrays resident (GSSplatData.ush).
static TAutoConsoleVariable<int32> CVarGaussiansAtheneaPackedResident(
	TEXT("r.Gaussians.Athenea.PackedResident"), 0,
	TEXT("A .athc's packed arrays kept on the GPU in place of the decoded ones (32 bytes a splat in place of 56): 0 above 20 million splats, 1 always, -1 never. Read when a resource is made."),
	ECVF_RenderThreadSafe);

BEGIN_SHADER_PARAMETER_STRUCT(FGSDecodeFileParameters, )
	SHADER_PARAMETER(uint32, Offset)
	SHADER_PARAMETER(uint32, Count)
	SHADER_PARAMETER(float, UnitsToCm)
	SHADER_PARAMETER(FVector3f, FileToLocalX)
	SHADER_PARAMETER(FVector3f, FileToLocalY)
	SHADER_PARAMETER(FVector3f, FileToLocalZ)
	SHADER_PARAMETER_SRV(ByteAddressBuffer, InBytes)
	SHADER_PARAMETER(FUintVector4, Streams0)
	SHADER_PARAMETER(FUintVector2, Streams1)
	SHADER_PARAMETER(FUintVector2, Streams2)
	SHADER_PARAMETER(uint32, FractionalBits)
	SHADER_PARAMETER(uint32, SmallestThree)
	SHADER_PARAMETER(uint32, SpzRest)
	SHADER_PARAMETER_UAV(RWByteAddressBuffer, Scratch)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<float3>, OutPositions)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<float3>, OutScales)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, OutOrientations)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, OutSHAndOpacities)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, OutHighOrderSH)
	// athenea: an .athz's levels' words, written from the decoded tails (GSAthzLodCS).
	SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, OutLodWords)
END_SHADER_PARAMETER_STRUCT()

class FGSDecodeSplatCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSDecodeSplatCS);
	SHADER_USE_PARAMETER_STRUCT(FGSDecodeSplatCS, FGlobalShader);
	using FParameters = FGSDecodeFileParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSDecodeSplatCS, "/Plugin/StarLightGS/GSDecode.usf", "GSDecodeSplatCS", SF_Compute);

class FGSDecodeSpzCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSDecodeSpzCS);
	SHADER_USE_PARAMETER_STRUCT(FGSDecodeSpzCS, FGlobalShader);
	using FParameters = FGSDecodeFileParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSDecodeSpzCS, "/Plugin/StarLightGS/GSDecode.usf", "GSDecodeSpzCS", SF_Compute);

class FGSDecodeSogCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSDecodeSogCS);
	SHADER_USE_PARAMETER_STRUCT(FGSDecodeSogCS, FGlobalShader);
	using FParameters = FGSDecodeFileParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSDecodeSogCS, "/Plugin/StarLightGS/GSDecode.usf", "GSDecodeSogCS", SF_Compute);

class FGSDecodeGltfCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSDecodeGltfCS);
	SHADER_USE_PARAMETER_STRUCT(FGSDecodeGltfCS, FGlobalShader);
	using FParameters = FGSDecodeFileParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSDecodeGltfCS, "/Plugin/StarLightGS/GSDecode.usf", "GSDecodeGltfCS", SF_Compute);

class FGSDecodeAthc3CS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSDecodeAthc3CS);
	SHADER_USE_PARAMETER_STRUCT(FGSDecodeAthc3CS, FGlobalShader);
	using FParameters = FGSDecodeFileParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSDecodeAthc3CS, "/Plugin/StarLightGS/GSDecode.usf", "GSDecodeAthc3CS", SF_Compute);

class FGSAthc3UndeltaCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSAthc3UndeltaCS);
	SHADER_USE_PARAMETER_STRUCT(FGSAthc3UndeltaCS, FGlobalShader);
	using FParameters = FGSDecodeFileParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSAthc3UndeltaCS, "/Plugin/StarLightGS/GSDecode.usf", "GSAthc3UndeltaCS", SF_Compute);

class FGSAthc3CpcaCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSAthc3CpcaCS);
	SHADER_USE_PARAMETER_STRUCT(FGSAthc3CpcaCS, FGlobalShader);
	using FParameters = FGSDecodeFileParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSAthc3CpcaCS, "/Plugin/StarLightGS/GSDecode.usf", "GSAthc3CpcaCS", SF_Compute);

// athenea: an .athz's tables and pages (GSDecode.usf's athz section).
class FGSAthzTablesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSAthzTablesCS);
	SHADER_USE_PARAMETER_STRUCT(FGSAthzTablesCS, FGlobalShader);
	using FParameters = FGSDecodeFileParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSAthzTablesCS, "/Plugin/StarLightGS/GSDecode.usf", "GSAthzTablesCS", SF_Compute);

class FGSAthzDecodeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSAthzDecodeCS);
	SHADER_USE_PARAMETER_STRUCT(FGSAthzDecodeCS, FGlobalShader);
	using FParameters = FGSDecodeFileParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSAthzDecodeCS, "/Plugin/StarLightGS/GSDecode.usf", "GSAthzDecodeCS", SF_Compute);

// athenea: an .athz's CPCA transfer codes expanded by the file's basis.
class FGSAthzCpcaCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSAthzCpcaCS);
	SHADER_USE_PARAMETER_STRUCT(FGSAthzCpcaCS, FGlobalShader);
	using FParameters = FGSDecodeFileParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSAthzCpcaCS, "/Plugin/StarLightGS/GSDecode.usf", "GSAthzCpcaCS", SF_Compute);

// athenea: a PLY's or USD stage's rest coefficients packed into halves.
class FGSPackSHCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSPackSHCS);
	SHADER_USE_PARAMETER_STRUCT(FGSPackSHCS, FGlobalShader);
	using FParameters = FGSDecodeFileParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSPackSHCS, "/Plugin/StarLightGS/GSDecode.usf", "GSPackSHCS", SF_Compute);

// athenea: an .athz's levels of detail, two words a gaussian from the tails
// the decode left in the scratch (a splat's finest group, a group's cell).
class FGSAthzLodCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSAthzLodCS);
	SHADER_USE_PARAMETER_STRUCT(FGSAthzLodCS, FGlobalShader);
	using FParameters = FGSDecodeFileParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSAthzLodCS, "/Plugin/StarLightGS/GSDecode.usf", "GSAthzLodCS", SF_Compute);

// athenea: a .athc v3's levels of detail, two words a gaussian from CORE's
// tails and its block's LOD word.
class FGSAthc3LodCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSAthc3LodCS);
	SHADER_USE_PARAMETER_STRUCT(FGSAthc3LodCS, FGlobalShader);
	using FParameters = FGSDecodeFileParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSAthc3LodCS, "/Plugin/StarLightGS/GSDecode.usf", "GSAthc3LodCS", SF_Compute);

// athenea: an .athz's layer K, the splats' influences and gradients lifted
// from the decode's scratch into the skin's buffers.
class FGSAthzSkinCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSAthzSkinCS);
	SHADER_USE_PARAMETER_STRUCT(FGSAthzSkinCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, Offset)
		SHADER_PARAMETER(uint32, Count)
		SHADER_PARAMETER(uint32, InfluenceAt)
		SHADER_PARAMETER(uint32, GradientAt)
		SHADER_PARAMETER_UAV(RWByteAddressBuffer, Scratch)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, OutInfluences)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, OutGradients)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSAthzSkinCS, "/Plugin/StarLightGS/GSDecode.usf", "GSAthzSkinCS", SF_Compute);

// athenea: a .athc v3 TX cloud's streams into the transfer's buffers.
class FGSDecodeAthc3TxCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSDecodeAthc3TxCS);
	SHADER_USE_PARAMETER_STRUCT(FGSDecodeAthc3TxCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, Offset)
		SHADER_PARAMETER(uint32, Count)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, InBytes)
		SHADER_PARAMETER_UAV(RWByteAddressBuffer, Scratch)
		SHADER_PARAMETER(FUintVector4, Streams0)
		SHADER_PARAMETER(FUintVector2, Streams1)
		SHADER_PARAMETER(FUintVector2, Streams2)
		SHADER_PARAMETER(uint32, TxDirect)
		SHADER_PARAMETER(uint32, TxField)
		SHADER_PARAMETER(uint32, TxCellWords)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, OutTransfer)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, OutMetallic)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, OutSurface)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, OutCells)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, OutField)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, OutCurvature)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, OutEmission)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSDecodeAthc3TxCS, "/Plugin/StarLightGS/GSDecode.usf", "GSDecodeAthc3TxCS", SF_Compute);


// athenea: a skinned cloud posed (GSSkin.usf).
class FGSSkinCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSSkinCS);
	SHADER_USE_PARAMETER_STRUCT(FGSSkinCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, Count)
		SHADER_PARAMETER(uint32, PerSplat)
		SHADER_PARAMETER(uint32, GradientsIn)
		SHADER_PARAMETER(float, UnitsToCm)
		SHADER_PARAMETER(FVector3f, FileAxisX)
		SHADER_PARAMETER(FVector3f, FileAxisY)
		SHADER_PARAMETER(FVector3f, FileAxisZ)
		SHADER_PARAMETER_SRV(StructuredBuffer<float3>, RestPositions)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, RestRotations)
		SHADER_PARAMETER_SRV(StructuredBuffer<float3>, RestScales)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, Influences)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, Gradients)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, SkinXforms)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float3>, OutPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float3>, OutScales)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, OutOrientations)
		SHADER_PARAMETER(uint32, VisibilityParts)
		SHADER_PARAMETER(FVector4f, SunDirection)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, Parts)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, PartOf)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, Texels)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, Ambient)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PartFrames)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float2>, OutVisibility)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSSkinCS, "/Plugin/StarLightGS/GSSkin.usf", "GSSkinCS", SF_Compute);

// athenea: translucent shadows (GSVertexFactory.ush): every gaussian casts,
// each pixel as often as it is opaque there, rather than the opaque ones alone.
static TAutoConsoleVariable<int32> CVarGaussiansAtheneaTranslucentShadows(
	TEXT("r.Gaussians.Athenea.TranslucentShadows"),
	0,
	TEXT("1: every gaussian casts a shadow as opaque as it is (a stochastic mask the shadow's filter averages); 0: only those at least 0.9 opaque, whole. Read when a cloud's resource is made."),
	ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarGaussiansAtheneaShadowClip(
	TEXT("r.Gaussians.Athenea.ShadowClip"),
	0.3333f,
	TEXT("The splats' shadow material's opacity mask clip value, which translucent shadows scale against."),
	ECVF_RenderThreadSafe);
// athenea: how far a gaussian's quad in a directional light's shadow map is
// pushed from the light, in its own deviations along the light: the surface
// it shades lies at the blend's depth, which half its gaussians are in front
// of, and unpushed they shadow it in a grain of dots (self-shadow acne).
static TAutoConsoleVariable<float> CVarGaussiansAtheneaShadowPush(
	TEXT("r.Gaussians.Athenea.ShadowPush"),
	8.0f,
	TEXT("A gaussian's shadow in a directional light pushed away from the light by this many of its deviations along it (0: at its centre). Read when a cloud's resource is made."),
	ECVF_RenderThreadSafe);

IMPLEMENT_UNIFORM_BUFFER_STRUCT(FGaussianSplatShaderResources, "GSResources")
IMPLEMENT_UNIFORM_BUFFER_STRUCT(FGaussianSplatPrimitiveShaderResources, "GSPrimitiveResources")

FGaussianSplatResource::FGaussianSplatResource(FGaussianSplatData InSplatData)
	: SplatData(MoveTemp(InSplatData))
{
}

FGaussianSplatResource::~FGaussianSplatResource()
{
	// All RHI resources should have already been released by calling ReleaseRHI
}

void FGaussianSplatResource::InitRHI(FRHICommandListBase& RHICmdList)
{
	check(IsInRenderingThread());

	// Do not create resources if there are no splats
	if (SplatData.SplatCount == 0)
	{
		return;
	}

	// Ensure all required fields are there
	check(SplatData.bPacked || (
		SplatData.SplatCount == SplatData.Positions.Num() &&
		SplatData.SplatCount == SplatData.Scales.Num() &&
		SplatData.SplatCount == SplatData.Orientations.Num() &&
		SplatData.SplatCount == SplatData.ZeroOrderHarmonicsCoefficientsAndOpacities.Num()
	));

	// If optional fields are declared, then ensure there is the correct quantity
	check(!SplatData.UseNormals() || SplatData.SplatCount == SplatData.Normals.Num());
	check(SplatData.bPacked || !SplatData.UseHighOrderSH() || SplatData.SplatCount * FGaussianSplatData::NumHighOrderSH == SplatData.HighOrderHarmonicsCoefficients.Num());
	check(!SplatData.bPacked || SplatData.PackedFormat != 0 || (SplatData.PackedPositions.Num() == int32(SplatData.SplatCount)
		&& SplatData.PackedShape.Num() == int32(SplatData.SplatCount) * 4
		&& SplatData.PackedSH.Num() == int32(SplatData.SplatCount * SplatData.PackedSHWords)));
	check(!SplatData.UseMetallicRoughness() || SplatData.SplatCount == SplatData.MetallicRoughness.Num());

	// Allocates, populates, and creates an SRV for a given array.
	// If Predicate is false, then a dummy buffer with no data will be created instead
	auto CreateBuffer = [&]<typename T>(const TArray<T>& Source, const TCHAR* DebugName, bool Predicate=true)
	{
		uint32 BytesPerElement = Source.GetTypeSize();
		uint32 NumElements = Source.Num();

		FBufferRHIRef Buffer = nullptr;
		FShaderResourceViewRHIRef SRV = nullptr;

		if (NumElements > 0 && Predicate)
		{
			const FRHIBufferCreateDesc CreateDesc =
				FRHIBufferCreateDesc::CreateStructured(DebugName, BytesPerElement * NumElements, BytesPerElement)
				.AddUsage(EBufferUsageFlags::ShaderResource)
				.DetermineInitialState()
				.SetInitActionInitializer();

			FRHIBufferInitializer Initializer = RHICmdList.CreateBufferInitializer(CreateDesc);
			Initializer.WriteData(Source.GetData(), BytesPerElement * NumElements);
			Buffer = Initializer.Finalize();
		}
		else
		{
			// Creates a single-element buffer to allow for a valid SRV to be placed into the uniform buffer
			// Unreal will validate that all resources in the UB are valid
			// We cannot use GSystemTextures here because we do not have a RDGBuilder
			const FRHIBufferCreateDesc CreateDesc =
				FRHIBufferCreateDesc::CreateStructured(DebugName, BytesPerElement, BytesPerElement)
				.AddUsage(EBufferUsageFlags::ShaderResource)
				.DetermineInitialState();
			Buffer = RHICmdList.CreateBuffer(CreateDesc);
		}

		SRV = RHICmdList.CreateShaderResourceView(
			Buffer,
			FRHIViewDesc::CreateBufferSRV()
			.SetType(FRHIViewDesc::EBufferType::Structured)
			.SetStride(BytesPerElement));

		return FBufferResources{ Buffer, SRV };
	};

	// athenea: a buffer the device writes after it is made (a skinned cloud's pose).
	auto CreateWritable = [&]<typename T>(const TArray<T>& Source, const TCHAR* DebugName, FUnorderedAccessViewRHIRef& OutUAV)
	{
		const uint32 Stride = Source.GetTypeSize();
		const FRHIBufferCreateDesc CreateDesc =
			FRHIBufferCreateDesc::CreateStructured(DebugName, Stride * Source.Num(), Stride)
			.AddUsage(EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess)
			.SetInitialState(ERHIAccess::SRVMask)
			.SetInitActionInitializer();
		FRHIBufferInitializer Initializer = RHICmdList.CreateBufferInitializer(CreateDesc);
		Initializer.WriteData(Source.GetData(), Stride * Source.Num());
		FBufferRHIRef Buffer = Initializer.Finalize();
		FShaderResourceViewRHIRef SRV = RHICmdList.CreateShaderResourceView(Buffer,
			FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Structured).SetStride(Stride));
		OutUAV = RHICmdList.CreateUnorderedAccessView(Buffer,
			FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Structured).SetStride(Stride));
		return FBufferResources{ Buffer, SRV };
	};

	if (SplatData.bPacked)
	{
		// athenea: the packed arrays go up as they are, and a compute pass
		// writes the float buffers the rasteriser reads. The CPU copies bytes.
		const uint32 Count = SplatData.SplatCount;
		const bool bAthenea = SplatData.PackedFormat == 0;
		FBufferResources InPositions = CreateBuffer(SplatData.PackedPositions, TEXT("GaussianSplatsPackedPositions"));
		FBufferResources InShape = CreateBuffer(SplatData.PackedShape, TEXT("GaussianSplatsPackedShape"));
		FBufferResources InSH = CreateBuffer(SplatData.PackedSH, TEXT("GaussianSplatsPackedSH"), SplatData.PackedSHWords > 0);

		// A file's own bytes, as a byte-address buffer padded to a word.
		FBufferRHIRef BytesBuffer;
		FShaderResourceViewRHIRef BytesSRV;
		if (!bAthenea)
		{
			const uint32 Bytes = FMath::Max<uint32>(Align(uint32(SplatData.PackedBytes.Num()), 4u), 4u);
			const FRHIBufferCreateDesc BytesDesc =
				FRHIBufferCreateDesc::CreateByteAddress(TEXT("GaussianSplatsFileBytes"), Bytes, 4)
				.AddUsage(EBufferUsageFlags::ShaderResource)
				.DetermineInitialState()
				.SetInitActionInitializer();
			FRHIBufferInitializer Initializer = RHICmdList.CreateBufferInitializer(BytesDesc);
			TArray<uint8> Padded = SplatData.PackedBytes;
			Padded.SetNumZeroed(int32(Bytes));
			Initializer.WriteData(Padded.GetData(), Bytes);
			BytesBuffer = Initializer.Finalize();
			BytesSRV = RHICmdList.CreateShaderResourceView(BytesBuffer,
				FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Raw));
		}

		auto CreateDecoded = [&](uint32 BytesPerElement, uint32 NumElements, const TCHAR* DebugName, FUnorderedAccessViewRHIRef& OutUAV)
		{
			const FRHIBufferCreateDesc CreateDesc =
				FRHIBufferCreateDesc::CreateStructured(DebugName, BytesPerElement * NumElements, BytesPerElement)
				.AddUsage(EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess)
				.SetInitialState(ERHIAccess::UAVCompute);
			FBufferRHIRef Buffer = RHICmdList.CreateBuffer(CreateDesc);
			FShaderResourceViewRHIRef SRV = RHICmdList.CreateShaderResourceView(Buffer,
				FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Structured).SetStride(BytesPerElement));
			OutUAV = RHICmdList.CreateUnorderedAccessView(Buffer,
				FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Structured).SetStride(BytesPerElement));
			return FBufferResources{ Buffer, SRV };
		};

		FUnorderedAccessViewRHIRef PositionsUAV, ScalesUAV, OrientationsUAV, SHAndOpacitiesUAV, HighOrderSHUAV;
		// athenea: a .athc too large to decode keeps its packed arrays resident
		// (32 bytes a splat in place of 56; GSSplatData.ush unpacks them where
		// they are read). r.Gaussians.Athenea.PackedResident: 0 above 20
		// million splats, 1 always, -1 never.
		{
			const int32 Mode = CVarGaussiansAtheneaPackedResident.GetValueOnAnyThread();
			bPackedResident = bAthenea && SplatData.PackedPositions.Num() == int32(Count)
				&& (Mode > 0 || (Mode == 0 && Count > 20000000u));
		}
		const uint32 Decoded = bPackedResident ? 1u : Count;
		if (bPackedResident)
		{
			PackedPositionsBuffer = InPositions;
			PackedShapeBuffer = InShape;
		}
		PositionsBuffer = CreateDecoded(sizeof(FVector3f), Decoded, TEXT("GaussianSplatsPositions"), PositionsUAV);
		ScalesBuffer = CreateDecoded(sizeof(FVector3f), Decoded, TEXT("GaussianSplatsScales"), ScalesUAV);
		OrientationsBuffer = CreateDecoded(sizeof(FVector4f), Decoded, TEXT("GaussianSplatsOrientations"), OrientationsUAV);
		SHAndOpacitiesBuffer = CreateDecoded(sizeof(FVector4f), Decoded, TEXT("GaussianSplatsSHAndOpacities"), SHAndOpacitiesUAV);
		// The rest coefficients as halves, 23 words a splat, only where the
		// cloud draws any (a word otherwise, which the decoders write nothing to).
		HighOrderSHBuffer = CreateDecoded(sizeof(uint32), SplatData.UseHighOrderSH() ? Count * 23u : 1u, TEXT("GaussianSplatHighOrderSH"), HighOrderSHUAV);

		FRHICommandListImmediate& Immediate = FRHICommandListImmediate::Get();
		TShaderMapRef<FGSDecodeAtheneaCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		// A scratch the decoders may write words into before they read them: a
		// .athc v3's delta-coded sections, summed (PackedStreams[4] bytes).
		const TArray<uint32>& S = SplatData.PackedStreams;
		const bool bAthc3 = SplatData.PackedFormat == 5 || SplatData.PackedFormat == 6;
		const uint32 ScratchBytes = bAthc3 && S.Num() > 4 ? S[4] : 0u;
		FBufferRHIRef ScratchBuffer;
		FUnorderedAccessViewRHIRef ScratchUAV;
		if (!bAthenea)
		{
			const FRHIBufferCreateDesc ScratchDesc =
				FRHIBufferCreateDesc::CreateByteAddress(TEXT("GaussianSplatsDecodeScratch"), FMath::Max(Align(ScratchBytes, 4u), 4u), 4)
				.AddUsage(EBufferUsageFlags::UnorderedAccess)
				.SetInitialState(ERHIAccess::UAVCompute);
			ScratchBuffer = RHICmdList.CreateBuffer(ScratchDesc);
			ScratchUAV = RHICmdList.CreateUnorderedAccessView(ScratchBuffer,
				FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Raw));
		}

		// An .athz's levels' words, written by the device (GSAthzLodCS) where
		// a .athc's come from the file.
		FUnorderedAccessViewRHIRef AthzLodUAV;
		// A .athc v3's likewise, from its blocks (GSAthc3LodCS).
		const bool bAthc3Lod = SplatData.PackedFormat == 5 && SplatData.LodLevels > 0 && SplatData.LodWords.IsEmpty();
		if (!bAthenea && ((SplatData.PackedFormat == 6 && SplatData.LodLevels > 0 && S.Num() > 8) || bAthc3Lod))
		{
			const FRHIBufferCreateDesc LodDesc =
				FRHIBufferCreateDesc::CreateStructured(TEXT("GaussianSplatLodWords"), Count * 2u * 4u, 4u)
				.AddUsage(EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess)
				.SetInitialState(ERHIAccess::UAVCompute);
			FBufferRHIRef LodBuffer = RHICmdList.CreateBuffer(LodDesc);
			AthzLodUAV = RHICmdList.CreateUnorderedAccessView(LodBuffer,
				FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Structured).SetStride(4u));
			LodWordsBuffer.Buffer = LodBuffer;
			LodWordsBuffer.SRV = RHICmdList.CreateShaderResourceView(LodBuffer,
				FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Structured).SetStride(4u));
		}

		// A dispatch holds at most 65535 groups of 64 along x.
		const uint32 Batch = 65535u * 64u;
		bool bScratchFilled = ScratchBytes == 0;
		for (uint32 Offset = 0; Offset < Count && !bAthenea; Offset += Batch)
		{
			FGSDecodeFileParameters Parameters;
			Parameters.Scratch = ScratchUAV;
			Parameters.Offset = Offset;
			Parameters.Count = Count;
			Parameters.UnitsToCm = SplatData.PackedUnitsToCm;
			Parameters.FileToLocalX = SplatData.FileAxisX;
			Parameters.FileToLocalY = SplatData.FileAxisY;
			Parameters.FileToLocalZ = SplatData.FileAxisZ;
			Parameters.InBytes = BytesSRV;
			Parameters.Streams0 = FUintVector4(S.Num() > 0 ? S[0] : 0, S.Num() > 1 ? S[1] : 0, S.Num() > 2 ? S[2] : 0, S.Num() > 3 ? S[3] : 0);
			Parameters.Streams1 = FUintVector2(S.Num() > 4 ? S[4] : 0, S.Num() > 5 ? S[5] : 0);
			Parameters.Streams2 = FUintVector2(S.Num() > 6 ? S[6] : 0, S.Num() > 7 ? S[7] : 0);
			Parameters.FractionalBits = uint32(SplatData.PackedSpz.X);
			Parameters.SmallestThree = uint32(SplatData.PackedSpz.Y);
			Parameters.SpzRest = uint32(SplatData.PackedSpz.Z);
			Parameters.OutPositions = PositionsUAV;
			Parameters.OutScales = ScalesUAV;
			Parameters.OutOrientations = OrientationsUAV;
			Parameters.OutSHAndOpacities = SHAndOpacitiesUAV;
			Parameters.OutHighOrderSH = HighOrderSHUAV;
			const FIntVector Groups(FMath::DivideAndRoundUp(FMath::Min(Batch, Count - Offset), 64u), 1, 1);
			if (SplatData.PackedFormat == 6 && Offset == 0)
			{
				// An .athz: its tables, then each level coarsest first and its
				// splats last, into the scratch the version 3 decoders read.
				const uint32 HeadAt = uint32(SplatData.PackedSpz.X);
				const uint32* Bytes = reinterpret_cast<const uint32*>(SplatData.PackedBytes.GetData());
				const uint32 Tables = Bytes[HeadAt / 4], TableLog = Bytes[HeadAt / 4 + 1], PageRuns = Bytes[HeadAt / 4 + 18];
				const uint32 LevelsAt = Bytes[HeadAt / 4 + 23], LevelCount = Bytes[HeadAt / 4 + 24];
				Parameters.FractionalBits = HeadAt;
				FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSAthzTablesCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), Parameters,
					FIntVector(FMath::DivideAndRoundUp((Tables << TableLog), 256u), 1, 1));
				Immediate.Transition(FRHITransitionInfo(ScratchUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
				for (uint32 Level = 0; Level < LevelCount; Level++)
				{
					const uint32 Pages = Bytes[LevelsAt / 4 + Level * 40 + 2];
					FGSDecodeFileParameters LevelParameters = Parameters;
					LevelParameters.Offset = Level;
					FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSAthzDecodeCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), LevelParameters,
						FIntVector(FMath::DivideAndRoundUp(Pages * PageRuns, 8u), 1, 1));
					Immediate.Transition(FRHITransitionInfo(ScratchUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
				}
				// A CPCA transfer: the splats' codes into their values, then each
				// level's (a head of 11 words each, PackedSpz.Z of them).
				if (SplatData.PackedSpz.Y != 0)
				{
					const uint32 Heads = FMath::Max(uint32(SplatData.PackedSpz.Z), 1u);
					for (uint32 h = 0; h < Heads; h++)
					{
						const uint32 HeadBytes = uint32(SplatData.PackedSpz.Y) + h * 44u;
						const uint32 N = Bytes[HeadBytes / 4];
						for (uint32 First = 0; First < N; First += Batch)
						{
							FGSDecodeFileParameters CpcaParameters = Parameters;
							CpcaParameters.Offset = First;
							CpcaParameters.FractionalBits = HeadBytes;
							FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSAthzCpcaCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), CpcaParameters,
								FIntVector(FMath::DivideAndRoundUp(FMath::Min(Batch, N - First), 64u), 1, 1));
						}
					}
					Immediate.Transition(FRHITransitionInfo(ScratchUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
				}
				// The levels' words from the decoded tails (PackedStreams[8]: the rows).
				if (AthzLodUAV.IsValid())
				{
					FGSDecodeFileParameters LodParameters = Parameters;
					LodParameters.FractionalBits = S[8];
					LodParameters.OutLodWords = AthzLodUAV;
					for (uint32 First = 0; First < Count; First += Batch)
					{
						LodParameters.Offset = First;
						FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSAthzLodCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), LodParameters,
							FIntVector(FMath::DivideAndRoundUp(FMath::Min(Batch, Count - First), 64u), 1, 1));
					}
					Immediate.Transition(FRHITransitionInfo(AthzLodUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
				}
				Parameters.Offset = Offset;
				Parameters.FractionalBits = uint32(SplatData.PackedSpz.X);
			}
			if (!bScratchFilled)
			{
				// One group a block, a thread a word of a row.
				FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSAthc3UndeltaCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), Parameters,
					FIntVector(int32(S[1]), 1, 1));
				Immediate.Transition(FRHITransitionInfo(ScratchUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
				bScratchFilled = true;
			}
			if (SplatData.PackedFormat == 5 && S.Num() > 7 && (S[7] & 2u) != 0)
			{
				// CPCA-coded transfer sections rebuilt into the scratch first.
				FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSAthc3CpcaCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), Parameters, Groups);
				Immediate.Transition(FRHITransitionInfo(ScratchUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
			}
			if (bAthc3)
			{
				FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSDecodeAthc3CS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), Parameters, Groups);
				if (bAthc3Lod && AthzLodUAV.IsValid())
				{
					FGSDecodeFileParameters LodParameters = Parameters;
					LodParameters.OutLodWords = AthzLodUAV;
					FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSAthc3LodCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), LodParameters, Groups);
					if (Offset + Batch >= Count)
					{
						Immediate.Transition(FRHITransitionInfo(AthzLodUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
					}
				}
			}
			else if (SplatData.PackedFormat == 1)
			{
				FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSDecodeSplatCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), Parameters, Groups);
			}
			else if (SplatData.PackedFormat == 4)
			{
				FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSDecodeGltfCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), Parameters, Groups);
			}
			else if (SplatData.PackedFormat == 3)
			{
				FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSDecodeSogCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), Parameters, Groups);
			}
			else
			{
				FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSDecodeSpzCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), Parameters, Groups);
			}
		}

		// A .athc v3 TX cloud: its transfer, surface, cells and field written
		// by the device into the buffers a USD TX cloud's arrays fill.
		if ((SplatData.PackedFormat == 5 || SplatData.PackedFormat == 6) && SplatData.bHasTransfer)
		{
			const uint32 Stride = 4u * SplatData.TransferDirectCount;
			FUnorderedAccessViewRHIRef TransferUAV, MetallicUAV, SurfaceUAV, CellsUAV, FieldUAV, CurvatureUAV, EmissionUAV;
			PackedEmission = CreateDecoded(sizeof(float), SplatData.bPackedEmission ? Count * 3u : 1u, TEXT("GaussianSplatEmission"), EmissionUAV);
			PackedCurvature = CreateDecoded(sizeof(float), SplatData.bPackedCurvature ? Count * 3u : 1u, TEXT("GaussianSplatCurvature"), CurvatureUAV);
			PackedTransfer = CreateDecoded(sizeof(float), Count * Stride, TEXT("GaussianSplatTransfer"), TransferUAV);
			PackedMetallic = CreateDecoded(sizeof(float), Count, TEXT("GaussianSplatTransferMetallic"), MetallicUAV);
			PackedSurface = CreateDecoded(sizeof(float), Count * FGaussianSplatData::SurfaceStride, TEXT("GaussianSplatTransferSurface"), SurfaceUAV);
			PackedCells = CreateDecoded(sizeof(uint32), FMath::Max(Count * SplatData.CellWords, 1u), TEXT("GaussianSplatTransferCells"), CellsUAV);
			PackedField = CreateDecoded(sizeof(float), SplatData.bPackedTransferField ? Count * 48u : 1u, TEXT("GaussianSplatTransferField"), FieldUAV);
			for (uint32 Offset = 0; Offset < Count; Offset += Batch)
			{
				FGSDecodeAthc3TxCS::FParameters Parameters;
				Parameters.Offset = Offset;
				Parameters.Count = Count;
				Parameters.InBytes = BytesSRV;
				Parameters.Scratch = ScratchUAV;
				Parameters.Streams0 = FUintVector4(S[0], S[1], S[2], S[3]);
				Parameters.Streams1 = FUintVector2(S[4], S[5]);
				Parameters.Streams2 = FUintVector2(S[6], S[7]);
				Parameters.TxDirect = SplatData.TransferDirectCount;
				Parameters.TxField = SplatData.bPackedTransferField ? 1u : 0u;
				Parameters.TxCellWords = SplatData.CellWords;
				Parameters.OutTransfer = TransferUAV;
				Parameters.OutMetallic = MetallicUAV;
				Parameters.OutSurface = SurfaceUAV;
				Parameters.OutCells = CellsUAV;
				Parameters.OutField = FieldUAV;
				Parameters.OutCurvature = CurvatureUAV;
				Parameters.OutEmission = EmissionUAV;
				FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSDecodeAthc3TxCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), Parameters,
					FIntVector(FMath::DivideAndRoundUp(FMath::Min(Batch, Count - Offset), 64u), 1, 1));
			}
			Immediate.Transition({
				FRHITransitionInfo(TransferUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
				FRHITransitionInfo(MetallicUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
				FRHITransitionInfo(SurfaceUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
				FRHITransitionInfo(CellsUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
				FRHITransitionInfo(FieldUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
				FRHITransitionInfo(CurvatureUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
				FRHITransitionInfo(EmissionUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
			});
		}

		// Resident packed arrays need no decode but their rest coefficients'
		// (the other outputs are a word each, and writes past it are dropped).
		const bool bDecodeAthenea = bAthenea && (!bPackedResident || SplatData.UseHighOrderSH());
		for (uint32 Offset = 0; Offset < Count && bDecodeAthenea; Offset += Batch)
		{
			FGSDecodeAtheneaCS::FParameters Parameters;
			Parameters.Offset = Offset;
			Parameters.Count = Count;
			Parameters.SHWords = SplatData.PackedSHWords;
			Parameters.RestPerColour = SplatData.PackedRestPerColour;
			Parameters.UnitsToCm = SplatData.PackedUnitsToCm;
			Parameters.FileToLocalX = SplatData.FileAxisX;
			Parameters.FileToLocalY = SplatData.FileAxisY;
			Parameters.FileToLocalZ = SplatData.FileAxisZ;
			Parameters.InPositions = InPositions.SRV;
			Parameters.InShape = InShape.SRV;
			Parameters.InSH = InSH.SRV;
			Parameters.OutPositions = PositionsUAV;
			Parameters.OutScales = ScalesUAV;
			Parameters.OutOrientations = OrientationsUAV;
			Parameters.OutSHAndOpacities = SHAndOpacitiesUAV;
			Parameters.OutHighOrderSH = HighOrderSHUAV;
			FComputeShaderUtils::Dispatch(Immediate, Shader, Parameters, FIntVector(FMath::DivideAndRoundUp(FMath::Min(Batch, Count - Offset), 64u), 1, 1));
		}

		// athenea: an .athz's layer K (PackedStreams 9-11: the splats'
		// influences and gradients in the scratch, and how many splats).
		if (SplatData.bSkinFromDecode && SplatData.PackedFormat == 6 && S.Num() > 11 && S[9] != 0u && ScratchUAV.IsValid())
		{
			const uint32 Splats = S[11];
			FUnorderedAccessViewRHIRef InfluencesUAV, GradientsUAV;
			SkinInfluencesBuffer = CreateDecoded(sizeof(uint32), Splats * 4u, TEXT("GaussianSplatsSkinInfluences"), InfluencesUAV);
			SkinGradientsBuffer = CreateDecoded(sizeof(uint32), S[10] != 0u ? Splats * 3u : 1u, TEXT("GaussianSplatsSkinGradients"), GradientsUAV);
			Immediate.Transition(FRHITransitionInfo(ScratchUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
			for (uint32 First = 0; First < Splats; First += Batch)
			{
				FGSAthzSkinCS::FParameters SkinParameters;
				SkinParameters.Offset = First;
				SkinParameters.Count = Splats;
				SkinParameters.InfluenceAt = S[9];
				SkinParameters.GradientAt = S[10];
				SkinParameters.Scratch = ScratchUAV;
				SkinParameters.OutInfluences = InfluencesUAV;
				SkinParameters.OutGradients = GradientsUAV;
				FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSAthzSkinCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), SkinParameters,
					FIntVector(FMath::DivideAndRoundUp(FMath::Min(Batch, Splats - First), 64u), 1, 1));
			}
			Immediate.Transition({ FRHITransitionInfo(InfluencesUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
				FRHITransitionInfo(GradientsUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask) });
		}

		if (SplatData.IsSkinned() && !bPackedResident)
		{
			// athenea: a skinned .athc's decoded buffers are what its pose is written into.
			SkinPositionsUAV = PositionsUAV;
			SkinScalesUAV = ScalesUAV;
			SkinOrientationsUAV = OrientationsUAV;
		}
		Immediate.Transition({
			FRHITransitionInfo(PositionsUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
			FRHITransitionInfo(ScalesUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
			FRHITransitionInfo(OrientationsUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
			FRHITransitionInfo(SHAndOpacitiesUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
			FRHITransitionInfo(HighOrderSHUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
		});
	}
	else
	{
		// athenea: a skinned cloud's buffers are written each frame (GSSkin.usf).
		if (SplatData.IsSkinned())
		{
			PositionsBuffer = CreateWritable(SplatData.Positions, TEXT("GaussianSplatsPositions"), SkinPositionsUAV);
			ScalesBuffer = CreateWritable(SplatData.Scales, TEXT("GaussianSplatsScales"), SkinScalesUAV);
			OrientationsBuffer = CreateWritable(SplatData.Orientations, TEXT("GaussianSplatsOrientations"), SkinOrientationsUAV);
		}
		else
		{
			PositionsBuffer = CreateBuffer(SplatData.Positions, TEXT("GaussianSplatsPositions"));
			ScalesBuffer = CreateBuffer(SplatData.Scales, TEXT("GaussianSplatsScales"));
			OrientationsBuffer = CreateBuffer(SplatData.Orientations, TEXT("GaussianSplatsOrientations"));
		}
		SHAndOpacitiesBuffer = CreateBuffer(SplatData.ZeroOrderHarmonicsCoefficientsAndOpacities, TEXT("GaussianSplatsSHAndOpacities"));
		// The rest coefficients, uploaded as floats and packed into halves on
		// the device (GSPackSHCS), as the decoders write them.
		const uint32 Count = SplatData.SplatCount;
		const bool bSH = SplatData.UseHighOrderSH() && SplatData.HighOrderHarmonicsCoefficients.Num() == int32(Count * FGaussianSplatData::NumHighOrderSH);
		const FRHIBufferCreateDesc PackedDesc =
			FRHIBufferCreateDesc::CreateStructured(TEXT("GaussianSplatHighOrderSH"), (bSH ? Count * 23u : 1u) * 4u, 4u)
			.AddUsage(EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess)
			.SetInitialState(ERHIAccess::UAVCompute);
		FBufferRHIRef PackedSH = RHICmdList.CreateBuffer(PackedDesc);
		HighOrderSHBuffer = FBufferResources{ PackedSH, RHICmdList.CreateShaderResourceView(PackedSH,
			FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Structured).SetStride(4u)) };
		if (bSH)
		{
			const uint32 Bytes = Count * FGaussianSplatData::NumHighOrderSH * 4u;
			const FRHIBufferCreateDesc FloatsDesc =
				FRHIBufferCreateDesc::CreateByteAddress(TEXT("GaussianSplatHighOrderSHFloats"), Bytes, 4)
				.AddUsage(EBufferUsageFlags::ShaderResource)
				.DetermineInitialState()
				.SetInitActionInitializer();
			FRHIBufferInitializer Initializer = RHICmdList.CreateBufferInitializer(FloatsDesc);
			Initializer.WriteData(SplatData.HighOrderHarmonicsCoefficients.GetData(), Bytes);
			FBufferRHIRef Floats = Initializer.Finalize();
			FGSDecodeFileParameters Parameters;
			Parameters.Count = Count;
			Parameters.InBytes = RHICmdList.CreateShaderResourceView(Floats, FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Raw));
			Parameters.OutHighOrderSH = RHICmdList.CreateUnorderedAccessView(PackedSH,
				FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Structured).SetStride(4u));
			FRHICommandListImmediate& Immediate = FRHICommandListImmediate::Get();
			const uint32 Batch = 65535u * 64u;
			for (uint32 First = 0; First < Count; First += Batch)
			{
				Parameters.Offset = First;
				FComputeShaderUtils::Dispatch(Immediate, TShaderMapRef<FGSPackSHCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), Parameters,
					FIntVector(FMath::DivideAndRoundUp(FMath::Min(Batch, Count - First), 64u), 1, 1));
			}
			Immediate.Transition(FRHITransitionInfo(Parameters.OutHighOrderSH, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		}
		else
		{
			FRHICommandListImmediate::Get().Transition(FRHITransitionInfo(PackedSH, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		}
	}

	// Will be a buffer with a single element if splat data does not use property
	// Uniform buffers do not allow null SRVs, has to point to a valid resource
	NormalsBuffer = CreateBuffer(SplatData.Normals, TEXT("GaussianSplatsNormals"), SplatData.UseNormals());
	MetallicRoughnessBuffer = CreateBuffer(SplatData.MetallicRoughness, TEXT("GaussianSplatMetallicRoughness"), SplatData.UseMetallicRoughness());
	// athenea: A SKINNED CLOUD: its bind pose a copy of the buffers as made
	// (USD) or decoded (.athc), its influences, gradients and parts' fields.
	bSkinned = SplatData.IsSkinned() && SkinPositionsUAV.IsValid()
		&& (SplatData.SkinInfluences.Num() == int32(SplatData.SplatCount * SplatData.SkinPerSplat)
			|| (SplatData.bSkinFromDecode && SkinInfluencesBuffer.SRV.IsValid()));
	if (bSkinned)
	{
		auto Copy = [&](const FBufferResources& From, const TCHAR* DebugName)
		{
			const uint32 Size = From.Buffer->GetSize(), Stride = From.Buffer->GetStride();
			const FRHIBufferCreateDesc CreateDesc =
				FRHIBufferCreateDesc::CreateStructured(DebugName, Size, Stride)
				.AddUsage(EBufferUsageFlags::ShaderResource)
				.SetInitialState(ERHIAccess::CopyDest);
			FBufferRHIRef Buffer = RHICmdList.CreateBuffer(CreateDesc);
			FRHICommandListImmediate& Immediate = FRHICommandListImmediate::Get();
			Immediate.Transition(FRHITransitionInfo(From.Buffer, ERHIAccess::SRVMask, ERHIAccess::CopySrc));
			Immediate.CopyBufferRegion(Buffer, 0, From.Buffer, 0, Size);
			Immediate.Transition({ FRHITransitionInfo(From.Buffer, ERHIAccess::CopySrc, ERHIAccess::SRVMask),
				FRHITransitionInfo(Buffer, ERHIAccess::CopyDest, ERHIAccess::SRVMask) });
			return FBufferResources{ Buffer, RHICmdList.CreateShaderResourceView(Buffer,
				FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Structured).SetStride(Stride)) };
		};
		SkinRestPositionsBuffer = Copy(PositionsBuffer, TEXT("GaussianSplatsSkinRestPositions"));
		SkinRestRotationsBuffer = Copy(OrientationsBuffer, TEXT("GaussianSplatsSkinRestRotations"));
		SkinRestScalesBuffer = Copy(ScalesBuffer, TEXT("GaussianSplatsSkinRestScales"));
		if (!SplatData.bSkinFromDecode)
		{
			SkinInfluencesBuffer = CreateBuffer(SplatData.SkinInfluences, TEXT("GaussianSplatsSkinInfluences"));
			SkinGradientsBuffer = CreateBuffer(SplatData.SkinGradients, TEXT("GaussianSplatsSkinGradients"), !SplatData.SkinGradients.IsEmpty());
		}
		SkinShown = -1.0f;
		SkinSunShown = FVector4f(0, 0, 0, -1);
		// The parts' baked visibility, and a factor pair a gaussian (1 until posed).
		VisParts = SplatData.VisibilityParts.Num() % 12 == 0 && SplatData.VisibilityPartOf.Num() == int32(SplatData.SplatCount)
			? uint32(SplatData.VisibilityParts.Num() / 12) : 0u;
		TArray<FVector4f> PartRows;
		for (uint32 k = 0; k < VisParts; ++k)
		{
			const float* P = &SplatData.VisibilityParts[k * 12];
			PartRows.Add(FVector4f(P[0], P[1], P[2], P[3]));
			PartRows.Add(FVector4f(P[4], P[5], P[6], P[7]));
			PartRows.Add(FVector4f(P[8], P[9], P[10], P[11]));
		}
		VisPartsBuffer = CreateBuffer(PartRows, TEXT("GaussianSplatsVisParts"), VisParts > 0);
		VisPartOfBuffer = CreateBuffer(SplatData.VisibilityPartOf, TEXT("GaussianSplatsVisPartOf"), VisParts > 0);
		VisTexelsBuffer = CreateBuffer(SplatData.VisibilityTexels, TEXT("GaussianSplatsVisTexels"), VisParts > 0);
		VisAmbientBuffer = CreateBuffer(SplatData.VisibilityAmbient, TEXT("GaussianSplatsVisAmbient"), VisParts > 0);
		TArray<FVector2f> Ones;
		Ones.Init(FVector2f(1.0f, 1.0f), VisParts > 0 ? int32(SplatData.SplatCount) : 1);
		VisibilityBuffer = CreateWritable(Ones, TEXT("GaussianSplatsVisibility"), VisibilityUAV);
	}

	const uint32 LightLayerCount = uint32(FMath::Min(SplatData.LightLayerNames.Num(), GSMaxLightLayers));
	const bool bLightLayers = LightLayerCount > 0 && SplatData.LightLayers.Num() >= int32(SplatData.SplatCount * LightLayerCount);
	LightLayersBuffer = CreateBuffer(SplatData.LightLayers, TEXT("GaussianSplatLightLayers"), bLightLayers);
	const bool bCrypto = SplatData.CryptoIds.Num() == int32(SplatData.SplatCount);
	CryptoBuffer = CreateBuffer(SplatData.CryptoIds, TEXT("GaussianSplatCryptoIds"), bCrypto);
	const bool bHardness = SplatData.Hardness.Num() == int32(SplatData.SplatCount);
	HardnessBuffer = CreateBuffer(SplatData.Hardness, TEXT("GaussianSplatHardness"), bHardness);
	// A float cloud says whether it holds a surfel by its scales; a packed one
	// by its merged levels (HasSurfels).
	bSurfels = bHardness;
	for (int32 i = 0; !bSurfels && i < SplatData.Scales.Num(); i++)
	{
		const FVector3f& S = SplatData.Scales[i];
		bSurfels = FMath::Min3(S.X, S.Y, S.Z) == 0.0f;
	}
	const bool bDeviceTransfer = SplatData.bPacked && (SplatData.PackedFormat == 5 || SplatData.PackedFormat == 6) && SplatData.bHasTransfer;
	if (bDeviceTransfer)
	{
		TransferBuffer = PackedTransfer;
		TransferMetallicBuffer = PackedMetallic;
		TransferSurfaceBuffer = PackedSurface;
		TransferCellsBuffer = PackedCells;
		TransferFieldBuffer = PackedField;
		CurvatureBuffer = PackedCurvature;
		EmissionBuffer = PackedEmission;
	}
	else
	{
		TransferBuffer = CreateBuffer(SplatData.Transfer, TEXT("GaussianSplatTransfer"), SplatData.bHasTransfer);
		TransferMetallicBuffer = CreateBuffer(SplatData.TransferMetallic, TEXT("GaussianSplatTransferMetallic"), SplatData.bHasTransfer);
		TransferSurfaceBuffer = CreateBuffer(SplatData.TransferSurface, TEXT("GaussianSplatTransferSurface"), SplatData.bHasTransfer);
		TransferCellsBuffer = CreateBuffer(SplatData.TransferCells, TEXT("GaussianSplatTransferCells"), SplatData.CellWords > 0);
		TransferFieldBuffer = CreateBuffer(SplatData.TransferField, TEXT("GaussianSplatTransferField"), !SplatData.TransferField.IsEmpty());
		CurvatureBuffer = CreateBuffer(SplatData.TransferCurvature, TEXT("GaussianSplatCurvature"), !SplatData.TransferCurvature.IsEmpty());
		EmissionBuffer = CreateBuffer(SplatData.Emission, TEXT("GaussianSplatEmission"), !SplatData.Emission.IsEmpty());
	}

	if (LodWordsBuffer.SRV.IsValid())
	{
		// Written by the device from an .athz's decoded tails.
		LodLevels = SplatData.LodLevels;
	}
	else
	{
		LodLevels = SplatData.LodWords.Num() == int32(SplatData.SplatCount) * 2 ? SplatData.LodLevels : 0u;
		LodWordsBuffer = CreateBuffer(SplatData.LodWords, TEXT("GaussianSplatLodWords"), LodLevels > 0);
	}
	{
		// athenea: where each finest-level group's splats begin. A .athc's splats
		// are in Morton order, so a group's are one run and the runs are in the
		// groups' order: the culling pass then walks the groups and writes a
		// drawn group's run, not the splats (GSplatCullingGroupsCS). Read from
		// the cut's words; anything else found there leaves the runs unused.
		TArray<uint32> Starts;
		bool bRuns = false;
		LodSplatsEnd = 0;
		if (LodLevels > 0 && SplatData.LodWords.Num() == int32(SplatData.SplatCount) * 2)
		{
			const uint32 Count = uint32(SplatData.SplatCount);
			uint32 Previous = MAX_uint32;
			uint32 i = 0;
			bRuns = true;
			for (; i < Count; i++)
			{
				const uint32 B = SplatData.LodWords[int32(i * 2 + 1)];
				if (B != FGaussianSplatData::kLodLeaf)
				{
					break;
				}
				const uint32 A = SplatData.LodWords[int32(i * 2)];
				if (A != Previous)
				{
					if (A != uint32(Starts.Num()))
					{
						bRuns = false;
						break;
					}
					Starts.Add(i);
					Previous = A;
				}
			}
			LodSplatsEnd = i;
			bRuns = bRuns && Starts.Num() > 0 && i < Count && (SplatData.LodWords[int32(i * 2 + 1)] & 0xA0000000u) == 0u;
		}
		if (!bRuns)
		{
			Starts.Empty();
		}
		LodGroupRuns = uint32(Starts.Num());
		LodGroupStartsBuffer = CreateBuffer(Starts, TEXT("GaussianSplatLodGroupStarts"), bRuns);
	}
	bVolume = SplatData.bVolume && SplatData.Volume.Num() == int32(SplatData.SplatCount);
	VolumeBuffer = CreateBuffer(SplatData.Volume, TEXT("GaussianSplatVolume"), bVolume);

	// Create uniform buffer
	FGaussianSplatShaderResources Parameters;
	Parameters.SplatCount = SplatData.SplatCount;

	Parameters.Positions = PositionsBuffer.SRV;
	Parameters.Scales = ScalesBuffer.SRV;
	Parameters.Orientations = OrientationsBuffer.SRV;
	Parameters.SHAndOpacities = SHAndOpacitiesBuffer.SRV;

	Parameters.Normals = NormalsBuffer.SRV;
	Parameters.HighOrderSH = HighOrderSHBuffer.SRV;
	Parameters.MetallicRoughness = MetallicRoughnessBuffer.SRV;

	Parameters.ColorFlags = (SplatData.UseHighOrderSH() ? 1u : 0u) | (SplatData.bLinearizeColor ? 2u : 0u)
		| (SplatData.bHasTransfer ? 4u : 0u);
	Parameters.TransferDirectCount = SplatData.bHasTransfer ? SplatData.TransferDirectCount : 0u;
	Parameters.Transfer = TransferBuffer.SRV;
	Parameters.TransferMetallic = TransferMetallicBuffer.SRV;
	Parameters.TransferSurface = TransferSurfaceBuffer.SRV;
	Parameters.CellWords = SplatData.CellWords;
	Parameters.TransferCells = TransferCellsBuffer.SRV;
	Parameters.HasField = (!SplatData.TransferField.IsEmpty() || (bDeviceTransfer && SplatData.bPackedTransferField)) ? 1u : 0u;
	Parameters.TransferField = TransferFieldBuffer.SRV;
	Parameters.HasCurvature = (SplatData.bHasTransfer && (!SplatData.TransferCurvature.IsEmpty() || (bDeviceTransfer && SplatData.bPackedCurvature))) ? 1u : 0u;
	Parameters.TransferCurvature = CurvatureBuffer.SRV;
	Parameters.HasEmission = (SplatData.bHasTransfer && (!SplatData.Emission.IsEmpty() || (bDeviceTransfer && SplatData.bPackedEmission))) ? 1u : 0u;
	Parameters.Emission = EmissionBuffer.SRV;
	Parameters.HasHardness = SplatData.Hardness.Num() == int32(SplatData.SplatCount) ? 1u : 0u;
	Parameters.Hardness = HardnessBuffer.SRV;
	Parameters.TransferZonal = SplatData.bHasTransfer && SplatData.bTransferZonal ? 1u : 0u;
	Parameters.Catcher = SplatData.bHasTransfer && SplatData.bShadowCatcher ? 1u : 0u;
	Parameters.LightCatcher = SplatData.bLightCatcher && SplatData.LightLayerNames.Num() > 0 ? 1u : 0u;
	Parameters.HasCrypto = SplatData.CryptoIds.Num() == int32(SplatData.SplatCount) ? 1u : 0u;
	Parameters.ShadowCut = CVarGaussiansAtheneaTranslucentShadows.GetValueOnAnyThread() != 0 ? 1.0f / 255.0f : 0.9f;
	Parameters.ShadowClip = CVarGaussiansAtheneaShadowClip.GetValueOnAnyThread();
	Parameters.ShadowPush = FMath::Max(CVarGaussiansAtheneaShadowPush.GetValueOnAnyThread(), 0.0f);
	Parameters.CryptoIds = CryptoBuffer.SRV;
	Parameters.HasVisibility = bSkinned && VisParts > 0 ? 1u : 0u;
	if (!VisibilityBuffer.SRV.IsValid())
	{
		VisibilityBuffer = CreateBuffer(TArray<FVector2f>({ FVector2f(1.0f, 1.0f) }), TEXT("GaussianSplatsVisibilityNone"));
	}
	Parameters.Visibility = VisibilityBuffer.SRV;
	Parameters.LightLayerCount = bLightLayers ? LightLayerCount : 0u;
	Parameters.LightLayerSplats = bLightLayers ? SplatData.SplatCount : 0u;
	Parameters.LightLayers = LightLayersBuffer.SRV;
	Parameters.FileUnitsToCm = SplatData.FileUnitsToCm;
	Parameters.GlassIor = SplatData.GlassIor;
	Parameters.FileAxisX = SplatData.FileAxisX;
	Parameters.FileAxisY = SplatData.FileAxisY;
	Parameters.FileAxisZ = SplatData.FileAxisZ;
	Parameters.LodLevels = LodLevels;
	Parameters.LodFinestLevel = SplatData.LodFinestLevel;
	Parameters.LodFinestBase = SplatData.LodFinestBase;
	{
		// Each level's run of groups (the morph's parents): the data's, or
		// read from the words -- a level's groups are one run.
		uint32 First[FGaussianSplatData::kLodLevelRuns] = {};
		uint32 Count[FGaussianSplatData::kLodLevelRuns] = {};
		if (SplatData.LodLevelRuns.Num() == FGaussianSplatData::kLodLevelRuns * 2)
		{
			for (int32 l = 0; l < FGaussianSplatData::kLodLevelRuns; l++)
			{
				First[l] = SplatData.LodLevelRuns[l * 2];
				Count[l] = SplatData.LodLevelRuns[l * 2 + 1];
			}
		}
		else if (LodLevels > 0 && SplatData.LodWords.Num() == int32(SplatData.SplatCount) * 2)
		{
			for (uint32 i = 0; i < uint32(SplatData.SplatCount); i++)
			{
				const uint32 B = SplatData.LodWords[int32(i * 2 + 1)];
				if ((B & (FGaussianSplatData::kLodLeaf | 0x20000000u)) == 0u && (B & 0xFFu) < uint32(FGaussianSplatData::kLodLevelRuns))
				{
					const uint32 l = B & 0xFFu;
					First[l] = Count[l] == 0u ? i : First[l];
					Count[l]++;
				}
			}
		}
		for (int32 l = 0; l < FGaussianSplatData::kLodLevelRuns; l++)
		{
			Parameters.LodLevelRuns[l] = FUintVector4(First[l], Count[l], 0u, 0u);
		}
		LodFinestGroups = (LodLevels > 0 && SplatData.LodFinestLevel < uint32(FGaussianSplatData::kLodLevelRuns)
			&& First[SplatData.LodFinestLevel] == SplatData.LodFinestBase) ? Count[SplatData.LodFinestLevel] : 0u;
	}
	Parameters.LodExtent = SplatData.LodExtent;
	Parameters.LodBoundsLo = SplatData.LodBoundsLo;
	Parameters.LodUnitsToCm = SplatData.bPacked ? SplatData.PackedUnitsToCm : SplatData.FileUnitsToCm;
	Parameters.LodWords = LodWordsBuffer.SRV;
	Parameters.LodGroupStarts = LodGroupStartsBuffer.SRV;
	Parameters.LodSplatsEnd = LodSplatsEnd;
	Parameters.LodFinestGroups = LodFinestGroups;
	Parameters.LodGroupRuns = GetLodGroupRuns();
	if (!PackedPositionsBuffer.SRV.IsValid())
	{
		PackedPositionsBuffer = CreateBuffer(TArray<FVector4f>(), TEXT("GaussianSplatsPackedPositionsNone"));
		PackedShapeBuffer = CreateBuffer(TArray<uint32>(), TEXT("GaussianSplatsPackedShapeNone"));
	}
	Parameters.PackedResident = bPackedResident ? 1u : 0u;
	Parameters.PackedUnitsToCm = SplatData.PackedUnitsToCm;
	Parameters.PackedPositions = PackedPositionsBuffer.SRV;
	Parameters.PackedShape = PackedShapeBuffer.SRV;
	Parameters.VolumeOn = bVolume ? 1u : 0u;
	Parameters.VolumeDensityScale = SplatData.VolumeDensityScale;
	Parameters.VolumeEmissionScale = SplatData.VolumeEmissionScale;
	Parameters.VolumeAnisotropy = SplatData.VolumeAnisotropy;
	Parameters.VolumeTemperatureScale = SplatData.VolumeTemperatureScale;
	Parameters.VolumeTemperatureOffset = SplatData.VolumeTemperatureOffset;
	Parameters.VolumeAlbedo = SplatData.VolumeAlbedo;
	Parameters.VolumeEmission = SplatData.VolumeEmission;
	Parameters.Volume = VolumeBuffer.SRV;

	UniformBuffer = TUniformBufferRef<FGaussianSplatShaderResources>::CreateUniformBufferImmediate(Parameters, UniformBuffer_MultiFrame);
}

void FGaussianSplatResource::ReleaseRHI()
{
	check(IsInRenderingThread());

	PackedTransfer.SafeRelease();
	PackedMetallic.SafeRelease();
	PackedSurface.SafeRelease();
	PackedCells.SafeRelease();
	PackedField.SafeRelease();
	PackedCurvature.SafeRelease();
	CurvatureBuffer.SafeRelease();
	PackedEmission.SafeRelease();
	EmissionBuffer.SafeRelease();
	HardnessBuffer.SafeRelease();
	LightLayersBuffer.SafeRelease();
	CryptoBuffer.SafeRelease();
	SkinRestPositionsBuffer.SafeRelease();
	SkinRestRotationsBuffer.SafeRelease();
	SkinRestScalesBuffer.SafeRelease();
	SkinInfluencesBuffer.SafeRelease();
	SkinGradientsBuffer.SafeRelease();
	SkinPositionsUAV.SafeRelease();
	SkinScalesUAV.SafeRelease();
	SkinOrientationsUAV.SafeRelease();
	VisPartsBuffer.SafeRelease();
	VisPartOfBuffer.SafeRelease();
	VisTexelsBuffer.SafeRelease();
	VisAmbientBuffer.SafeRelease();
	VisibilityBuffer.SafeRelease();
	VisibilityUAV.SafeRelease();
	VisParts = 0;
	bSkinned = false;
	LodWordsBuffer.SafeRelease();
	LodGroupStartsBuffer.SafeRelease();
	PackedPositionsBuffer.SafeRelease();
	PackedShapeBuffer.SafeRelease();
	VolumeBuffer.SafeRelease();
	TransferBuffer.SafeRelease();
	TransferSurfaceBuffer.SafeRelease();
	TransferCellsBuffer.SafeRelease();
	TransferFieldBuffer.SafeRelease();
	TransferMetallicBuffer.SafeRelease();
	UniformBuffer.SafeRelease();

	PositionsBuffer.SafeRelease();
	ScalesBuffer.SafeRelease();
	OrientationsBuffer.SafeRelease();
	SHAndOpacitiesBuffer.SafeRelease();

	NormalsBuffer.SafeRelease();
	HighOrderSHBuffer.SafeRelease();
	MetallicRoughnessBuffer.SafeRelease();
}

void FGaussianSplatResource::UpdateSplats(FRHICommandListBase& RHICmdList, FGaussianSplatData InSplatData)
{
	check(IsInRenderingThread());

	SplatData = MoveTemp(InSplatData);
	UpdateRHI(RHICmdList);
}

void FGaussianSplatResource::Skin_RenderThread(FRDGBuilder& GraphBuilder, float Seconds, const FMatrix& LocalToWorld, const FVector4f& SunToLight)
{
	if (!bSkinned || !SkinPositionsUAV.IsValid())
	{
		return;
	}
	const TArray<float>& Times = SplatData.SkinTimes;
	const float At = FMath::Clamp(Seconds, Times[0], Times.Last());
	// The sun in the file's frame: the world's way to it, into the component's
	// space and onto the file's axes.
	FVector4f Sun(0, 0, 0, 0);
	if (SunToLight.W > 0.0f)
	{
		const FVector Local = LocalToWorld.InverseTransformVector(FVector(SunToLight.X, SunToLight.Y, SunToLight.Z)).GetSafeNormal();
		const FVector3f L(Local);
		Sun = FVector4f(FVector3f(L | SplatData.FileAxisX, L | SplatData.FileAxisY, L | SplatData.FileAxisZ).GetSafeNormal(), 1.0f);
	}
	if (At == SkinShown && (VisParts == 0 || Sun.Equals(SkinSunShown, 1.0e-4f)))
	{
		return;
	}
	SkinShown = At;
	SkinSunShown = Sun;
	// The two sampled poses around the instant, blended row by row.
	int32 Next = 0;
	while (Next < Times.Num() - 1 && Times[Next] < At)
	{
		++Next;
	}
	const int32 Prev = FMath::Max(Next - 1, 0);
	const float Span = Times[Next] - Times[Prev];
	const float T = Span > 0.0f ? FMath::Clamp((At - Times[Prev]) / Span, 0.0f, 1.0f) : 0.0f;
	const int32 Rows = int32(SplatData.SkinJoints) * 3;
	TArray<FVector4f> Xforms;
	Xforms.SetNumUninitialized(Rows);
	for (int32 r = 0; r < Rows; ++r)
	{
		const FVector4f& A = SplatData.SkinXforms[Prev * Rows + r];
		Xforms[r] = A + (SplatData.SkinXforms[Next * Rows + r] - A) * T;
	}
	FRDGBufferRef XformsBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("GaussianSplatsSkinXforms"), Xforms);
	// Each part's posed-to-rest map: the inverse of its joint's C = X G
	// (athenea's visPartFrames).
	TArray<FVector4f> Frames;
	for (uint32 k = 0; k < VisParts; ++k)
	{
		const uint32 Joint = FMath::Min(uint32(FMath::Max(SplatData.VisibilityParts[k * 12 + 3], 0.0f)), SplatData.SkinJoints - 1u);
		const FVector4f& R0 = Xforms[Joint * 3], & R1 = Xforms[Joint * 3 + 1], & R2 = Xforms[Joint * 3 + 2];
		FMatrix44f M = FMatrix44f::Identity;
		for (int32 c = 0; c < 4; ++c)
		{
			M.M[c][0] = R0[c];
			M.M[c][1] = R1[c];
			M.M[c][2] = R2[c];
		}
		// M is the transpose (row vectors): its inverse, transposed back.
		const FMatrix44f Inv = M.Inverse();
		Frames.Add(FVector4f(Inv.M[0][0], Inv.M[1][0], Inv.M[2][0], Inv.M[3][0]));
		Frames.Add(FVector4f(Inv.M[0][1], Inv.M[1][1], Inv.M[2][1], Inv.M[3][1]));
		Frames.Add(FVector4f(Inv.M[0][2], Inv.M[1][2], Inv.M[2][2], Inv.M[3][2]));
	}
	if (Frames.IsEmpty())
	{
		Frames.Add(FVector4f(0, 0, 0, 0));
	}
	FRDGBufferRef FramesBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("GaussianSplatsVisFrames"), Frames);

	FGSSkinCS::FParameters* Parameters = GraphBuilder.AllocParameters<FGSSkinCS::FParameters>();
	// An .athz's layer K poses its splats, the first elements; its merged
	// groups after them are not drawn while it is skinned.
	const bool bDecoded = SplatData.bSkinFromDecode && SplatData.PackedStreams.Num() > 11;
	Parameters->Count = bDecoded ? SplatData.PackedStreams[11] : SplatData.SplatCount;
	Parameters->PerSplat = SplatData.SkinPerSplat;
	Parameters->GradientsIn = bDecoded ? (SplatData.PackedStreams[10] != 0u ? 1u : 0u) : (SplatData.SkinGradients.IsEmpty() ? 0u : 1u);
	Parameters->UnitsToCm = SplatData.FileUnitsToCm;
	Parameters->FileAxisX = SplatData.FileAxisX;
	Parameters->FileAxisY = SplatData.FileAxisY;
	Parameters->FileAxisZ = SplatData.FileAxisZ;
	Parameters->RestPositions = SkinRestPositionsBuffer.SRV;
	Parameters->RestRotations = SkinRestRotationsBuffer.SRV;
	Parameters->RestScales = SkinRestScalesBuffer.SRV;
	Parameters->Influences = SkinInfluencesBuffer.SRV;
	Parameters->Gradients = SkinGradientsBuffer.SRV;
	Parameters->SkinXforms = GraphBuilder.CreateSRV(XformsBuffer);
	Parameters->OutPositions = SkinPositionsUAV;
	Parameters->OutScales = SkinScalesUAV;
	Parameters->OutOrientations = SkinOrientationsUAV;
	Parameters->VisibilityParts = VisParts;
	Parameters->SunDirection = Sun;
	Parameters->Parts = VisPartsBuffer.SRV;
	Parameters->PartOf = VisPartOfBuffer.SRV;
	Parameters->Texels = VisTexelsBuffer.SRV;
	Parameters->Ambient = VisAmbientBuffer.SRV;
	Parameters->PartFrames = GraphBuilder.CreateSRV(FramesBuffer);
	Parameters->OutVisibility = VisibilityUAV;
	FUnorderedAccessViewRHIRef Uavs[4] = { SkinPositionsUAV, SkinScalesUAV, SkinOrientationsUAV, VisibilityUAV };
	GraphBuilder.AddPass(RDG_EVENT_NAME("Gaussians: skin %u", SplatData.SplatCount), Parameters, ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
		[Parameters, Uavs](FRHIComputeCommandList& RHICmdList)
		{
			RHICmdList.Transition({
				FRHITransitionInfo(Uavs[0], ERHIAccess::SRVMask, ERHIAccess::UAVCompute),
				FRHITransitionInfo(Uavs[1], ERHIAccess::SRVMask, ERHIAccess::UAVCompute),
				FRHITransitionInfo(Uavs[2], ERHIAccess::SRVMask, ERHIAccess::UAVCompute),
				FRHITransitionInfo(Uavs[3], ERHIAccess::SRVMask, ERHIAccess::UAVCompute),
			});
			FComputeShaderUtils::Dispatch(RHICmdList, TShaderMapRef<FGSSkinCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), *Parameters,
				FIntVector(FMath::DivideAndRoundUp(Parameters->Count, 64u), 1, 1));
			RHICmdList.Transition({
				FRHITransitionInfo(Uavs[0], ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
				FRHITransitionInfo(Uavs[1], ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
				FRHITransitionInfo(Uavs[2], ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
				FRHITransitionInfo(Uavs[3], ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
			});
		});
}
