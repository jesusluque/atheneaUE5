#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/PrimitiveComponent.h"

#include "GSplatAsset.h"

#include "GaussianSplatComponent.generated.h"


// athenea: a group of lamps baked into a cloud's light layers.
USTRUCT(BlueprintType)
struct FGSLightGroup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lamps")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lamps")
	FLinearColor Color = FLinearColor::White;

	// The group's radiance (athenea's emissionLuminance): what its layer, baked
	// at a radiance of one, is multiplied by.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lamps", meta = (ClampMin = "0"))
	float Intensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lamps")
	bool bOn = true;
};

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

	// athenea: A CLOUD CARRIED BY A SKELETON (an asset from athenea mesh2splat
	// --skinned): its poses played at SkinRate, looping, or set by SkinTime
	// (keyable in Sequencer, with bSkinPlaying off).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splats|Skeleton")
	bool bSkinPlaying = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splats|Skeleton")
	bool bSkinLoop = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splats|Skeleton")
	float SkinRate = 1.0f;

	// Seconds into the cloud's poses.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Gaussian Splats|Skeleton")
	float SkinTime = 0.0f;

	UFUNCTION(BlueprintCallable, Category = "Gaussian Splats|Skeleton")
	void SetSkinTime(float Seconds);

	// athenea: CRYPTOMATTE -- draw only the gaussians that came from this prim
	// (its path in the asset's CryptoNames, or its last name). Empty: all.
	// r.Gaussians.Athenea.CryptoView 1 draws each gaussian in its id's colour.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splats|Matte")
	FString MatteIsolate;

	// athenea: a splat edit over this cloud (keep, remove or grade what a box
	// or a sphere holds; athenea's SplatEdit). Active, it replaces the one the
	// asset's stage carried.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splats|Edit")
	FGSSplatEdit Edit;

	// athenea: THE LAMP GROUPS a cloud's light layers are drawn at, matched to
	// the asset's groups by name (an asset's group not listed here is drawn at
	// the level its stage left it). A group's weight is Color x Intensity, 0
	// where off; changed at run time without re-creating the render state.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splats|Lamps")
	TArray<FGSLightGroup> LightGroups;

	// One group's level (Sequencer and Blueprints): added to LightGroups if missing.
	UFUNCTION(BlueprintCallable, Category = "Gaussian Splats|Lamps")
	void SetLightGroup(const FString& Name, FLinearColor Color, float Intensity, bool bOn);

	// The groups listed with the levels the asset's stage gives them.
	UFUNCTION(CallInEditor, Category = "Gaussian Splats|Lamps")
	void FillLightGroupsFromAsset();

	// Each of the asset's groups' weight (rgb), in its order.
	TArray<FVector4f> GetLightWeights() const;

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

	// The groups' weights handed to the proxy, without re-creating it.
	void PushLightWeights();

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
