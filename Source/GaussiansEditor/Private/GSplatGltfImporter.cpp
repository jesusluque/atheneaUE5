// Copyright (c) 2026 jesus luque.
//
// glTF 2.0 with KHR_gaussian_splatting (.glb, or .gltf with its buffers beside
// it or in data URIs): a POINTS primitive whose attributes are a gaussian's
// POSITION, KHR_gaussian_splatting:ROTATION (x y z w), :SCALE (linear),
// :OPACITY (linear) and :SH_DEGREE_l_COEF_n (rgb), with COLOR_0 as the colour
// where no constant coefficient is given -- the layout athenea writes
// (io/SplatWriters.cpp, writeGlbGaussians) and PlayCanvas's splat-transform
// writes.
//
// The CPU parses the JSON and copies the buffers; every accessor is read on
// the GPU (GSDecodeGltfCS), whatever its component type, normalisation and
// stride. Kept in PackedBytes:
//   the file's buffers, one after another, each starting on a word;
//   at PackedStreams[0], 21 accessor descriptors of four words -- the byte
//   address of the first element, the stride, the component type |
//   normalized << 16 | components << 20, and whether it is present -- for
//   position, rotation, scale, opacity, COLOR_0, then the 16 coefficients.
// PackedSpz.Z: the harmonic coefficients a colour beyond the constant one
// that are drawn (0 when the import leaves them out).
//
// Axes: glTF's own, +Y up, +Z the asset's front and +X its left -- an SPZ's
// right-up-back turned half round about the vertical -- so it comes into UE
// as an SPZ does after that turn, (z, -x, y): a cloud written to both formats
// lands in the same place, facing the same way. Bounds come from the
// POSITION accessor's min and max, which glTF requires.

#include "GSplatImporter.h"
#include "GSplatImportData.h"

#if WITH_EDITOR

#include "Dom/JsonObject.h"
#include "GaussianData.h"
#include "GaussiansEditorModule.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace GSplatGltf
{
	constexpr uint32 kGlbMagic = 0x46546C67;   // "glTF"
	constexpr uint32 kJsonChunk = 0x4E4F534A;
	constexpr uint32 kBinChunk = 0x004E4942;
	constexpr int32 kSlots = 21;
	const TCHAR* kExtension = TEXT("KHR_gaussian_splatting");

	uint32 U32(const TArray<uint8>& b, int64 At)
	{
		return uint32(b[At]) | (uint32(b[At + 1]) << 8) | (uint32(b[At + 2]) << 16) | (uint32(b[At + 3]) << 24);
	}

	// The JSON and the binary chunk of a .glb, or the JSON of a .gltf.
	bool SplitFile(const FString& Filename, const TArray<uint8>& File, FString& Json, TArray<uint8>& Bin, FString& Why)
	{
		if (File.Num() >= 12 && U32(File, 0) == kGlbMagic)
		{
			if (U32(File, 4) != 2)
			{
				Why = FString::Printf(TEXT("glTF binary version %u."), U32(File, 4));
				return false;
			}
			int64 At = 12;
			bool bJson = false;
			while (At + 8 <= File.Num())
			{
				const uint32 Length = U32(File, At);
				const uint32 Type = U32(File, At + 4);
				if (At + 8 + Length > File.Num())
				{
					Why = TEXT("a glb chunk runs past the file.");
					return false;
				}
				if (Type == kJsonChunk && !bJson)
				{
					FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(&File[At + 8]), int32(Length));
					Json = FString(Text.Length(), Text.Get());
					bJson = true;
				}
				else if (Type == kBinChunk && Bin.Num() == 0)
				{
					Bin.Append(&File[At + 8], Length);
				}
				At += 8 + Length;
			}
			if (!bJson)
			{
				Why = TEXT("a glb without its JSON chunk.");
				return false;
			}
			return true;
		}
		FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(File.GetData()), File.Num());
		Json = FString(Text.Length(), Text.Get());
		return true;
	}

	bool IsIdentityNode(const TSharedPtr<FJsonObject>& Node)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (Node->TryGetArrayField(TEXT("matrix"), A))
		{
			for (int32 k = 0; k < A->Num() && k < 16; k++)
			{
				if (FMath::Abs((*A)[k]->AsNumber() - ((k % 5) == 0 ? 1.0 : 0.0)) > 1e-6)
				{
					return false;
				}
			}
		}
		auto Is = [&](const TCHAR* Key, const TArray<double>& Identity)
		{
			const TArray<TSharedPtr<FJsonValue>>* V = nullptr;
			if (!Node->TryGetArrayField(Key, V))
			{
				return true;
			}
			for (int32 k = 0; k < V->Num() && k < Identity.Num(); k++)
			{
				if (FMath::Abs((*V)[k]->AsNumber() - Identity[k]) > 1e-6)
				{
					return false;
				}
			}
			return true;
		};
		return Is(TEXT("translation"), { 0, 0, 0 }) && Is(TEXT("rotation"), { 0, 0, 0, 1 }) && Is(TEXT("scale"), { 1, 1, 1 });
	}

	uint32 ComponentBytes(uint32 Type)
	{
		switch (Type)
		{
		case 5120: case 5121: return 1;
		case 5122: case 5123: return 2;
		case 5125: case 5126: return 4;
		default: return 0;
		}
	}

	uint32 TypeComponents(const FString& Type)
	{
		return Type == TEXT("SCALAR") ? 1 : Type == TEXT("VEC2") ? 2 : Type == TEXT("VEC3") ? 3 : Type == TEXT("VEC4") ? 4 : 0;
	}
}

using namespace GSplatGltf;

bool GSplatImporter::IsGltfFile(const FString& Filename)
{
	const FString Extension = FPaths::GetExtension(Filename).ToLower();
	return Extension == TEXT("glb") || Extension == TEXT("gltf");
}

bool GSplatImporter::GltfHasSplats(const FString& Filename)
{
	// Enough of the file to see whether it names the extension; a mesh file
	// is left to UE's own glTF importer.
	TArray<uint8> File;
	if (!FFileHelper::LoadFileToArray(File, *Filename))
	{
		return false;
	}
	FString Json, Why;
	TArray<uint8> Bin;
	return SplitFile(Filename, File, Json, Bin, Why) && Json.Contains(kExtension);
}

bool GSplatImporter::ImportGltf(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats)
{
	auto Fail = [&](const FString& Why)
	{
		UE_LOG(LogGSplatEditor, Error, TEXT("%s: %s"), *Filename, *Why);
		return false;
	};

	TArray<uint8> File;
	if (!FFileHelper::LoadFileToArray(File, *Filename))
	{
		return Fail(TEXT("cannot be read."));
	}
	FString JsonText, Why;
	TArray<uint8> Bin;
	if (!SplitFile(Filename, File, JsonText, Bin, Why))
	{
		return Fail(Why);
	}
	File.Empty();
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid())
	{
		return Fail(TEXT("its JSON does not parse."));
	}

	// Extensions a reader must know: this one, and mesh quantisation (normalised
	// integer attributes, which the kernel reads); any other is refused.
	const TArray<TSharedPtr<FJsonValue>>* Required = nullptr;
	if (Root->TryGetArrayField(TEXT("extensionsRequired"), Required))
	{
		for (const TSharedPtr<FJsonValue>& V : *Required)
		{
			const FString Name = V->AsString();
			if (Name != kExtension && Name != TEXT("KHR_mesh_quantization"))
			{
				return Fail(FString::Printf(TEXT("requires %s, which is not read."), *Name));
			}
		}
	}

	// The buffers, one after another, each on a word.
	const TArray<TSharedPtr<FJsonValue>>* BuffersJson = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ViewsJson = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* AccessorsJson = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* MeshesJson = nullptr;
	if (!Root->TryGetArrayField(TEXT("buffers"), BuffersJson) || !Root->TryGetArrayField(TEXT("bufferViews"), ViewsJson)
		|| !Root->TryGetArrayField(TEXT("accessors"), AccessorsJson) || !Root->TryGetArrayField(TEXT("meshes"), MeshesJson))
	{
		return Fail(TEXT("no buffers, buffer views, accessors or meshes."));
	}
	TArray<uint8> Bytes;
	TArray<uint64> BufferBase, BufferLength;
	for (int32 b = 0; b < BuffersJson->Num(); b++)
	{
		const TSharedPtr<FJsonObject> Buffer = (*BuffersJson)[b]->AsObject();
		const uint64 Length = uint64(Buffer->GetNumberField(TEXT("byteLength")));
		FString Uri;
		TArray<uint8> Data;
		if (!Buffer->TryGetStringField(TEXT("uri"), Uri))
		{
			if (b != 0 || uint64(Bin.Num()) < Length)
			{
				return Fail(FString::Printf(TEXT("buffer %d has no data."), b));
			}
			Data = MoveTemp(Bin);
		}
		else if (Uri.StartsWith(TEXT("data:")))
		{
			int32 Comma = INDEX_NONE;
			if (!Uri.FindChar(TEXT(','), Comma) || !FBase64::Decode(Uri.Mid(Comma + 1), Data))
			{
				return Fail(FString::Printf(TEXT("buffer %d: a data URI that does not decode."), b));
			}
		}
		else
		{
			const FString Path = FPaths::Combine(FPaths::GetPath(Filename), Uri.Replace(TEXT("%20"), TEXT(" ")));
			if (!FFileHelper::LoadFileToArray(Data, *Path))
			{
				return Fail(FString::Printf(TEXT("buffer %d: %s cannot be read."), b, *Path));
			}
		}
		if (uint64(Data.Num()) < Length)
		{
			return Fail(FString::Printf(TEXT("buffer %d holds %d bytes where %llu were due."), b, Data.Num(), Length));
		}
		BufferBase.Add(uint64(Bytes.Num()));
		BufferLength.Add(Length);
		Bytes.Append(Data.GetData(), int64(Length));
		Bytes.SetNumZeroed(Align(Bytes.Num(), 4));
	}

	// The first primitive that carries the extension.
	TSharedPtr<FJsonObject> Primitive;
	int32 MeshIndex = INDEX_NONE, Others = 0;
	for (int32 m = 0; m < MeshesJson->Num(); m++)
	{
		const TArray<TSharedPtr<FJsonValue>>* Primitives = nullptr;
		if (!(*MeshesJson)[m]->AsObject()->TryGetArrayField(TEXT("primitives"), Primitives))
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& P : *Primitives)
		{
			const TSharedPtr<FJsonObject>* Extensions = nullptr;
			if (P->AsObject()->TryGetObjectField(TEXT("extensions"), Extensions) && (*Extensions)->HasField(kExtension))
			{
				if (!Primitive.IsValid())
				{
					Primitive = P->AsObject();
					MeshIndex = m;
				}
				else
				{
					Others++;
				}
			}
		}
	}
	if (!Primitive.IsValid())
	{
		return Fail(TEXT("no primitive with KHR_gaussian_splatting."));
	}
	if (Others > 0)
	{
		UE_LOG(LogGSplatEditor, Warning, TEXT("%s: %d more primitives with gaussians are left out; the first is imported."), *Filename, Others);
	}
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (Root->TryGetArrayField(TEXT("nodes"), Nodes))
	{
		for (const TSharedPtr<FJsonValue>& N : *Nodes)
		{
			int32 Mesh = INDEX_NONE;
			if (N->AsObject()->TryGetNumberField(TEXT("mesh"), Mesh) && Mesh == MeshIndex && !IsIdentityNode(N->AsObject()))
			{
				UE_LOG(LogGSplatEditor, Warning, TEXT("%s: the node holding the gaussians has a transform, which is not applied; place the actor instead."), *Filename);
			}
		}
	}
	const TSharedPtr<FJsonObject> Extension = Primitive->GetObjectField(TEXT("extensions"))->GetObjectField(kExtension);
	FString Kernel = TEXT("ellipse"), ColorSpace = TEXT("srgb_rec709_display");
	Extension->TryGetStringField(TEXT("kernel"), Kernel);
	Extension->TryGetStringField(TEXT("colorSpace"), ColorSpace);
	if (Kernel != TEXT("ellipse"))
	{
		UE_LOG(LogGSplatEditor, Warning, TEXT("%s: kernel '%s' is drawn as an ellipse."), *Filename, *Kernel);
	}
	const bool bLinear = ColorSpace.StartsWith(TEXT("lin"));
	if (!bLinear && ColorSpace != TEXT("srgb_rec709_display"))
	{
		UE_LOG(LogGSplatEditor, Warning, TEXT("%s: colour space '%s' is read as sRGB."), *Filename, *ColorSpace);
	}

	// The accessors, as descriptors the kernel reads.
	const TSharedPtr<FJsonObject> Attributes = Primitive->GetObjectField(TEXT("attributes"));
	TArray<FString> Names = {
		TEXT("POSITION"),
		TEXT("KHR_gaussian_splatting:ROTATION"),
		TEXT("KHR_gaussian_splatting:SCALE"),
		TEXT("KHR_gaussian_splatting:OPACITY"),
		TEXT("COLOR_0") };
	for (uint32 k = 0; k < 16; k++)
	{
		const uint32 l = k == 0 ? 0 : k < 4 ? 1 : k < 9 ? 2 : 3;
		Names.Add(FString::Printf(TEXT("KHR_gaussian_splatting:SH_DEGREE_%u_COEF_%u"), l, k - l * l));
	}
	const uint32 Want[kSlots] = { 3, 4, 3, 1, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
	TArray<uint32> Table;
	Table.SetNumZeroed(kSlots * 4);
	int64 Count = -1;
	TSharedPtr<FJsonObject> PositionAccessor;
	uint32 Rest = 0;
	for (int32 s = 0; s < kSlots; s++)
	{
		int32 Index = INDEX_NONE;
		if (!Attributes->TryGetNumberField(Names[s], Index))
		{
			continue;
		}
		if (Index < 0 || Index >= AccessorsJson->Num())
		{
			return Fail(FString::Printf(TEXT("%s names accessor %d, which is not there."), *Names[s], Index));
		}
		const TSharedPtr<FJsonObject> Accessor = (*AccessorsJson)[Index]->AsObject();
		if (Accessor->HasField(TEXT("sparse")))
		{
			return Fail(FString::Printf(TEXT("%s is a sparse accessor, which is not read."), *Names[s]));
		}
		const uint32 Type = uint32(Accessor->GetNumberField(TEXT("componentType")));
		const uint32 Components = TypeComponents(Accessor->GetStringField(TEXT("type")));
		const int64 n = int64(Accessor->GetNumberField(TEXT("count")));
		bool bNormalized = false;
		Accessor->TryGetBoolField(TEXT("normalized"), bNormalized);
		const uint32 Size = ComponentBytes(Type);
		if (Size == 0 || Components == 0 || (Want[s] != 0 && Components != Want[s]) || (s == 4 && Components < 3))
		{
			return Fail(FString::Printf(TEXT("%s is a %s of component type %u, which is not a gaussian's."),
				*Names[s], *Accessor->GetStringField(TEXT("type")), Type));
		}
		if (Count >= 0 && n != Count)
		{
			return Fail(FString::Printf(TEXT("%s holds %lld elements where the others hold %lld."), *Names[s], n, Count));
		}
		Count = n;
		if (s == 0)
		{
			PositionAccessor = Accessor;
		}
		if (s > 5)
		{
			Rest = FMath::Max(Rest, uint32(s - 5));
		}
		int32 View = INDEX_NONE;
		if (!Accessor->TryGetNumberField(TEXT("bufferView"), View))
		{
			continue;   // all zeros, as glTF reads an accessor with no view
		}
		if (View < 0 || View >= ViewsJson->Num())
		{
			return Fail(FString::Printf(TEXT("%s names buffer view %d, which is not there."), *Names[s], View));
		}
		const TSharedPtr<FJsonObject> ViewJson = (*ViewsJson)[View]->AsObject();
		const int32 Buffer = int32(ViewJson->GetNumberField(TEXT("buffer")));
		double ViewOffset = 0, AccessorOffset = 0, Stride = 0;
		ViewJson->TryGetNumberField(TEXT("byteOffset"), ViewOffset);
		ViewJson->TryGetNumberField(TEXT("byteStride"), Stride);
		Accessor->TryGetNumberField(TEXT("byteOffset"), AccessorOffset);
		const uint64 Element = uint64(Size) * Components;
		const uint64 Step = Stride > 0 ? uint64(Stride) : Element;
		const uint64 ViewLength = uint64(ViewJson->GetNumberField(TEXT("byteLength")));
		if (Buffer < 0 || Buffer >= BufferBase.Num() || uint64(ViewOffset) + ViewLength > BufferLength[Buffer]
			|| (n > 0 && uint64(AccessorOffset) + Step * uint64(n - 1) + Element > ViewLength))
		{
			return Fail(FString::Printf(TEXT("%s runs past its buffer."), *Names[s]));
		}
		const uint64 Address = BufferBase[Buffer] + uint64(ViewOffset) + uint64(AccessorOffset);
		if (Address % Size != 0 || Step % Size != 0 || Address + Step * uint64(FMath::Max<int64>(n, 1)) > 0xFFFFFFFFull)
		{
			return Fail(FString::Printf(TEXT("%s is not aligned to its components, or lies past 4 GB."), *Names[s]));
		}
		Table[s * 4 + 0] = uint32(Address);
		Table[s * 4 + 1] = uint32(Step);
		Table[s * 4 + 2] = Type | (bNormalized ? (1u << 16) : 0u) | (Components << 20);
		Table[s * 4 + 3] = 1;
	}
	if (!PositionAccessor.IsValid() || Count <= 0 || Count > 0x7FFFFFFF)
	{
		return Fail(TEXT("no gaussians: POSITION missing or empty."));
	}
	const TArray<TSharedPtr<FJsonValue>>* MinJson = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* MaxJson = nullptr;
	if (!PositionAccessor->TryGetArrayField(TEXT("min"), MinJson) || !PositionAccessor->TryGetArrayField(TEXT("max"), MaxJson)
		|| MinJson->Num() < 3 || MaxJson->Num() < 3)
	{
		return Fail(TEXT("POSITION has no min and max, which glTF requires."));
	}
	// Coefficients are drawn up to the highest present; a degree is all or none
	// in a writer that follows the extension.
	Rest = Rest > 8 ? 15 : Rest > 3 ? 8 : Rest > 0 ? 3 : 0;

	const uint32 TableAt = uint32(Bytes.Num());
	Bytes.Append(reinterpret_cast<const uint8*>(Table.GetData()), Table.Num() * sizeof(uint32));

	const TArray<EGSplatSwizzleAxis> Swizzle{ EGSplatSwizzleAxis::Z, EGSplatSwizzleAxis::Xn, EGSplatSwizzleAxis::Y };
	const uint32 n = uint32(Count);
	ImportData.Swizzle = Swizzle;
	ImportData.ScaleFactor = 100.0f;
	ImportData.bApplyGammaCorrection = !bLinear;
	ImportData.NumSplats = int32(n);
	ImportData.bHasHighOrderSH = Rest > 0;
	ImportData.bHasNormals = false;
	ImportData.bHasMetallicRoughness = false;

	Splats.Empty();
	Splats.bPacked = true;
	Splats.PackedFormat = 4;
	Splats.SplatCount = n;
	Splats.bHasHighOrderSH = Rest > 0 && ImportData.bImportHighOrderSH;
	Splats.PackedUnitsToCm = ImportData.ScaleFactor;
	Splats.PackedBytes = MoveTemp(Bytes);
	Splats.PackedStreams = { TableAt };
	Splats.PackedSpz = FIntVector(0, 0, Splats.bHasHighOrderSH ? int32(Rest) : 0);
	Splats.FileAxisX = SwizzleVector(1.0f, 0.0f, 0.0f, Swizzle);
	Splats.FileAxisY = SwizzleVector(0.0f, 1.0f, 0.0f, Swizzle);
	Splats.FileAxisZ = SwizzleVector(0.0f, 0.0f, 1.0f, Swizzle);
	Splats.bLinearizeColor = !bLinear;

	FBox Box(ForceInit);
	Box += FVector(SwizzleVector(float((*MinJson)[0]->AsNumber()), float((*MinJson)[1]->AsNumber()), float((*MinJson)[2]->AsNumber()), Swizzle) * ImportData.ScaleFactor);
	Box += FVector(SwizzleVector(float((*MaxJson)[0]->AsNumber()), float((*MaxJson)[1]->AsNumber()), float((*MaxJson)[2]->AsNumber()), Swizzle) * ImportData.ScaleFactor);
	Splats.SetBounds(FBoxSphereBounds(Box));

	UE_LOG(LogGSplatEditor, Display, TEXT("%s: %u gaussians, glTF KHR_gaussian_splatting, %u harmonic coefficients a colour, %s."),
		*Filename, n, Rest, *ColorSpace);
	return true;
}

#endif // WITH_EDITOR
