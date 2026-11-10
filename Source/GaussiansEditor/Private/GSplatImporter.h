#pragma once

// The .athc and .athz cloud importers and the swizzle are the runtime
// module's (GSplatRuntimeImport.h): a packaged game reads them too.
#include "GSplatRuntimeImport.h"

class UGSplatImportData;
class UGSplatAsset;
struct FGaussianSplatData;


namespace happly
{
	class PLYData;
}


namespace GSplatImporter
{
	// Will allocate ply data - please delete yourself later
	bool ParsePly(const FString& Filename, UGSplatImportData& ImportData, happly::PLYData** OutPlyData);
	bool ImportPly(const UGSplatImportData& ImportData, happly::PLYData* PlyData, FGaussianSplatData& Splats);

	// athenea: a USD stage's first gaussian ParticleField (GSplatUsdImporter.cpp).
	bool IsUsdFile(const FString& Filename);
	bool StageHasSplats(const FString& Filename);
	bool ImportUsd(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats);

	// athenea: .splat and .spz kept as their bytes (GSplatFileImporter.cpp).
	bool IsSplatFile(const FString& Filename);
	bool ImportSplatFile(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats);

	// athenea: PlayCanvas's .sog, or an unbundled SOG's meta.json (GSplatSogImporter.cpp).
	bool IsSogFile(const FString& Filename);
	bool ImportSog(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats);

	// athenea: a zip's entries by name (stored or deflated, no zip64; GSplatSogImporter.cpp).
	bool ReadZip(const TArray<uint8>& Zip, TMap<FString, TArray<uint8>>& Out, FString& Why);

	// athenea: a .athz -- the web's scene (a zip: scene.json, its clouds as
	// .athc, its sky) or athenea's paged cloud ('ATHZ'); GSplatSceneImporter.cpp.
	bool IsAthzFile(const FString& Filename);
	bool IsAthzScene(const FString& Filename);
	bool ImportAthzScene(const FString& Filename, UObject* InParent, FName InName, EObjectFlags Flags, UGSplatAsset& First);

	// athenea: glTF with KHR_gaussian_splatting, .glb or .gltf (GSplatGltfImporter.cpp).
	bool IsGltfFile(const FString& Filename);
	bool GltfHasSplats(const FString& Filename);
	bool ImportGltf(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats);
};

