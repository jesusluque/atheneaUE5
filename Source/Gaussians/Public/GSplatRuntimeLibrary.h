// Copyright (c) 2026 jesus luque.
//
// athenea's files loaded at runtime, from disk, in a packaged game as in the
// editor: a transient asset made from the file, for a splat component to draw.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GSplatRuntimeLibrary.generated.h"

class UGSplatAsset;

UCLASS()
class GAUSSIANS_API UGSplatRuntimeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// A .athc (versions 1 to 3, its levels of detail included) or a paged
	// .athz cloud read from Path into a new transient asset, or null where the
	// file is not one or does not read (the log says why). Its GPU resource is
	// made when a component first draws it.
	UFUNCTION(BlueprintCallable, Category = "Gaussian Splats")
	static UGSplatAsset* LoadGaussianSplatFile(const FString& Path);
};
