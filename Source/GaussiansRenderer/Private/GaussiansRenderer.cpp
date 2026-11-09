#include "GaussiansRenderer.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FGaussiansRendererModule"

DEFINE_LOG_CATEGORY(LogGaussiansRenderer);


static TAutoConsoleVariable<bool> CVarGaussiansRelighting(
	TEXT("r.Gaussians.Relighting"),
	true,
	TEXT("Global toggle for Gaussian relighting."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<bool> CVarGaussiansShadows(
	TEXT("r.Gaussians.Shadows"),
	true,
	TEXT("Global toggle for Gaussian shadow casting."),
	ECVF_RenderThreadSafe
);


namespace Gaussians
{

	bool IsRelightingEnabled()
	{
		return CVarGaussiansRelighting.GetValueOnAnyThread();
	}

	bool IsRelightingEnabled_RenderThread()
	{
		return CVarGaussiansRelighting.GetValueOnRenderThread();
	}

	bool IsShadowsEnabled()
	{
		return CVarGaussiansShadows.GetValueOnAnyThread();
	}

	bool IsShadowsEnabled_RenderThread()
	{
		return CVarGaussiansShadows.GetValueOnRenderThread();
	}

}

void FGaussiansRendererModule::StartupModule()
{
	FString PluginDir = IPluginManager::Get().FindPlugin(TEXT("StarLightGS"))->GetBaseDir();

	FString PluginShaderDir = FPaths::Combine(PluginDir, TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/StarLightGS"), PluginShaderDir);
	AddShaderSourceSharedVirtualDirectory(TEXT("/Plugin/StarLightGS/Shared/"));
}

void FGaussiansRendererModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FGaussiansRendererModule, GaussiansRenderer)