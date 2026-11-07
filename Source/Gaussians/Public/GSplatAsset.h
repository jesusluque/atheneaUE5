#pragma once

#include "GaussianData.h"
#include "CoreMinimal.h"

#include "GSplatAsset.generated.h"


class FGaussianSplatResource;


// Manages the lifetime of the GPU resources of a GSplat asset
// Owned by the GSplat asset itself, and can only be created by the GSplat asset
class FGaussianSplatResourceWrapper
{
public:
	struct FKey
	{
		// Using key idiom to still allow wrappers to be constructed with calls to MakeShared
	private:
		friend class UGSplatAsset;
		FKey() = default;
	};

	FGaussianSplatResourceWrapper(FKey&&, TPimplPtr<FGaussianSplatResource>&& InResource);
	~FGaussianSplatResourceWrapper();

	FGaussianSplatResource* GetResource() const { return Resource.Get(); }

private:
	TPimplPtr<FGaussianSplatResource> Resource;
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGSplatAssetUpdate);

/**
 *  GSplat asset
 */
UCLASS()
class GAUSSIANS_API UGSplatAsset : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Gaussians", meta=(ShowOnlyInnerProperties))
	FGaussianSplatData Splats;

public:
	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
#endif

	//~ End UObject Interface


	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Gaussians")
	void RecalculateBounds();

	//--- GPU Resource Management ---//

	// If the GPU resources have not been created yet,
	// it will allocate buffers to contain GPU copy of splat data.
	//
	// Note: Resource should only be dereferenced on render thread!
	// This function should always return a valid pointer, but its underlying
	// GPU resources may or may not exist.
	TSharedPtr<FGaussianSplatResourceWrapper> GetOrCreateResource();

	void DetachResource();

	// Will release and re-create GPU resources.
	// No-op if resources are not in use anywhere (they will be recreated once somebody starts to use them anyway)
	// This should be called if the CPU data has changed,
	// and the new data will be uploaded to the GPU.
	void RecreateResource() const;


	//--- Asset Actions ---//

	// Invoked when the underlying splat data has been modified - e.g. on reimport.
	// Used to tell components when to re-create their rendering resources
	FGSplatAssetUpdate OnAssetUpdate;

private:
	// The wrapper is given out externally, and manages the lifetime of the GPU resources
	// This allows the GPU resources to be released when not in use
	// and their lifetime to be sustained if this asset is unexpectedly released
	TWeakPtr<FGaussianSplatResourceWrapper> WeakWrapper;

public:
#if WITH_EDITORONLY_DATA

	UPROPERTY(VisibleAnywhere, Instanced, Category = ImportSettings)
	TObjectPtr<class UGSplatImportData> AssetImportData;
#endif
};
