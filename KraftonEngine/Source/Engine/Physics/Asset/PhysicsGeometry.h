#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Serialization/Archive.h"

// PhysicsGeometry.h  —  콜리전 기하 요소

struct FKSphereElem
{
    FVector Center = FVector(0.f, 0.f, 0.f);
    float   Radius = 0.1f;

    void Serialize(FArchive& Ar);
};

struct FKBoxElem
{
    FVector Center   = FVector(0.f, 0.f, 0.f);
    FQuat   Rotation = FQuat();
    float   HalfX    = 0.1f;
    float   HalfY    = 0.1f;
    float   HalfZ    = 0.1f;

    void Serialize(FArchive& Ar);
};

struct FKCapsuleElem
{
    FVector Center     = FVector(0.f, 0.f, 0.f);
    FQuat   Rotation   = FQuat();
    float   Radius     = 0.1f;
    float   HalfHeight = 0.1f;

    void Serialize(FArchive& Ar);
};

// 한 본에 귀속되는 모든 콜리전 기하를 묶는 컨테이너
struct FKAggregateGeom
{
    TArray<FKSphereElem>  SphereElems;
    TArray<FKBoxElem>     BoxElems;
    TArray<FKCapsuleElem> CapsuleElems;

    bool  IsEmpty()           const { return SphereElems.empty() && BoxElems.empty() && CapsuleElems.empty(); }
    int32 GetTotalPrimCount() const { return (int32)(SphereElems.size() + BoxElems.size() + CapsuleElems.size()); }

    void Serialize(FArchive& Ar);
};
