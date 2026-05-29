#pragma once

#include "Physics/ConstraintInstance.h"

class IPhysicsRuntime
{
public:
	virtual ~IPhysicsRuntime() = default;

	virtual FBodyInstance* CreateRigidBody(const FPhysicsBodyDesc& Desc) = 0;
	virtual void DestroyRigidBody(FBodyInstance* Body) = 0;

	virtual FPhysicsShapeHandle CreateShape(FBodyInstance* Body, const FPhysicsShapeDesc& Desc) = 0;

	virtual FConstraintInstance* CreateD6Joint(const FPhysicsConstraintDesc& Desc) = 0;
	virtual void DestroyJoint(FConstraintInstance* Joint) = 0;

	virtual bool GetBodyTransform(const FBodyInstance* Body, FTransform& OutTransform) const = 0;
	virtual void SetBodyTransform(FBodyInstance* Body, const FTransform& Transform, bool bTeleport = true) = 0;
	virtual void SetKinematicTarget(FBodyInstance* Body, const FTransform& Transform) = 0;

	virtual void GetPhysicsStats(FPhysicsStats& OutStats) const = 0;
	virtual void ExtractPhysicsDebugLines(TArray<FPhysicsDebugLine>& OutLines) const = 0;
};
