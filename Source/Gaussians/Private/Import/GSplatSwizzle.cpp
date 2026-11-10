// Copyright (c) 2026 jesus luque.
//
// The file's axes taken into UE's, shared by every importer (the editor's and
// the runtime's), and the runtime importers' log.

#include "GSplatRuntimeImport.h"
#include "GSplatImportData.h"

DEFINE_LOG_CATEGORY(LogGSplatImport);

static float SwizzleComponent(float InX, float InY, float InZ, EGSplatSwizzleAxis Swizzle)
{
	switch (Swizzle)
	{
	case EGSplatSwizzleAxis::X:	 return  InX;
	case EGSplatSwizzleAxis::Y:	 return  InY;
	case EGSplatSwizzleAxis::Z:	 return  InZ;
	case EGSplatSwizzleAxis::Xn: return -InX;
	case EGSplatSwizzleAxis::Yn: return -InY;
	case EGSplatSwizzleAxis::Zn: return -InZ;
	default:
		checkNoEntry();
	}
	return 0.0f;
}

FVector3f SwizzleVector(float InX, float InY, float InZ, const TArray<EGSplatSwizzleAxis>& Swizzle)
{
	check(Swizzle.Num() == 3);
	return {
		SwizzleComponent(InX, InY, InZ, Swizzle[0]),
		SwizzleComponent(InX, InY, InZ, Swizzle[1]),
		SwizzleComponent(InX, InY, InZ, Swizzle[2])
	};
}

FVector3f SwizzleVector(const FVector3f& V, const TArray<EGSplatSwizzleAxis>& Swizzle)
{
	check(Swizzle.Num() == 3);
	return {
		SwizzleComponent(V.X, V.Y, V.Z, Swizzle[0]),
		SwizzleComponent(V.X, V.Y, V.Z, Swizzle[1]),
		SwizzleComponent(V.X, V.Y, V.Z, Swizzle[2])
	};
}

static float SwizzleComponentAbs(float InX, float InY, float InZ, EGSplatSwizzleAxis Swizzle)
{
	switch (Swizzle)
	{
	case EGSplatSwizzleAxis::X:
	case EGSplatSwizzleAxis::Xn: return  InX;
	case EGSplatSwizzleAxis::Y:
	case EGSplatSwizzleAxis::Yn: return  InY;
	case EGSplatSwizzleAxis::Z:
	case EGSplatSwizzleAxis::Zn: return  InZ;
	default:
		checkNoEntry();
	}
	return 0.0f;
}

FVector3f SwizzleVectorAbs(float InX, float InY, float InZ, const TArray<EGSplatSwizzleAxis>& Swizzle)
{
	check(Swizzle.Num() == 3);
	return {
		SwizzleComponentAbs(InX, InY, InZ, Swizzle[0]),
		SwizzleComponentAbs(InX, InY, InZ, Swizzle[1]),
		SwizzleComponentAbs(InX, InY, InZ, Swizzle[2])
	};
}

static FVector3f UnitVectorForAxis(EGSplatSwizzleAxis Axis)
{
	switch (Axis)
	{
	case EGSplatSwizzleAxis::X:	 return  FVector3f::UnitX();
	case EGSplatSwizzleAxis::Y:	 return  FVector3f::UnitY();
	case EGSplatSwizzleAxis::Z:	 return  FVector3f::UnitZ();
	case EGSplatSwizzleAxis::Xn: return -FVector3f::UnitX();
	case EGSplatSwizzleAxis::Yn: return -FVector3f::UnitY();
	case EGSplatSwizzleAxis::Zn: return -FVector3f::UnitZ();
	default:
		checkNoEntry();
	}
	return FVector3f::ZeroVector;
}

FVector4f SwizzleQuaternion(float InX, float InY, float InZ, float InW, const TArray<EGSplatSwizzleAxis>& Swizzle)
{
	check(Swizzle.Num() == 3);
	FVector3f XAxis = UnitVectorForAxis(Swizzle[0]);
	FVector3f YAxis = UnitVectorForAxis(Swizzle[1]);
	FVector3f ZAxis = UnitVectorForAxis(Swizzle[2]);
	FVector3f XCrossY = XAxis ^ YAxis;
	bool bRightHanded = (XCrossY | ZAxis) > 0;

	return FVector4f{
		SwizzleComponent(InX, InY, InZ, Swizzle[0]),
		SwizzleComponent(InX, InY, InZ, Swizzle[1]),
		SwizzleComponent(InX, InY, InZ, Swizzle[2]),
		bRightHanded ? -InW : InW
	};
}

