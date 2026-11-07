#include "GaussianSplatClippingVolume.h"

#include "GSClippingVolumeRebuildService.h"
#include "GSWorldSubsystem.h"
//#include "Builders/CubeBuilder.h"
//#include "Builders/TetrahedronBuilder.h"
#include "Components/BrushComponent.h"
#include "Engine/World.h"


AGaussianSplatClippingVolume::AGaussianSplatClippingVolume()
{
	ClippingTags.AddTag(FGameplayTag::RequestGameplayTag("StarLightGS.ClippingVolumes.Default", false));

	bColored = true;
	BrushColor.R = 0;
	BrushColor.G = 128;
	BrushColor.B = 128;
	BrushColor.A = 255;

	GetBrushComponent()->SetMobility(EComponentMobility::Movable);

	PreviousScale = GetActorRelativeScale3D();
}


void AGaussianSplatClippingVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (UWorld* World = GetWorld())
	{
		if (UGSWorldSubsystem* WorldSubsystem = World->GetSubsystem<UGSWorldSubsystem>())
		{
			WorldSubsystem->AddClippingVolume(this);
		}
	}
}

void AGaussianSplatClippingVolume::BeginDestroy()
{
	Super::BeginDestroy();

	if (UWorld* World = GetWorld())
	{
		if (UGSWorldSubsystem* WorldSubsystem = World->GetSubsystem<UGSWorldSubsystem>())
		{
			WorldSubsystem->RemoveClippingVolume(this);
		}
	}
}

#if WITH_EDITOR
void AGaussianSplatClippingVolume::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	MarkClippingStateDirty();
}

void AGaussianSplatClippingVolume::EditorApplyScale(const FVector& DeltaScale, const FVector* PivotLocation,
	bool bAltDown, bool bShiftDown, bool bCtrlDown)
{
	Super::EditorApplyScale(DeltaScale, PivotLocation, bAltDown, bShiftDown, bCtrlDown);

	if (Shape == EGaussianSplatClippingVolumeShape::Sphere)
	{
		ApplyUniformScale(DeltaScale);
	}
	PreviousScale = GetActorRelativeScale3D();
}

void AGaussianSplatClippingVolume::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();

	static const FName Name_RelativeScale3D = USceneComponent::GetRelativeScale3DPropertyName();
	if (PropertyName == Name_RelativeScale3D)
	{
		if (Shape == EGaussianSplatClippingVolumeShape::Sphere)
		{
			FVector DeltaScale = GetActorRelativeScale3D() - PreviousScale;
			ApplyUniformScale(DeltaScale);
		}

		PreviousScale = GetActorRelativeScale3D();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(AGaussianSplatClippingVolume, Shape))
	{
		FGSClippingVolumeRebuildService::RequestRebuild(this);

		if (Shape == EGaussianSplatClippingVolumeShape::Sphere)
		{
			ApplyUniformScale(FVector{ UE_DOUBLE_SMALL_NUMBER });
		}

		GetOnVolumeShapeChangedDelegate().Broadcast(*this);
	}

	MarkClippingStateDirty();

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void AGaussianSplatClippingVolume::ApplyUniformScale(const FVector& DeltaScale)
{
	// Work out axis that changed
	double Max = DeltaScale.GetAbsMax();
	int32 Axis = FMath::Abs(DeltaScale.X) == Max ? 0 : (FMath::Abs(DeltaScale.Y) == Max ? 1 : 2);

	double NewScale = GetActorRelativeScale3D()[Axis];

	SetActorRelativeScale3D(FVector{ NewScale });
}

void AGaussianSplatClippingVolume::PopulateClippingVolumeProxy(FGSClippingVolumeProxy& OutProxy) const
{
	OutProxy.Mode = Mode;
	OutProxy.Shape = Shape;
	OutProxy.Priority = Priority;

	// Build oriented bounds
	OutProxy.OrientedBounds.Center = GetActorLocation();

	auto Scale = GetActorRelativeScale3D();
	// Convert from scale to extent by multiplying by 100
	OutProxy.OrientedBounds.ExtentX = 100.0f * Scale.X;
	OutProxy.OrientedBounds.ExtentY = 100.0f * Scale.Y;
	OutProxy.OrientedBounds.ExtentZ = 100.0f * Scale.Z;

	OutProxy.OrientedBounds.AxisX = GetActorForwardVector();
	OutProxy.OrientedBounds.AxisY = GetActorRightVector();
	OutProxy.OrientedBounds.AxisZ = GetActorUpVector();

	OutProxy.Tags = ClippingTags;
	OutProxy.bExactMatchOnly = bClippingTagsExactMatchOnly;
}

void AGaussianSplatClippingVolume::MarkClippingStateDirty() const
{
	if (UWorld* World = GetWorld())
	{
		if (UGSWorldSubsystem* WorldSubsystem = World->GetSubsystem<UGSWorldSubsystem>())
		{
			WorldSubsystem->MarkClippingDirtyFlags();
		}
	}
}
