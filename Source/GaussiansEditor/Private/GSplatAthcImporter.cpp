// Copyright (c) 2026 jesus luque.
//
// A .athc, athenea's chunked container (modules/lod/src/Athc.cpp), as a packed
// asset: the finest level, every chunk's positions, shape and harmonic words
// copied as they are. The GPU decodes them when the resource is made
// (GSDecode.usf). Merged levels, normals, emission and the extras are not read
// yet.
//
//   page 0        header (104 bytes: FileHeader below)
//   page 1..      level table, chunk table (offset, count, pad a chunk)
//   each chunk    positions (16 B a splat), shape (16 B), SH (4 * shWords B),
//                 then the finest group, normals, emission, extras

#include "GSplatImporter.h"
#include "GSplatImportData.h"

#if WITH_EDITOR

#include "GaussianData.h"
#include "GaussiansEditorModule.h"
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

bool GSplatImporter::IsAthcFile(const FString& Filename)
{
	return FPaths::GetExtension(Filename).ToLower() == TEXT("athc");
}

bool GSplatImporter::ImportAthc(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats)
{
	TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Filename));
	if (!File)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("Cannot open %s."), *Filename);
		return false;
	}

	FAthcHeader H;
	if (!ReadAt(*File, 0, &H, sizeof(H)) || FMemory::Memcmp(H.Magic, "ATHC", 4) != 0)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: no ATHC magic."), *Filename);
		return false;
	}
	if (H.Version < 1 || H.Version > 2)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: version %u, this reads 1 and 2."), *Filename, H.Version);
		return false;
	}
	if (H.Count == 0 || H.ChunkSplats == 0 || H.Chunks != (uint64(H.Count) + H.ChunkSplats - 1) / H.ChunkSplats
		|| H.RestPerColour > 15 || H.SHWords != (H.RestPerColour * 3 + 1) / 2)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: a header that does not add up (%u splats, %u chunks of %u, %u rest, %u words)."),
			*Filename, H.Count, H.Chunks, H.ChunkSplats, H.RestPerColour, H.SHWords);
		return false;
	}

	TArray<FAthcChunk> Chunks;
	Chunks.SetNumUninitialized(H.Chunks);
	if (!ReadAt(*File, H.ChunkTable, Chunks.GetData(), uint64(H.Chunks) * sizeof(FAthcChunk)))
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: the chunk table is cut short."), *Filename);
		return false;
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

	Splats.Empty();
	Splats.bHasNormals = false;
	Splats.bHasHighOrderSH = H.RestPerColour > 0 && ImportData.bImportHighOrderSH;
	Splats.bHasMetallicRoughness = false;
	Splats.bPacked = true;
	Splats.SplatCount = H.Count;
	Splats.PackedSHWords = H.SHWords;
	Splats.PackedRestPerColour = Splats.bHasHighOrderSH ? H.RestPerColour : 0;
	Splats.PackedUnitsToCm = ImportData.ScaleFactor;
	Splats.PackedPositions.SetNumUninitialized(H.Count);
	Splats.PackedShape.SetNumUninitialized(int32(H.Count) * 4);
	Splats.PackedSH.SetNumUninitialized(int32(H.Count * H.SHWords));

	uint64 Done = 0;
	for (uint32 c = 0; c < H.Chunks; c++)
	{
		const FAthcChunk& Chunk = Chunks[c];
		const uint32 Expected = FMath::Min(H.ChunkSplats, H.Count - c * H.ChunkSplats);
		if (Chunk.Count != Expected)
		{
			UE_LOG(LogGSplatEditor, Error, TEXT("%s: chunk %u holds %u splats, where %u were due."), *Filename, c, Chunk.Count, Expected);
			return false;
		}
		const uint64 n = Chunk.Count;
		const bool bRead =
			ReadAt(*File, Chunk.Offset, &Splats.PackedPositions[Done], n * 16)
			&& ReadAt(*File, Chunk.Offset + n * 16, &Splats.PackedShape[Done * 4], n * 16)
			&& (H.SHWords == 0 || ReadAt(*File, Chunk.Offset + n * 32, &Splats.PackedSH[Done * H.SHWords], n * 4 * H.SHWords));
		if (!bRead)
		{
			UE_LOG(LogGSplatEditor, Error, TEXT("%s: chunk %u is cut short."), *Filename, c);
			return false;
		}
		Done += n;
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

	UE_LOG(LogGSplatEditor, Display, TEXT("%s: %u gaussians in %u chunks, %u rest coefficients a colour, %s colour, %u levels not read."),
		*Filename, H.Count, H.Chunks, H.RestPerColour, (H.Flags & kAthcLinear) ? TEXT("linear") : TEXT("sRGB"), H.Levels);
	return true;
}

#endif // WITH_EDITOR
