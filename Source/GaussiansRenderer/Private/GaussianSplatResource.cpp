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
	check(!SplatData.bPacked || (SplatData.PackedPositions.Num() == int32(SplatData.SplatCount)
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
		FBufferResources InPositions = CreateBuffer(SplatData.PackedPositions, TEXT("GaussianSplatsPackedPositions"));
		FBufferResources InShape = CreateBuffer(SplatData.PackedShape, TEXT("GaussianSplatsPackedShape"));
		FBufferResources InSH = CreateBuffer(SplatData.PackedSH, TEXT("GaussianSplatsPackedSH"), SplatData.PackedSHWords > 0);

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

		// A dispatch holds at most 65535 groups of 64 along x.
		const uint32 Batch = 65535u * 64u;
		for (uint32 Offset = 0; Offset < Count; Offset += Batch)
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

	Parameters.ColorFlags = (SplatData.UseHighOrderSH() ? 1u : 0u) | (SplatData.bLinearizeColor ? 2u : 0u);
	Parameters.FileAxisX = SplatData.FileAxisX;
	Parameters.FileAxisY = SplatData.FileAxisY;
	Parameters.FileAxisZ = SplatData.FileAxisZ;

	UniformBuffer = TUniformBufferRef<FGaussianSplatShaderResources>::CreateUniformBufferImmediate(Parameters, UniformBuffer_MultiFrame);
}

void FGaussianSplatResource::ReleaseRHI()
{
	check(IsInRenderingThread());

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
