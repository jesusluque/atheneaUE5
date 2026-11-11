#include "GaussianSplatComponent.h"

#include "GameplayTagsManager.h"
#include "GaussianSplatSceneProxy.h"
#include "GSWorldSubsystem.h"
#include "Components/BillboardComponent.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/Material.h"

UGaussianSplatComponent::UGaussianSplatComponent()
{
	static ConstructorHelpers::FObjectFinder<UMaterial> MaterialFinder(TEXT("/StarLightGS/Materials/M_SplatShadowMaterial.M_SplatShadowMaterial"));
	checkf(MaterialFinder.Succeeded(), TEXT("Could not find asset - plugin is not installed correctly."));
	ShadowMaterial = MaterialFinder.Object;

	ClippingTags.AddTag(UGameplayTagsManager::Get().RequestGameplayTag("StarLightGS.ClippingVolumes.Default", false));

	// Splats cast into UE's shadow maps by default (the proxy's shadow pass,
	// r.Gaussians.Shadows); a primitive component casts none unless told.
	CastShadow = true;
	bCastDynamicShadow = true;

	// A sequence plays in the editor's viewports as in the game.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bTickInEditor = true;

#if WITH_EDITORONLY_DATA
	bVisualizeComponent = true;

	static ConstructorHelpers::FObjectFinder<UTexture2D> SpriteFinder(TEXT("/StarLightGS/Splidget.Splidget"));
	checkf(SpriteFinder.Succeeded(), TEXT("Could not find asset - plugin is not installed correctly."));
	SpriteTexture = SpriteFinder.Object;

#endif
}

#if WITH_EDITOR
void UGaussianSplatComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Stops from crashing when adding splat as component in blueprint
	// TODO: Splat doesn't render in blueprint viewport though
	if (HasAnyFlags(RF_ClassDefaultObject) || !IsValid(this))
	{
		return;
	}

	bool bCullingDirty = false;

	FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UGaussianSplatComponent, LightGroups)
		|| PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UGaussianSplatComponent, LightGroups))
	{
		PushLightWeights();
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UGaussianSplatComponent, GaussianSplatAsset))
	{
		bCullingDirty = true;

		if (auto Prev = PrevGaussianSplatAsset.Pin())
		{
			Prev->OnAssetUpdate.RemoveDynamic(this, &UGaussianSplatComponent::OnAssetUpdated);
		}
		PrevGaussianSplatAsset = GaussianSplatAsset;
		if (GaussianSplatAsset)
		{
			GaussianSplatAsset->OnAssetUpdate.AddDynamic(this, &UGaussianSplatComponent::OnAssetUpdated);
		}
	}

	bCullingDirty |= PropertyName == GET_MEMBER_NAME_CHECKED(UGaussianSplatComponent, SplatScaleMultiplier);
	bCullingDirty |= PropertyName == GET_MEMBER_NAME_CHECKED(UGaussianSplatComponent, MatteIsolate);
	bCullingDirty |= PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UGaussianSplatComponent, Edit);
	bCullingDirty |= PropertyName == GET_MEMBER_NAME_CHECKED(UGaussianSplatComponent, ClippingTags);
	bCullingDirty |= PropertyName == GET_MEMBER_NAME_CHECKED(UGaussianSplatComponent, bClippingTagsExactMatchOnly);

	if (bCullingDirty)
	{
		MarkRenderStateDirty();
	}

	if (SpriteTexture && SpriteComponent)
	{
		SpriteComponent->SetSprite(SpriteTexture);
	}


}
#endif

void UGaussianSplatComponent::OnRegister()
{
	Super::OnRegister();

	PrevGaussianSplatAsset = GaussianSplatAsset;
	if (GaussianSplatAsset)
	{
		GaussianSplatAsset->OnAssetUpdate.AddDynamic(this, &UGaussianSplatComponent::OnAssetUpdated);
	}

	#if WITH_EDITOR
	if (SpriteTexture && SpriteComponent)
	{
		SpriteComponent->SetSprite(SpriteTexture);
	}
	#endif
}

void UGaussianSplatComponent::OnUnregister()
{
	Super::OnUnregister();

	if (GaussianSplatAsset)
	{
		GaussianSplatAsset->OnAssetUpdate.RemoveDynamic(this, &UGaussianSplatComponent::OnAssetUpdated);
	}
}

#if WITH_EDITOR
void UGaussianSplatComponent::PostEditComponentMove(bool bFinished)
{
	Super::PostEditComponentMove(bFinished);

	MarkRenderStateDirty();
}
#endif


FPrimitiveSceneProxy* UGaussianSplatComponent::CreateSceneProxy()
{
	return new FGaussianSplatSceneProxy(this);
}

FBoxSphereBounds UGaussianSplatComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (GaussianSplatAsset)
	{
		FBoxSphereBounds LocalBox = GaussianSplatAsset->Splats.GetBounds();

		// Transform to world space
		FBoxSphereBounds WorldBox = LocalBox.TransformBy(LocalToWorld);

		return FBoxSphereBounds(WorldBox);
	}

	return FBoxSphereBounds{ };
}

void UGaussianSplatComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	Super::GetUsedMaterials(OutMaterials, bGetDebugMaterials);
	
	//register material used for splats if any, this will propagate to proxy and permute shader with custom vertex factory.
	if (ShadowMaterial)
		OutMaterials.Add(ShadowMaterial);
}

void UGaussianSplatComponent::ExportClippedSplat()
{
	if (UGSWorldSubsystem* WorldSubsystem = GetWorld()->GetSubsystem<UGSWorldSubsystem>())
	{
		WorldSubsystem->ExportClippedSplat(this);
	}
}

void UGaussianSplatComponent::OnAssetUpdated()
{
	MarkRenderStateDirty();
}


AGaussianSplatActor::AGaussianSplatActor(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	GaussianSplatComponent = CreateDefaultSubobject<UGaussianSplatComponent>(TEXT("GaussianSplatComponent"));
	RootComponent = GaussianSplatComponent;
}

void UGaussianSplatComponent::SetGaussianSplatAsset(UGSplatAsset* Asset)
{
	if (Asset == GaussianSplatAsset)
	{
		return;
	}
	if (auto Prev = PrevGaussianSplatAsset.Pin())
	{
		Prev->OnAssetUpdate.RemoveDynamic(this, &UGaussianSplatComponent::OnAssetUpdated);
	}
	GaussianSplatAsset = Asset;
	PrevGaussianSplatAsset = GaussianSplatAsset;
	if (GaussianSplatAsset)
	{
		GaussianSplatAsset->OnAssetUpdate.AddDynamic(this, &UGaussianSplatComponent::OnAssetUpdated);
	}
	UpdateBounds();
	MarkRenderStateDirty();
}

TArray<FVector4f> UGaussianSplatComponent::GetLightWeights() const
{
	TArray<FVector4f> Weights;
	if (!GaussianSplatAsset)
	{
		return Weights;
	}
	const FGaussianSplatData& Splats = GaussianSplatAsset->Splats;
	for (int32 k = 0; k < Splats.LightLayerNames.Num() && k < GSMaxLightLayers; ++k)
	{
		FLinearColor W = Splats.LightLayerDefaults.IsValidIndex(k) ? Splats.LightLayerDefaults[k] : FLinearColor::Black;
		for (const FGSLightGroup& Group : LightGroups)
		{
			// A group's emission layer (its indirect light) follows the group.
			if (Group.Name == Splats.LightLayerNames[k] || TEXT("emission:") + Group.Name == Splats.LightLayerNames[k])
			{
				W = Group.bOn ? Group.Color * Group.Intensity : FLinearColor::Black;
				break;
			}
		}
		Weights.Add(FVector4f(W.R, W.G, W.B, 0.0f));
	}
	return Weights;
}

void UGaussianSplatComponent::PushLightWeights()
{
	if (FGaussianSplatSceneProxy* Proxy = static_cast<FGaussianSplatSceneProxy*>(SceneProxy))
	{
		ENQUEUE_RENDER_COMMAND(GSLightWeights)([Proxy, Weights = GetLightWeights()](FRHICommandListImmediate&)
		{
			Proxy->SetLightWeights_RenderThread(Weights);
		});
	}
}

void UGaussianSplatComponent::SetLightGroup(const FString& Name, FLinearColor Color, float Intensity, bool bOn)
{
	FGSLightGroup* Found = LightGroups.FindByPredicate([&](const FGSLightGroup& G) { return G.Name == Name; });
	if (!Found)
	{
		Found = &LightGroups.AddDefaulted_GetRef();
		Found->Name = Name;
	}
	Found->Color = Color;
	Found->Intensity = FMath::Max(Intensity, 0.0f);
	Found->bOn = bOn;
	PushLightWeights();
}

void UGaussianSplatComponent::FillLightGroupsFromAsset()
{
	if (!GaussianSplatAsset)
	{
		return;
	}
	const FGaussianSplatData& Splats = GaussianSplatAsset->Splats;
	LightGroups.Reset();
	for (int32 k = 0; k < Splats.LightLayerNames.Num(); ++k)
	{
		if (Splats.LightLayerNames[k].StartsWith(TEXT("emission:")))
		{
			continue;
		}
		FGSLightGroup& Group = LightGroups.AddDefaulted_GetRef();
		Group.Name = Splats.LightLayerNames[k];
		const FLinearColor D = Splats.LightLayerDefaults.IsValidIndex(k) ? Splats.LightLayerDefaults[k] : FLinearColor::Black;
		const float Level = FMath::Max3(D.R, D.G, D.B);
		Group.bOn = Level > 0.0f;
		Group.Intensity = Level > 0.0f ? Level : 1.0f;
		Group.Color = Level > 0.0f ? FLinearColor(D.R / Level, D.G / Level, D.B / Level, 1.0f) : FLinearColor::White;
	}
	PushLightWeights();
}

void UGaussianSplatComponent::SetSequenceTime(float Seconds)
{
	SequenceTime = Seconds;
	const int32 Count = SequenceFrames.Num();
	if (Count == 0)
	{
		return;
	}
	int32 Frame = FMath::FloorToInt32(SequenceTime * FMath::Max(SequenceFramesPerSecond, 0.01f));
	Frame = bSequenceLoop ? ((Frame % Count) + Count) % Count : FMath::Clamp(Frame, 0, Count - 1);
	if (Frame != SequenceShown || SequenceFrames[Frame] != GaussianSplatAsset)
	{
		SequenceShown = Frame;
		if (SequenceFrames[Frame])
		{
			SetGaussianSplatAsset(SequenceFrames[Frame]);
		}
	}
}

void UGaussianSplatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (SequenceFrames.Num() > 0)
	{
		SetSequenceTime(bSequencePlaying ? SequenceTime + DeltaTime : SequenceTime);
	}
	if (GaussianSplatAsset && GaussianSplatAsset->Splats.IsSkinned())
	{
		SetSkinTime(bSkinPlaying ? SkinTime + DeltaTime * SkinRate : SkinTime);
	}
}

void UGaussianSplatComponent::SetSkinTime(float Seconds)
{
	const float Duration = GaussianSplatAsset && GaussianSplatAsset->Splats.SkinTimes.Num() > 0 ? GaussianSplatAsset->Splats.SkinTimes.Last() : 0.0f;
	SkinTime = Duration > 0.0f ? (bSkinLoop ? FMath::Fmod(FMath::Fmod(Seconds, Duration) + Duration, Duration) : FMath::Clamp(Seconds, 0.0f, Duration)) : 0.0f;
	if (FGaussianSplatSceneProxy* Proxy = static_cast<FGaussianSplatSceneProxy*>(SceneProxy))
	{
		ENQUEUE_RENDER_COMMAND(GSSkinTime)([Proxy, Seconds = SkinTime](FRHICommandListImmediate&)
		{
			Proxy->SetSkinSeconds_RenderThread(Seconds);
		});
	}
}
