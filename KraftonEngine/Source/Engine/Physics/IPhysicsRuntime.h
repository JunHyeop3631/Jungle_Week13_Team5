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
	virtual bool Initialize() = 0;

	virtual FVehicle4WInstance* CreateVehicle4W(const FVehicle4WDesc& Desc) = 0;
	virtual void DestroyVehicle4W(FVehicle4WInstance* Vehicle) = 0;
	virtual void SetVehicle4WInput(FVehicle4WInstance* Vehicle, const FVehicle4WInput& Input) = 0;
	virtual bool GetVehicle4WWheelTransforms(const FVehicle4WInstance* Vehicle, TStaticArray<FTransform, 4>& OutTransforms) const = 0;

	virtual void Simulate(float DeltaTime) = 0;

	virtual bool GetBodyTransform(const FBodyInstance* Body, FTransform& OutTransform) const = 0;
	virtual void SetBodyTransform(FBodyInstance* Body, const FTransform& Transform, bool bTeleport = true) = 0;
	virtual void SetKinematicTarget(FBodyInstance* Body, const FTransform& Transform) = 0;
	// Dynamic↔Kinematic 런타임 전환. 패시브 ragdoll 진입 시 Bodies[]를 Dynamic으로 토글하는 경로.
	// Dynamic 전환 시 구현체가 wake 시키도록 책임진다. Static 전환은 actor 재생성이 필요하므로 지원하지 않는다.
	virtual void SetBodyType(FBodyInstance* Body, EPhysicsBodyType NewType) = 0;

	virtual void GetPhysicsStats(FPhysicsStats& OutStats) const = 0;
	virtual void ExtractPhysicsDebugLines(TArray<FPhysicsDebugLine>& OutLines) const = 0;
	virtual void ExtractVehicleDebugLines(TArray<FPhysicsDebugLine>& OutLines) const = 0;
};
