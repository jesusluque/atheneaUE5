// Copyright (c) 2026 jesus luque.
//
// The importers' automation tests (Session Frontend, or
// -ExecCmds="Automation RunTests Athenea; Quit"): each test file athenea's
// test matrix uses, read by its importer into a FGaussianSplatData, and what
// the import must have made of it checked -- counts, bounds, the levels'
// words, a medium's values, a TX cloud's transfer. The files are looked for in
// ATHENEA_UE_TEST_FILES (default: ~/athenea-unreal); a file that is not there
// is reported and skipped. Nothing here needs a GPU: the decode runs when a
// resource is made, not at the import.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "GSplatImporter.h"
#include "GSplatAsset.h"
#include "GSplatRuntimeLibrary.h"
#include "GSplatImportData.h"
#include "GaussianData.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"

namespace AtheneaTests
{
	FString Folder()
	{
		const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("ATHENEA_UE_TEST_FILES"));
		return Env.IsEmpty() ? FPaths::Combine(FPlatformProcess::UserDir(), TEXT(".."), TEXT("athenea-unreal")) : Env;
	}

	// The file's path, or empty (and a note in the test's log) when it is missing.
	FString Find(FAutomationTestBase& Test, const TCHAR* Name)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(Folder(), Name));
		if (!FPaths::FileExists(Path))
		{
			Test.AddInfo(FString::Printf(TEXT("%s is not in %s: skipped."), Name, *Folder()));
			return FString();
		}
		return Path;
	}

	// Through the importer the factory would choose for it.
	bool Import(const FString& Path, FGaussianSplatData& Splats)
	{
		UGSplatImportData* Data = NewObject<UGSplatImportData>();
		if (GSplatImporter::IsUsdFile(Path))
		{
			return GSplatImporter::ImportUsd(Path, *Data, Splats);
		}
		if (GSplatImporter::IsAthcFile(Path))
		{
			return GSplatImporter::ImportAthc(Path, *Data, Splats);
		}
		if (GSplatImporter::IsAthzFile(Path) && !GSplatImporter::IsAthzScene(Path))
		{
			return GSplatImporter::ImportAthzCloud(Path, *Data, Splats);
		}
		if (GSplatImporter::IsSplatFile(Path))
		{
			return GSplatImporter::ImportSplatFile(Path, *Data, Splats);
		}
		if (GSplatImporter::IsSogFile(Path))
		{
			return GSplatImporter::ImportSog(Path, *Data, Splats);
		}
		if (GSplatImporter::IsGltfFile(Path))
		{
			return GSplatImporter::ImportGltf(Path, *Data, Splats);
		}
		happly::PLYData* Ply = nullptr;
		const bool bParsed = GSplatImporter::ParsePly(Path, *Data, &Ply);
		const bool bRead = bParsed && GSplatImporter::ImportPly(*Data, Ply, Splats);
		return bRead;
	}

	// Every format reads, holds gaussians and has bounds.
	void Common(FAutomationTestBase& Test, const TCHAR* Name, FGaussianSplatData& Splats, bool& bRan)
	{
		bRan = false;
		const FString Path = Find(Test, Name);
		if (Path.IsEmpty())
		{
			return;
		}
		bRan = true;
		Test.TestTrue(FString::Printf(TEXT("%s imports"), Name), Import(Path, Splats));
		Test.TestTrue(FString::Printf(TEXT("%s holds gaussians"), Name), Splats.SplatCount > 0);
		Test.TestTrue(FString::Printf(TEXT("%s has bounds"), Name), Splats.GetBounds().SphereRadius > 0.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtheneaImportFormats, "Athenea.Import.Formats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAtheneaImportFormats::RunTest(const FString& Parameters)
{
	for (const TCHAR* Name : { TEXT("biker.spz"), TEXT("biker.glb"), TEXT("cat.spz"), TEXT("dwarf_warrior.splat"), TEXT("sh3.ply"),
		TEXT("sh3.sog"), TEXT("pawn_3dgs.ply"), TEXT("close.spz"), TEXT("pawn.usdc"), TEXT("pawntop.athz"), TEXT("close.athz") })
	{
		FGaussianSplatData Splats;
		bool bRan = false;
		AtheneaTests::Common(*this, Name, Splats, bRan);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtheneaImportLevels, "Athenea.Import.Levels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAtheneaImportLevels::RunTest(const FString& Parameters)
{
	// pawn.athc: 325,833 splats and 8 levels of 166,740 groups, two words each.
	FGaussianSplatData Splats;
	bool bRan = false;
	AtheneaTests::Common(*this, TEXT("pawn.athc"), Splats, bRan);
	if (bRan)
	{
		TestEqual(TEXT("pawn.athc's levels"), Splats.LodLevels, 8u);
		TestEqual(TEXT("pawn.athc's splats and groups"), Splats.SplatCount, 325833u + 166740u);
		TestEqual(TEXT("pawn.athc's words, two a gaussian"), Splats.LodWords.Num(), int32(Splats.SplatCount) * 2);
		uint32 Leaves = 0, Coarsest = 0;
		for (uint32 i = 0; i < Splats.SplatCount; i++)
		{
			const uint32 B = Splats.LodWords[int32(i * 2 + 1)];
			Leaves += (B & FGaussianSplatData::kLodLeaf) ? 1u : 0u;
			Coarsest += (B & FGaussianSplatData::kLodCoarsest) ? 1u : 0u;
		}
		TestEqual(TEXT("pawn.athc's splats marked leaves"), Leaves, 325833u);
		TestTrue(TEXT("pawn.athc's coarsest level marked"), Coarsest > 0u);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtheneaImportAthzLevels, "Athenea.Import.AthzLevels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAtheneaImportAthzLevels::RunTest(const FString& Parameters)
{
	// pawntop.athz: 292,056 splats and 7 merged levels, the groups after the
	// splats; the words are the device's (GSAthzLodCS), so none on the CPU.
	FGaussianSplatData Splats;
	bool bRan = false;
	AtheneaTests::Common(*this, TEXT("pawntop.athz"), Splats, bRan);
	if (bRan)
	{
		TestEqual(TEXT("pawntop.athz's levels"), Splats.LodLevels, 7u);
		TestTrue(TEXT("its groups after its splats"), Splats.SplatCount > 292056u && Splats.LodFinestBase >= 292056u);
		TestTrue(TEXT("its octree"), Splats.LodExtent > 0.0f);
		TestEqual(TEXT("no words on the CPU"), Splats.LodWords.Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtheneaImportPagedTree, "Athenea.Import.PagedTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAtheneaImportPagedTree::RunTest(const FString& Parameters)
{
	// A web scene's paged .athc (version 3): every node but one root has a
	// parent, and a node with children is not a leaf.
	FGaussianSplatData Splats;
	bool bRan = false;
	AtheneaTests::Common(*this, TEXT("car_M_Pawn_Top_W.athc"), Splats, bRan);
	if (bRan)
	{
		TestTrue(TEXT("the tree is read"), Splats.LodLevels > 0 && Splats.LodWords.Num() == int32(Splats.SplatCount) * 2);
		uint32 Roots = 0, Merges = 0;
		for (uint32 i = 0; i < Splats.SplatCount && Splats.LodWords.Num() > 0; i++)
		{
			Roots += Splats.LodWords[int32(i * 2)] == ~0u ? 1u : 0u;
			Merges += (Splats.LodWords[int32(i * 2 + 1)] & FGaussianSplatData::kLodLeaf) ? 0u : 1u;
		}
		TestEqual(TEXT("one root"), Roots, 1u);
		TestEqual(TEXT("its merges"), Merges, 349076u);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtheneaImportMedium, "Athenea.Import.Medium",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAtheneaImportMedium::RunTest(const FString& Parameters)
{
	// A vdb2splat frame under a stage that sets the medium's constants: frame
	// 10, still burning (a cold frame's file has no temperature primvar, and
	// its gaussians read 0).
	FGaussianSplatData Splats;
	bool bRan = false;
	AtheneaTests::Common(*this, TEXT("fireball/f_0010.usda"), Splats, bRan);
	if (bRan)
	{
		TestTrue(TEXT("a medium"), Splats.bVolume);
		TestEqual(TEXT("sigma and temperature a gaussian"), Splats.Volume.Num(), int32(Splats.SplatCount));
		TestEqual(TEXT("the stage's density scale"), Splats.VolumeDensityScale, 20.0f);
		TestEqual(TEXT("the stage's emission scale"), Splats.VolumeEmissionScale, 40.0f);
		TestEqual(TEXT("kelvin a unit"), Splats.VolumeTemperatureScale, 2400.0f);
		float Hottest = 0.0f;
		for (const FVector2f& V : Splats.Volume)
		{
			Hottest = FMath::Max(Hottest, V.Y);
		}
		TestTrue(TEXT("temperatures read"), Hottest > 0.0f);
	}
	FGaussianSplatData Cold;
	AtheneaTests::Common(*this, TEXT("fireball/f_0040.usda"), Cold, bRan);
	if (bRan)
	{
		float Hottest = 0.0f;
		for (const FVector2f& V : Cold.Volume)
		{
			Hottest = FMath::Max(Hottest, V.Y);
		}
		TestTrue(TEXT("a cold frame is a medium"), Cold.bVolume && Cold.Volume.Num() == int32(Cold.SplatCount));
		TestEqual(TEXT("a cold frame's temperatures"), Hottest, 0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtheneaImportTransfer, "Athenea.Import.Transfer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAtheneaImportTransfer::RunTest(const FString& Parameters)
{
	for (const TCHAR* Name : { TEXT("paint_tx.usdc"), TEXT("dragon_tx.usdc") })
	{
		FGaussianSplatData Splats;
		bool bRan = false;
		AtheneaTests::Common(*this, Name, Splats, bRan);
		if (bRan)
		{
			TestTrue(FString::Printf(TEXT("%s is a TX cloud"), Name), Splats.bHasTransfer);
			TestEqual(FString::Printf(TEXT("%s's 16 direct values"), Name), Splats.TransferDirectCount, 16u);
			TestEqual(FString::Printf(TEXT("%s's transfer, 64 values a gaussian"), Name), Splats.Transfer.Num(), int32(Splats.SplatCount) * 64);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtheneaRuntimeLoad, "Athenea.Runtime.Load",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAtheneaRuntimeLoad::RunTest(const FString& Parameters)
{
	// A .athc read at runtime, as a packaged game reads it: the same asset the
	// editor's import makes, transient; a missing file is null and says so.
	const FString Path = AtheneaTests::Find(*this, TEXT("pawn.athc"));
	if (!Path.IsEmpty())
	{
		UGSplatAsset* Asset = UGSplatRuntimeLibrary::LoadGaussianSplatFile(Path);
		TestNotNull(TEXT("pawn.athc loads at runtime"), Asset);
		if (Asset)
		{
			TestEqual(TEXT("its splats and groups"), Asset->Splats.SplatCount, 325833u + 166740u);
			TestEqual(TEXT("its levels"), Asset->Splats.LodLevels, 8u);
			TestTrue(TEXT("transient"), Asset->HasAnyFlags(RF_Transient));
		}
	}
	AddExpectedError(TEXT("no such file"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("a missing file"), UGSplatRuntimeLibrary::LoadGaussianSplatFile(TEXT("C:/no/such/file.athc")));
	return true;
}

#endif

