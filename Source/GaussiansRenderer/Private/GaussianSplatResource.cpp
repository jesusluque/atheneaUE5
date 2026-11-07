#include "GaussianSplatResource.h"

#include "GaussianData.h"
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
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, OutHighOrderSH)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FGSDecodeAtheneaCS, "/Plugin/StarLightGS/GSDecode.usf", "GSDecodeAtheneaCS", SF_Compute);

// athenea: a file's own packing (.splat, .spz) decoded from its bytes.
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
	SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, OutHighOrderSH)
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
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSDecodeAthc3TxCS, "/Plugin/StarLightGS/GSDecode.usf", "GSDecodeAthc3TxCS", SF_Compute);


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
		PositionsBuffer = CreateDecoded(sizeof(FVector3f), Count, TEXT("GaussianSplatsPositions"), PositionsUAV);
		ScalesBuffer = CreateDecoded(sizeof(FVector3f), Count, TEXT("GaussianSplatsScales"), ScalesUAV);
		OrientationsBuffer = CreateDecoded(sizeof(FVector4f), Count, TEXT("GaussianSplatsOrientations"), OrientationsUAV);
		SHAndOpacitiesBuffer = CreateDecoded(sizeof(FVector4f), Count, TEXT("GaussianSplatsSHAndOpacities"), SHAndOpacitiesUAV);
		HighOrderSHBuffer = CreateDecoded(sizeof(float), Count * FGaussianSplatData::NumHighOrderSH, TEXT("GaussianSplatHighOrderSH"), HighOrderSHUAV);

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
			FUnorderedAccessViewRHIRef TransferUAV, MetallicUAV, SurfaceUAV, CellsUAV, FieldUAV, CurvatureUAV;
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
			});
		}

		for (uint32 Offset = 0; Offset < Count && bAthenea; Offset += Batch)
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
		PositionsBuffer = CreateBuffer(SplatData.Positions, TEXT("GaussianSplatsPositions"));
		ScalesBuffer = CreateBuffer(SplatData.Scales, TEXT("GaussianSplatsScales"));
		OrientationsBuffer = CreateBuffer(SplatData.Orientations, TEXT("GaussianSplatsOrientations"));
		SHAndOpacitiesBuffer = CreateBuffer(SplatData.ZeroOrderHarmonicsCoefficientsAndOpacities, TEXT("GaussianSplatsSHAndOpacities"));
		HighOrderSHBuffer = CreateBuffer(SplatData.HighOrderHarmonicsCoefficients, TEXT("GaussianSplatHighOrderSH"), SplatData.UseHighOrderSH());
	}

	// Will be a buffer with a single element if splat data does not use property
	// Uniform buffers do not allow null SRVs, has to point to a valid resource
	NormalsBuffer = CreateBuffer(SplatData.Normals, TEXT("GaussianSplatsNormals"), SplatData.UseNormals());
	MetallicRoughnessBuffer = CreateBuffer(SplatData.MetallicRoughness, TEXT("GaussianSplatMetallicRoughness"), SplatData.UseMetallicRoughness());
	const bool bDeviceTransfer = SplatData.bPacked && (SplatData.PackedFormat == 5 || SplatData.PackedFormat == 6) && SplatData.bHasTransfer;
	if (bDeviceTransfer)
	{
		TransferBuffer = PackedTransfer;
		TransferMetallicBuffer = PackedMetallic;
		TransferSurfaceBuffer = PackedSurface;
		TransferCellsBuffer = PackedCells;
		TransferFieldBuffer = PackedField;
		CurvatureBuffer = PackedCurvature;
	}
	else
	{
		TransferBuffer = CreateBuffer(SplatData.Transfer, TEXT("GaussianSplatTransfer"), SplatData.bHasTransfer);
		TransferMetallicBuffer = CreateBuffer(SplatData.TransferMetallic, TEXT("GaussianSplatTransferMetallic"), SplatData.bHasTransfer);
		TransferSurfaceBuffer = CreateBuffer(SplatData.TransferSurface, TEXT("GaussianSplatTransferSurface"), SplatData.bHasTransfer);
		TransferCellsBuffer = CreateBuffer(SplatData.TransferCells, TEXT("GaussianSplatTransferCells"), SplatData.CellWords > 0);
		TransferFieldBuffer = CreateBuffer(SplatData.TransferField, TEXT("GaussianSplatTransferField"), !SplatData.TransferField.IsEmpty());
		CurvatureBuffer = CreateBuffer(SplatData.TransferCurvature, TEXT("GaussianSplatCurvature"), !SplatData.TransferCurvature.IsEmpty());
	}

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
	Parameters.FileUnitsToCm = SplatData.FileUnitsToCm;
	Parameters.GlassIor = SplatData.GlassIor;
	Parameters.FileAxisX = SplatData.FileAxisX;
	Parameters.FileAxisY = SplatData.FileAxisY;
	Parameters.FileAxisZ = SplatData.FileAxisZ;

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
