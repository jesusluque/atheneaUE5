#include "GSWorldSubsystem.h"

#include "GaussianSplatClippingVolume.h"
#include "GSViewExtension.h"

#include "GaussianSplatComponent.h"
#include "GaussianSplatSceneProxy.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"


void UGSWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	GaussianRendererViewExtension = FSceneViewExtensions::NewExtension<FGSViewExtension>(this, this);
}

void UGSWorldSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UGSWorldSubsystem::AddClippingVolume(const AGaussianSplatClippingVolume* ClippingVolume)
{
	check(IsInGameThread() || IsInParallelGameThread());

	if (ClippingVolume && !ClippingVolumes.Contains(ClippingVolume))
	{
		ClippingVolumes.Add(ClippingVolume);
		bClippingVolumesDirty = true;
	}
}

void UGSWorldSubsystem::RemoveClippingVolume(const AGaussianSplatClippingVolume* ClippingVolume)
{
	check(IsInGameThread() || IsInParallelGameThread());

	if (ClippingVolume && ClippingVolumes.Contains(ClippingVolume))
	{
		ClippingVolumes.Remove(ClippingVolume);
		bClippingVolumesDirty = true;
	}
}

TArray<FGSClippingVolumeProxy> UGSWorldSubsystem::BuildClippingVolumeProxies()
{
	TArray<FGSClippingVolumeProxy> OutProxies;
	OutProxies.SetNum(ClippingVolumes.Num());

	for (int32 Index = 0; Index < ClippingVolumes.Num(); Index++)
	{
		if (!ClippingVolumes[Index] || !ClippingVolumes[Index]->bEnabled)
			continue;

		ClippingVolumes[Index]->PopulateClippingVolumeProxy(OutProxies[Index]);
	}

	return OutProxies;
}

bool UGSWorldSubsystem::GetForceClippingEvaluation() const
{
	return bClippingVolumesDirty;

}

void UGSWorldSubsystem::MarkClippingDirtyFlags()
{
	bClippingVolumesDirty = true;
}

void UGSWorldSubsystem::ClearClippingDirtyFlags()
{
	bClippingVolumesDirty = false;
}

void UGSWorldSubsystem::CleanClippingVolumesCollection()
{
	ClippingVolumes.RemoveAll([](const auto& Elem) { return Elem == nullptr; });
}

bool UGSWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::Editor || WorldType == EWorldType::PIE || WorldType == EWorldType::Type::EditorPreview || WorldType == EWorldType::Type::GamePreview;
}

void UGSWorldSubsystem::ExportClippedSplat(UGaussianSplatComponent* Component)
{
	FGaussianSplatSceneProxy* SceneProxy = static_cast<FGaussianSplatSceneProxy*>(Component->GetSceneProxy());

	TArray<FVector3f> Positions;
	TArray<FVector3f> Scales;
	TArray<FQuat4f>   Orientations;
	TArray<FVector4f> SHAndOpacities;
	TArray<FVector3f> Normals;
	TArray<FVector2f> MetallicRoughness;

	GaussianRendererViewExtension->CopySplatGPUToCPU(
		SceneProxy,
		Positions,
		Scales,
		Orientations,
		SHAndOpacities,
		Normals,
		MetallicRoughness
	);

	// Data is now ready to use to populate asset
	// TODO: I want this to work in the runtime module, not editor module, since this is related to the world...
	// TODO: In the future it would be better to trigger this from the editor module, then this can hand back off to the editor module for asset creation
	const FString AssetNameSuffix = Component->GetOwner()->GetName() + "_Exported";

	const FString BasePackageName = TEXT("/Game/Splats/");
	const FString PackagePath = FPackageName::GetLongPackagePath(BasePackageName);

	const FString AssetName = FPackageName::GetLongPackageAssetName(BasePackageName) + AssetNameSuffix;
	const FString PackageName = PackagePath + TEXT("/") + AssetName;

	UPackage* Package = CreatePackage(*PackageName);

	// Create the Texture
	UGSplatAsset* GSplatAsset = NewObject<UGSplatAsset>(Package, UGSplatAsset::StaticClass(), FName(*AssetName), EObjectFlags::RF_Public | EObjectFlags::RF_Standalone);
	GSplatAsset->AddToRoot();

	{
		GSplatAsset->Splats.SplatCount = Positions.Num();

		GSplatAsset->Splats.Positions = MoveTemp(Positions);
		GSplatAsset->Splats.Scales = MoveTemp(Scales);
		GSplatAsset->Splats.Orientations = MoveTemp(Orientations);
		GSplatAsset->Splats.ZeroOrderHarmonicsCoefficientsAndOpacities = MoveTemp(SHAndOpacities);
		if (Normals.Num())
		{
			GSplatAsset->Splats.bHasNormals = true;
			GSplatAsset->Splats.Normals = MoveTemp(Normals);
		}
		if (MetallicRoughness.Num())
		{
			GSplatAsset->Splats.bHasMetallicRoughness = true;
			GSplatAsset->Splats.MetallicRoughness = MoveTemp(MetallicRoughness);
		}

		GSplatAsset->Splats.RecalculateBounds();
	}

	(void)Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(GSplatAsset);
}
