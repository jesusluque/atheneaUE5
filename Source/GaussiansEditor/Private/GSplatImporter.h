#pragma once

class UGSplatImportData;
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

	// athenea: a .athc's finest level, packed (GSplatAthcImporter.cpp).
	bool IsAthcFile(const FString& Filename);
	bool ImportAthc(const FString& Filename, UGSplatImportData& ImportData, FGaussianSplatData& Splats);
};

enum class EGSplatSwizzleAxis : uint8;

// The file's axes taken into UE's, shared by the PLY and USD importers.
FVector3f SwizzleVector(float InX, float InY, float InZ, const TArray<EGSplatSwizzleAxis>& Swizzle);
FVector3f SwizzleVectorAbs(float InX, float InY, float InZ, const TArray<EGSplatSwizzleAxis>& Swizzle);
FVector4f SwizzleQuaternion(float InX, float InY, float InZ, float InW, const TArray<EGSplatSwizzleAxis>& Swizzle);
