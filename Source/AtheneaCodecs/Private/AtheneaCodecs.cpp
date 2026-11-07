// Copyright (c) 2026 jesus luque.

#include "AtheneaCodecs.h"
#include "Modules/ModuleManager.h"

THIRD_PARTY_INCLUDES_START
#include "zlib.h"
extern "C"
{
#include "zstd.h"
#include "src/webp/decode.h"
}
THIRD_PARTY_INCLUDES_END

IMPLEMENT_MODULE(FDefaultModuleImpl, AtheneaCodecs)

bool AtheneaCodecs::Gunzip(const uint8* In, int64 InSize, TArray<uint8>& Out)
{
	z_stream Stream = {};
	// 16 + MAX_WBITS: a gzip header, as SPZ's own reader asks for.
	if (inflateInit2(&Stream, 16 + MAX_WBITS) != Z_OK)
	{
		return false;
	}
	Stream.next_in = const_cast<Bytef*>(In);
	Stream.avail_in = uInt(InSize);
	Out.Reset();
	uint8 Chunk[1 << 16];
	int Result = Z_OK;
	do
	{
		Stream.next_out = Chunk;
		Stream.avail_out = sizeof(Chunk);
		Result = inflate(&Stream, Z_NO_FLUSH);
		if (Result != Z_OK && Result != Z_STREAM_END)
		{
			inflateEnd(&Stream);
			return false;
		}
		Out.Append(Chunk, int32(sizeof(Chunk) - Stream.avail_out));
	} while (Result != Z_STREAM_END);
	inflateEnd(&Stream);
	return true;
}

bool AtheneaCodecs::InflateRaw(const uint8* In, int64 InSize, uint8* Out, int64 OutSize)
{
	z_stream Stream = {};
	// A zip entry is raw deflate: negative window bits, no header.
	if (inflateInit2(&Stream, -MAX_WBITS) != Z_OK)
	{
		return false;
	}
	Stream.next_in = const_cast<Bytef*>(In);
	Stream.avail_in = uInt(InSize);
	Stream.next_out = Out;
	Stream.avail_out = uInt(OutSize);
	const int Status = inflate(&Stream, Z_FINISH);
	inflateEnd(&Stream);
	return Status == Z_STREAM_END && int64(Stream.total_out) == OutSize;
}

bool AtheneaCodecs::ZstdDecompress(const uint8* In, int64 InSize, uint8* Out, int64 OutSize)
{
	const size_t Done = ZSTD_decompress(Out, size_t(OutSize), In, size_t(InSize));
	return !ZSTD_isError(Done) && int64(Done) == OutSize;
}

bool AtheneaCodecs::DecodeWebP(const uint8* In, int64 InSize, TArray<uint8>& OutRgba, int32& OutWidth, int32& OutHeight)
{
	int Width = 0, Height = 0;
	if (!WebPGetInfo(In, size_t(InSize), &Width, &Height) || Width <= 0 || Height <= 0)
	{
		return false;
	}
	OutRgba.SetNumUninitialized(int64(Width) * Height * 4);
	if (!WebPDecodeRGBAInto(In, size_t(InSize), OutRgba.GetData(), size_t(OutRgba.Num()), Width * 4))
	{
		return false;
	}
	OutWidth = Width;
	OutHeight = Height;
	return true;
}
