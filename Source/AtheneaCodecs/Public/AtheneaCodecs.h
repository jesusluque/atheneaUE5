// Copyright (c) 2026 jesus luque.
//
// What athenea's importers need to read compressed files: the CPU only
// undoes the files' own compression; what the bytes mean is decoded on the
// GPU.

#pragma once

#include "CoreMinimal.h"

namespace AtheneaCodecs
{
	// A gzip stream (.spz versions 2 and 3), whole.
	ATHENEACODECS_API bool Gunzip(const uint8* In, int64 InSize, TArray<uint8>& Out);

	// A raw deflate stream of a known inflated size (a zip entry, .sog).
	ATHENEACODECS_API bool InflateRaw(const uint8* In, int64 InSize, uint8* Out, int64 OutSize);

	// One zstd frame of a known decompressed size (.spz version 4's streams).
	ATHENEACODECS_API bool ZstdDecompress(const uint8* In, int64 InSize, uint8* Out, int64 OutSize);

	// A WebP image (lossless or lossy) as 8-bit RGBA, rows top down.
	ATHENEACODECS_API bool DecodeWebP(const uint8* In, int64 InSize, TArray<uint8>& OutRgba, int32& OutWidth, int32& OutHeight);
}
