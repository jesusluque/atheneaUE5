// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Gaussians : ModuleRules
{
    public Gaussians(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"ProceduralMeshComponent",

				"GaussiansRenderer"
            }
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"GameplayTags",

				"RHI",
				"RenderCore", 
				"Renderer",

				"NiagaraCore", 
				"Niagara", 

				"VectorVM", 
				"Projects",

				// athenea: the .athc and .athz importers read at runtime too
				"AtheneaCodecs"
            }
		);
	}
}
