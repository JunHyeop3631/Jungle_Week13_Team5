#include "Physics/Asset/PhysicsGeometry.h"
#include "Serialization/Archive.h"

void FKSphereElem::Serialize(FArchive& Ar)
{
    Ar << Center;
    Ar << Radius;
}

void FKBoxElem::Serialize(FArchive& Ar)
{
    Ar << Center;
    Ar << Rotation;
    Ar << HalfX;
    Ar << HalfY;
    Ar << HalfZ;
}

void FKCapsuleElem::Serialize(FArchive& Ar)
{
    Ar << Center;
    Ar << Rotation;
    Ar << Radius;
    Ar << HalfHeight;
}

void FKAggregateGeom::Serialize(FArchive& Ar)
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
