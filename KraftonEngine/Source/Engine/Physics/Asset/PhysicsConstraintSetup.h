#pragma once

#include "Object/Object.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Source/Engine/Physics/Asset/PhysicsConstraintSetup.generated.h"


enum class EConstraintMotion : uint8
{
    Locked  = 0,
    Limited = 1,
    Free    = 2,
};

// UPhysicsConstraintSetup  —  관절 제한 설계도
UCLASS()
class UPhysicsConstraintSetup : public UObject
{
public:
	GENERATED_BODY()
    UPhysicsConstraintSetup() = default;

    FString ParentBoneName;
    FString ChildBoneName;

    FVector ParentAnchorPos = FVector(0.f, 0.f, 0.f);
    FQuat   ParentAnchorRot = FQuat();  // identity

    EConstraintMotion TwistMotion = EConstraintMotion::Limited;
    float TwistLimitAngle  = 45.f;

    EConstraintMotion Swing1Motion = EConstraintMotion::Limited;
    float Swing1LimitAngle = 45.f;

    EConstraintMotion Swing2Motion = EConstraintMotion::Limited;
    float Swing2LimitAngle = 45.f;

    bool bLockLinearMotion = true;
};
