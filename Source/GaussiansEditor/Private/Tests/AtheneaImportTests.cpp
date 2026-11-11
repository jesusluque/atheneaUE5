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
#include "Misc/App.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "ImageUtils.h"
#include "GaussianSplatComponent.h"
#include "GSFrameCompare.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Engine/TextureCube.h"
#include "EngineUtils.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtheneaImportAthc3Levels, "Athenea.Import.Athc3Levels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAtheneaImportAthc3Levels::RunTest(const FString& Parameters)
{
	// athc3_levels.athc: a version 3 file of 172,788 splats in 3 chunks and 9
	// levels (kind 0 blocks, coarsest first) of 107,954 groups, the finest
	// level's 80,896 after the others' 27,058. Its words are written on the
	// device (GSAthc3LodCS), so none are read here.
	FGaussianSplatData Splats;
	bool bRan = false;
	AtheneaTests::Common(*this, TEXT("athc3_levels.athc"), Splats, bRan);
	if (bRan)
	{
		TestEqual(TEXT("athc3_levels.athc's levels"), Splats.LodLevels, 9u);
		TestEqual(TEXT("athc3_levels.athc's splats and groups"), Splats.SplatCount, 172788u + 107954u);
		TestEqual(TEXT("athc3_levels.athc's finest level"), Splats.LodFinestLevel, 9u);
		TestEqual(TEXT("athc3_levels.athc's finest groups' first"), Splats.LodFinestBase, 172788u + 27058u);
		TestTrue(TEXT("athc3_levels.athc's words left to the device"), Splats.LodWords.IsEmpty());
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

// A word of light (RGB9E5), as the shaders read it back.
static FVector3f AtheneaUnpackRgb9e5(uint32 Word)
{
	const float Step = FMath::Exp2(float(int32(Word >> 27) - 24));
	return FVector3f(float(Word & 511u), float((Word >> 9) & 511u), float((Word >> 18) & 511u)) * Step;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtheneaImportFeatures, "Athenea.Import.Features",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAtheneaImportFeatures::RunTest(const FString& Parameters)
{
	// athenea_features.usda: four gaussians with an edit over them, two lamp
	// layers and their groups, a light catcher's flag and Cryptomatte ids.
	FGaussianSplatData Splats;
	bool bRan = false;
	AtheneaTests::Common(*this, TEXT("athenea_features.usda"), Splats, bRan);
	if (!bRan)
	{
		return true;
	}
	// The edit, on /World above the field: a y-up stage in metres, so (x, z, y) x 100.
	TestTrue(TEXT("the edit is active"), Splats.Edit.bActive);
	TestTrue(TEXT("the edit removes"), Splats.Edit.Mode == EGSSplatEditMode::Remove);
	TestTrue(TEXT("the edit's centre in local centimetres"), Splats.Edit.Centre.Equals(FVector3f(100.0f, 300.0f, 200.0f), 1.0e-3f));
	TestTrue(TEXT("the edit's half extents in local centimetres"), Splats.Edit.Size.Equals(FVector3f(50.0f, 200.0f, 100.0f), 1.0e-3f));
	TestEqual(TEXT("the edit's max scale in centimetres"), Splats.Edit.MaxScale, 10.0f, 1.0e-4f);
	TestEqual(TEXT("the edit's saturation"), Splats.Edit.Saturation, 0.5f, 1.0e-6f);
	// The lamps: two groups, a word each a gaussian, at the stage's levels.
	TestEqual(TEXT("two lamp layers"), Splats.LightLayerNames.Num(), 2);
	TestEqual(TEXT("a word a group a gaussian"), Splats.LightLayers.Num(), int32(Splats.SplatCount) * 2);
	const int32 Low = Splats.LightLayerNames.IndexOfByKey(FString(TEXT("low")));
	const int32 Tail = Splats.LightLayerNames.IndexOfByKey(FString(TEXT("tail")));
	TestTrue(TEXT("the groups are named"), Low != INDEX_NONE && Tail != INDEX_NONE);
	if (Low != INDEX_NONE && Tail != INDEX_NONE && Splats.LightLayers.Num() == int32(Splats.SplatCount) * 2)
	{
		auto Layer = [&](int32 Splat, int32 Group) { return AtheneaUnpackRgb9e5(Splats.LightLayers[Splat * 2 + Group]) / 16384.0f; };
		TestTrue(TEXT("a layer's light comes back"), Layer(0, Low).Equals(FVector3f(1.0f, 0.5f, 0.25f), 1.0f / 256.0f));
		TestTrue(TEXT("a faint light keeps its mantissa"), Layer(1, Low).Equals(FVector3f(0.001f, 0.002f, 0.003f), 0.003f / 256.0f));
		TestTrue(TEXT("the other group's light"), Layer(1, Tail).Equals(FVector3f(0.5f, 0.0f, 0.0f), 1.0f / 256.0f));
		TestTrue(TEXT("no light is none"), Layer(2, Low).IsNearlyZero());
		TestTrue(TEXT("a group on at its luminance times colour"),
			Splats.LightLayerDefaults[Low].Equals(FLinearColor(3.0f, 2.7f, 2.4f, 1.0f), 1.0e-4f));
		TestTrue(TEXT("a group off is at 0"), FVector3f(Splats.LightLayerDefaults[Tail].R, Splats.LightLayerDefaults[Tail].G, Splats.LightLayerDefaults[Tail].B).IsNearlyZero());
	}
	TestTrue(TEXT("a light catcher"), Splats.bLightCatcher);
	// Cryptomatte: the ids as stored, the manifest's names and hashes.
	TestEqual(TEXT("an id a gaussian"), Splats.CryptoIds.Num(), int32(Splats.SplatCount));
	if (Splats.CryptoIds.Num() == 4)
	{
		TestEqual(TEXT("an id as stored"), Splats.CryptoIds[0], 0x12345678u);
		TestEqual(TEXT("a negative int as its bits"), Splats.CryptoIds[3], 0xabcdef01u);
	}
	const int32 Wheel = Splats.CryptoNames.IndexOfByKey(FString(TEXT("/World/Wheel")));
	TestTrue(TEXT("the manifest names the wheel"), Wheel != INDEX_NONE);
	if (Wheel != INDEX_NONE)
	{
		TestEqual(TEXT("the wheel's hash"), Splats.CryptoHashes[Wheel], 0xabcdef01u);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtheneaImportSkin, "Athenea.Import.Skin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAtheneaImportSkin::RunTest(const FString& Parameters)
{
	// The sparrow as mesh2splat --skinned --transfer wrote it (USD), and as the
	// web's .athc carries it (SKIN and ATSK): 4 joints a gaussian with their
	// gradients, 61 poses at 30 a second.
	for (const TCHAR* Name : { TEXT("sparrow160tx.usdc"), TEXT("sparrow160tx.athc") })
	{
		FGaussianSplatData Splats;
		bool bRan = false;
		AtheneaTests::Common(*this, Name, Splats, bRan);
		if (!bRan)
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: a skeleton carries it"), Name), Splats.IsSkinned());
		TestEqual(FString::Printf(TEXT("%s: four influences a gaussian"), Name), Splats.SkinPerSplat, 4u);
		TestEqual(FString::Printf(TEXT("%s: a word an influence"), Name), Splats.SkinInfluences.Num(), int32(Splats.SplatCount) * 4);
		TestEqual(FString::Printf(TEXT("%s: 61 poses"), Name), Splats.SkinTimes.Num(), 61);
		if (Splats.SkinTimes.Num() == 61)
		{
			TestEqual(FString::Printf(TEXT("%s: two seconds of poses"), Name), Splats.SkinTimes.Last(), 2.0f, 1.0e-4f);
		}
		TestEqual(FString::Printf(TEXT("%s: three rows a joint a pose"), Name), Splats.SkinXforms.Num(), Splats.SkinTimes.Num() * int32(Splats.SkinJoints) * 3);
		TestEqual(FString::Printf(TEXT("%s: three gradient words a gaussian"), Name), Splats.SkinGradients.Num(), int32(Splats.SplatCount) * 3);
		// A gaussian's weights add up to one.
		if (Splats.SkinInfluences.Num() >= 4)
		{
			uint32 Sum = 0;
			for (int32 k = 0; k < 4; k++)
			{
				Sum += Splats.SkinInfluences[k] >> 16;
			}
			TestTrue(FString::Printf(TEXT("%s: the first gaussian's weights add up to one"), Name), FMath::Abs(int32(Sum) - 65535) <= 4);
		}
	}
	FGaussianSplatData Usd;
	bool bRan = false;
	AtheneaTests::Common(*this, TEXT("sparrow160tx.usdc"), Usd, bRan);
	if (bRan)
	{
		TestTrue(TEXT("the USD sparrow's transfer is zonal"), Usd.bHasTransfer && Usd.bTransferZonal);
		TestEqual(TEXT("read as nine harmonics"), Usd.TransferDirectCount, 9u);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtheneaImportAthzSkin, "Athenea.Import.AthzSkin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAtheneaImportAthzSkin::RunTest(const FString& Parameters)
{
	// sparrow_k.athz: athenea's sparrow as an .athz with layer K, 1,504,096
	// splats and 615,617 merged groups, carried by 609 joints over 61 poses.
	// The influences are decoded on the device; here, the rig's rows.
	FGaussianSplatData Splats;
	bool bRan = false;
	AtheneaTests::Common(*this, TEXT("sparrow_k.athz"), Splats, bRan);
	if (bRan)
	{
		TestTrue(TEXT("sparrow_k.athz is skinned"), Splats.IsSkinned() && Splats.bSkinFromDecode);
		TestEqual(TEXT("sparrow_k.athz's joints"), Splats.SkinJoints, 609u);
		TestEqual(TEXT("sparrow_k.athz's poses"), Splats.SkinTimes.Num(), 61);
		TestEqual(TEXT("two seconds of poses at 30 a second"), Splats.SkinTimes.Num() > 0 ? Splats.SkinTimes.Last() : 0.0f, 2.0f, 1.0e-4f);
		TestEqual(TEXT("three rows a joint a pose"), Splats.SkinXforms.Num(), 61 * 609 * 3);
		TestTrue(TEXT("the splats and the influences' words named"), Splats.PackedStreams.Num() > 11 && Splats.PackedStreams[11] == 1504096u && Splats.PackedStreams[9] != 0u);
		TestEqual(TEXT("read as y-up: the file's y is UE's z"), Splats.FileAxisY, FVector3f(0, 0, 1));

		// The sparrow was written from a z-up stage: bFileZUp maps the file's
		// (x, y, z) to (x, -y, z), so it stands as in athenea.
		UGSplatImportData* ZUp = NewObject<UGSplatImportData>();
		ZUp->bFileZUp = true;
		FGaussianSplatData Standing;
		TestTrue(TEXT("sparrow_k.athz imports z-up"), GSplatImporter::ImportAthzCloud(AtheneaTests::Find(*this, TEXT("sparrow_k.athz")), *ZUp, Standing));
		TestEqual(TEXT("z-up: the file's y is UE's -y"), Standing.FileAxisY, FVector3f(0, -1, 0));
		TestEqual(TEXT("z-up: the file's z is UE's z"), Standing.FileAxisZ, FVector3f(0, 0, 1));
		TestEqual(TEXT("z-up: the same splats"), Standing.SplatCount, Splats.SplatCount);
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


// athenea: A FRAME AGAINST ATHENEA'S (development 7). The surfel fixture --
// six discs turned 0, 50 and 75 degrees, gaussian and hard -- drawn from the
// camera the check script frames it with (from -x, 2.8 bounds radii back
// and 0.6 up; 45 degrees, 1024 square) into an sRGB target with no tone
// curve and 0 EV, and compared on the device with athenea's frame of the
// same file and camera (surfels_athenea.png): in 8-bit sRGB steps of each
// pixel's largest channel difference, a mean, p99, max and the share over 2.
// Needs a GPU: run without -nullrhi (ue13frame.ps1); skipped otherwise.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtheneaFrameSurfels, "Athenea.Frame.Surfels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAtheneaFrameSurfels::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender() || !GEditor)
	{
		AddInfo(TEXT("no GPU to draw on (-nullrhi): Athenea.Frame.Surfels skipped."));
		return true;
	}
	const FString Fixture = AtheneaTests::Find(*this, TEXT("surfels.usda"));
	const FString Expected = AtheneaTests::Find(*this, TEXT("surfels_athenea.png"));
	if (Fixture.IsEmpty() || Expected.IsEmpty())
	{
		return true;
	}
	UGSplatAsset* Asset = NewObject<UGSplatAsset>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestTrue(TEXT("surfels.usda imports"), AtheneaTests::Import(Fixture, Asset->Splats)))
	{
		return false;
	}
	UTexture2D* Reference = FImageUtils::ImportFileAsTexture2D(Expected);
	if (!TestNotNull(TEXT("athenea's frame reads"), Reference))
	{
		return false;
	}
	// Its bytes as they are: the compare reads sRGB values on both sides.
	Reference->SRGB = false;
	Reference->UpdateResource();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	AGaussianSplatActor* Actor = World->SpawnActor<AGaussianSplatActor>();
	Actor->GaussianSplatComponent->GaussianSplatAsset = Asset;
	Actor->GaussianSplatComponent->MarkRenderStateDirty();

	// The fixture's bounds: centre (0, 0, 40) cm, widest half extent 80.
	const FVector Target(0.0, 0.0, 40.0);
	const FVector Eye = Target + FVector(-2.8 * 80.0, 0.0, 0.6 * 80.0);
	ASceneCapture2D* Capture = World->SpawnActor<ASceneCapture2D>(Eye, (Target - Eye).Rotation());
	UTextureRenderTarget2D* Target2D = UKismetRenderingLibrary::CreateRenderTarget2D(World, 1024, 1024, RTF_RGBA8_SRGB);
	USceneCaptureComponent2D* Camera = Capture->GetCaptureComponent2D();
	Camera->FOVAngle = 45.0f;
	Camera->CaptureSource = SCS_FinalToneCurveHDR;
	Camera->TextureTarget = Target2D;
	Camera->bCaptureEveryFrame = false;
	// The fixture alone, whatever map the editor has open: no floor, sky or
	// fog behind it, as athenea draws it.
	Camera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Camera->ShowOnlyActors.Add(Actor);
	Camera->ShowFlags.SetAtmosphere(false);
	Camera->ShowFlags.SetFog(false);
	Camera->ShowFlags.SetVolumetricFog(false);
	Camera->ShowFlags.SetCloud(false);
	FPostProcessSettings& Post = Camera->PostProcessSettings;
	Post.bOverride_ToneCurveAmount = true;
	Post.ToneCurveAmount = 0.0f;
	Post.bOverride_AutoExposureMethod = true;
	Post.AutoExposureMethod = AEM_Manual;
	Post.bOverride_AutoExposureBias = true;
	Post.AutoExposureBias = 0.0f;
	Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Post.AutoExposureApplyPhysicalCameraExposure = false;
	Camera->PostProcessBlendWeight = 1.0f;

	// An editor with no viewport draws a scene only when it is captured: some
	// frames for the resource to be made and the shaders to be ready, then
	// the compare.
	TSharedRef<int32> Frames = MakeShared<int32>(0);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Camera, Frames]()
	{
		Camera->CaptureScene();
		return ++(*Frames) >= 120;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Target2D, Reference, Actor, Capture]()
	{
		const Gaussians::FFrameCompare C = Gaussians::CompareFrames(Target2D, Reference);
		const FString Line = FString::Printf(TEXT("surfels against athenea: mean %.3f, p99 %u, max %u steps, %.2f %% over 2"), C.Mean, C.P99, C.Max, 100.0 * C.Over2);
		AddInfo(Line);
		UE_LOG(LogTemp, Display, TEXT("athenea: %s"), *Line);
		// The frame drawn, beside the fixtures, to look at when it fails.
		UKismetRenderingLibrary::ExportRenderTarget(Actor->GetWorld(), Target2D, AtheneaTests::Folder(), TEXT("surfels_frame_test.png"));
		if (TestTrue(TEXT("the frames compared"), C.bValid))
		{
			// Measured 11 Nov on the 3070: mean 3.49, p99 28, max 37 (the 75
			// degree disc's edge); the bounds leave room for a driver, not for
			// a change in how a surfel is drawn.
			TestTrue(TEXT("mean within 5 steps"), C.Mean <= 5.0);
			TestTrue(TEXT("p99 within 36 steps"), C.P99 <= 36u);
			TestTrue(TEXT("max within 64 steps"), C.Max <= 64u);
		}
		Actor->Destroy();
		Capture->Destroy();
		return true;
	}));
	return true;
}

namespace AtheneaTests
{
	// The check script's capture: the actor alone, from Eye to Target, 45
	// degrees, into a 1024 square sRGB target with no tone curve and 0 EV.
	ASceneCapture2D* NeutralCapture(UWorld* World, AActor* Only, const FVector& Eye, const FVector& Target, UTextureRenderTarget2D*& OutTarget)
	{
		ASceneCapture2D* Capture = World->SpawnActor<ASceneCapture2D>(Eye, (Target - Eye).Rotation());
		OutTarget = UKismetRenderingLibrary::CreateRenderTarget2D(World, 1024, 1024, RTF_RGBA8_SRGB);
		USceneCaptureComponent2D* Camera = Capture->GetCaptureComponent2D();
		Camera->FOVAngle = 45.0f;
		Camera->CaptureSource = SCS_FinalToneCurveHDR;
		Camera->TextureTarget = OutTarget;
		Camera->bCaptureEveryFrame = false;
		Camera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		Camera->ShowOnlyActors.Add(Only);
		Camera->ShowFlags.SetAtmosphere(false);
		Camera->ShowFlags.SetFog(false);
		Camera->ShowFlags.SetVolumetricFog(false);
		Camera->ShowFlags.SetCloud(false);
		// No temporal filter or motion blur: athenea's frame is one sample of
		// the scene, and a capture's TSR blurs a gaussian's texture.
		Camera->ShowFlags.SetAntiAliasing(false);
		Camera->ShowFlags.SetTemporalAA(false);
		Camera->ShowFlags.SetMotionBlur(false);
		FPostProcessSettings& Post = Camera->PostProcessSettings;
		Post.bOverride_ToneCurveAmount = true;
		Post.ToneCurveAmount = 0.0f;
		Post.bOverride_AutoExposureMethod = true;
		Post.AutoExposureMethod = AEM_Manual;
		Post.bOverride_AutoExposureBias = true;
		Post.AutoExposureBias = 0.0f;
		Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		Post.AutoExposureApplyPhysicalCameraExposure = false;
		Camera->PostProcessBlendWeight = 1.0f;
		return Capture;
	}
}

// athenea: RELIGHTING AGAINST ATHENEA. The TX sparrow (sparrow160tx.usdc at
// 100, posed at 0.5 s) lit by the studio sky -- a sky light of the check
// levels' cubemap (/Game/Tx/studio) turned 180 degrees, which is athenea's
// dome turned up (+y to +z) and not about it -- drawn from the camera the
// side by sides use and compared with athenea's frame of the same stage
// drawn with its dome lighting and not seen (athenea:domeVisible off),
// sparrow_sky_athenea.png: the bird, its alpha the bird's coverage eroded by
// 3 pixels.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtheneaFrameSparrowSky, "Athenea.Frame.SparrowSky",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAtheneaFrameSparrowSky::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender() || !GEditor)
	{
		AddInfo(TEXT("no GPU to draw on (-nullrhi): Athenea.Frame.SparrowSky skipped."));
		return true;
	}
	const FString Fixture = AtheneaTests::Find(*this, TEXT("sparrow160tx.usdc"));
	const FString Expected = AtheneaTests::Find(*this, TEXT("sparrow_sky_athenea.png"));
	UTextureCube* Sky = LoadObject<UTextureCube>(nullptr, TEXT("/Game/Tx/studio.studio"));
	if (Fixture.IsEmpty() || Expected.IsEmpty())
	{
		return true;
	}
	if (!Sky)
	{
		AddInfo(TEXT("no /Game/Tx/studio cubemap in the project (setup_files.py imports it): skipped."));
		return true;
	}
	UGSplatAsset* Asset = NewObject<UGSplatAsset>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestTrue(TEXT("sparrow160tx.usdc imports"), AtheneaTests::Import(Fixture, Asset->Splats)))
	{
		return false;
	}
	UTexture2D* Reference = FImageUtils::ImportFileAsTexture2D(Expected);
	if (!TestNotNull(TEXT("athenea's frame reads"), Reference))
	{
		return false;
	}
	Reference->SRGB = false;
	Reference->UpdateResource();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	// One sky light lights a scene: the map's own are put out.
	TArray<USkyLightComponent*> Others;
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		USkyLightComponent* C = It->GetLightComponent();
		if (C && C->IsVisible())
		{
			C->SetVisibility(false);
			Others.Add(C);
		}
	}
	ASkyLight* Light = World->SpawnActor<ASkyLight>();
	USkyLightComponent* LightComponent = Light->GetLightComponent();
	LightComponent->SetMobility(EComponentMobility::Movable);
	LightComponent->SourceType = SLS_SpecifiedCubemap;
	LightComponent->Cubemap = Sky;
	LightComponent->SourceCubemapAngle = 180.0f;
	LightComponent->bRealTimeCapture = false;
	LightComponent->MarkRenderStateDirty();
	LightComponent->RecaptureSky();

	AGaussianSplatActor* Actor = World->SpawnActor<AGaussianSplatActor>();
	Actor->SetActorScale3D(FVector(100.0));
	UGaussianSplatComponent* Splats = Actor->GaussianSplatComponent;
	Splats->GaussianSplatAsset = Asset;
	Splats->bSkinPlaying = false;
	Splats->SetSkinTime(0.5f);
	Splats->MarkRenderStateDirty();

	// The side by sides' camera: UE's framing of the sparrow's bounds (centre
	// (0, -1.145, 4.280) cm, widest half extent 11.849).
	const FVector Target(0.0, -1.145024, 4.279795);
	const FVector Eye = Target + FVector(-2.8 * 11.849185, 0.0, 0.6 * 11.849185);
	UTextureRenderTarget2D* Frame = nullptr;
	ASceneCapture2D* Capture = AtheneaTests::NeutralCapture(World, Actor, Eye, Target, Frame);
	USceneCaptureComponent2D* Camera = Capture->GetCaptureComponent2D();

	TSharedRef<int32> Frames = MakeShared<int32>(0);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Camera, Frames, LightComponent]()
	{
		if (*Frames == 30)
		{
			LightComponent->RecaptureSky();
		}
		Camera->CaptureScene();
		return ++(*Frames) >= 150;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Frame, Reference, Actor, Capture, Light, Others]()
	{
		const Gaussians::FFrameCompare C = Gaussians::CompareFrames(Frame, Reference);
		const FString Line = FString::Printf(TEXT("sparrow under the studio sky against athenea: mean %.3f, p99 %u, max %u steps, %.2f %% over 2, %llu pixels"),
			C.Mean, C.P99, C.Max, 100.0 * C.Over2, C.Pixels);
		AddInfo(Line);
		UE_LOG(LogTemp, Display, TEXT("athenea: %s"), *Line);
		UKismetRenderingLibrary::ExportRenderTarget(Actor->GetWorld(), Frame, AtheneaTests::Folder(), TEXT("sparrow_sky_frame_test.png"));
		if (TestTrue(TEXT("the frames compared"), C.bValid))
		{
			TestTrue(TEXT("the bird compared"), C.Pixels > 20000u);
			// Measured 11 Nov on the 3070: mean 10.4, p99 45, max 83 over
			// 76,110 pixels; over the bird UE's linear means are 0.95, 0.95
			// and 0.94 of athenea's (decisions 17).
			TestTrue(TEXT("mean within 13 steps"), C.Mean <= 13.0);
			TestTrue(TEXT("p99 within 56 steps"), C.P99 <= 56u);
		}
		Actor->Destroy();
		Capture->Destroy();
		Light->Destroy();
		for (USkyLightComponent* O : Others)
		{
			O->SetVisibility(true);
		}
		return true;
	}));
	return true;
}
