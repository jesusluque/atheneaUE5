// Copyright (c) 2026 jesus luque.
//
// A .athz the web publishes: a zip of a scene.json, its clouds as .athc files
// (version 3, the web's) and its sky as an HDR image. Imported as the scene's
// clouds, one asset a file -- the first into the asset the import asked for,
// the others beside it, named after it and their object -- and the sky as a
// texture cube beside them, which a sky light takes (operations §6.1). The
// scene's units and up axis are athenea's (metres, y up), as a .athc's; a
// transform other than the identity, the camera and the sky's settings are
// logged, not applied: a level places the actors, a camera and a sky light.
//
// athenea's own .athz, the paged cloud whose header is 'ATHZ', is not a zip:
// IsAthzScene tells them apart by their first bytes.

#include "GSplatImporter.h"
#include "GSplatImportData.h"

#if WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "GSplatAsset.h"
#include "GaussianData.h"
#include "GaussiansEditorModule.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

bool GSplatImporter::IsAthzFile(const FString& Filename)
{
	return FPaths::GetExtension(Filename).ToLower() == TEXT("athz");
}

bool GSplatImporter::IsAthzScene(const FString& Filename)
{
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Filename));
	uint8 Magic[4] = { 0, 0, 0, 0 };
	if (!Reader || Reader->TotalSize() < 4)
	{
		return false;
	}
	Reader->Serialize(Magic, 4);
	return Magic[0] == 'P' && Magic[1] == 'K' && Magic[2] == 3 && Magic[3] == 4;
}

bool GSplatImporter::ImportAthzScene(const FString& Filename, UObject* InParent, FName InName, EObjectFlags Flags, UGSplatAsset& First)
{
	auto Fail = [&](const FString& Why)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: %s"), *Filename, *Why);
		return false;
	};
	TArray<uint8> Zip;
	TMap<FString, TArray<uint8>> Files;
	FString Why;
	if (!FFileHelper::LoadFileToArray(Zip, *Filename) || !ReadZip(Zip, Files, Why))
	{
		return Fail(Why.IsEmpty() ? FString(TEXT("cannot be read.")) : Why + TEXT("."));
	}
	Zip.Empty();
	const TArray<uint8>* SceneBytes = Files.Find(TEXT("scene.json"));
	if (!SceneBytes)
	{
		return Fail(TEXT("no scene.json in the bundle."));
	}
	TSharedPtr<FJsonObject> Scene;
	const FString SceneText = FString(SceneBytes->Num(), reinterpret_cast<const UTF8CHAR*>(SceneBytes->GetData()));
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SceneText), Scene) || !Scene.IsValid())
	{
		return Fail(TEXT("scene.json does not parse."));
	}
	FString Units = TEXT("m"), UpAxis = TEXT("Y");
	Scene->TryGetStringField(TEXT("units"), Units);
	Scene->TryGetStringField(TEXT("upAxis"), UpAxis);
	if (Units != TEXT("m") || UpAxis != TEXT("Y"))
	{
		UE_LOG(LogGSplatEditor, Warning, TEXT("%s: a scene in %s with %s up is read as athenea's metres, y up."), *Filename, *Units, *UpAxis);
	}

	// Each object's .athc files, in order.
	struct FCloud { FString Object; FString Path; };
	TArray<FCloud> Clouds;
	const TArray<TSharedPtr<FJsonValue>>* Objects = nullptr;
	if (Scene->TryGetArrayField(TEXT("objects"), Objects))
	{
		for (const TSharedPtr<FJsonValue>& V : *Objects)
		{
			const TSharedPtr<FJsonObject> Object = V->AsObject();
			FString Name = TEXT("cloud");
			Object->TryGetStringField(TEXT("name"), Name);
			const TSharedPtr<FJsonObject>* Transform = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Matrix = nullptr;
			if (Object->TryGetObjectField(TEXT("transform"), Transform) && (*Transform)->TryGetArrayField(TEXT("matrix"), Matrix))
			{
				for (int32 k = 0; k < Matrix->Num() && k < 16; k++)
				{
					if (FMath::Abs((*Matrix)[k]->AsNumber() - ((k % 5) == 0 ? 1.0 : 0.0)) > 1e-6)
					{
						UE_LOG(LogGSplatEditor, Warning, TEXT("%s: %s has a transform, which is not applied; place its actor instead."), *Filename, *Name);
						break;
					}
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* ObjectFiles = nullptr;
			if (Object->TryGetArrayField(TEXT("files"), ObjectFiles))
			{
				for (const TSharedPtr<FJsonValue>& F : *ObjectFiles)
				{
					FString Format, Path;
					F->AsObject()->TryGetStringField(TEXT("format"), Format);
					F->AsObject()->TryGetStringField(TEXT("path"), Path);
					if (Format == TEXT("athc"))
					{
						Clouds.Add({ Name, Path });
					}
					else
					{
						UE_LOG(LogGSplatEditor, Display, TEXT("%s: %s's %s (%s) is not read."), *Filename, *Name, *Path, *Format);
					}
				}
			}
		}
	}
	if (Clouds.Num() == 0)
	{
		return Fail(TEXT("its scene names no .athc cloud."));
	}

	// Each cloud through the .athc importer, from a file of its own bytes.
	const FString Temp = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("athenea"), TEXT("athz"));
	IFileManager::Get().MakeDirectory(*Temp, true);
	const FString Folder = FPackageName::GetLongPackagePath(InParent->GetOutermost()->GetName());
	int32 Imported = 0;
	for (int32 c = 0; c < Clouds.Num(); c++)
	{
		const TArray<uint8>* Bytes = Files.Find(Clouds[c].Path);
		if (!Bytes)
		{
			UE_LOG(LogGSplatEditor, Error, TEXT("%s: %s is not in the bundle."), *Filename, *Clouds[c].Path);
			continue;
		}
		const FString Path = FPaths::Combine(Temp, FPaths::GetCleanFilename(Clouds[c].Path));
		if (!FFileHelper::SaveArrayToFile(*Bytes, *Path))
		{
			UE_LOG(LogGSplatEditor, Error, TEXT("%s: %s cannot be written to %s."), *Filename, *Clouds[c].Path, *Path);
			continue;
		}
		UGSplatAsset* Asset = &First;
		UPackage* Package = nullptr;
		if (c > 0)
		{
			const FString AssetName = FString::Printf(TEXT("%s_%s"), *InName.ToString(), *FPaths::GetBaseFilename(Clouds[c].Path));
			Package = CreatePackage(*FPaths::Combine(Folder, AssetName));
			Asset = NewObject<UGSplatAsset>(Package, UGSplatAsset::StaticClass(), FName(*AssetName), Flags | RF_Public | RF_Standalone);
			Asset->AssetImportData = NewObject<UGSplatImportData>(Asset, UGSplatImportData::StaticClass());
		}
		Asset->Splats.Empty();
		const bool bRead = ImportAthc(Path, *Asset->AssetImportData, Asset->Splats);
		IFileManager::Get().Delete(*Path);
		if (!bRead)
		{
			Asset->Splats.Empty();
			continue;
		}
		Asset->AssetImportData->Update(Filename);
		if (Package)
		{
			FAssetRegistryModule::AssetCreated(Asset);
			Package->MarkPackageDirty();
		}
		Imported++;
		UE_LOG(LogGSplatEditor, Display, TEXT("%s: %s's %s imported as %s."), *Filename, *Clouds[c].Object, *Clouds[c].Path, *Asset->GetPathName());
	}

	// The sky, as UE imports an HDR image, beside the clouds.
	const TSharedPtr<FJsonObject>* Sky = nullptr;
	FString SkyPath;
	if (Scene->TryGetObjectField(TEXT("sky"), Sky) && (*Sky)->TryGetStringField(TEXT("hdr"), SkyPath))
	{
		if (const TArray<uint8>* SkyBytes = Files.Find(SkyPath))
		{
			const FString Path = FPaths::Combine(Temp, FPaths::GetCleanFilename(SkyPath));
			if (FFileHelper::SaveArrayToFile(*SkyBytes, *Path))
			{
				FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
				const TArray<UObject*> SkyAssets = AssetTools.Get().ImportAssets({ Path }, Folder);
				IFileManager::Get().Delete(*Path);
				double Intensity = 1.0, RotationY = 0.0;
				(*Sky)->TryGetNumberField(TEXT("intensity"), Intensity);
				(*Sky)->TryGetNumberField(TEXT("rotationY"), RotationY);
				UE_LOG(LogGSplatEditor, Display, TEXT("%s: the sky %s imported (%d asset); a sky light takes it with intensity %.3g, turned %.3g degrees about the up axis past SourceCubemapAngle 180."),
					*Filename, *SkyPath, SkyAssets.Num(), Intensity, RotationY);
			}
		}
	}

	// The camera, logged in the scene's frame.
	const TArray<TSharedPtr<FJsonValue>>* Cameras = nullptr;
	if (Scene->TryGetArrayField(TEXT("cameras"), Cameras) && Cameras->Num() > 0)
	{
		const TSharedPtr<FJsonObject> Camera = (*Cameras)[0]->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
		double Fov = 0.0;
		Camera->TryGetNumberField(TEXT("fov"), Fov);
		if (Camera->TryGetArrayField(TEXT("keys"), Keys) && Keys->Num() > 0)
		{
			const TSharedPtr<FJsonObject> Key = (*Keys)[0]->AsObject();
			auto Triple = [&](const TCHAR* Field)
			{
				const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
				return Key->TryGetArrayField(Field, A) && A->Num() >= 3
					? FString::Printf(TEXT("(%.4g, %.4g, %.4g)"), (*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber())
					: FString(TEXT("?"));
			};
			UE_LOG(LogGSplatEditor, Display, TEXT("%s: its camera's first key at %s looking at %s, %.3g degrees vertical, %d keys (the scene's metres, y up)."),
				*Filename, *Triple(TEXT("position")), *Triple(TEXT("target")), Fov, Keys->Num());
		}
	}

	UE_LOG(LogGSplatEditor, Display, TEXT("%s: a web scene, %d of its %d clouds imported."), *Filename, Imported, Clouds.Num());
	return Imported > 0 && First.Splats.SplatCount > 0;
}

#endif // WITH_EDITOR
