// Copyright (c) 2026 jesus luque.
//
// The decompressors and image decoders athenea's file formats need, compiled
// from their own sources: zstd's decompressor (BSD, Private/zstd, for .spz
// version 4), libwebp's decoder (BSD, Private/libwebp, for PlayCanvas's .sog),
// and UE's own zlib for gzip (.spz versions 2 and 3).

using UnrealBuildTool;
using System.IO;

public class AtheneaCodecs : ModuleRules
{
	public AtheneaCodecs(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		bDisableStaticAnalysis = true;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
		AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");

		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private", "zstd"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private", "libwebp"));
		PrivateDefinitions.Add("ZSTD_DISABLE_ASM=1");
		PrivateDefinitions.Add("ZSTD_LEGACY_SUPPORT=0");
		PrivateDefinitions.Add("ZSTD_MULTITHREAD=0");
		PrivateDefinitions.Add("DEBUGLEVEL=0");

		// Third-party C as its authors wrote it.
		CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;
		CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Off;
		bEnableExceptions = false;
	}
}
