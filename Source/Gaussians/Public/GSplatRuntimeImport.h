// Copyright (c) 2026 jesus luque.
//
// athenea's own files read without the editor (a packaged game loads them
// from disk, UGSplatRuntimeLibrary::LoadGaussianSplatFile), and the axes'
// swizzle every importer shares. The editor's importers call the same.

#pragma once

#include "CoreMinimal.h"

class UGSplatImportData;
struct FGaussianSplatData;
enum class EGSplatSwizzleAxis : uint8;

GAUSSIANS_API DECLARE_LOG_CATEGORY_EXTERN(LogGSplatImport, Log, All);

namespace GSplatImporter
{
	// athenea: a .athc, versions 1 to 3, packed (GSplatAthcImporter.cpp).
	GAUSSIANS_API bool IsAthcFile(const FString& Filename);
	GAUSSIANS_API bool ImportAthc(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats);

	// athenea: a .athz paged cloud ('ATHZ'), not a web scene (GSplatAthzImporter.cpp).
	GAUSSIANS_API bool ImportAthzCloud(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats);
}

// The file's axes taken into UE's.
GAUSSIANS_API FVector3f SwizzleVector(float InX, float InY, float InZ, const TArray<EGSplatSwizzleAxis>& Swizzle);
GAUSSIANS_API FVector3f SwizzleVector(const FVector3f& V, const TArray<EGSplatSwizzleAxis>& Swizzle);
GAUSSIANS_API FVector3f SwizzleVectorAbs(float InX, float InY, float InZ, const TArray<EGSplatSwizzleAxis>& Swizzle);
GAUSSIANS_API FVector4f SwizzleQuaternion(float InX, float InY, float InZ, float InW, const TArray<EGSplatSwizzleAxis>& Swizzle);
