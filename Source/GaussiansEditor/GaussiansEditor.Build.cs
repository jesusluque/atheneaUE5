using UnrealBuildTool;
using System.IO;

public class GaussiansEditor: ModuleRules
{
    public GaussiansEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "../ThirdParty/happly"));

        PublicDependencyModuleNames.Add("Gaussians");

        PrivateDependencyModuleNames.AddRange(new string[] 
        {
            "Core", 
            "CoreUObject", 

            "AppFramework",

            "Engine", 
            "UnrealEd",
            "EditorSubsystem",
            "ApplicationCore",
            "InputCore",

            "Slate",
            "SlateCore",
            "ToolWidgets",
            "ToolMenus",
            "ContentBrowser",

            "GaussiansRenderer", 
            "Gaussians", 

            // athenea: OpenUSD as UE bundles it, for USD ParticleFields.
            "UnrealUSDWrapper",

            // athenea: gzip, zstd and WebP for .spz and .sog.
            "AtheneaCodecs",
            "Json",
            "AssetTools",
        });

        bUseRTTI = true;
    }
}
