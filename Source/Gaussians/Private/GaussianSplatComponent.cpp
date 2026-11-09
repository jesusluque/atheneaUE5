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
}
