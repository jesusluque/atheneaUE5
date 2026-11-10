#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/PrimitiveComponent.h"

#include "GSplatAsset.h"

#include "GaussianSplatComponent.generated.h"


UCLASS(Blueprintable, ClassGroup = (Rendering), hidecategories = (Collision, Physics, Navigation), meta=(BlueprintSpawnableComponent))
class GAUSSIANS_API UGaussianSplatComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:

	//  TOD O: Need custom setter to make it BP-RW
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gaussian Splats")
	TObjectPtr<UGSplatAsset> GaussianSplatAsset = nullptr;


	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gaussian Splats")
	float SplatScaleMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gaussian Splats", AdvancedDisplay)
	UMaterialInterface* ShadowMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gaussian Splats|Clipping", DisplayName="Tags")
	FGameplayTagContainer ClippingTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gaussian Splats|Clipping", DisplayName="Exact Match Only")
	bool bClippingTagsExactMatchOnly = false;

	// athenea: A SEQUENCE -- a medium's frames from athenea vdb2splat, or any
	// clouds -- drawn one a frame at SequenceFramesPerSecond, the component's
	// asset switched as the time passes (in the editor too). Empty: the asset
	// above alone.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splats|Sequence")
	TArray<TObjectPtr<UGSplatAsset>> SequenceFrames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splats|Sequence", meta = (ClampMin = "0.01"))
	float SequenceFramesPerSecond = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splats|Sequence")
	bool bSequenceLoop = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splats|Sequence")
	bool bSequencePlaying = true;

	// Seconds into the sequence. Keyable in Sequencer (with bSequencePlaying
	// off, the track decides the frame, as Movie Render Queue renders it).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Gaussian Splats|Sequence")
	float SequenceTime = 0.0f;

	// The sequence's frame at a time (seconds), drawn now.
	UFUNCTION(BlueprintCallable, Category = "Gaussian Splats|Sequence")
	void SetSequenceTime(float Seconds);

	// Draws another cloud: the delegates and the render state follow it.
	UFUNCTION(BlueprintCallable, Category = "Gaussian Splats")
	void SetGaussianSplatAsset(UGSplatAsset* Asset);

public:

	UGaussianSplatComponent();

	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditComponentMove(bool bFinished) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	//~ Begin UPrimitiveComponent Interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	//~ End UPrimitiveComponent Interface

	UFUNCTION(Category="Gaussian Splats", CallInEditor, DisplayName="Export Clipped Splat")
	void ExportClippedSplat();

private:
	UFUNCTION()
	void OnAssetUpdated();

private:
	TWeakObjectPtr<UGSplatAsset> PrevGaussianSplatAsset = nullptr;
	int32 SequenceShown = INDEX_NONE;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UTexture2D* SpriteTexture;
#endif
};


UCLASS(Blueprintable)
class GAUSSIANS_API AGaussianSplatActor : public AActor
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Gaussian Splats", meta=(AllowPrivateAccess=true))
	TObjectPtr<UGaussianSplatComponent> GaussianSplatComponent;
};
