#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Serialization/Archive.h"

// PhysicsGeometry.h  —  콜리전 기하 요소

struct FKSphereElem
{
    FVector Center = FVector(0.f, 0.f, 0.f);
    float   Radius = 15.f;

    void Serialize(FArchive& Ar)
    {
        Ar << Center;
        Ar << Radius;
    }
};

struct FKBoxElem
{
    FVector Center   = FVector(0.f, 0.f, 0.f);
    FQuat   Rotation = FQuat();
    float   HalfX    = 10.f;
    float   HalfY    = 10.f;
    float   HalfZ    = 10.f;

    void Serialize(FArchive& Ar)
    {
        Ar << Center;
        Ar << Rotation;
        Ar << HalfX;
        Ar << HalfY;
        Ar << HalfZ;
    }
};

struct FKCapsuleElem
{
    FVector Center     = FVector(0.f, 0.f, 0.f);
    FQuat   Rotation   = FQuat();
    float   Radius     = 10.f;
    float   HalfHeight = 20.f;

    void Serialize(FArchive& Ar)
    {
        Ar << Center;
        Ar << Rotation;
        Ar << Radius;
        Ar << HalfHeight;
    }
};

// 한 본에 귀속되는 모든 콜리전 기하를 묶는 컨테이너
struct FKAggregateGeom
{
    TArray<FKSphereElem>  SphereElems;
    TArray<FKBoxElem>     BoxElems;
    TArray<FKCapsuleElem> CapsuleElems;

    bool  IsEmpty()           const { return SphereElems.empty() && BoxElems.empty() && CapsuleElems.empty(); }
    int32 GetTotalPrimCount() const { return (int32)(SphereElems.size() + BoxElems.size() + CapsuleElems.size()); }

    void Serialize(FArchive& Ar)
    {
        // Sphere
        uint32 SphereCount = (uint32)SphereElems.size();
        Ar << SphereCount;
        if (Ar.IsLoading()) SphereElems.resize(SphereCount);
        for (auto& E : SphereElems) E.Serialize(Ar);

        // Box
        uint32 BoxCount = (uint32)BoxElems.size();
        Ar << BoxCount;
        if (Ar.IsLoading()) BoxElems.resize(BoxCount);
        for (auto& E : BoxElems) E.Serialize(Ar);

        // Capsule
        uint32 CapsuleCount = (uint32)CapsuleElems.size();
        Ar << CapsuleCount;
        if (Ar.IsLoading()) CapsuleElems.resize(CapsuleCount);
        for (auto& E : CapsuleElems) E.Serialize(Ar);
    }
};
