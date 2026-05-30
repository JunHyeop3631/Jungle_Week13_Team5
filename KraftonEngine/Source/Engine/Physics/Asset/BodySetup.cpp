#include "Physics/Asset/BodySetup.h"
#include "Serialization/Archive.h"

void UBodySetup::Serialize(FArchive& Ar)
{
    Ar << BoneName;
    AggregateGeom.Serialize(Ar);
    Ar << Mass;
    Ar << LinearDamping;
    Ar << AngularDamping;
    Ar << Friction;
    Ar << Restitution;
    Ar << bSimulatePhysics;
    // 아래 필드는 이후 버전에서 추가됨. (구버전 에셋은 재저장 필요)
    Ar << bEnableGravity;
    Ar << PhysicsType;
    Ar << CollisionEnabled;
}
