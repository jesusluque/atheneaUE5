// Copyright (c) 2026 jesus luque.

#include "GSplatRuntimeLibrary.h"

#include "GSplatAsset.h"
#include "GSplatImportData.h"
#include "GSplatRuntimeImport.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

UGSplatAsset* UGSplatRuntimeLibrary::LoadGaussianSplatFile(const FString& Path)
{
	if (!FPaths::FileExists(Path))
	{
		UE_LOG(LogGSplatImport, Error, TEXT("%s: no such file."), *Path);
		return nullptr;
	}
	const FString Extension = FPaths::GetExtension(Path).ToLower();
	UGSplatAsset* Asset = NewObject<UGSplatAsset>(GetTransientPackage(), NAME_None, RF_Transient);
	UGSplatImportData* Data = NewObject<UGSplatImportData>(Asset);
	bool bRead = false;
	if (GSplatImporter::IsAthcFile(Path))
	{
		bRead = GSplatImporter::ImportAthc(Path, *Data, Asset->Splats);
	}
	else if (Extension == TEXT("athz"))
	{
		bRead = GSplatImporter::ImportAthzCloud(Path, *Data, Asset->Splats);
	}
	else
	{
		UE_LOG(LogGSplatImport, Error, TEXT("%s: a runtime load reads .athc and paged .athz clouds; import other formats in the editor."), *Path);
	}
	if (!bRead)
	{
		Asset->MarkAsGarbage();
		return nullptr;
	}
	UE_LOG(LogGSplatImport, Display, TEXT("%s: loaded at runtime, %u gaussians."), *Path, Asset->Splats.SplatCount);
	return Asset;
}
