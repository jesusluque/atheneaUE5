// Copyright (c) 2026 jesus luque.
//
// A .athc, athenea's chunked container (modules/lod/src/Athc.cpp), as a packed
// asset: the finest level, every chunk's positions, shape and harmonic words
// copied as they are. The GPU decodes them when the resource is made
// (GSDecode.usf). Merged levels, normals, emission and the extras are not read
// yet.
//
// Version 3 (sparkwebGPU's, for the web; docs/docs/athc-v3.md there) is the
// same values in sections, each gzipped or not and stored as v2's words, as
// byte planes or as byte planes of 16-bit differences. Its chunks' CORE and
// SHRS sections are kept as stored (after the gunzip) with a table of blocks,
// and the GPU reads them in any of the three (PackedFormat 5,
// GSDecodeAthc3CS). The levels, the material, the transfer and the rest are
// not read yet.
//
//   page 0        header (104 bytes: FileHeader below)
//   page 1..      level table, chunk table (offset, count, pad a chunk)
//   each chunk    positions (16 B a splat), shape (16 B), SH (4 * shWords B),
//                 then the finest group, normals, emission, extras

#include "GSplatRuntimeImport.h"
#include "GSplatImportData.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"


#include "AtheneaCodecs.h"
#include "GaussianData.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

namespace
{
	struct FAthcHeader
	{
		char     Magic[4];
		uint32   Version;
		uint32   Count;
		uint32   RestPerColour;
		uint32   SHWords;
		uint32   Levels;
		uint32   ChunkSplats;
		uint32   Chunks;
		uint32   FinestGroups;
		float    BoundsLo[3];
		float    Extent;
		float    BoundsMin[3];
		float    BoundsMax[3];
		uint32   Flags;
		uint64   LevelTable;
		uint64   ChunkTable;
		uint64   Starts;
	};
	static_assert(sizeof(FAthcHeader) == 104, "the .athc header is 104 bytes");

	struct FAthcLevel
	{
		uint32 Level;
		uint32 Groups;
		uint64 Offset;
	};
	static_assert(sizeof(FAthcLevel) == 16, "a .athc level entry is 16 bytes");

	struct FAthcChunk
	{
		uint64 Offset;
		uint32 Count;
		uint32 Pad;
	};
	static_assert(sizeof(FAthcChunk) == 16, "a .athc chunk entry is 16 bytes");

	constexpr uint32 kAthcLinear = 2;

	bool ReadAt(IFileHandle& File, uint64 Offset, void* Out, uint64 Bytes)
	{
		return File.Seek(int64(Offset)) && File.Read(static_cast<uint8*>(Out), int64(Bytes));
	}
}

namespace GSplatAthc3
{
	uint32 W32(const uint8* b) { return uint32(b[0]) | (uint32(b[1]) << 8) | (uint32(b[2]) << 16) | (uint32(b[3]) << 24); }
	uint64 W64(const uint8* b) { return uint64(W32(b)) | (uint64(W32(b + 4)) << 32); }
	float F32(const uint8* b) { const uint32 v = W32(b); float f; FMemory::Memcpy(&f, &v, 4); return f; }
	constexpr uint32 Tag(const char* s) { return uint32(uint8(s[0])) | (uint32(uint8(s[1])) << 8) | (uint32(uint8(s[2])) << 16) | (uint32(uint8(s[3])) << 24); }
	constexpr uint32 kKindChunk = 1;
	constexpr uint32 kKindPage = 2;
	constexpr uint32 kSections = 9;
	constexpr uint32 kAthcNormals = 1;
	constexpr uint32 kAthcEmission = 4;
	constexpr uint32 kAthcTransfer = 32;

	bool Import(IFileHandle& File, const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats)
	{
		auto Fail = [&](const FString& Why)
		{
			UE_LOG(LogGSplatImport, Error, TEXT("%s: %s"), *Filename, *Why);
			return false;
		};
		uint8 H[176];
		FMemory::Memzero(H);
		if (!ReadAt(File, 0, H, 160))
		{
			return Fail(TEXT("a version 3 header cut short."));
		}
		if (W32(H + 4) != 3)
		{
			return Fail(FString::Printf(TEXT("ATH3 version %u, this reads 3."), W32(H + 4)));
		}
		const uint32 Flags = W32(H + 8), Count = W32(H + 12), RestPerColour = W32(H + 32), SHWords = W32(H + 36);
		const uint32 TransferCount = W32(H + 40), ShadowWords = W32(H + 44), PbrWords = W32(H + 48), LobesWords = W32(H + 52);
		const uint32 SectionCount = W32(H + 60), BlockCount = W32(H + 104);
		const uint64 SectionTable = W64(H + 112), BlockIndex = W64(H + 120);
		if (Count == 0 || RestPerColour > 15 || SHWords != FMath::Max((RestPerColour * 3 + 1) / 2, 1u) || SectionCount == 0 || SectionCount > 64 || BlockCount == 0)
		{
			return Fail(FString::Printf(TEXT("a version 3 header that does not add up (%u splats, %u rest, %u words, %u sections, %u blocks)."),
				Count, RestPerColour, SHWords, SectionCount, BlockCount));
		}

		// The sections read, in the order GSDecode.usf names them; a TX cloud's
		// material and transfer only where the transfer is one the plugin draws
		// (9 or 16 direct values, with or without the indirect half and the
		// field; not a zonal one).
		static const char* kRead[kSections] = { "CORE", "SHRS", "TREE", "MATL", "SHAD", "TXDI", "TXIN", "TXFD", "CURV" };
		const uint32 Direct = (TransferCount == 9 || TransferCount == 36 || TransferCount == 84) ? 9u
			: (TransferCount == 16 || TransferCount == 64 || TransferCount == 112) ? 16u : 0u;
		const bool bTransfer = Direct > 0 && (Flags & kAthcTransfer) != 0;
		TArray<uint8> Table;
		Table.SetNumUninitialized(int32(SectionCount) * 32);
		if (!ReadAt(File, SectionTable, Table.GetData(), Table.Num()))
		{
			return Fail(TEXT("the section table is cut short."));
		}
		int32 Index[kSections];
		uint32 Words[kSections], Encoding[kSections], Compression[kSections];
		for (uint32 k = 0; k < kSections; k++)
		{
			Index[k] = INDEX_NONE;
			Words[k] = Encoding[k] = Compression[k] = 0;
		}
		for (uint32 s = 0; s < SectionCount; s++)
		{
			const uint8* E = &Table[s * 32];
			for (uint32 k = 0; k < kSections; k++)
			{
				if (W32(E) == Tag(kRead[k]))
				{
					Index[k] = int32(s);
					Encoding[k] = W32(E + 8);
					Compression[k] = W32(E + 12);
					Words[k] = W32(E + 16);
				}
			}
		}
		if (!bTransfer)
		{
			for (uint32 k = 3; k < kSections; k++)
			{
				Index[k] = INDEX_NONE;
			}
		}
		if (Index[0] == INDEX_NONE || Words[0] != 9)
		{
			return Fail(Index[0] == INDEX_NONE ? FString(TEXT("no CORE section."))
				: FString::Printf(TEXT("CORE of %u words a splat, where 9 were due."), Words[0]));
		}
		for (uint32 k = 0; k < kSections; k++)
		{
			const bool bCpcaAllowed = k >= 5;
			if (Index[k] != INDEX_NONE && ((Encoding[k] > 2 && !(bCpcaAllowed && Encoding[k] == 3)) || Compression[k] > 1))
			{
				return Fail(FString::Printf(TEXT("section %hs in encoding %u, compression %u, which this does not read."), kRead[k], Encoding[k], Compression[k]));
			}
		}

		// Each section's arrays (athc_v3.rs section_arrays).
		uint32 Arrays[kSections][4];
		FMemory::Memzero(Arrays);
		Arrays[0][0] = 4; Arrays[0][1] = 4; Arrays[0][2] = 1;
		Arrays[2][0] = 1; Arrays[2][1] = 1;
		{
			uint32 a = 0;
			for (const uint32 w : { (Flags & kAthcNormals) ? 1u : 0u, (Flags & kAthcEmission) ? 1u : 0u, PbrWords, LobesWords })
			{
				if (w > 0 && a < 4)
				{
					Arrays[3][a++] = w;
				}
			}
			if (Arrays[3][0] + Arrays[3][1] + Arrays[3][2] + Arrays[3][3] != Words[3])
			{
				FMemory::Memzero(Arrays[3]);
				Arrays[3][0] = Words[3];
			}
		}
		for (const uint32 k : { 1u, 4u, 5u, 6u, 7u, 8u })
		{
			Arrays[k][0] = Words[k];
		}
		if (Words[2] != 2)
		{
			FMemory::Memzero(Arrays[2]);
			Arrays[2][0] = Words[2];
		}

		// The blocks: each chunk's or page's sections, gunzipped, in a row.
		const uint64 Entry = 32 + 16 * uint64(SectionCount);
		TArray<uint8> BlockIndexBytes;
		BlockIndexBytes.SetNumUninitialized(int64(BlockCount) * int64(Entry));
		if (!ReadAt(File, BlockIndex, BlockIndexBytes.GetData(), BlockIndexBytes.Num()))
		{
			return Fail(TEXT("the block index is cut short."));
		}
		TArray<uint32> Blocks;
		TArray<uint8> Data;
		uint32 Done = 0, Used = 0, Pages = 0, ScratchBytes = 0;
		// A paged tree's pages in order (the TREE words name them by it): each
		// one's first splat and its TREE words, page then place|count<<16.
		TArray<uint32> PageStart;
		TArray<TArray<uint32>> PageTree;
		bool bTreeRead = true;
		bool bCpca = false, bDelta = false;
		TArray<uint8> Stored, Raw;
		for (uint32 b = 0; b < BlockCount; b++)
		{
			const uint8* E = &BlockIndexBytes[b * Entry];
			// Chunks (kind 1) hold the finest splats; a paged file's pages (kind
			// 2) hold its whole tree, merges and leaves, TREE telling them apart.
			// A merged level (kind 0) is not read yet.
			const uint32 Kind = W32(E);
			const uint32 n = W32(E + 12);
			if ((Kind != kKindChunk && Kind != kKindPage) || n == 0)
			{
				continue;
			}
			Blocks.Add(Done);
			Blocks.Add(n);
			if (Kind == kKindPage)
			{
				PageStart.Add(Done);
				PageTree.AddDefaulted();
			}
			for (uint32 k = 0; k < kSections; k++)
			{
				const int32 s = Index[k];
				if (s == INDEX_NONE || (k == 2 && Kind != kKindPage))
				{
					Blocks.Append({ ~0u, 0u, 0u });
					continue;
				}
				const uint8* Span = E + 32 + 16 * s;
				const uint64 At = W64(Span);
				const uint32 StoredBytes = W32(Span + 8), RawBytes = W32(Span + 12);
				if (Encoding[k] != 3 && RawBytes != n * Words[k] * 4)
				{
					return Fail(FString::Printf(TEXT("a block's section %hs holds %u bytes where %u were due."), kRead[k], RawBytes, n * Words[k] * 4));
				}
				Stored.SetNumUninitialized(StoredBytes);
				if (!ReadAt(File, At, Stored.GetData(), StoredBytes))
				{
					return Fail(TEXT("a block runs past the file."));
				}
				const TArray<uint8>* Bytes = &Stored;
				if (Compression[k] == 1)
				{
					Raw.Reset();
					// A CPCA section's raw bytes are its decoded halves; its payload,
					// what the gunzip gives, is smaller.
					if (!AtheneaCodecs::Gunzip(Stored.GetData(), Stored.Num(), Raw) || (Encoding[k] != 3 && Raw.Num() != int32(RawBytes)))
					{
						return Fail(FString::Printf(TEXT("a block's section %hs does not gunzip to its %u bytes."), kRead[k], RawBytes));
					}
					Bytes = &Raw;
				}
				if (k == 2 && Kind == kKindPage)
				{
					// The TREE words, as plain words or byte planes; delta-coded
					// ones leave the cut out. Its two arrays of one word are cut
					// from a run of whole rows (as GSDecode.usf's Athc3Word reads
					// them): the run's word r is array r / n's element r % n, and
					// node e's words are the run's 2e and 2e + 1.
					TArray<uint32>& T = PageTree.Last();
					const TArray<uint8>& R = *Bytes;
					if ((Encoding[2] == 0 || Encoding[2] == 1) && Words[2] == 2 && R.Num() >= int32(n * 8))
					{
						T.SetNumZeroed(int32(n * 2));
						for (uint32 r = 0; r < 2 * n; r++)
						{
							const uint32 Arr = r / n, i = r % n;
							uint32 v = 0;
							for (uint32 q = 0; q < 4; q++)
							{
								const uint32 Byte = Encoding[2] == 0 ? (Arr * n + i) * 4 + q : Arr * n * 4 + q * n + i;
								v |= uint32(R[int32(Byte)]) << (8 * q);
							}
							T[int32(r)] = v;
						}
					}
					else
					{
						bTreeRead = false;
					}
				}
				const uint32 SectionAt = uint32(Data.Num());
				Data.Append(*Bytes);
				Data.SetNumZeroed(Align(Data.Num(), 4));
				uint32 ScratchAt = 0;
				if (Encoding[k] >= 2)
				{
					ScratchAt = ScratchBytes;
					ScratchBytes += n * Words[k] * 4;
					bDelta |= Encoding[k] == 2;
					bCpca |= Encoding[k] == 3;
				}
				Blocks.Append({ SectionAt, Encoding[k], ScratchAt });
			}
			Blocks.Append({ 0u, 0u, 0u });   // to 32 words (ATHC3_BLOCK_WORDS)
			Done += n;
			Used++;
			Pages += Kind == kKindPage ? 1 : 0;
		}
		if (Done != Count)
		{
			return Fail(FString::Printf(TEXT("its chunks or pages hold %u splats where the header says %u."), Done, Count));
		}
		if (Used > 65535 || uint64(Data.Num()) + uint64(ScratchBytes) > 0xFFFFFFFFull)
		{
			return Fail(FString::Printf(TEXT("%u blocks, more than one dispatch takes, or past 4 GB."), Used));
		}

		// The descriptors, then the blocks.
		const uint32 TableAt = uint32(Data.Num());
		TArray<uint32> Desc;
		for (uint32 k = 0; k < kSections; k++)
		{
			const bool bPresent = Index[k] != INDEX_NONE && !(k == 2 && Pages == 0);
			Desc.Append({ bPresent ? 1u : 0u, Words[k], 0u, 0u, Arrays[k][0], Arrays[k][1], Arrays[k][2], Arrays[k][3] });
		}
		Data.Append(reinterpret_cast<const uint8*>(Desc.GetData()), Desc.Num() * sizeof(uint32));
		Data.Append(reinterpret_cast<const uint8*>(Blocks.GetData()), Blocks.Num() * sizeof(uint32));

		ImportData.Swizzle = { EGSplatSwizzleAxis::X, EGSplatSwizzleAxis::Z, EGSplatSwizzleAxis::Y };
		ImportData.ScaleFactor = 100.0f;
		ImportData.bApplyGammaCorrection = (Flags & kAthcLinear) == 0;
		ImportData.NumSplats = int32(Count);
		ImportData.bHasHighOrderSH = RestPerColour > 0 && Index[1] != INDEX_NONE;
		ImportData.bHasNormals = false;
		ImportData.bHasMetallicRoughness = false;

		Splats.Empty();
		Splats.bPacked = true;
		Splats.PackedFormat = 5;
		Splats.SplatCount = Count;
		Splats.bHasHighOrderSH = ImportData.bHasHighOrderSH && ImportData.bImportHighOrderSH;
		Splats.PackedUnitsToCm = ImportData.ScaleFactor;
		Splats.PackedBytes = MoveTemp(Data);
		Splats.PackedStreams = { TableAt, Used, SHWords, Splats.bHasHighOrderSH ? RestPerColour : 0u,
			ScratchBytes, bTransfer ? TransferCount : 0u, Flags, (bDelta ? 1u : 0u) | (bCpca ? 2u : 0u) };
		if (bTransfer)
		{
			// The transfer's buffers are written on the device (GSDecodeAthc3TxCS).
			Splats.bHasTransfer = true;
			Splats.TransferDirectCount = Direct;
			Splats.CellWords = (Index[4] != INDEX_NONE && (ShadowWords == 8 || ShadowWords == 32)) ? ShadowWords : 0u;
			Splats.bPackedTransferField = Direct == 16 && TransferCount == 112;
			Splats.bPackedCurvature = Index[8] != INDEX_NONE && Words[8] == 2;
			Splats.FileUnitsToCm = 100.0f;
		}
		// A paged tree's cut, as athenea's lod_tree_cut makes it: each node's
		// parent from its parent's TREE words (the children's page and place,
		// and their count), and whether it is a leaf. The culling pass draws a
		// node that is a leaf or narrow enough on screen and whose parent is
		// not (GSplatCulling.usf); the merges keep their opacity (bit 4 of
		// PackedStreams[7]).
		uint32 TreeNodes = 0;
		if (Pages > 0 && bTreeRead && PageTree.Num() == PageStart.Num())
		{
			TArray<uint32> LodWords;
			LodWords.Init(~0u, int32(Count) * 2);
			for (uint32 i = 0; i < Count; i++)
			{
				LodWords[int32(i * 2 + 1)] = FGaussianSplatData::kLodTree | FGaussianSplatData::kLodLeaf;
			}
			bool bFits = true;
			for (int32 p = 0; p < PageTree.Num() && bFits; p++)
			{
				const TArray<uint32>& T = PageTree[p];
				for (int32 i = 0; i * 2 + 1 < T.Num(); i++)
				{
					const uint32 Node = PageStart[p] + uint32(i);
					const uint32 ChildPage = T[i * 2], Place = T[i * 2 + 1] & 0xFFFFu, Children = T[i * 2 + 1] >> 16;
					if (Children == 0)
					{
						continue;
					}
					if (ChildPage >= uint32(PageStart.Num()) || Node >= Count)
					{
						bFits = false;
						break;
					}
					LodWords[int32(Node * 2 + 1)] = FGaussianSplatData::kLodTree;
					TreeNodes++;
					for (uint32 c = 0; c < Children; c++)
					{
						const uint32 Child = PageStart[int32(ChildPage)] + Place + c;
						if (Child < Count)
						{
							LodWords[int32(Child * 2)] = Node;
						}
					}
				}
			}
			if (bFits && TreeNodes > 0)
			{
				Splats.LodWords = MoveTemp(LodWords);
				Splats.LodLevels = 1;
				Splats.PackedStreams[7] |= 4u;
			}
			else
			{
				TreeNodes = 0;
			}
		}

		const TArray<EGSplatSwizzleAxis>& Swizzle = ImportData.Swizzle;
		Splats.FileAxisX = SwizzleVector(1.0f, 0.0f, 0.0f, Swizzle);
		Splats.FileAxisY = SwizzleVector(0.0f, 1.0f, 0.0f, Swizzle);
		Splats.FileAxisZ = SwizzleVector(0.0f, 0.0f, 1.0f, Swizzle);
		Splats.bLinearizeColor = ImportData.bApplyGammaCorrection;

		FBox Box(ForceInit);
		Box += FVector(SwizzleVector(F32(H + 80), F32(H + 84), F32(H + 88), Swizzle) * ImportData.ScaleFactor);
		Box += FVector(SwizzleVector(F32(H + 92), F32(H + 96), F32(H + 100), Swizzle) * ImportData.ScaleFactor);
		Splats.SetBounds(FBoxSphereBounds(Box));

		const FString TransferText = bTransfer
			? FString::Printf(TEXT("of %u values%s%s"), TransferCount, bCpca ? TEXT(", CPCA") : TEXT(""), Splats.CellWords ? TEXT(", cells") : TEXT(""))
			: TransferCount > 0 ? FString::Printf(TEXT("of %u values not drawn (zonal or another layout)"), TransferCount) : FString(TEXT("none"));
		UE_LOG(LogGSplatImport, Display, TEXT("%s: %u gaussians in %u blocks%s, .athc version 3 (CORE encoding %u), %u rest coefficients a colour, %s colour, transfer %s; the levels not read."),
			*Filename, Count, Used, TreeNodes > 0 ? *FString::Printf(TEXT(" (a paged tree of %u merges, cut each frame by r.Gaussians.Athenea.LodPixels)"), TreeNodes)
				: Pages > 0 ? TEXT(" (error-band pages: the merges drawn at no opacity, the leaves alone seen)") : TEXT(""),
			Encoding[0], RestPerColour, (Flags & kAthcLinear) ? TEXT("linear") : TEXT("sRGB"), *TransferText);
		return true;
	}

	// A version 2 TX cloud as format 5: each chunk's bytes as the file has them,
	// its arrays described where v2 lays them -- positions, shape, harmonics,
	// tail, then normals, emission, pbr, lobes (MATL), the transfer's halves
	// (one TXDI of all its words) and the open cells (SHAD).
	bool FromV2(IFileHandle& File, const FString& Filename, const FAthcHeader& H, const uint32* Extra, const TArray<FAthcChunk>& Chunks,
		UGSplatImportData& ImportData, FGaussianSplatData& Splats)
	{
		auto Fail = [&](const FString& Why)
		{
			UE_LOG(LogGSplatImport, Error, TEXT("%s: %s"), *Filename, *Why);
			return false;
		};
		const uint32 PbrWords = Extra[0], LobesWords = Extra[1], TransferCount = Extra[2], TransferWords = Extra[3], ShadowWords = Extra[4];
		const uint32 Direct = (TransferCount == 9 || TransferCount == 36 || TransferCount == 84) ? 9u
			: (TransferCount == 16 || TransferCount == 64 || TransferCount == 112) ? 16u : 0u;
		if (Direct == 0 || TransferWords != (TransferCount + 1) / 2)
		{
			return Fail(FString::Printf(TEXT("a transfer of %u values in %u words, which is not drawn."), TransferCount, TransferWords));
		}
		const uint32 Normals = (H.Flags & kAthcNormals) ? 1u : 0u, Emission = (H.Flags & kAthcEmission) ? 1u : 0u;
		const uint32 MaterialWords = Normals + Emission + ((H.Flags & 16u) ? PbrWords + LobesWords : 0u);
		const uint32 Row = 4 + 4 + H.SHWords + 1 + MaterialWords + TransferWords + ShadowWords;

		// The merged levels: blocks as the chunks are, a group's octree cell
		// where a splat has its finest group (the word after the harmonics).
		// They follow the splats, decoded as they are, and are cut each frame.
		TArray<FAthcLevel> Levels;
		if (H.Levels > 0 && H.LevelTable > 0)
		{
			Levels.SetNumUninitialized(H.Levels);
			if (!ReadAt(File, H.LevelTable, Levels.GetData(), uint64(H.Levels) * sizeof(FAthcLevel)))
			{
				Levels.Empty();
			}
		}
		uint32 FinestLevel = 0, CoarsestLevel = ~0u;
		uint64 Groups = 0;
		for (const FAthcLevel& L : Levels)
		{
			Groups += L.Groups;
			FinestLevel = FMath::Max(FinestLevel, L.Level);
			CoarsestLevel = FMath::Min(CoarsestLevel, L.Level);
		}
		const FAthcLevel* Finest = Levels.FindByPredicate([&](const FAthcLevel& L) { return L.Level == FinestLevel; });
		if (Levels.Num() > 0 && (Finest == nullptr || Finest->Groups != H.FinestGroups))
		{
			UE_LOG(LogGSplatImport, Warning, TEXT("%s: levels that do not add up; the splats alone are read."), *Filename);
			Levels.Empty();
			Groups = 0;
		}
		TArray<uint32> LodWords;
		uint32 FinestBase = 0;

		TArray<uint8> Data;
		TArray<uint32> Blocks;
		uint32 Done = 0;
		// Every block: the chunks' splats, then the levels' groups.
		struct FBlockSource { uint64 Offset; uint32 Count; uint32 Level; bool bLevel; };
		TArray<FBlockSource> Sources;
		for (int32 c = 0; c < Chunks.Num(); c++)
		{
			Sources.Add({ Chunks[c].Offset, Chunks[c].Count, 0u, false });
		}
		for (const FAthcLevel& L : Levels)
		{
			Sources.Add({ L.Offset, L.Groups, L.Level, true });
		}
		for (int32 c = 0; c < Sources.Num(); c++)
		{
			const uint32 n = Sources[c].Count;
			const uint32 At = uint32(Data.Num());
			Data.AddUninitialized(int64(n) * Row * 4);
			if (!ReadAt(File, Sources[c].Offset, Data.GetData() + At, uint64(n) * Row * 4))
			{
				return Fail(FString::Printf(TEXT("block %d is cut short."), c));
			}
			const uint32 Sh = At + n * 32, Matl = Sh + n * 4 * (H.SHWords + 1), Tx = Matl + n * 4 * MaterialWords, Shad = Tx + n * 4 * TransferWords;
			if (Levels.Num() > 0)
			{
				// The tail: a splat's finest group (leaf), or a group's cell and level.
				const uint32* TailWords = reinterpret_cast<const uint32*>(Data.GetData() + Sh + n * 4 * H.SHWords);
				if (Sources[c].bLevel && Sources[c].Level == FinestLevel)
				{
					FinestBase = Done;
				}
				const uint32 Mark = Sources[c].bLevel
					? (Sources[c].Level | (Sources[c].Level == CoarsestLevel ? FGaussianSplatData::kLodCoarsest : 0u))
					: FGaussianSplatData::kLodLeaf;
				for (uint32 i = 0; i < n; i++)
				{
					LodWords.Add(TailWords[i]);
					LodWords.Add(Mark);
				}
			}
			Blocks.Append({ Done, n,
				At, 0u, 0u,                                       // CORE: positions, shape
				Sh, 0u, 0u,                                       // SHRS
				~0u, 0u, 0u,                                      // TREE: none
				MaterialWords ? Matl : ~0u, 0u, 0u,               // MATL
				ShadowWords ? Shad : ~0u, 0u, 0u,                 // SHAD
				Tx, 0u, 0u,                                       // TXDI: the whole transfer
				~0u, 0u, 0u, ~0u, 0u, 0u,                         // TXIN, TXFD: in TXDI
				~0u, 0u, 0u,                                      // CURV: not read from a v2
				0u, 0u, 0u });
			Done += n;
		}
		if (Done != H.Count + Groups || uint64(Data.Num()) > 0xFFFFFFF0ull)
		{
			return Fail(FString::Printf(TEXT("its chunks hold %u splats where the header says %u, or past 4 GB."), uint32(Done - Groups), H.Count));
		}

		uint32 Desc[kSections][8];
		FMemory::Memzero(Desc);
		auto Set = [&](uint32 k, uint32 Words, std::initializer_list<uint32> Arrays)
		{
			Desc[k][0] = Words > 0 ? 1u : 0u;
			Desc[k][1] = Words;
			uint32 a = 4;
			for (const uint32 w : Arrays)
			{
				if (w > 0 && a < 8)
				{
					Desc[k][a++] = w;
				}
			}
		};
		Set(0, 8, { 4, 4 });
		Set(1, H.SHWords, { H.SHWords });
		Set(3, MaterialWords, { Normals, Emission, (H.Flags & 16u) ? PbrWords : 0u, (H.Flags & 16u) ? LobesWords : 0u });
		Set(4, ShadowWords, { ShadowWords });
		Set(5, TransferWords, { TransferWords });
		const uint32 TableAt = uint32(Data.Num());
		Data.Append(reinterpret_cast<const uint8*>(Desc), sizeof(Desc));
		Data.Append(reinterpret_cast<const uint8*>(Blocks.GetData()), Blocks.Num() * sizeof(uint32));

		ImportData.Swizzle = { EGSplatSwizzleAxis::X, EGSplatSwizzleAxis::Z, EGSplatSwizzleAxis::Y };
		ImportData.ScaleFactor = 100.0f;
		ImportData.bApplyGammaCorrection = (H.Flags & kAthcLinear) == 0;
		ImportData.NumSplats = int32(Done);
		ImportData.bHasHighOrderSH = H.RestPerColour > 0;
		ImportData.bHasNormals = false;
		ImportData.bHasMetallicRoughness = false;

		Splats.Empty();
		Splats.bPacked = true;
		Splats.PackedFormat = 5;
		Splats.SplatCount = Done;
		if (Levels.Num() > 0)
		{
			Splats.LodWords = MoveTemp(LodWords);
			Splats.LodLevels = uint32(Levels.Num());
			Splats.LodFinestLevel = FinestLevel;
			Splats.LodFinestBase = FinestBase;
			Splats.LodBoundsLo = FVector3f(H.BoundsLo[0], H.BoundsLo[1], H.BoundsLo[2]);
			Splats.LodExtent = H.Extent;
		}
		Splats.bHasHighOrderSH = ImportData.bHasHighOrderSH && ImportData.bImportHighOrderSH;
		Splats.PackedUnitsToCm = ImportData.ScaleFactor;
		Splats.PackedBytes = MoveTemp(Data);
		Splats.PackedStreams = { TableAt, uint32(Sources.Num()), H.SHWords, Splats.bHasHighOrderSH ? H.RestPerColour : 0u,
			0u, TransferCount, H.Flags, 0u };
		Splats.bHasTransfer = true;
		Splats.TransferDirectCount = Direct;
		Splats.CellWords = (ShadowWords == 8 || ShadowWords == 32) ? ShadowWords : 0u;
		Splats.bPackedTransferField = Direct == 16 && TransferCount == 112;
		const TArray<EGSplatSwizzleAxis>& Swizzle = ImportData.Swizzle;
		Splats.FileAxisX = SwizzleVector(1.0f, 0.0f, 0.0f, Swizzle);
		Splats.FileAxisY = SwizzleVector(0.0f, 1.0f, 0.0f, Swizzle);
		Splats.FileAxisZ = SwizzleVector(0.0f, 0.0f, 1.0f, Swizzle);
		Splats.bLinearizeColor = ImportData.bApplyGammaCorrection;
		FBox Box(ForceInit);
		Box += FVector(SwizzleVector(H.BoundsMin[0], H.BoundsMin[1], H.BoundsMin[2], Swizzle) * ImportData.ScaleFactor);
		Box += FVector(SwizzleVector(H.BoundsMax[0], H.BoundsMax[1], H.BoundsMax[2], Swizzle) * ImportData.ScaleFactor);
		Splats.SetBounds(FBoxSphereBounds(Box));
		UE_LOG(LogGSplatImport, Display, TEXT("%s: %u gaussians in %d chunks, .athc version 2 with a transfer of %u values%s, %u rest coefficients a colour, %s colour; %d levels of detail (%llu merged groups), cut each frame by r.Gaussians.Athenea.LodPixels."),
			*Filename, H.Count, Chunks.Num(), TransferCount, Splats.CellWords ? TEXT(" and cells") : TEXT(""), H.RestPerColour,
			(H.Flags & kAthcLinear) ? TEXT("linear") : TEXT("sRGB"), Levels.Num(), Groups);
		return true;
	}
}

bool GSplatImporter::IsAthcFile(const FString& Filename)
{
	return FPaths::GetExtension(Filename).ToLower() == TEXT("athc");
}

bool GSplatImporter::ImportAthc(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats)
{
	TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Filename));
	if (!File)
	{
		UE_LOG(LogGSplatImport, Error, TEXT("Cannot open %s."), *Filename);
		return false;
	}

	FAthcHeader H;
	if (ReadAt(*File, 0, &H, 4) && FMemory::Memcmp(H.Magic, "ATH3", 4) == 0)
	{
		return GSplatAthc3::Import(*File, Filename, ImportData, Splats);
	}
	if (!ReadAt(*File, 0, &H, sizeof(H)) || FMemory::Memcmp(H.Magic, "ATHC", 4) != 0)
	{
		UE_LOG(LogGSplatImport, Error, TEXT("%s: no ATHC magic."), *Filename);
		return false;
	}
	if (H.Version < 1 || H.Version > 2)
	{
		UE_LOG(LogGSplatImport, Error, TEXT("%s: version %u, this reads 1 and 2."), *Filename, H.Version);
		return false;
	}
	if (H.Count == 0 || H.ChunkSplats == 0 || H.Chunks != (uint64(H.Count) + H.ChunkSplats - 1) / H.ChunkSplats
		|| H.RestPerColour > 15 || (H.SHWords != (H.RestPerColour * 3 + 1) / 2 && !(H.RestPerColour == 0 && H.SHWords == 1)))
	{
		UE_LOG(LogGSplatImport, Error, TEXT("%s: a header that does not add up (%u splats, %u chunks of %u, %u rest, %u words)."),
			*Filename, H.Count, H.Chunks, H.ChunkSplats, H.RestPerColour, H.SHWords);
		return false;
	}

	TArray<FAthcChunk> Chunks;
	Chunks.SetNumUninitialized(H.Chunks);
	if (!ReadAt(*File, H.ChunkTable, Chunks.GetData(), uint64(H.Chunks) * sizeof(FAthcChunk)))
	{
		UE_LOG(LogGSplatImport, Error, TEXT("%s: the chunk table is cut short."), *Filename);
		return false;
	}
	// A TX cloud's chunks carry the material, the transfer and the cells after
	// the harmonics: read through version 3's machinery, every section in v2's
	// words.
	if (H.Version == 2 && (H.Flags & GSplatAthc3::kAthcTransfer) != 0)
	{
		uint32 Extra[8];
		if (!ReadAt(*File, 104, Extra, sizeof(Extra)))
		{
			UE_LOG(LogGSplatImport, Error, TEXT("%s: the extra header is cut short."), *Filename);
			return false;
		}
		return GSplatAthc3::FromV2(*File, Filename, H, Extra, Chunks, ImportData, Splats);
	}

	// athenea's clouds are y up, right-handed and in metres; UE takes them as
	// (x, z, y) in centimetres, the PLY importer's mapping for a y-up file.
	ImportData.Swizzle = { EGSplatSwizzleAxis::X, EGSplatSwizzleAxis::Z, EGSplatSwizzleAxis::Y };
	ImportData.ScaleFactor = 100.0f;
	ImportData.bApplyGammaCorrection = (H.Flags & kAthcLinear) == 0;
	ImportData.NumSplats = int32(H.Count);
	ImportData.bHasHighOrderSH = H.RestPerColour > 0;
	ImportData.bHasNormals = false;
	ImportData.bHasMetallicRoughness = false;

	// The merged levels: each a block as a chunk's, its groups' octree cells
	// where a chunk has its splats' finest groups. They follow the splats in
	// the packed arrays, decoded as the splats are, and cut every frame.
	TArray<FAthcLevel> Levels;
	if (H.Levels > 0 && H.LevelTable > 0)
	{
		Levels.SetNumUninitialized(H.Levels);
		if (!ReadAt(*File, H.LevelTable, Levels.GetData(), uint64(H.Levels) * sizeof(FAthcLevel)))
		{
			UE_LOG(LogGSplatImport, Warning, TEXT("%s: the level table is cut short; the splats alone are read."), *Filename);
			Levels.Empty();
		}
	}
	uint64 Total = H.Count;
	uint32 FinestLevel = 0, CoarsestLevel = ~0u;
	for (const FAthcLevel& L : Levels)
	{
		Total += L.Groups;
		FinestLevel = FMath::Max(FinestLevel, L.Level);
		CoarsestLevel = FMath::Min(CoarsestLevel, L.Level);
	}
	const FAthcLevel* Finest = Levels.FindByPredicate([&](const FAthcLevel& L) { return L.Level == FinestLevel; });
	if (Levels.Num() > 0 && (Finest == nullptr || Finest->Groups != H.FinestGroups || Total > 0x7FFFFFFF))
	{
		UE_LOG(LogGSplatImport, Warning, TEXT("%s: levels that do not add up (finest %u groups where the header says %u); the splats alone are read."),
			*Filename, Finest ? Finest->Groups : 0u, H.FinestGroups);
		Levels.Empty();
		Total = H.Count;
	}

	Splats.Empty();
	Splats.bHasNormals = false;
	Splats.bHasHighOrderSH = H.RestPerColour > 0 && ImportData.bImportHighOrderSH;
	Splats.bHasMetallicRoughness = false;
	Splats.bPacked = true;
	Splats.SplatCount = uint32(Total);
	Splats.PackedSHWords = H.SHWords;
	Splats.PackedRestPerColour = Splats.bHasHighOrderSH ? H.RestPerColour : 0;
	Splats.PackedUnitsToCm = ImportData.ScaleFactor;
	Splats.PackedPositions.SetNumUninitialized(int32(Total));
	Splats.PackedShape.SetNumUninitialized(int32(Total) * 4);
	Splats.PackedSH.SetNumUninitialized(int32(Total * H.SHWords));
	if (Levels.Num() > 0)
	{
		Splats.LodWords.SetNumUninitialized(int32(Total) * 2);
	}
	// Where a block's tail (finest groups or cells) starts, after its harmonics.
	const uint64 TailAfter = 32 + 4 * uint64(H.SHWords);
	TArray<uint32> Tail;

	uint64 Done = 0;
	for (uint32 c = 0; c < H.Chunks; c++)
	{
		const FAthcChunk& Chunk = Chunks[c];
		const uint32 Expected = FMath::Min(H.ChunkSplats, H.Count - c * H.ChunkSplats);
		if (Chunk.Count != Expected)
		{
			UE_LOG(LogGSplatImport, Error, TEXT("%s: chunk %u holds %u splats, where %u were due."), *Filename, c, Chunk.Count, Expected);
			return false;
		}
		const uint64 n = Chunk.Count;
		const bool bRead =
			ReadAt(*File, Chunk.Offset, &Splats.PackedPositions[Done], n * 16)
			&& ReadAt(*File, Chunk.Offset + n * 16, &Splats.PackedShape[Done * 4], n * 16)
			&& (H.SHWords == 0 || ReadAt(*File, Chunk.Offset + n * 32, &Splats.PackedSH[Done * H.SHWords], n * 4 * H.SHWords));
		if (!bRead)
		{
			UE_LOG(LogGSplatImport, Error, TEXT("%s: chunk %u is cut short."), *Filename, c);
			return false;
		}
		if (Levels.Num() > 0)
		{
			// Each splat's finest-level group: copied, with the leaf mark.
			Tail.SetNumUninitialized(int32(n));
			if (!ReadAt(*File, Chunk.Offset + n * TailAfter, Tail.GetData(), n * 4))
			{
				UE_LOG(LogGSplatImport, Error, TEXT("%s: chunk %u's groups are cut short."), *Filename, c);
				return false;
			}
			for (uint64 i = 0; i < n; i++)
			{
				Splats.LodWords[int32((Done + i) * 2)] = Tail[int32(i)];
				Splats.LodWords[int32((Done + i) * 2 + 1)] = FGaussianSplatData::kLodLeaf;
			}
		}
		Done += n;
	}
	uint32 FinestBase = 0;
	for (const FAthcLevel& L : Levels)
	{
		const uint64 n = L.Groups;
		if (L.Level == FinestLevel)
		{
			FinestBase = uint32(Done);
		}
		Tail.SetNumUninitialized(int32(n));
		const bool bRead =
			ReadAt(*File, L.Offset, &Splats.PackedPositions[Done], n * 16)
			&& ReadAt(*File, L.Offset + n * 16, &Splats.PackedShape[Done * 4], n * 16)
			&& (H.SHWords == 0 || ReadAt(*File, L.Offset + n * 32, &Splats.PackedSH[Done * H.SHWords], n * 4 * H.SHWords))
			&& ReadAt(*File, L.Offset + n * TailAfter, Tail.GetData(), n * 4);
		if (!bRead)
		{
			UE_LOG(LogGSplatImport, Error, TEXT("%s: level %u is cut short."), *Filename, L.Level);
			return false;
		}
		// Each group's cell code and level (and whether it is the coarsest kept).
		for (uint64 i = 0; i < n; i++)
		{
			Splats.LodWords[int32((Done + i) * 2)] = Tail[int32(i)];
			Splats.LodWords[int32((Done + i) * 2 + 1)] = L.Level | (L.Level == CoarsestLevel ? FGaussianSplatData::kLodCoarsest : 0u);
		}
		Done += n;
	}
	if (Levels.Num() > 0)
	{
		Splats.LodLevels = uint32(Levels.Num());
		Splats.LodFinestLevel = FinestLevel;
		Splats.LodFinestBase = FinestBase;
		Splats.LodBoundsLo = FVector3f(H.BoundsLo[0], H.BoundsLo[1], H.BoundsLo[2]);
		Splats.LodExtent = H.Extent;
	}

	const TArray<EGSplatSwizzleAxis>& Swizzle = ImportData.Swizzle;
	Splats.FileAxisX = SwizzleVector(1.0f, 0.0f, 0.0f, Swizzle);
	Splats.FileAxisY = SwizzleVector(0.0f, 1.0f, 0.0f, Swizzle);
	Splats.FileAxisZ = SwizzleVector(0.0f, 0.0f, 1.0f, Swizzle);
	Splats.bLinearizeColor = ImportData.bApplyGammaCorrection;

	// The file's own bounds, taken into UE's axes: bookkeeping on two corners.
	const FVector3f A = SwizzleVector(H.BoundsMin[0], H.BoundsMin[1], H.BoundsMin[2], Swizzle) * ImportData.ScaleFactor;
	const FVector3f B = SwizzleVector(H.BoundsMax[0], H.BoundsMax[1], H.BoundsMax[2], Swizzle) * ImportData.ScaleFactor;
	FBox Box(ForceInit);
	Box += FVector(A);
	Box += FVector(B);
	Splats.SetBounds(FBoxSphereBounds(Box));

	UE_LOG(LogGSplatImport, Display, TEXT("%s: %u gaussians in %u chunks, %u rest coefficients a colour, %s colour; %d levels of detail (%llu merged groups), cut each frame by r.Gaussians.Athenea.LodPixels."),
		*Filename, H.Count, H.Chunks, H.RestPerColour, (H.Flags & kAthcLinear) ? TEXT("linear") : TEXT("sRGB"), Levels.Num(), Total - H.Count);
	return true;
}

