#include "GSViewExtension.h"
#include "DataDrivenShaderPlatformInfo.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "HAL/IConsoleManager.h"
#include "RendererInterface.h"
#include "RendererClasses.h"
#include "ScreenPass.h"
#include "ScenePrivate.h"

#include "GaussianSplatResource.h"
#include "GaussianSplatInterface.h"

#include "GaussiansDefinitions.h"

// athenea: timed apart, as the other stages are.
DECLARE_GPU_STAT_NAMED(GaussiansCulling, TEXT("Gaussians: Culling"));


class FGSplatCullingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSplatCullingCS)
	SHADER_USE_PARAMETER_STRUCT(FGSplatCullingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FGaussianSplatShaderResources, GSResource)
		SHADER_PARAMETER_STRUCT_REF(FPrimitiveUniformShaderParameters, Primitive)

		SHADER_PARAMETER(float, SplatScaleMultiplier)
		SHADER_PARAMETER(FVector3f, LodEyeLocal)
		SHADER_PARAMETER(float, LodPixelsPerUnit)
		SHADER_PARAMETER(float, LodThreshold)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, LodBudget)
		SHADER_PARAMETER(float, LodDissolve)
		SHADER_PARAMETER(float, LodThin)
		SHADER_PARAMETER(float, LodThinLeast)
		SHADER_PARAMETER(float, LodMorph)
		SHADER_PARAMETER(uint32, LodFrustumOn)
		SHADER_PARAMETER_ARRAY(FVector4f, LodFrustum, [4])
		SHADER_PARAMETER(uint32, LodGroupStateOn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, LodGroupState)

		SHADER_PARAMETER(int32, NumClippingVolumes)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FGaussians_ClippingVolume>, ClippingVolumes)

		// Out
		SHADER_PARAMETER_UAV(RWBuffer<uint32>, RWSplatIndexBufferAllocator)
		SHADER_PARAMETER_UAV(RWBuffer<uint32>, RWSplatIndices)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), GetThreadGroupSize1D());
		// athenea: the visible splats appended a wave at a time where the
		// platform guarantees wave operations (D3D12 SM6), one at a time elsewhere.
		const bool bWave = FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(Parameters.Platform) == ERHIFeatureSupport::RuntimeGuaranteed;
		OutEnvironment.SetDefine(TEXT("GS_WAVE_APPEND"), bWave ? 1 : 0);
		if (bWave)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}

	static uint32 GetThreadGroupSize1D() { return 128; }
};

// athenea: the pixels a merged cell may span before its children are drawn in
// its place (athenea's lod threshold, 1 by default); 0 draws every splat.
static TAutoConsoleVariable<float> CVarAtheneaLodPixels(
	TEXT("r.Gaussians.Athenea.LodPixels"), 1.0f,
	TEXT("athenea levels of detail: the pixels a merged cell may span before its children are drawn in its place (0: every splat)."),
	ECVF_RenderThreadSafe);

IMPLEMENT_GLOBAL_SHADER(FGSplatCullingCS, "/Plugin/StarLightGS/GSplatCulling.usf", "GSplatCullingCS", SF_Compute);

// athenea: the view's splat budget brought up to date from what the last
// frame wanted (GSLodBudgetCS), before this frame's cut reads it.
class FGSLodBudgetCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSLodBudgetCS)
	SHADER_USE_PARAMETER_STRUCT(FGSLodBudgetCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWLodBudget)
		SHADER_PARAMETER(uint32, LodBudgetOn)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FGSLodBudgetCS, "/Plugin/StarLightGS/GSplatCulling.usf", "GSLodBudgetCS", SF_Compute);

// athenea: the cut by its groups (GSplatCullingGroupsCS): the culling pass's
// own parameters, another kernel.
class FGSplatCullingGroupsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSplatCullingGroupsCS)
	using FParameters = FGSplatCullingCS::FParameters;
	SHADER_USE_PARAMETER_STRUCT(FGSplatCullingGroupsCS, FGlobalShader);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), 64);
		OutEnvironment.SetDefine(TEXT("GS_WAVE_APPEND"), 0);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSplatCullingGroupsCS, "/Plugin/StarLightGS/GSplatCulling.usf", "GSplatCullingGroupsCS", SF_Compute);

static TAutoConsoleVariable<int32> CVarAtheneaLodGroupRuns(
	TEXT("r.Gaussians.Athenea.LodGroupRuns"), 1,
	TEXT("athenea levels of detail: 1 cuts a .athc by walking its groups and writing each drawn group's run of splats, where nothing but the levels culls it (no clipping volume); 0 walks every splat."),
	ECVF_RenderThreadSafe);

// athenea: the finest level's groups judged once a view (GSLodStateCS), for
// the culling pass's splats to read.
class FGSLodStateCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGSLodStateCS)
	SHADER_USE_PARAMETER_STRUCT(FGSLodStateCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FGaussianSplatShaderResources, GSResource)
		SHADER_PARAMETER_STRUCT_REF(FPrimitiveUniformShaderParameters, Primitive)
		SHADER_PARAMETER(FVector3f, LodEyeLocal)
		SHADER_PARAMETER(float, LodPixelsPerUnit)
		SHADER_PARAMETER(float, LodThreshold)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, LodBudget)
		SHADER_PARAMETER(float, LodDissolve)
		SHADER_PARAMETER(float, LodThin)
		SHADER_PARAMETER(float, LodThinLeast)
		SHADER_PARAMETER(uint32, LodFrustumOn)
		SHADER_PARAMETER_ARRAY(FVector4f, LodFrustum, [4])
		SHADER_PARAMETER(uint32, LodFinestGroups)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWLodGroupState)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), 128);
		OutEnvironment.SetDefine(TEXT("GS_WAVE_APPEND"), 0);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGSLodStateCS, "/Plugin/StarLightGS/GSplatCulling.usf", "GSLodStateCS", SF_Compute);

static TAutoConsoleVariable<int32> CVarAtheneaLodGroupState(
	TEXT("r.Gaussians.Athenea.LodGroupState"), 1,
	TEXT("athenea levels of detail: 1 judges each finest-level group once a view and has its splats read the result; 0 has every splat judge its group's cell itself (the same cut, slower)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarAtheneaLodDissolve(
	TEXT("r.Gaussians.Athenea.LodDissolve"), 2.0f,
	TEXT("athenea levels of detail: each merged group switches at its own threshold, LodPixels times this to the power (h - 1/2), h hashed from its cell, so the cut's edge is spread over a band instead of sliding as one ring (1: athenea's cut)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarAtheneaLodMorph(
	TEXT("r.Gaussians.Athenea.LodMorph"), 2.0f,
	TEXT("athenea levels of detail: a gaussian is drawn toward its parent group as the parent nears its threshold -- from this many thresholds, fully at one -- so the cut switches a parent for children already standing where it does (up to 1: no morph)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarAtheneaLodSweep(
	TEXT("r.Gaussians.Athenea.LodSweep"), 0.0f,
	TEXT("athenea levels of detail, for measuring a cut's switches: LodPixels is multiplied by 2 to the power (this times a sawtooth of the frame number, 0..1 over 128 frames), so a still view's image changes only as groups switch (0: off)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarAtheneaLodFrustum(
	TEXT("r.Gaussians.Athenea.LodFrustum"), 2,
	TEXT("athenea levels of detail: a view's cut leaves out the octree cells its frustum does not reach (a tenth wider than the image), so the prepass walks what the view shows. The shadow passes draw the same list: 2 does it for the clouds that cast no shadow, 1 for every cloud (their shadows then cast only by what the view shows), 0 for none."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarAtheneaLodMorphSplats(
	TEXT("r.Gaussians.Athenea.LodMorphSplats"), 0.0f,
	TEXT("athenea levels of detail: LodMorph for the splats alone -- a splat drawn toward its finest-level group while the group is within this many of its thresholds of being drawn, the merged levels as they are (up to 1: no morph)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarAtheneaLodThin(
	TEXT("r.Gaussians.Athenea.LodThin"), 4.0f,
	TEXT("athenea levels of detail: a finest-level group's splats are thinned before the group is drawn in their place -- while its cell spans under this many thresholds a fraction of them is drawn (a hash of each splat, the same every frame), halving with each equal step of the logarithm down to an eighth at the switch, those drawn widened to cover what they did. Up to 1: every splat."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarAtheneaLodThinLeast(
	TEXT("r.Gaussians.Athenea.LodThinLeast"), 0.03125f,
	TEXT("athenea levels of detail: the fraction of a group's splats LodThin leaves drawn where the group takes their place (a thirty-second by default: one or two of a group's tens)."),
	ECVF_RenderThreadSafe);

FGSLodCut GSLodCutOf(const IGaussianSplatInterface& Proxy, const FViewInfo& ViewInfo)
{
	// athenea's cut: the eye in the primitive's local space, and the pixels a
	// unit at unit distance spans (half the view's height times the
	// projection's y scale).
	FGSLodCut Cut;
	Cut.EyeLocal = FVector3f(Proxy.GetSplatLocalToWorld().InverseTransformPosition(ViewInfo.ViewMatrices.GetViewOrigin()));
	Cut.PixelsPerUnit = 0.5f * float(ViewInfo.ViewRect.Height()) * float(ViewInfo.ViewMatrices.GetProjectionMatrix().M[1][1]);
	Cut.Threshold = FMath::Max(CVarAtheneaLodPixels.GetValueOnRenderThread(), 0.0f);
	const float Sweep = CVarAtheneaLodSweep.GetValueOnRenderThread();
	if (Sweep != 0.0f && ViewInfo.Family != nullptr)
	{
		Cut.Threshold *= FMath::Pow(2.0f, Sweep * float(ViewInfo.Family->FrameNumber % 128u) / 128.0f);
	}
	Cut.Dissolve = FMath::Max(CVarAtheneaLodDissolve.GetValueOnRenderThread(), 1.0f);
	const float Morph = CVarAtheneaLodMorph.GetValueOnRenderThread();
	Cut.Morph = Morph > 1.0f ? Morph : 0.0f;
	const float MorphSplats = CVarAtheneaLodMorphSplats.GetValueOnRenderThread();
	Cut.MorphSplats = MorphSplats > 1.0f ? MorphSplats : 0.0f;
	const float Thin = CVarAtheneaLodThin.GetValueOnRenderThread();
	Cut.Thin = Thin > 1.0f ? Thin : 0.0f;
	Cut.ThinLeast = FMath::Clamp(CVarAtheneaLodThinLeast.GetValueOnRenderThread(), 1.0f / 1024.0f, 1.0f);
	// A skinned cloud is posed splat by splat: its merged groups have no
	// influences, so the cut draws its splats alone.
	if (const FGaussianSplatResource* Resource = Proxy.GetResource_RenderThread(); Resource && Resource->IsSkinned())
	{
		Cut.Threshold = 0.0f;
		Cut.Thin = 0.0f;
		Cut.Morph = 0.0f;
		Cut.MorphSplats = 0.0f;
		Cut.Dissolve = 1.0f;
	}
	// The view's four side planes in the primitive's local space: a point p
	// goes to view space as p * (LocalToWorld * View), +z forward; inside is
	// |x| <= z tx and |y| <= z ty.
	const FMatrix& Projection = ViewInfo.ViewMatrices.GetProjectionMatrix();
	const int32 FrustumMode = CVarAtheneaLodFrustum.GetValueOnRenderThread();
	Cut.bFrustum = (FrustumMode == 1 || (FrustumMode == 2 && !Proxy.CastsSplatShadow())) && ViewInfo.IsPerspectiveProjection()
		&& Projection.M[0][0] > 0.0 && Projection.M[1][1] > 0.0;
	if (Cut.bFrustum)
	{
		const FMatrix ToView = Proxy.GetSplatLocalToWorld() * ViewInfo.ViewMatrices.GetViewMatrix();
		const double Tx = 1.1 / Projection.M[0][0];
		const double Ty = 1.1 / Projection.M[1][1];
		const double Normals[4][3] = { { -1.0, 0.0, Tx }, { 1.0, 0.0, Tx }, { 0.0, -1.0, Ty }, { 0.0, 1.0, Ty } };
		for (int32 k = 0; k < 4; k++)
		{
			const double* N = Normals[k];
			Cut.Frustum[k] = FVector4f(
				float(ToView.M[0][0] * N[0] + ToView.M[0][1] * N[1] + ToView.M[0][2] * N[2]),
				float(ToView.M[1][0] * N[0] + ToView.M[1][1] * N[1] + ToView.M[1][2] * N[2]),
				float(ToView.M[2][0] * N[0] + ToView.M[2][1] * N[1] + ToView.M[2][2] * N[2]),
				float(ToView.M[3][0] * N[0] + ToView.M[3][1] * N[1] + ToView.M[3][2] * N[2]));
		}
	}
	return Cut;
}

static TAutoConsoleVariable<int32> CVarAtheneaLodBudget(
	TEXT("r.Gaussians.Athenea.LodBudget"), 1,
	TEXT("1: when a frame's levels of detail want more splats than r.Gaussians.MaxSplats holds, the next frames raise LodPixels until they fit (far detail goes coarser, no cloud is cut short); 0: the threshold as set."),
	ECVF_RenderThreadSafe);


uint32 ConvertClippingMode(EGaussianSplatClippingVolumeMode Mode)
{
	switch (Mode) {
	case EGaussianSplatClippingVolumeMode::ClipAnyInside:  return GAUSSIANS_CLIPPING_MODE_CLIP_INSIDE_ANY;
	case EGaussianSplatClippingVolumeMode::ClipAllInside:  return GAUSSIANS_CLIPPING_MODE_CLIP_INSIDE_ALL;
	case EGaussianSplatClippingVolumeMode::ClipAnyOutside: return GAUSSIANS_CLIPPING_MODE_CLIP_OUTSIDE_ANY;
	case EGaussianSplatClippingVolumeMode::ClipAllOutside: return GAUSSIANS_CLIPPING_MODE_CLIP_OUTSIDE_ALL;
	default:
		checkNoEntry(); break;
	}
	return -1;
}

uint32 ConvertClippingShape(EGaussianSplatClippingVolumeShape Shape)
{
	switch (Shape) {
	case EGaussianSplatClippingVolumeShape::Box:	return GAUSSIANS_CLIPPING_SHAPE_BOX;
	case EGaussianSplatClippingVolumeShape::Sphere: return GAUSSIANS_CLIPPING_SHAPE_SPHERE;
	default:
		checkNoEntry(); break;
	}
	return -1;
}


void FGSViewExtension::GSCullingPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& InView,
	const TSet<IGaussianSplatInterface*>& Proxies,
	const TArray<FGSClippingVolumeProxy>& CullingVolumes,
	bool bForceEnable,
	FRDGBufferRef LodBudget
)
{
	check(InView.bIsViewInfo);
	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(InView);

	RDG_EVENT_SCOPE_STAT(GraphBuilder, GaussiansCulling, "Gaussians: Culling");

	{
		FGSLodBudgetCS::FParameters* BudgetParameters = GraphBuilder.AllocParameters<FGSLodBudgetCS::FParameters>();
		BudgetParameters->RWLodBudget = GraphBuilder.CreateUAV(LodBudget, PF_R32_UINT);
		BudgetParameters->LodBudgetOn = CVarAtheneaLodBudget.GetValueOnRenderThread() != 0 ? 1u : 0u;
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("GSLodBudget"),
			TShaderMapRef<FGSLodBudgetCS>(GetGlobalShaderMap(InView.GetShaderPlatform())), BudgetParameters, FIntVector(1, 1, 1));
	}

	TArray<const FGSClippingVolumeProxy*> RelevantCullingVolumes;
	RelevantCullingVolumes.Reserve(CullingVolumes.Num());

	for (IGaussianSplatInterface* Proxy : Proxies)
	{
		// Build list of culling volumes for this proxy
		// First determine which culling volumes to apply
		RelevantCullingVolumes.Empty(CullingVolumes.Num());
		for (const auto& Volume : CullingVolumes)
		{
			if (Proxy->HasAnyClippingTags(Volume.Tags, Volume.bExactMatchOnly))
			{
				RelevantCullingVolumes.Add(&Volume);
			}
		}

		// Check if culling required
		if (!Proxy->RequiresNewCullingPass() && !bForceEnable)
			continue;

		const uint32 NumCullingVolumes = RelevantCullingVolumes.Num();
		FRDGBufferRef CullingVolumesBuffer = NumCullingVolumes == 0
			? GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(FGaussians_ClippingVolume))
			: GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(FGaussians_ClippingVolume), NumCullingVolumes),
				TEXT("Gaussians.ClippingVolumes")
			);

		if (NumCullingVolumes > 0)
		{
			FGaussians_ClippingVolume* ClippingVolumeShaderData = GraphBuilder.AllocPODArray<FGaussians_ClippingVolume>(NumCullingVolumes);

			auto WritePtr = ClippingVolumeShaderData;
			for (const auto& Volume : RelevantCullingVolumes)
			{
				WritePtr->Origin = static_cast<FVector3f>(Volume->OrientedBounds.Center);
				WritePtr->PackModeAndShape(ConvertClippingMode(Volume->Mode), ConvertClippingShape(Volume->Shape));
				WritePtr->AxisX_HalfExtentX = FVector4f{ static_cast<FVector3f>(Volume->OrientedBounds.AxisX), static_cast<float>(Volume->OrientedBounds.ExtentX) };
				WritePtr->AxisY_HalfExtentY = FVector4f{ static_cast<FVector3f>(Volume->OrientedBounds.AxisY), static_cast<float>(Volume->OrientedBounds.ExtentY) };
				WritePtr->AxisZ_HalfExtentZ = FVector4f{ static_cast<FVector3f>(Volume->OrientedBounds.AxisZ), static_cast<float>(Volume->OrientedBounds.ExtentZ) };

				WritePtr++;
			}

			// Safe to no copy since initial data was allocated via graph builder
			GraphBuilder.QueueBufferUpload(CullingVolumesBuffer, ClippingVolumeShaderData, NumCullingVolumes * sizeof(FGaussians_ClippingVolume), ERDGInitialDataFlags::NoCopy);
		}

		FRHIUnorderedAccessView* IndexBuffer = Proxy->GetIndexBufferUAV();
		FRHIUnorderedAccessView* CountBuffer = Proxy->GetIndexCountBufferUAV();

		// Clear counter
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("GSplatClearIndexCounter"),
			ERDGPassFlags::NeverCull,
			[CountBuffer](FRHICommandListImmediate& RHICmdList)
			{
				FUintVector4 Values{ 0, 0, 0, 0 };
				RHICmdList.ClearUAVUint(CountBuffer, Values);
			}
		);

		{
			TUniformBufferRef<FGaussianSplatShaderResources> ResourceUniformBuffer = TUniformBufferRef<FGaussianSplatShaderResources>{ Proxy->GetResource_RenderThread()->GetUniformBuffer() };
			TUniformBufferRef<FPrimitiveUniformShaderParameters> PrimitiveUniformBuffer = TUniformBufferRef<FPrimitiveUniformShaderParameters>{ Proxy->GetProxyUniformBuffer() };

			FGSplatCullingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGSplatCullingCS::FParameters>();
			PassParameters->View = ViewInfo.ViewUniformBuffer;
			PassParameters->GSResource = ResourceUniformBuffer;
			PassParameters->Primitive = PrimitiveUniformBuffer;

			PassParameters->SplatScaleMultiplier = Proxy->GetScaleMultiplier();
			const FGSLodCut Cut = GSLodCutOf(*Proxy, ViewInfo);
			PassParameters->LodEyeLocal = Cut.EyeLocal;
			PassParameters->LodPixelsPerUnit = Cut.PixelsPerUnit;
			PassParameters->LodThreshold = Cut.Threshold;
			PassParameters->LodBudget = GraphBuilder.CreateSRV(LodBudget, PF_R32_UINT);
			PassParameters->LodDissolve = Cut.Dissolve;
			PassParameters->LodThin = Cut.Thin;
			PassParameters->LodThinLeast = Cut.ThinLeast;
			PassParameters->LodMorph = Cut.Morph;
			PassParameters->LodFrustumOn = Cut.bFrustum ? 1u : 0u;
			for (int32 k = 0; k < 4; k++)
			{
				PassParameters->LodFrustum[k] = Cut.Frustum[k];
			}

			// The finest level's groups judged once, for the splats to read.
			// The groups' runs written instead (below): nothing reads the state then.
			const bool bGroupRuns = CVarAtheneaLodGroupRuns.GetValueOnRenderThread() != 0 && NumCullingVolumes == 0
				&& Cut.Threshold > 0.0f && Proxy->GetResource_RenderThread()->GetLodGroupRuns() > 0;
			const uint32 FinestGroups = CVarAtheneaLodGroupState.GetValueOnRenderThread() != 0 && !bGroupRuns
				? Proxy->GetResource_RenderThread()->GetLodFinestGroups() : 0u;
			FRDGBufferRef GroupState = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), FMath::Max(FinestGroups, 1u)), TEXT("Gaussians.LodGroupState"));
			if (FinestGroups > 0 && Cut.Threshold > 0.0f)
			{
				FGSLodStateCS::FParameters* State = GraphBuilder.AllocParameters<FGSLodStateCS::FParameters>();
				State->View = ViewInfo.ViewUniformBuffer;
				State->GSResource = ResourceUniformBuffer;
				State->Primitive = PrimitiveUniformBuffer;
				State->LodEyeLocal = Cut.EyeLocal;
				State->LodPixelsPerUnit = Cut.PixelsPerUnit;
				State->LodThreshold = Cut.Threshold;
				State->LodBudget = GraphBuilder.CreateSRV(LodBudget, PF_R32_UINT);
				State->LodDissolve = Cut.Dissolve;
				State->LodThin = Cut.Thin;
				State->LodThinLeast = Cut.ThinLeast;
				State->LodFrustumOn = Cut.bFrustum ? 1u : 0u;
				for (int32 k = 0; k < 4; k++)
				{
					State->LodFrustum[k] = Cut.Frustum[k];
				}
				State->LodFinestGroups = FinestGroups;
				State->RWLodGroupState = GraphBuilder.CreateUAV(GroupState, PF_R32_UINT);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("GSLodState"),
					TShaderMapRef<FGSLodStateCS>(GetGlobalShaderMap(InView.GetShaderPlatform())), State,
					FComputeShaderUtils::GetGroupCountWrapped(FinestGroups, 128));
				PassParameters->LodGroupStateOn = 1u;
			}
			else
			{
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GroupState, PF_R32_UINT), 0u);
				PassParameters->LodGroupStateOn = 0u;
			}
			PassParameters->LodGroupState = GraphBuilder.CreateSRV(GroupState, PF_R32_UINT);

			PassParameters->NumClippingVolumes = NumCullingVolumes;
			PassParameters->ClippingVolumes = GraphBuilder.CreateSRV(CullingVolumesBuffer);

			PassParameters->RWSplatIndexBufferAllocator = CountBuffer;
			PassParameters->RWSplatIndices = IndexBuffer;

			const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(InView.GetShaderPlatform());
			if (bGroupRuns)
			{
				const uint32 Groups = Proxy->GetResource_RenderThread()->GetSplatCount() - Proxy->GetResource_RenderThread()->GetLodSplatsEnd();
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("GSplatCullingGroups"),
					TShaderMapRef<FGSplatCullingGroupsCS>(ShaderMap),
					PassParameters,
					FComputeShaderUtils::GetGroupCountWrapped(Groups, 64));
			}
			else
			{
				FIntVector DispatchSize = FComputeShaderUtils::GetGroupCountWrapped(Proxy->GetResource_RenderThread()->GetSplatCount(), FGSplatCullingCS::GetThreadGroupSize1D());
				TShaderMapRef<FGSplatCullingCS> ComputeShader(ShaderMap);
				FComputeShaderUtils::AddPass<FGSplatCullingCS>(
					GraphBuilder,
					RDG_EVENT_NAME("GSplatCulling"),
					ComputeShader,
					PassParameters,
					DispatchSize
				);
			}
		}

		Proxy->MarkIndexBufferUpdated();
	}
}
