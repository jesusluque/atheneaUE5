// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gaussians.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "GameplayTagsManager.h"

#define LOCTEXT_NAMESPACE "FGaussiansModule"

DEFINE_LOG_CATEGORY(LogGSplats);


void FGaussiansModule::StartupModule()
{
	FString PluginDir = IPluginManager::Get().FindPlugin(TEXT("StarLightGS"))->GetBaseDir();

	FString PluginConfigDir = FPaths::Combine(PluginDir, TEXT("Config/Tags"));
	UGameplayTagsManager::Get().AddTagIniSearchPath(PluginConfigDir);
}

void FGaussiansModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FGaussiansModule, Gaussians)