// Copyright (c) 2026 jesus luque.
//
// PlayCanvas's SOG ("Spatially Ordered Gaussians"), as athenea reads it
// (io/Sog.cpp): a zip of lossless WebP images (or a meta.json beside them) and
// a meta.json of ranges (version 1) or 256-entry codebooks (version 2). The
// CPU opens the archive, decodes the images to their texels and parses the
// metadata; what the texels mean is GSDecodeSogCS (GSDecode.usf).
//
// Kept in PackedBytes, in this order, each starting where PackedStreams says:
//   0 means_l   1 means_u   2 quats   3 scales   4 sh0   (RGBA8 texels)
//   5 shN_labels   6 shN_centroids                       (RGBA8 texels, or none)
//   7 floats: means min (3), max (3); scales min (3), max (3); sh0 min (4),
//     max (4); shN min, max; then the codebooks, scales, sh0 and shN, 256 each
// PackedSpz: (version, harmonic coefficients kept a colour, the palette's
// width in texels).
//
// Axes: a SOG is written in the coordinates of the 3DGS PLY it came from, so
// it comes into UE as a PLY does (COLMAP's right, down, front).

#include "GSplatImporter.h"
#include "GSplatImportData.h"

#if WITH_EDITOR

#include "AtheneaCodecs.h"
#include "Dom/JsonObject.h"
#include "GaussianData.h"
#include "GaussiansEditorModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	uint16 U16(const TArray<uint8>& b, int64 At) { return uint16(b[At] | (b[At + 1] << 8)); }
	uint32 U32(const TArray<uint8>& b, int64 At)
	{
		return uint32(b[At]) | (uint32(b[At + 1]) << 8) | (uint32(b[At + 2]) << 16) | (uint32(b[At + 3]) << 24);
	}

	// A zip's entries by name: stored or deflated, no zip64, as SOG bundles are.
	bool ReadZip(const TArray<uint8>& z, TMap<FString, TArray<uint8>>& Out, FString& Why)
	{
		if (z.Num() < 22)
		{
			Why = TEXT("not a zip bundle");
			return false;
		}
		int64 Eocd = z.Num() - 22;
		const int64 Floor = z.Num() > 22 + 65535 ? z.Num() - 22 - 65535 : 0;
		while (Eocd > Floor && U32(z, Eocd) != 0x06054b50u)
		{
			--Eocd;
		}
		if (U32(z, Eocd) != 0x06054b50u)
		{
			Why = TEXT("not a zip bundle");
			return false;
		}
		const uint16 Entries = U16(z, Eocd + 10);
		int64 At = U32(z, Eocd + 16);
		for (uint16 e = 0; e < Entries; ++e)
		{
			if (At + 46 > z.Num() || U32(z, At) != 0x02014b50u)
			{
				Why = TEXT("a broken zip directory");
				return false;
			}
			const uint16 Method = U16(z, At + 10);
			const uint32 Packed = U32(z, At + 20);
			const uint32 Unpacked = U32(z, At + 24);
			const uint16 NameLength = U16(z, At + 28);
			const uint16 ExtraLength = U16(z, At + 30);
			const uint16 CommentLength = U16(z, At + 32);
			const uint32 Local = U32(z, At + 42);
			if (Packed == 0xFFFFFFFFu || Unpacked == 0xFFFFFFFFu || Local == 0xFFFFFFFFu)
			{
				Why = TEXT("zip64 bundles are not read");
				return false;
			}
			const FString Name = FString(NameLength, reinterpret_cast<const UTF8CHAR*>(z.GetData() + At + 46));
			if (int64(Local) + 30 > z.Num() || U32(z, Local) != 0x04034b50u)
			{
				Why = TEXT("a broken zip entry");
				return false;
			}
			const int64 Payload = int64(Local) + 30 + U16(z, Local + 26) + U16(z, Local + 28);
			if (Payload + Packed > z.Num())
			{
				Why = TEXT("a truncated zip entry");
				return false;
			}
			TArray<uint8> Data;
			Data.SetNumUninitialized(Unpacked);
			if (Method == 0)
			{
				FMemory::Memcpy(Data.GetData(), z.GetData() + Payload, Unpacked);
			}
			else if (Method == 8)
			{
				if (!AtheneaCodecs::InflateRaw(z.GetData() + Payload, Packed, Data.GetData(), Unpacked))
				{
					Why = FString::Printf(TEXT("'%s' does not inflate"), *Name);
					return false;
				}
			}
			else
			{
				Why = FString::Printf(TEXT("zip method %u for '%s'"), Method, *Name);
				return false;
			}
			Out.Add(Name, MoveTemp(Data));
			At += 46 + NameLength + ExtraLength + CommentLength;
		}
		return true;
	}

	void ReadRange(const TSharedPtr<FJsonObject>& Section, float* Mins, float* Maxs, int32 n)
	{
		auto Read = [&](const TCHAR* Key, float* Into)
		{
			if (!Section.IsValid() || !Section->HasField(Key))
			{
				return;
			}
			const TSharedPtr<FJsonValue> V = Section->TryGetField(Key);
			for (int32 k = 0; k < n; ++k)
			{
				if (V->Type == EJson::Array)
				{
					const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
					if (k < A.Num())
					{
						Into[k] = float(A[k]->AsNumber());
					}
				}
				else if (V->Type == EJson::Number)
				{
					Into[k] = float(V->AsNumber());
				}
			}
		};
		Read(TEXT("mins"), Mins);
		Read(TEXT("maxs"), Maxs);
	}

	TArray<float> ReadCodebook(const TSharedPtr<FJsonObject>& Section)
	{
		TArray<float> Book;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (Section.IsValid() && Section->TryGetArrayField(TEXT("codebook"), A))
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				Book.Add(V->Type == EJson::Number ? float(V->AsNumber()) : 0.0f);
			}
		}
		return Book;
	}
}

bool GSplatImporter::ReadZip(const TArray<uint8>& Zip, TMap<FString, TArray<uint8>>& Out, FString& Why)
{
	return ::ReadZip(Zip, Out, Why);
}

bool GSplatImporter::IsSogFile(const FString& Filename)
{
	const FString Extension = FPaths::GetExtension(Filename).ToLower();
	return Extension == TEXT("sog") || FPaths::GetCleanFilename(Filename).ToLower() == TEXT("meta.json");
}

bool GSplatImporter::ImportSog(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats)
{
	TMap<FString, TArray<uint8>> Files;
	FString Why;
	if (FPaths::GetExtension(Filename).ToLower() == TEXT("json"))
	{
		// An unbundled SOG: the meta.json and the WebP images beside it.
		TArray<uint8> Meta;
		if (!FFileHelper::LoadFileToArray(Meta, *Filename))
		{
			UE_LOG(LogGSplatEditor, Error, TEXT("Cannot read %s."), *Filename);
			return false;
		}
		Files.Add(TEXT("meta.json"), MoveTemp(Meta));
		TArray<FString> Images;
		IFileManager::Get().FindFiles(Images, *FPaths::Combine(FPaths::GetPath(Filename), TEXT("*.webp")), true, false);
		for (const FString& Image : Images)
		{
			TArray<uint8> Bytes;
			if (FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(FPaths::GetPath(Filename), Image)))
			{
				Files.Add(Image, MoveTemp(Bytes));
			}
		}
	}
	else
	{
		TArray<uint8> Zip;
		if (!FFileHelper::LoadFileToArray(Zip, *Filename) || !ReadZip(Zip, Files, Why))
		{
			UE_LOG(LogGSplatEditor, Error, TEXT("%s: %s."), *Filename, Why.IsEmpty() ? TEXT("cannot be read") : *Why);
			return false;
		}
	}

	const TArray<uint8>* Meta = Files.Find(TEXT("meta.json"));
	if (!Meta)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: no meta.json."), *Filename);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const FString MetaText = FString(Meta->Num(), reinterpret_cast<const UTF8CHAR*>(Meta->GetData()));
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MetaText), Root) || !Root.IsValid())
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: meta.json does not parse."), *Filename);
		return false;
	}
	const uint32 Version = Root->HasField(TEXT("version")) ? uint32(Root->GetNumberField(TEXT("version"))) : 1u;
	const uint32 Count = Root->HasField(TEXT("count")) ? uint32(Root->GetNumberField(TEXT("count"))) : 0u;
	if (Version != 1 && Version != 2)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: SOG version %u."), *Filename, Version);
		return false;
	}
	if (Count == 0)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: meta.json names no splats."), *Filename);
		return false;
	}
	auto Section = [&](const TCHAR* Key) { const TSharedPtr<FJsonObject>* O = nullptr; return Root->TryGetObjectField(Key, O) ? *O : TSharedPtr<FJsonObject>(); };

	float Floats[22] = {};
	ReadRange(Section(TEXT("means")), Floats + 0, Floats + 3, 3);
	ReadRange(Section(TEXT("scales")), Floats + 6, Floats + 9, 3);
	ReadRange(Section(TEXT("sh0")), Floats + 12, Floats + 16, 4);
	ReadRange(Section(TEXT("shN")), Floats + 20, Floats + 21, 1);
	const TArray<float> ScalesBook = ReadCodebook(Section(TEXT("scales")));
	const TArray<float> Sh0Book = ReadCodebook(Section(TEXT("sh0")));
	const TArray<float> ShNBook = ReadCodebook(Section(TEXT("shN")));
	if (Version == 2 && (ScalesBook.Num() != 256 || Sh0Book.Num() != 256))
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: a version 2 SOG without its 256-entry codebooks."), *Filename);
		return false;
	}

	// The images, decoded to their texels as stored (RGBA8, never premultiplied:
	// these channels are indices and packed integers, not colour).
	const TCHAR* Names[7] = { TEXT("means_l.webp"), TEXT("means_u.webp"), TEXT("quats.webp"), TEXT("scales.webp"),
	                          TEXT("sh0.webp"), TEXT("shN_labels.webp"), TEXT("shN_centroids.webp") };
	TArray<uint8> Bytes;
	TArray<uint32> Streams;
	int32 CentroidsWidth = 0;
	bool bHasLabels = false;
	for (int32 k = 0; k < 7; ++k)
	{
		Streams.Add(uint32(Bytes.Num()));
		const TArray<uint8>* Image = Files.Find(Names[k]);
		if (!Image)
		{
			if (k < 5)
			{
				UE_LOG(LogGSplatEditor, Error, TEXT("%s: no %s."), *Filename, Names[k]);
				return false;
			}
			continue;
		}
		TArray<uint8> Rgba;
		int32 W = 0, H = 0;
		if (!AtheneaCodecs::DecodeWebP(Image->GetData(), Image->Num(), Rgba, W, H))
		{
			UE_LOG(LogGSplatEditor, Error, TEXT("%s: %s is not a WebP image."), *Filename, Names[k]);
			return false;
		}
		if (k < 6 && uint64(W) * uint64(H) < Count)
		{
			UE_LOG(LogGSplatEditor, Error, TEXT("%s: %s holds fewer texels than %u splats."), *Filename, Names[k], Count);
			return false;
		}
		bHasLabels |= k == 5;
		CentroidsWidth = k == 6 ? W : CentroidsWidth;
		Bytes.Append(Rgba);
	}
	Streams.Add(uint32(Bytes.Num()));
	Bytes.Append(reinterpret_cast<const uint8*>(Floats), sizeof(Floats));
	for (const TArray<float>* Book : { &ScalesBook, &Sh0Book, &ShNBook })
	{
		TArray<float> Padded = *Book;
		Padded.SetNumZeroed(256);
		Bytes.Append(reinterpret_cast<const uint8*>(Padded.GetData()), 256 * sizeof(float));
	}

	// The palette's coefficients a colour, as athenea reads them: 64 entries a
	// row, a row CentroidsWidth / 64 texels an entry.
	const int32 PerEntry = CentroidsWidth / 64;
	uint32 Coefficients = (bHasLabels && CentroidsWidth > 0) ? (PerEntry >= 15 ? 15u : PerEntry >= 8 ? 8u : PerEntry >= 3 ? 3u : 0u) : 0u;
	if (Version == 2 && ShNBook.Num() != 256)
	{
		Coefficients = 0;
	}

	const TArray<EGSplatSwizzleAxis> Swizzle{ EGSplatSwizzleAxis::Z, EGSplatSwizzleAxis::X, EGSplatSwizzleAxis::Yn };
	ImportData.Swizzle = Swizzle;
	ImportData.ScaleFactor = 100.0f;
	ImportData.bApplyGammaCorrection = true;
	ImportData.NumSplats = int32(Count);
	ImportData.bHasHighOrderSH = Coefficients > 0;
	ImportData.bHasNormals = false;
	ImportData.bHasMetallicRoughness = false;

	Splats.Empty();
	Splats.bPacked = true;
	Splats.PackedFormat = 3;
	Splats.SplatCount = Count;
	Splats.bHasHighOrderSH = Coefficients > 0 && ImportData.bImportHighOrderSH;
	Splats.PackedUnitsToCm = ImportData.ScaleFactor;
	Splats.PackedBytes = MoveTemp(Bytes);
	Splats.PackedStreams = MoveTemp(Streams);
	Splats.PackedSpz = FIntVector(int32(Version), Splats.bHasHighOrderSH ? int32(Coefficients) : 0, CentroidsWidth);
	Splats.FileAxisX = SwizzleVector(1.0f, 0.0f, 0.0f, Swizzle);
	Splats.FileAxisY = SwizzleVector(0.0f, 1.0f, 0.0f, Swizzle);
	Splats.FileAxisZ = SwizzleVector(0.0f, 0.0f, 1.0f, Swizzle);
	Splats.bLinearizeColor = true;

	// The bounds from the ranges: positions are sign(n) (exp |n| - 1) of a value
	// within them, which keeps their order.
	auto Unlog = [](float n) { return FMath::Sign(n) * (FMath::Exp(FMath::Abs(n)) - 1.0f); };
	const FVector3f Lo(Unlog(Floats[0]), Unlog(Floats[1]), Unlog(Floats[2]));
	const FVector3f Hi(Unlog(Floats[3]), Unlog(Floats[4]), Unlog(Floats[5]));
	FBox Box(ForceInit);
	Box += FVector(SwizzleVector(Lo.X, Lo.Y, Lo.Z, Swizzle) * ImportData.ScaleFactor);
	Box += FVector(SwizzleVector(Hi.X, Hi.Y, Hi.Z, Swizzle) * ImportData.ScaleFactor);
	Splats.SetBounds(FBoxSphereBounds(Box));

	UE_LOG(LogGSplatEditor, Display, TEXT("%s: %u gaussians, SOG version %u, %u harmonic coefficients a colour."),
		*Filename, Count, Version, Coefficients);
	return true;
}

#endif // WITH_EDITOR
