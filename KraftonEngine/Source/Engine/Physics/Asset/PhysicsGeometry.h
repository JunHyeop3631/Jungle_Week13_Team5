#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Quat.h"

// PhysicsGeometry.h  —  콜리전 기하 요소

struct FKSphereElem
{
    FVector Center = FVector(0.f, 0.f, 0.f);
    float Radius = 15.f;
};

struct FKBoxElem
{
    FVector Center = FVector(0.f, 0.f, 0.f);
    FQuat Rotation = FQuat();
    float HalfX = 10.f;
    float HalfY = 10.f;
    float HalfZ = 10.f;
};

struct FKCapsuleElem
{
    FVector Center = FVector(0.f, 0.f, 0.f);
    FQuat Rotation = FQuat();
    float Radius = 10.f;
    float HalfHeight = 20.f;
};

// 한 본에 귀속되는 모든 콜리전 기하를 묶는 컨테이너
struct FKAggregateGeom
{
    TArray<FKSphereElem> SphereElems;
    TArray<FKBoxElem> BoxElems;
    TArray<FKCapsuleElem> CapsuleElems;

    bool IsEmpty() const { return SphereElems.empty() && BoxElems.empty() && CapsuleElems.empty(); }
    int32 GetTotalPrimCount() const { return (int32)(SphereElems.size() + BoxElems.size() + CapsuleElems.size()); }
};
