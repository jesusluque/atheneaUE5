// Copyright (c) 2026 jesus luque.
//
// athenea's .athz (lod/Athz.h on athenea's athz branch): a cloud with its levels
// of detail in pages a GPU decodes. Versions 2 to 4 (version 1 is version 2
// with no transforms, and is read as such).
//
//   [0]      header, 64 bytes: 'ATHZ', version, flags, layers, counts, the
//            tables' offsets
//   [asset]  the cloud's layout and bounds, its levels, each plane's coding
//            for the two kinds (merged groups, splats), the rANS frequencies,
//            where each layer's section is
//   [pages]  64 bytes a page: count and band, its box and error, each layer's
//            offset and bytes in its section, a CRC-32
//
// The CPU copies the pages' coded bytes (layers G and A, and T where the
// transfer is lossless) and writes what says where they are: the page list,
// the planes, the frequencies, a descriptor a level. The GPU expands the
// tables, decodes every level coarsest first into Scratch -- a word may be
// coded against its parent group -- and the cloud's splats last, laid out as a
// .athc chunk's arrays, which the version 3 decoders read as encoding 4
// (GSDecode.usf, GSAthzDecodeCS). The merged levels are decoded only as the
// splats' parents: what is drawn is the splats. A CPCA layer T is decoded as
// its codes and expanded by the basis the file keeps (GSAthzCpcaCS); the
// curvature is read with the transfer. Layer K (a rig) is not read. The
// header's and the pages' CRCs are checked before anything is decoded.

#include "GSplatRuntimeImport.h"
#include "GSplatImportData.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"


#include "GaussianData.h"
#include "Misc/Crc.h"
#include "Math/Float16.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace GSplatAthz
{
	struct FReader
	{
		const uint8* At;
		const uint8* End;
		bool bOk = true;
		template <typename T> T Get()
		{
			T v{};
			if (At + sizeof(T) > End)
			{
				bOk = false;
				return v;
			}
			FMemory::Memcpy(&v, At, sizeof(T));
			At += sizeof(T);
			return v;
		}
	};
	template <typename T> T GetAt(const TArray<uint8>& b, int64 At)
	{
		T v{};
		FMemory::Memcpy(&v, &b[At], sizeof(T));
		return v;
	}
	constexpr uint32 kFlagTransfer = 1, kFlagLinear = 8, kFlagEmission = 32;
	constexpr uint32 kLayerG = 1, kLayerA = 2, kLayerT = 4, kLayerK = 8;
	constexpr uint32 kNone = 0xFFFFFFFFu;
	constexpr uint32 kLevelWords = 40;
	// The Athc3 descriptors (GSDecode.usf): section order and block size.
	constexpr uint32 kSections = 9;
	constexpr uint32 kBlockWords = 32;
	constexpr uint32 kScratchWords = 4;   // encoding 4: the words are in Scratch already
}

bool GSplatImporter::ImportAthzCloud(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats)
{
	using namespace GSplatAthz;
	auto Fail = [&](const FString& Why)
	{
		UE_LOG(LogGSplatImport, Error, TEXT("%s: %s"), *Filename, *Why);
		return false;
	};
	TArray<uint8> File;
	if (!FFileHelper::LoadFileToArray(File, *Filename))
	{
		return Fail(TEXT("cannot be read."));
	}
	if (File.Num() < 4096 || FMemory::Memcmp(File.GetData(), "ATHZ", 4) != 0)
	{
		return Fail(TEXT("no ATHZ header."));
	}
	// Every CRC is zlib's CRC-32 over the bytes as stored (FCrc::MemCrc32 is
	// the same): the header's own over its first 60 bytes, and each page's
	// over its layers' bytes in layer order. A corrupt or cut file is
	// refused, never decoded.
	if (FCrc::MemCrc32(File.GetData(), 60) != GetAt<uint32>(File, 60))
	{
		return Fail(TEXT("its header is corrupt (its CRC-32 does not match)."));
	}
	const uint16 Version = GetAt<uint16>(File, 4);
	const uint32 Flags = GetAt<uint32>(File, 8), LayerMask = GetAt<uint32>(File, 12);
	const uint64 Gaussians = GetAt<uint64>(File, 16);
	const uint32 PageCount = GetAt<uint32>(File, 24);
	const uint16 RunGaussians = GetAt<uint16>(File, 28), PageRuns = GetAt<uint16>(File, 30);
	const uint64 AssetOffset = GetAt<uint64>(File, 32);
	const uint32 AssetBytes = GetAt<uint32>(File, 40);
	const uint64 PageTable = GetAt<uint64>(File, 44);
	if (Version < 1 || Version > 4 || RunGaussians == 0 || RunGaussians % 32 != 0 || RunGaussians > 256 || PageRuns == 0
		|| AssetOffset + AssetBytes > uint64(File.Num()) || PageTable + uint64(PageCount) * 64 > uint64(File.Num()))
	{
		return Fail(FString::Printf(TEXT("an ATHZ header this does not read (version %u)."), Version));
	}

	// The asset table, as athenea's decodeAsset reads it.
	FReader R{ File.GetData() + AssetOffset, File.GetData() + AssetOffset + AssetBytes };
	R.Get<uint32>();   // size
	const uint32 Kinds = R.Get<uint32>(), Planes = R.Get<uint32>(), TableLog = R.Get<uint32>(), Tables = R.Get<uint32>();
	const uint32 LevelCount = R.Get<uint32>(), Count = R.Get<uint32>(), FinestGroups = R.Get<uint32>();
	const uint32 ChunkSplats = R.Get<uint32>(), RestPerColour = R.Get<uint32>(), SHWords = R.Get<uint32>();
	const uint32 Normals = R.Get<uint32>() != 0 ? 1u : 0u, Emission = R.Get<uint32>() != 0 ? 1u : 0u;
	const uint32 PbrWords = R.Get<uint32>(), LobesWords = R.Get<uint32>(), TransferCount = R.Get<uint32>();
	const uint32 TransferWords = R.Get<uint32>(), ShadowWords = R.Get<uint32>();
	uint32 CurvatureWords = 0, CodedTransferWords = 0, InfluenceWords = 0, GradientWords = 0;
	uint32 SkinJoints = 0, SkinFrames = 0;
	TArray<uint8> Temporal;
	uint64 TemporalOffset = 0, TemporalBytes = 0;
	uint32 CpcaClusters = 0, CpcaComponents = 0;
	float CpcaLevels = 0.0f;
	uint64 BasisOffset = 0, BasisBytes = 0;
	if (Version >= 3)
	{
		CurvatureWords = R.Get<uint32>();
		CodedTransferWords = R.Get<uint32>();
		CpcaClusters = R.Get<uint32>(); CpcaComponents = R.Get<uint32>(); CpcaLevels = R.Get<float>();
		BasisOffset = R.Get<uint64>(); BasisBytes = R.Get<uint64>();
	}
	if (Version >= 4)
	{
		InfluenceWords = R.Get<uint32>();
		GradientWords = R.Get<uint32>();
		// Layer K: the joints and frames, and where the frames' block lies.
		SkinJoints = R.Get<uint32>(); SkinFrames = R.Get<uint32>();
		TemporalOffset = R.Get<uint64>(); TemporalBytes = R.Get<uint64>();
		// The frames' block, kept before the file's bytes go to the device.
		if (SkinFrames > 0 && TemporalOffset + TemporalBytes <= uint64(File.Num()))
		{
			Temporal.Append(File.GetData() + TemporalOffset, int32(TemporalBytes));
		}
	}
	float BoundsLo[3], Extent, BoundsMin[3], BoundsMax[3];
	for (float& v : BoundsLo) v = R.Get<float>();
	Extent = R.Get<float>();
	for (float& v : BoundsMin) v = R.Get<float>();
	for (float& v : BoundsMax) v = R.Get<float>();
	R.Get<uint64>();   // asset id
	uint64 SectionOffset[4], SectionBytes[4];
	for (uint64& v : SectionOffset) v = R.Get<uint64>();
	for (uint64& v : SectionBytes) v = R.Get<uint64>();
	R.Get<uint64>(); R.Get<uint64>();   // the finest level's starts
	struct FLevel { uint32 Level, Groups, FirstPage, Pages; };
	TArray<FLevel> Levels;
	for (uint32 l = 0; l < LevelCount && l < 31; l++)
	{
		FLevel E;
		E.Level = R.Get<uint32>(); E.Groups = R.Get<uint32>(); E.FirstPage = R.Get<uint32>(); E.Pages = R.Get<uint32>();
		Levels.Add(E);
	}
	TArray<uint32> PlaneEntries;
	PlaneEntries.SetNumUninitialized(int32(2 * Planes * 4));
	for (uint32& v : PlaneEntries) v = R.Get<uint32>();
	TArray<uint16> Frequencies;
	Frequencies.SetNumUninitialized(int32(Tables * 256));
	for (uint16& v : Frequencies) v = R.Get<uint16>();
	const bool bCpca = CodedTransferWords != 0;
	const uint32 CodedTransfer = bCpca ? CodedTransferWords : TransferWords;
	const uint32 Words = 4 + 4 + SHWords + 1 + Normals + Emission + PbrWords + LobesWords + CodedTransfer + ShadowWords
		+ CurvatureWords + InfluenceWords + GradientWords;
	if (!R.bOk || Kinds != 2 || LevelCount == 0 || LevelCount > 30 || Count == 0 || uint64(Count) != Gaussians
		|| Planes != 4 * Words || TableLog < 9 || TableLog > 12 || ChunkSplats != uint32(RunGaussians) * PageRuns
		|| SHWords == 0 || SHWords > 64 || RunGaussians / 32 > 8)
	{
		return Fail(TEXT("an asset table that does not add up."));
	}
	if (Version < 2)
	{
		for (int32 p = 0; p < PlaneEntries.Num(); p += 4)
		{
			PlaneEntries[p + 3] = 0;   // version 1: no transforms
		}
	}
	const uint32 LeafFirstPage = Levels.Last().FirstPage + Levels.Last().Pages;
	const uint32 LeafPages = (Count + ChunkSplats - 1) / ChunkSplats;
	if (PageCount < LeafFirstPage + LeafPages)
	{
		return Fail(TEXT("a prefix of its bands: the splats' pages are not all in the file."));
	}

	// A CPCA transfer: 4 words a splat against a basis of 256 clusters or
	// fewer, 8 axes a block, for the 112 values of a TX cloud (athenea's
	// decodeAsset refuses anything else). Its floats: the centroids and three
	// norms, then each block's means, axes and scales.
	const uint32 CpcaDims[3] = { 16, 48, 48 };
	uint64 BasisFloats = uint64(CpcaClusters) * 112 + 3;
	uint32 MeanAt[3] = { 0, 0, 0 };
	for (uint32 b = 0; b < 3; b++)
	{
		MeanAt[b] = uint32(BasisFloats);
		BasisFloats += uint64(CpcaClusters) * CpcaDims[b] * (1 + uint64(CpcaComponents)) + uint64(CpcaClusters) * CpcaComponents;
	}
	if (bCpca && (CodedTransferWords != 4 || TransferCount != 112 || TransferWords != 56 || CpcaClusters == 0
		|| CpcaClusters > 256 || CpcaComponents != 8 || !(CpcaLevels > 0.0f) || BasisBytes < BasisFloats * 4
		|| BasisOffset > uint64(File.Num()) || BasisBytes > uint64(File.Num()) - BasisOffset))
	{
		return Fail(TEXT("a CPCA transfer whose basis does not add up."));
	}

	// The layers decoded: geometry and appearance, and the transfer, lossless
	// or as CPCA codes; and which carry a rANS plane, by kind.
	const bool bTransfer = (Flags & kFlagTransfer) != 0 && TransferWords > 0;
	const bool bCurvature = CurvatureWords == 3;
	// Layer K (a skinned cloud's influences) is read where it is there.
	const bool bLayerK = InfluenceWords == 8 && SkinFrames > 0 && (LayerMask & kLayerK) != 0;
	const uint32 Layers = kLayerG | kLayerA | (bTransfer ? kLayerT : 0u) | (bLayerK ? kLayerK : 0u);
	uint32 HasRans[2] = { 0, 0 };
	for (uint32 k = 0; k < 2; k++)
	{
		for (uint32 p = 0; p < Planes; p++)
		{
			const uint32 E = PlaneEntries[(k * Planes + p) * 4];
			if ((E & 0xFF) == 1)
			{
				HasRans[k] |= 1u << (E >> 24);
			}
		}
	}

	// The scratch: the expanded tables, then every level's arrays and the
	// splats', each array contiguous, in the fields' order.
	const uint32 FieldWords[13] = { 4, 4, SHWords, 1, Normals, Emission, PbrWords, LobesWords, CodedTransfer, ShadowWords,
		CurvatureWords, InfluenceWords, GradientWords };
	uint64 Scratch = uint64(Tables) << TableLog << 2;
	TArray<TArray<uint32>> FieldBases;
	auto Lay = [&](uint32 N)
	{
		TArray<uint32> Bases;
		for (uint32 f = 0; f < 13; f++)
		{
			Bases.Add(uint32(Scratch));
			Scratch += uint64(N) * FieldWords[f] * 4;
		}
		return Bases;
	};
	for (const FLevel& L : Levels)
	{
		FieldBases.Add(Lay(L.Groups));
	}
	FieldBases.Add(Lay(Count));
	// A CPCA transfer's values, expanded from the splats' codes, then from
	// each level's (its merged groups are drawn too, and lit as the splats).
	const uint32 CpcaOut = uint32(FMath::Min<uint64>(Scratch, 0xFFFFFFFFull));
	TArray<uint32> LevelCpcaOut;
	if (bTransfer && bCpca)
	{
		Scratch += uint64(Count) * TransferWords * 4;
		for (const FLevel& L : Levels)
		{
			LevelCpcaOut.Add(uint32(FMath::Min<uint64>(Scratch, 0xFFFFFFFFull)));
			Scratch += uint64(L.Groups) * TransferWords * 4;
		}
	}
	if (Scratch > 0xFFFFFFF0ull)
	{
		return Fail(TEXT("more than 4 GB to decode into."));
	}

	// The bytes the device reads: the frequencies, the planes, the page list,
	// the pages' coded bytes, a descriptor a level, the header, the Athc3
	// descriptors.
	TArray<uint8> Bytes;
	auto Align4 = [&]() { Bytes.SetNumZeroed(Align(Bytes.Num(), 4)); };
	const uint32 FreqAt = uint32(Bytes.Num());
	Bytes.Append(reinterpret_cast<const uint8*>(Frequencies.GetData()), Frequencies.Num() * 2);
	Align4();
	const uint32 PlanesAt = uint32(Bytes.Num());
	Bytes.Append(reinterpret_cast<const uint8*>(PlaneEntries.GetData()), PlaneEntries.Num() * 4);
	// The pages: every level's, then the splats', in the file's order.
	TArray<uint32> PageList;
	TArray<uint8> Source;
	for (uint32 Page = 0; Page < LeafFirstPage + LeafPages; Page++)
	{
		const int64 E = int64(PageTable) + int64(Page) * 64;
		const uint32 Info0 = GetAt<uint32>(File, E);
		uint32 First = 0;
		if (Page >= LeafFirstPage)
		{
			First = (Page - LeafFirstPage) * ChunkSplats;
		}
		else
		{
			for (const FLevel& L : Levels)
			{
				if (Page >= L.FirstPage && Page < L.FirstPage + L.Pages)
				{
					First = (Page - L.FirstPage) * ChunkSplats;
				}
			}
		}
		uint32 Crc = 0;
		for (uint32 L = 0; L < 4; L++)
		{
			const uint32 Offset = GetAt<uint32>(File, E + 24 + L * 4), Size = GetAt<uint32>(File, E + 40 + L * 4);
			if (((LayerMask >> L) & 1u) == 0)
			{
				continue;
			}
			if (SectionOffset[L] + Offset + Size > uint64(File.Num()))
			{
				return Fail(FString::Printf(TEXT("page %u runs past the file."), Page));
			}
			Crc = FCrc::MemCrc32(File.GetData() + SectionOffset[L] + Offset, int32(Size), Crc);
		}
		if (Crc != GetAt<uint32>(File, E + 56))
		{
			return Fail(FString::Printf(TEXT("page %u is corrupt (its CRC-32 does not match)."), Page));
		}
		uint32 Src[4] = { kNone, kNone, kNone, kNone };
		for (uint32 L = 0; L < 4; L++)
		{
			const uint32 Offset = GetAt<uint32>(File, E + 24 + L * 4), Size = GetAt<uint32>(File, E + 40 + L * 4);
			if (((Layers >> L) & 1u) == 0 || ((LayerMask >> L) & 1u) == 0 || Size == 0)
			{
				continue;
			}
			if (SectionOffset[L] + Offset + Size > uint64(File.Num()))
			{
				return Fail(FString::Printf(TEXT("page %u runs past the file."), Page));
			}
			Src[L] = uint32(Source.Num());
			Source.Append(File.GetData() + SectionOffset[L] + Offset, Size);
			Source.SetNumZeroed(Align(Source.Num(), 4));
		}
		PageList.Append({ Src[0], Src[1], Src[2], Src[3], First, Info0 & 0xFFFF, 0u, 0u });
	}
	Align4();
	const uint32 PageListAt = uint32(Bytes.Num());
	Bytes.Append(reinterpret_cast<const uint8*>(PageList.GetData()), PageList.Num() * 4);
	const uint32 SourceAt = uint32(Bytes.Num());
	Bytes.Append(Source);
	Source.Empty();
	Align4();
	uint32 BasisAt = 0;
	if (bTransfer && bCpca)
	{
		BasisAt = uint32(Bytes.Num());
		Bytes.Append(File.GetData() + BasisOffset, int32(BasisFloats * 4));
	}
	File.Empty();
	Align4();
	// A descriptor a level, then the splats': kind, first page, pages, the
	// parents' count, leaves, 0, the level's bases, the parents' bases.
	TArray<uint32> LevelWords;
	for (int32 l = 0; l <= Levels.Num(); l++)
	{
		const bool bLeaves = l == Levels.Num();
		TArray<uint32> D;
		D.SetNumZeroed(kLevelWords);
		D[0] = bLeaves ? 1u : 0u;
		D[1] = bLeaves ? LeafFirstPage : Levels[l].FirstPage;
		D[2] = bLeaves ? LeafPages : Levels[l].Pages;
		D[3] = l > 0 ? Levels[l - 1].Groups : 0u;
		D[4] = bLeaves ? 1u : 0u;
		for (uint32 f = 0; f < 13; f++)
		{
			D[6 + f] = FieldBases[l][f];
			D[19 + f] = l > 0 ? FieldBases[l - 1][f] : 0u;
		}
		LevelWords.Append(D);
	}
	const uint32 LevelsAt = uint32(Bytes.Num());
	Bytes.Append(reinterpret_cast<const uint8*>(LevelWords.GetData()), LevelWords.Num() * 4);
	// The fields decoded: everything but layer K; the transfer and its open
	// directions where layer T is read, the curvature where it is three floats.
	uint32 Bound = 0;
	for (uint32 f = 0; f <= 10; f++)
	{
		Bound |= 1u << f;
	}
	if (!bTransfer)
	{
		Bound &= ~((1u << 8) | (1u << 9));
	}
	if (!bCurvature)
	{
		Bound &= ~(1u << 10);
	}
	// Layer K's influences and gradients, where the cloud is skinned
	// (GSAthzSkinCS lifts the splats' into the skin's buffers).
	const bool bSkin = bLayerK && SkinJoints > 0
		&& uint64(Temporal.Num()) == TemporalBytes
		&& TemporalBytes == 64 + uint64(SkinFrames) * 8 + uint64(SkinFrames) * SkinJoints * 24;
	if (bSkin)
	{
		Bound |= 1u << 11;
		Bound |= GradientWords == 3 ? 1u << 12 : 0u;
	}
	const uint32 Head[27] = { Tables, TableLog, Planes, Layers, HasRans[0], HasRans[1], 8 + SHWords,
		SHWords, Normals, Emission, PbrWords, LobesWords, CodedTransfer, ShadowWords, CurvatureWords, InfluenceWords, GradientWords,
		RunGaussians, PageRuns, FreqAt, PlanesAt, PageListAt, SourceAt, LevelsAt, uint32(Levels.Num()) + 1, 0u, Bound };
	const uint32 HeadAt = uint32(Bytes.Num());
	Bytes.Append(reinterpret_cast<const uint8*>(Head), sizeof(Head));
	// GSAthzCpcaCS's head: count, clusters, components, levels, each block's
	// first float, the basis, the splats' codes, the values out, words a splat.
	uint32 CpcaAt = 0;
	if (bTransfer && bCpca)
	{
		CpcaAt = uint32(Bytes.Num());
		const uint32 CpcaHead[11] = { Count, CpcaClusters, CpcaComponents, *reinterpret_cast<const uint32*>(&CpcaLevels),
			MeanAt[0], MeanAt[1], MeanAt[2], BasisAt, FieldBases.Last()[8], CpcaOut, TransferWords };
		Bytes.Append(reinterpret_cast<const uint8*>(CpcaHead), sizeof(CpcaHead));
		// One head a level after the splats' (PackedSpz.Z says how many in all).
		for (int32 l = 0; l < Levels.Num(); l++)
		{
			const uint32 LevelHead[11] = { Levels[l].Groups, CpcaClusters, CpcaComponents, *reinterpret_cast<const uint32*>(&CpcaLevels),
				MeanAt[0], MeanAt[1], MeanAt[2], BasisAt, FieldBases[l][8], LevelCpcaOut[l], TransferWords };
			Bytes.Append(reinterpret_cast<const uint8*>(LevelHead), sizeof(LevelHead));
		}
	}

	// The splats as a .athc v3 block whose sections are in Scratch.
	const TArray<uint32>& Leaf = FieldBases.Last();
	uint32 Desc[kSections][8];
	FMemory::Memzero(Desc);
	auto Set = [&](uint32 k, uint32 W, std::initializer_list<uint32> Arrays)
	{
		Desc[k][0] = W > 0 ? 1u : 0u;
		Desc[k][1] = W;
		uint32 a = 4;
		for (const uint32 x : Arrays)
		{
			if (x > 0 && a < 8)
			{
				Desc[k][a++] = x;
			}
		}
	};
	const uint32 Material = Normals + Emission + PbrWords + LobesWords;
	Set(0, 8, { 4, 4 });
	Set(1, SHWords, { SHWords });
	Set(3, Material, { Normals, Emission, PbrWords, LobesWords });
	if (bTransfer)
	{
		Set(4, ShadowWords, { ShadowWords });
		Set(5, TransferWords, { TransferWords });
	}
	if (bTransfer && bCurvature)
	{
		Set(8, CurvatureWords, { CurvatureWords });
	}
	// A block for the splats, then one a merged level: its groups follow the
	// splats in the asset, decoded as they are and cut every frame
	// (GSplatCulling.usf), as a .athc's levels.
	TArray<uint32> Blocks;
	auto AddBlock = [&](uint32 First, uint32 N, const TArray<uint32>& Bases, uint32 TransferBase)
	{
		TArray<uint32> Block;
		Block.SetNumZeroed(kBlockWords);
		Block[0] = First;
		Block[1] = N;
		for (uint32 k = 0; k < kSections; k++)
		{
			Block[2 + k * 3] = kNone;
		}
		auto Section = [&](uint32 k, uint32 Base)
		{
			Block[2 + k * 3] = 0;
			Block[3 + k * 3] = kScratchWords;
			Block[4 + k * 3] = Base;
		};
		Section(0, Bases[0]);             // positions, then shape
		Section(1, Bases[2]);             // harmonics
		Section(3, Bases[4]);             // normals, emission, pbr, lobes
		Section(4, Bases[9]);             // open directions
		Section(5, TransferBase);         // the transfer, expanded where it is CPCA
		Section(8, Bases[10]);            // the curvature
		Blocks.Append(Block);
	};
	AddBlock(0, Count, Leaf, bCpca ? CpcaOut : Leaf[8]);
	uint32 Groups = 0;
	TArray<uint32> LevelFirst;
	for (int32 l = 0; l < Levels.Num(); l++)
	{
		LevelFirst.Add(Count + Groups);
		AddBlock(Count + Groups, Levels[l].Groups, FieldBases[l], bCpca && bTransfer ? LevelCpcaOut[l] : FieldBases[l][8]);
		Groups += Levels[l].Groups;
	}
	const uint32 Total = Count + Groups;
	const uint32 TableAt = uint32(Bytes.Num());
	Bytes.Append(reinterpret_cast<const uint8*>(Desc), sizeof(Desc));
	Bytes.Append(reinterpret_cast<const uint8*>(Blocks.GetData()), Blocks.Num() * 4);
	// What GSAthzLodCS writes the levels' words from: the rows, then a row a
	// block -- first element, elements, its word (kLodLeaf for the splats, the
	// level and kLodCoarsest for the coarsest), where its tail is in Scratch
	// (a splat's finest group, a group's octree cell).
	const uint32 LodAt = uint32(Bytes.Num());
	{
		TArray<uint32> Rows = { uint32(Levels.Num()) + 1u, Total, Count, 0x80000000u, Leaf[3] };
		uint32 Coarsest = ~0u;
		for (const FLevel& L : Levels)
		{
			Coarsest = FMath::Min(Coarsest, L.Level);
		}
		for (int32 l = 0; l < Levels.Num(); l++)
		{
			Rows.Append({ LevelFirst[l], Levels[l].Groups, Levels[l].Level | (Levels[l].Level == Coarsest ? 0x40000000u : 0u), FieldBases[l][3] });
		}
		Rows.Insert(0u, 2);   // the splats' first element
		Bytes.Append(reinterpret_cast<const uint8*>(Rows.GetData()), Rows.Num() * 4);
	}

	ImportData.Swizzle = ImportData.bFileZUp
		? TArray<EGSplatSwizzleAxis>{ EGSplatSwizzleAxis::X, EGSplatSwizzleAxis::Yn, EGSplatSwizzleAxis::Z }
		: TArray<EGSplatSwizzleAxis>{ EGSplatSwizzleAxis::X, EGSplatSwizzleAxis::Z, EGSplatSwizzleAxis::Y };
	ImportData.ScaleFactor = 100.0f;
	ImportData.bApplyGammaCorrection = (Flags & kFlagLinear) == 0;
	ImportData.NumSplats = int32(Count);
	ImportData.bHasHighOrderSH = RestPerColour > 0;
	ImportData.bHasNormals = false;
	ImportData.bHasMetallicRoughness = false;

	Splats.Empty();
	Splats.bPacked = true;
	Splats.PackedFormat = 6;
	Splats.SplatCount = Total;
	Splats.bHasHighOrderSH = ImportData.bHasHighOrderSH && ImportData.bImportHighOrderSH;
	Splats.PackedUnitsToCm = ImportData.ScaleFactor;
	Splats.PackedBytes = MoveTemp(Bytes);
	// The .athc flags the decoders read (normals bit 0, linear 1, emission 2, material 4, transfer 5).
	const uint32 AthcFlags = (Normals ? 1u : 0u) | ((Flags & kFlagLinear) ? 2u : 0u) | (Emission ? 4u : 0u)
		| (PbrWords > 0 ? 16u : 0u) | (bTransfer ? 32u : 0u);
	Splats.PackedStreams = { TableAt, 1u + uint32(Levels.Num()), SHWords, Splats.bHasHighOrderSH ? RestPerColour : 0u,
		uint32(Scratch), bTransfer ? TransferCount : 0u, AthcFlags, 0u, LodAt,
		// [9], [10]: the splats' influences and gradients in the scratch (layer K).
		bSkin ? FieldBases.Last()[11] : 0u, bSkin && GradientWords == 3 ? FieldBases.Last()[12] : 0u,
		// [11]: the splats, the first elements (the levels' groups after them).
		Count };
	Splats.PackedSpz = FIntVector(int32(HeadAt), int32(CpcaAt), bCpca && bTransfer ? 1 + Levels.Num() : 0);
	// The levels' words are written on the device (GSAthzLodCS): the cut's
	// octree is the file's, the finest level's groups after the coarser ones.
	if (Levels.Num() > 0)
	{
		const FLevel* Finest = &Levels[0];
		int32 FinestIndex = 0;
		for (int32 l = 0; l < Levels.Num(); l++)
		{
			if (Levels[l].Level > Finest->Level)
			{
				Finest = &Levels[l];
				FinestIndex = l;
			}
		}
		Splats.LodLevels = uint32(Levels.Num());
		Splats.LodFinestLevel = Finest->Level;
		Splats.LodFinestBase = LevelFirst[FinestIndex];
		// Each level's run, by level number (the morph's parents).
		Splats.LodLevelRuns.Init(0u, FGaussianSplatData::kLodLevelRuns * 2);
		for (int32 l = 0; l < Levels.Num(); l++)
		{
			if (Levels[l].Level < uint32(FGaussianSplatData::kLodLevelRuns))
			{
				Splats.LodLevelRuns[int32(Levels[l].Level) * 2] = LevelFirst[l];
				Splats.LodLevelRuns[int32(Levels[l].Level) * 2 + 1] = Levels[l].Groups;
			}
		}
		Splats.LodBoundsLo = FVector3f(BoundsLo[0], BoundsLo[1], BoundsLo[2]);
		Splats.LodExtent = Extent;
	}
	// A zonal transfer (10 values: two lobes in the gaussian's frame) is read
	// as the .athc's is, records of nine with its ten values at the start.
	const uint32 Direct = (TransferCount == 9 || TransferCount == 10 || TransferCount == 36 || TransferCount == 84) ? 9u
		: (TransferCount == 16 || TransferCount == 64 || TransferCount == 112) ? 16u : 0u;
	if (bTransfer && Direct > 0)
	{
		Splats.bHasTransfer = true;
		Splats.TransferDirectCount = Direct;
		Splats.CellWords = (ShadowWords == 8 || ShadowWords == 32) ? ShadowWords : 0u;
		Splats.bPackedTransferField = Direct == 16 && TransferCount == 112;
		Splats.FileUnitsToCm = 100.0f;
		Splats.bPackedCurvature = bCurvature;
		Splats.bPackedEmission = Emission != 0;
		Splats.bTransferZonal = TransferCount == 10;
	}
	// LAYER K: A SKINNED CLOUD (athenea's readAthzSkinned). The splats'
	// four (joint, weight) pairs and their gradients are decoded with them
	// and lifted on the device; the frames' block -- the geometry's bind
	// transform (a USD row matrix), each frame's time code, and every joint's
	// matrix in the skinner's layout (column vectors, its three rows in
	// halves) -- is read here into the rows the USD reader makes: C = X G,
	// C[r][c] = sum_k X[r][k] G[c][k]. The file keeps time codes and no rate:
	// they are taken at 30 a second (SkinRate scales it).
	if (bSkin)
	{
		const uint8* K = Temporal.GetData();
		float G[4][4];
		FMemory::Memcpy(G, K, 64);
		TArray<double> Codes;
		Codes.SetNumUninitialized(int32(SkinFrames));
		FMemory::Memcpy(Codes.GetData(), K + 64, uint64(SkinFrames) * 8);
		const uint16* H = reinterpret_cast<const uint16*>(K + 64 + uint64(SkinFrames) * 8);
		Splats.SkinJoints = SkinJoints;
		Splats.SkinPerSplat = 4;
		Splats.bSkinFromDecode = true;
		for (uint32 f = 0; f < SkinFrames; f++)
		{
			Splats.SkinTimes.Add(float((Codes[int32(f)] - Codes[0]) / 30.0));
			for (uint32 j = 0; j < SkinJoints; j++)
			{
				const uint16* M = H + (uint64(f) * SkinJoints + j) * 12;
				for (int32 r = 0; r < 3; r++)
				{
					float X[4];
					for (int32 c = 0; c < 4; c++)
					{
						FFloat16 Half;
						Half.Encoded = M[r * 4 + c];
						X[c] = float(Half);
					}
					float Row[4];
					for (int32 c = 0; c < 4; c++)
					{
						Row[c] = X[0] * G[c][0] + X[1] * G[c][1] + X[2] * G[c][2] + X[3] * G[c][3];
					}
					Splats.SkinXforms.Add(FVector4f(Row[0], Row[1], Row[2], Row[3]));
				}
			}
		}
	}
	const TArray<EGSplatSwizzleAxis>& Swizzle = ImportData.Swizzle;
	Splats.FileAxisX = SwizzleVector(1.0f, 0.0f, 0.0f, Swizzle);
	Splats.FileAxisY = SwizzleVector(0.0f, 1.0f, 0.0f, Swizzle);
	Splats.FileAxisZ = SwizzleVector(0.0f, 0.0f, 1.0f, Swizzle);
	Splats.bLinearizeColor = ImportData.bApplyGammaCorrection;
	FBox Box(ForceInit);
	Box += FVector(SwizzleVector(BoundsMin[0], BoundsMin[1], BoundsMin[2], Swizzle) * ImportData.ScaleFactor);
	Box += FVector(SwizzleVector(BoundsMax[0], BoundsMax[1], BoundsMax[2], Swizzle) * ImportData.ScaleFactor);
	Splats.SetBounds(FBoxSphereBounds(Box));

	UE_LOG(LogGSplatImport, Display, TEXT("%s: %u gaussians, .athz version %u, %d merged levels (%u groups) cut each frame by r.Gaussians.Athenea.LodPixels, %u pages, transfer %s, curvature %s, CRCs checked."),
		*Filename, Count, Version, Levels.Num(), Groups, LeafFirstPage + LeafPages,
		bTransfer ? *FString::Printf(TEXT("of %u values%s"), TransferCount, bCpca ? *FString::Printf(TEXT(" as CPCA codes (%u clusters)"), CpcaClusters) : TEXT("")) : TEXT("none"),
		bCurvature && bTransfer ? TEXT("read") : TEXT("none"));
	if (bSkin)
	{
		UE_LOG(LogGSplatImport, Display, TEXT("%s: layer K, carried by %u joints, 4 influences a gaussian%s, %u poses over %.2f s (time codes at 30 a second); its merged levels are not drawn."),
			*Filename, SkinJoints, GradientWords == 3 ? TEXT(" with their gradients") : TEXT(""), SkinFrames, Splats.SkinTimes.Last());
	}
	return true;
}

