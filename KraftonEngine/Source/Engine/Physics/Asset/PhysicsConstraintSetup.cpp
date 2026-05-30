#include "Physics/Asset/PhysicsConstraintSetup.h"
#include "Serialization/Archive.h"

void UPhysicsConstraintSetup::Serialize(FArchive& Ar)
{
    Ar << ParentBoneName;
    Ar << ChildBoneName;
    Ar << ParentAnchorPos;
    Ar << ParentAnchorRot;
    Ar << TwistMotion;
    Ar << TwistLimitAngle;
    Ar << Swing1Motion;
    Ar << Swing1LimitAngle;
    Ar << Swing2Motion;
    Ar << Swing2LimitAngle;
    Ar << bLockLinearMotion;
}
