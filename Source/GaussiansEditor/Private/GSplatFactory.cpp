#include "GSplatFactory.h"

#if WITH_EDITOR

#include <happly.h>

#include "GaussiansEditorModule.h"
#include "GSplatAsset.h"
#include "GSplatImportData.h"
#include "GSplatImporter.h"
#include "GSplatImportWindow.h"
#include "EditorFramework/AssetImportData.h"
#include "Interfaces/IMainFrameModule.h"
#include "HAL/PlatformApplicationMisc.h"

#define LOCTEXT_NAMESPACE "GSplatFactory"


static bool ShowGSplatImportWindow(const FString& Filename, UGSplatImportData* ImportData)
{
	check(ImportData);

	TSharedPtr<SWindow> ParentWindow;

	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
	}

	// Compute centered window position based on max window size, which include when all categories are expanded
	constexpr float ImportWindowWidth = 550.0f;
	constexpr float ImportWindowHeight = 800.0f;
	FVector2D ImportWindowSize = FVector2D(ImportWindowWidth, ImportWindowHeight); // Max window size it can get based on current slate

	FSlateRect WorkAreaRect = FSlateApplicationBase::Get().GetPreferredWorkArea();
	FVector2D DisplayTopLeft(WorkAreaRect.Left, WorkAreaRect.Top);
	FVector2D DisplaySize(WorkAreaRect.Right - WorkAreaRect.Left, WorkAreaRect.Bottom - WorkAreaRect.Top);

	float ScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(DisplayTopLeft.X, DisplayTopLeft.Y);
	ImportWindowSize *= ScaleFactor;

	FVector2D WindowPosition = (DisplayTopLeft + (DisplaySize - ImportWindowSize) / 2.0f) / ScaleFactor;

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(NSLOCTEXT("UnrealEd", "GSplatImportOptionsTitle", "Gaussian Splat Import Options"))
		.SizingRule(ESizingRule::Autosized)
		.AutoCenter(EAutoCenter::None)
		.ClientSize(ImportWindowSize)
		.ScreenPosition(WindowPosition);

	TSharedPtr<SGSplatImportWindow> GSplatOptionWindow;
	Window->SetContent
	(
		SAssignNew(GSplatOptionWindow, SGSplatImportWindow)
		.ImportData(ImportData)
		.WidgetWindow(Window)
		.FullPath(FText::FromString(Filename))
		.MaxWindowHeight(ImportWindowHeight)
		.MaxWindowWidth(ImportWindowWidth)
	);

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

	ImportData->bImportNormals = GSplatOptionWindow->ShouldImportNormals();
	ImportData->bImportHighOrderSH = GSplatOptionWindow->ShouldImportHighOrderSH();
	ImportData->bImportMetallicRoughness = GSplatOptionWindow->ShouldImportMetallicRoughness();

	ImportData->bPopulateMRWithDefault = GSplatOptionWindow->ShouldPopulateMetallicRoughnessWithDefault();
	ImportData->MetallicDefaultValue = GSplatOptionWindow->GetDefaultMetallicValue();
	ImportData->RoughnessDefaultValue = GSplatOptionWindow->GetDefaultRoughnessValue();

	ImportData->ScaleFactor = GSplatOptionWindow->GetScaleFactor();

	GSplatOptionWindow->GetImportSwizzle(ImportData->Swizzle);

	ImportData->bBaseColorIsRGB = GSplatOptionWindow->GetBaseColorIsRGB();
	ImportData->bApplyGammaCorrection = GSplatOptionWindow->GetApplyGammaCorrection();
	ImportData->bOpacityAsAlpha = GSplatOptionWindow->GetOpacityAsAlpha();

	return GSplatOptionWindow->ShouldImport();
}


UGSplatFactory::UGSplatFactory()
{
	SupportedClass = UGSplatAsset::StaticClass();
	bCreateNew = true;
	bEditorImport = true;

	Formats.Add(TEXT("ply;GSplatImport"));
	// athenea: a USD stage is ours only when it holds a gaussian ParticleField
	// (FactoryCanImport); UE's USD importer keeps every other stage.
	Formats.Add(TEXT("usd;Gaussian splats (USD ParticleField)"));
	Formats.Add(TEXT("usda;Gaussian splats (USD ParticleField)"));
	Formats.Add(TEXT("usdc;Gaussian splats (USD ParticleField)"));
	Formats.Add(TEXT("athc;athenea chunked cloud"));
	Formats.Add(TEXT("splat;Gaussian splats (antimatter15)"));
	Formats.Add(TEXT("spz;Gaussian splats (Niantic SPZ)"));
	Formats.Add(TEXT("sog;Gaussian splats (PlayCanvas SOG)"));
	Formats.Add(TEXT("athz;athenea scene (web) or paged cloud"));
	Formats.Add(TEXT("glb;Gaussian splats (glTF KHR_gaussian_splatting)"));
	Formats.Add(TEXT("gltf;Gaussian splats (glTF KHR_gaussian_splatting)"));
	ImportPriority = DefaultImportPriority + 10;
}

bool UGSplatFactory::ShouldShowInNewMenu() const
{
	return false;
}

FText UGSplatFactory::GetDisplayName() const
{
	return LOCTEXT("GSplatFactoryDescription", "Gaussian Splat");
}

bool UGSplatFactory::CanCreateNew() const
{
	return false;
}

UObject* UGSplatFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
                                          UObject* Context, FFeedbackContext* Warn)
{
	UGSplatAsset* Object = NewObject<UGSplatAsset>(InParent, InClass, InName, Flags);
	return Object;
}

bool UGSplatFactory::FactoryCanImport(const FString& Filename)
{
	if (GSplatImporter::IsUsdFile(Filename))
	{
		return GSplatImporter::StageHasSplats(Filename);
	}
	if (GSplatImporter::IsGltfFile(Filename))
	{
		return GSplatImporter::GltfHasSplats(Filename);
	}
	if (GSplatImporter::IsAthcFile(Filename) || GSplatImporter::IsSplatFile(Filename) || GSplatImporter::IsSogFile(Filename)
		|| GSplatImporter::IsAthzFile(Filename))
	{
		return true;
	}
	return FPaths::GetExtension(Filename) == TEXT("ply");
}

bool UGSplatFactory::DoesSupportClass(UClass* Class)
{
	return Class == UGSplatAsset::StaticClass();
}

UClass* UGSplatFactory::ResolveSupportedClass()
{
	return UGSplatAsset::StaticClass();
}

UObject* UGSplatFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	return ImportInternal(InClass, InParent, InName, Flags, Filename, Parms, bOutOperationCanceled, false);
}

bool UGSplatFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UGSplatAsset* pGS = Cast<UGSplatAsset>(Obj);
	if (pGS && pGS->AssetImportData)
	{
		pGS->AssetImportData->ExtractFilenames(OutFilenames);
		return true;
	}
	return false;
}

void UGSplatFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UGSplatAsset* pGS = Cast<UGSplatAsset>(Obj);
	if (pGS && ensure(NewReimportPaths.Num() == 1))
	{
		pGS->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

EReimportResult::Type UGSplatFactory::Reimport(UObject* Obj)
{
	if (!Obj || !Obj->IsA(UGSplatAsset::StaticClass()))
	{
		return EReimportResult::Failed;
	}
	UGSplatAsset* pGS = Cast<UGSplatAsset>(Obj);

	const FString Filename = pGS->AssetImportData->GetFirstFilename();
	if (!Filename.Len() || IFileManager::Get().FileSize(*Filename) == INDEX_NONE || !FactoryCanImport(Filename))
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("Reimport failed! Filename '%s' is invalid or no such file exists."), *Filename);
		return EReimportResult::Failed;
	}

	bool bOutCanceled = false;
	if (!ImportInternal(pGS->GetClass(), pGS->GetOuter(), *pGS->GetName(), RF_Public | RF_Standalone, Filename, nullptr, bOutCanceled, true, pGS))
	{
		return bOutCanceled ? EReimportResult::Cancelled : EReimportResult::Failed;
	};

	pGS->DetachResource();
	pGS->OnAssetUpdate.Broadcast();
	(void)pGS->MarkPackageDirty();

	return EReimportResult::Succeeded;
}

UObject* UGSplatFactory::ImportInternal(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	const FString& Filename, const TCHAR* Parms, bool& bOutOperationCanceled, bool bIsReimport, UGSplatAsset* InExistingAsset)
{
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, InClass, InParent, InName, Parms);

	UGSplatAsset* Object = InExistingAsset ? InExistingAsset : NewObject<UGSplatAsset>(InParent, InClass, InName, Flags);
	if (!Object->AssetImportData)
	{
		Object->AssetImportData = NewObject<UGSplatImportData>(InParent, UGSplatImportData::StaticClass());
	}
	

	bOutOperationCanceled = false;
	const bool bIsUnattended = (IsAutomatedImport() || FApp::IsUnattended()
							 || IsRunningCommandlet() || GIsRunningUnattendedScript);

	if (Filename.IsEmpty())
	{
		return nullptr;
	}

	// athenea: a web scene's .athz brings several clouds and its sky.
	if (GSplatImporter::IsAthzFile(Filename) && GSplatImporter::IsAthzScene(Filename))
	{
		if (!GSplatImporter::ImportAthzScene(Filename, InParent, InName, Flags, *Object))
		{
			Object->Splats.Empty();
			return nullptr;
		}
		Object->AssetImportData->Update(Filename);
		GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, Object);
		return Object;
	}

	// athenea: a USD stage or a .athc carries its own axes, units and colour
	// space, so it is read with no dialogue.
	if (GSplatImporter::IsUsdFile(Filename) || GSplatImporter::IsAthcFile(Filename) || GSplatImporter::IsSplatFile(Filename)
		|| GSplatImporter::IsSogFile(Filename) || GSplatImporter::IsGltfFile(Filename) || GSplatImporter::IsAthzFile(Filename))
	{
		Object->Splats.Empty();
		const bool bRead = GSplatImporter::IsUsdFile(Filename)
			? GSplatImporter::ImportUsd(Filename, *Object->AssetImportData, Object->Splats)
			: GSplatImporter::IsAthcFile(Filename)
			? GSplatImporter::ImportAthc(Filename, *Object->AssetImportData, Object->Splats)
			: GSplatImporter::IsAthzFile(Filename)
			? GSplatImporter::ImportAthzCloud(Filename, *Object->AssetImportData, Object->Splats)
			: GSplatImporter::IsGltfFile(Filename)
			? GSplatImporter::ImportGltf(Filename, *Object->AssetImportData, Object->Splats)
			: GSplatImporter::IsSogFile(Filename)
			? GSplatImporter::ImportSog(Filename, *Object->AssetImportData, Object->Splats)
			: GSplatImporter::ImportSplatFile(Filename, *Object->AssetImportData, Object->Splats);
		if (!bRead)
		{
			Object->Splats.Empty();
			return nullptr;
		}
		Object->AssetImportData->Update(Filename);
		GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, Object);
		return Object;
	}

	// Parse file into preview data required for import dialogue
	happly::PLYData* PlyData = nullptr;
	ON_SCOPE_EXIT
	{
		if (PlyData)
		{
			delete PlyData;
		}
	};

	if (!GSplatImporter::ParsePly(Filename, *Object->AssetImportData, &PlyData))
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("Failed to parse ply: %s"), *Filename);

		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("ImportFailedDialogText", "Failed to parse ply file. See output log for details."),
			LOCTEXT("ImportFailedDialogTitle", "Ply Parse Failed")
		);
	}

	// Open import dialogue for user to select options
	if (!bIsUnattended)
	{
		if (!ShowGSplatImportWindow(Filename, Object->AssetImportData))
		{
			bOutOperationCanceled = true;

			if (bIsReimport)
			{
				return Object;
			}

			return nullptr;
		}
	}

	//Object could be existing object, only proceed with emptying when made sure we are reloading data
	Object->Splats.Empty();

	// Proceed with import, applying specified options during import
	if (!GSplatImporter::ImportPly(*Object->AssetImportData, PlyData, Object->Splats))
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("Failed to import ply: %s"), *Filename);
		Object->Splats.Empty();

		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("ImportFailedDialogText", "Failed to import ply file. See output log for details."),
			LOCTEXT("ImportFailedDialogTitle", "Ply Import Failed")
		);

		return nullptr;
	}

	Object->AssetImportData->Update(Filename);
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, Object);

	return Object;
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_EDITOR