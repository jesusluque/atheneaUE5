// Copyright (c) 2026 jesus luque.
//
// Splat files in their own packing, kept as their bytes and decoded on the GPU
// (GSDecode.usf), as athenea reads them (io/SplatReaders.cpp):
//
//   .splat  antimatter15's 32-byte records; COLMAP's axes (right, down,
//           front) and a capture's sRGB, as a 3DGS PLY
//   .spz    Niantic's: gzip around a 16-byte header and six streams
//           (versions 2 and 3), or a 32-byte header, a table of contents and
//           six zstd streams (version 4; five without harmonics); right,
//           up, back unless an extension says otherwise (extensions are not
//           read: right-up-back is taken), turned into UE as athenea turns it
//
// The CPU undoes the files' compression, copies bytes, and takes the cloud's
// bounds from its positions, which neither format stores.

#include "GSplatImporter.h"
#include "GSplatImportData.h"

#if WITH_EDITOR

#include "AtheneaCodecs.h"
#include "GaussianData.h"
#include "GaussiansEditorModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr uint32 kNgspMagic = 0x5053474e;   // "NGSP"

	void SetCommon(UGSplatImportData& ImportData, FGaussianSplatData& Splats, const TArray<EGSplatSwizzleAxis>& Swizzle,
		uint32 Count, uint8 Format, uint32 RestPerColour)
	{
		ImportData.Swizzle = Swizzle;
		ImportData.ScaleFactor = 100.0f;
		ImportData.bApplyGammaCorrection = true;
		ImportData.NumSplats = int32(Count);
		ImportData.bHasHighOrderSH = RestPerColour > 0;
		ImportData.bHasNormals = false;
		ImportData.bHasMetallicRoughness = false;

		Splats.Empty();
		Splats.bPacked = true;
		Splats.PackedFormat = Format;
		Splats.SplatCount = Count;
		Splats.bHasHighOrderSH = RestPerColour > 0 && ImportData.bImportHighOrderSH;
		Splats.PackedUnitsToCm = ImportData.ScaleFactor;
		Splats.FileAxisX = SwizzleVector(1.0f, 0.0f, 0.0f, Swizzle);
		Splats.FileAxisY = SwizzleVector(0.0f, 1.0f, 0.0f, Swizzle);
		Splats.FileAxisZ = SwizzleVector(0.0f, 0.0f, 1.0f, Swizzle);
		Splats.bLinearizeColor = true;
	}

	void SetBoundsFrom(FGaussianSplatData& Splats, const FVector3f& Lo, const FVector3f& Hi, const TArray<EGSplatSwizzleAxis>& Swizzle, float ScaleFactor)
	{
		FBox Box(ForceInit);
		Box += FVector(SwizzleVector(Lo.X, Lo.Y, Lo.Z, Swizzle) * ScaleFactor);
		Box += FVector(SwizzleVector(Hi.X, Hi.Y, Hi.Z, Swizzle) * ScaleFactor);
		Splats.SetBounds(FBoxSphereBounds(Box));
	}
}

bool GSplatImporter::IsSplatFile(const FString& Filename)
{
	const FString Extension = FPaths::GetExtension(Filename).ToLower();
	return Extension == TEXT("splat") || Extension == TEXT("spz");
}

bool GSplatImporter::ImportSplatFile(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats)
{
	TArray<uint8> File;
	if (!FFileHelper::LoadFileToArray(File, *Filename))
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("Cannot read %s."), *Filename);
		return false;
	}

	if (FPaths::GetExtension(Filename).ToLower() == TEXT("splat"))
	{
		if (File.Num() == 0 || File.Num() % 32 != 0)
		{
			UE_LOG(LogGSplatEditor, Error, TEXT("%s: %d bytes is not a whole number of 32-byte .splat records."), *Filename, File.Num());
			return false;
		}
		const uint32 Count = uint32(File.Num() / 32);
		const TArray<EGSplatSwizzleAxis> Swizzle{ EGSplatSwizzleAxis::Z, EGSplatSwizzleAxis::X, EGSplatSwizzleAxis::Yn };
		SetCommon(ImportData, Splats, Swizzle, Count, 1, 0);
		FVector3f Lo(TNumericLimits<float>::Max()), Hi(-TNumericLimits<float>::Max());
		for (uint32 i = 0; i < Count; i++)
		{
			FVector3f P;
			FMemory::Memcpy(&P, &File[i * 32], sizeof(P));
			Lo = FVector3f::Min(Lo, P);
			Hi = FVector3f::Max(Hi, P);
		}
		Splats.PackedBytes = MoveTemp(File);
		SetBoundsFrom(Splats, Lo, Hi, Swizzle, ImportData.ScaleFactor);
		UE_LOG(LogGSplatEditor, Display, TEXT("%s: %u gaussians, .splat records."), *Filename, Count);
		return true;
	}

	// .spz: gzip (versions 2 and 3) or NGSP with zstd streams (version 4).
	TArray<uint8> Streams;
	uint32 Version = 0, Count = 0, Degree = 0, FractionalBits = 0;
	const bool bGzip = File.Num() >= 2 && File[0] == 0x1f && File[1] == 0x8b;
	if (bGzip)
	{
		TArray<uint8> Data;
		if (!AtheneaCodecs::Gunzip(File.GetData(), File.Num(), Data) || Data.Num() < 16)
		{
			UE_LOG(LogGSplatEditor, Error, TEXT("%s: not a readable gzip stream."), *Filename);
			return false;
		}
		uint32 Magic = 0;
		FMemory::Memcpy(&Magic, &Data[0], 4);
		FMemory::Memcpy(&Version, &Data[4], 4);
		FMemory::Memcpy(&Count, &Data[8], 4);
		Degree = Data[12];
		FractionalBits = Data[13];
		if (Magic != kNgspMagic || Version < 2 || Version > 3)
		{
			UE_LOG(LogGSplatEditor, Error, TEXT("%s: an SPZ header this reads (NGSP, version 2 or 3) was not found (version %u)."), *Filename, Version);
			return false;
		}
		Streams.Append(Data.GetData() + 16, Data.Num() - 16);
	}
	else
	{
		if (File.Num() < 32)
		{
			UE_LOG(LogGSplatEditor, Error, TEXT("%s: too short for an SPZ header."), *Filename);
			return false;
		}
		uint32 Magic = 0, Toc = 0;
		FMemory::Memcpy(&Magic, &File[0], 4);
		FMemory::Memcpy(&Version, &File[4], 4);
		FMemory::Memcpy(&Count, &File[8], 4);
		Degree = File[12];
		FractionalBits = File[13];
		const uint32 NumStreams = File[15];
		FMemory::Memcpy(&Toc, &File[16], 4);
		// Five streams without harmonics (degree 0), six with them.
		const uint32 Needed = Degree > 0 ? 6u : 5u;
		if (Magic != kNgspMagic || Version < 4 || NumStreams < Needed || uint64(Toc) + uint64(NumStreams) * 16 > uint64(File.Num()))
		{
			UE_LOG(LogGSplatEditor, Error, TEXT("%s: an SPZ header this reads (NGSP, version 4) was not found (version %u)."), *Filename, Version);
			return false;
		}
		uint64 At = uint64(Toc) + uint64(NumStreams) * 16;
		for (uint32 s = 0; s < Needed; s++)
		{
			uint64 Compressed = 0, Uncompressed = 0;
			FMemory::Memcpy(&Compressed, &File[Toc + s * 16], 8);
			FMemory::Memcpy(&Uncompressed, &File[Toc + s * 16 + 8], 8);
			if (At + Compressed > uint64(File.Num()))
			{
				UE_LOG(LogGSplatEditor, Error, TEXT("%s: stream %u runs past the file."), *Filename, s);
				return false;
			}
			const int64 Start = Streams.Num();
			Streams.AddUninitialized(int64(Uncompressed));
			if (!AtheneaCodecs::ZstdDecompress(&File[At], int64(Compressed), Streams.GetData() + Start, int64(Uncompressed)))
			{
				UE_LOG(LogGSplatEditor, Error, TEXT("%s: stream %u does not decompress."), *Filename, s);
				return false;
			}
			At += Compressed;
		}
	}

	const uint32 Dims[5] = { 0, 3, 8, 15, 24 };
	if (Degree > 4 || FractionalBits >= 32 || Count == 0)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: degree %u, %u fractional bits, %u points: not a cloud this reads."), *Filename, Degree, FractionalBits, Count);
		return false;
	}
	const uint32 Rest = Dims[Degree];
	const bool bSmallestThree = Version >= 3;
	const uint64 n = Count;
	const uint64 Offsets[6] = {
		0,
		n * 9,
		n * 10,
		n * 13,
		n * 16,
		n * 16 + n * (bSmallestThree ? 4 : 3) };
	const uint64 Expected = Offsets[5] + n * Rest * 3;
	if (uint64(Streams.Num()) < Expected)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: %lld bytes of streams where %llu were due."), *Filename, int64(Streams.Num()), Expected);
		return false;
	}

	// Axes as athenea takes an SPZ: its decode turns the format's right, up,
	// back by negating y and z (splat_decode.slang, flipYZ) into a 3DGS PLY's
	// right, down, front, which comes into UE as (z, x, -y). Composed:
	// (-z, x, y). So a cloud stands in UE as it stands in athenea -- upright
	// when the file is right-up-back as the format says, upside down when it
	// holds a PLY's y-down coordinates without the extension saying so (in
	// athenea as well).
	const TArray<EGSplatSwizzleAxis> Swizzle{ EGSplatSwizzleAxis::Zn, EGSplatSwizzleAxis::X, EGSplatSwizzleAxis::Y };
	SetCommon(ImportData, Splats, Swizzle, Count, 2, Rest);
	Splats.PackedStreams = { uint32(Offsets[0]), uint32(Offsets[1]), uint32(Offsets[2]), uint32(Offsets[3]), uint32(Offsets[4]), uint32(Offsets[5]) };
	Splats.PackedSpz = FIntVector(int32(FractionalBits), bSmallestThree ? 1 : 0, Splats.bHasHighOrderSH ? int32(Rest) : 0);

	// The bounds, from the fixed point positions.
	FVector3f Lo(TNumericLimits<float>::Max()), Hi(-TNumericLimits<float>::Max());
	const float Fixed = 1.0f / float(1u << FractionalBits);
	for (uint64 i = 0; i < n; i++)
	{
		FVector3f P;
		for (int32 a = 0; a < 3; a++)
		{
			const uint8* B = &Streams[i * 9 + a * 3];
			int32 v = int32(B[0]) | (int32(B[1]) << 8) | (int32(B[2]) << 16);
			v = (v & 0x800000) ? (v | int32(0xFF000000)) : v;
			P[a] = float(v) * Fixed;
		}
		Lo = FVector3f::Min(Lo, P);
		Hi = FVector3f::Max(Hi, P);
	}
	Streams.SetNum(int64(Expected));
	Splats.PackedBytes = MoveTemp(Streams);
	SetBoundsFrom(Splats, Lo, Hi, Swizzle, ImportData.ScaleFactor);
	UE_LOG(LogGSplatEditor, Display, TEXT("%s: %u gaussians, SPZ version %u, degree %u, %s rotations."),
		*Filename, Count, Version, Degree, bSmallestThree ? TEXT("smallest-three") : TEXT("first-three"));
	return true;
}

#endif // WITH_EDITOR
