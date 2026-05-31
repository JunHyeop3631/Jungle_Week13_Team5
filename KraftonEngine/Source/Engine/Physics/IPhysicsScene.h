#pragma once

#include "Core/Types/CollisionTypes.h"
#include "Physics/ConstraintInstance.h"

class AActor;
class UPrimitiveComponent;

class IPhysicsScene
{
public:
	virtual ~IPhysicsScene() = default;

	virtual bool Initialize() = 0;
	virtual void Shutdown() = 0;
	virtual void Simulate(float DeltaTime) = 0;

	virtual void RegisterComponent(UPrimitiveComponent* Comp) = 0;
	virtual void UnregisterComponent(UPrimitiveComponent* Comp) = 0;
	virtual void RebuildBody(UPrimitiveComponent* Comp) = 0;

	virtual FBodyInstance* CreateRigidBody(const FPhysicsBodyDesc& Desc) = 0;
	virtual void DestroyRigidBody(FBodyInstance* Body) = 0;

	virtual FPhysicsShapeHandle CreateShape(FBodyInstance* Body, const FPhysicsShapeDesc& Desc) = 0;

	virtual FConstraintInstance* CreateD6Joint(const FPhysicsConstraintDesc& Desc) = 0;
	virtual void DestroyJoint(FConstraintInstance* Joint) = 0;

	virtual FVehicle4WInstance* CreateVehicle4W(const FVehicle4WDesc& Desc) = 0;
	virtual void DestroyVehicle4W(FVehicle4WInstance* Vehicle) = 0;
	virtual void SetVehicle4WInput(FVehicle4WInstance* Vehicle, const FVehicle4WInput& Input) = 0;
	virtual bool GetVehicle4WWheelTransforms(const FVehicle4WInstance* Vehicle, TStaticArray<FTransform, 4>& OutTransforms) const = 0;

	virtual void AddForce(UPrimitiveComponent* Comp, const FVector& Force) = 0;
	virtual void AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation) = 0;
	virtual void AddTorque(UPrimitiveComponent* Comp, const FVector& Torque) = 0;

	virtual FVector GetLinearVelocity(UPrimitiveComponent* Comp) const = 0;
	virtual void SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel) = 0;
	virtual FVector GetAngularVelocity(UPrimitiveComponent* Comp) const = 0;
	virtual void SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel) = 0;

	virtual void SetMass(UPrimitiveComponent* Comp, float Mass) = 0;
	virtual float GetMass(UPrimitiveComponent* Comp) const = 0;
	virtual void SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset) = 0;
	virtual FVector GetCenterOfMass(UPrimitiveComponent* Comp) const = 0;

	virtual bool Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic,
		const AActor* IgnoreActor = nullptr) const = 0;

	virtual bool RaycastByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		uint32 ObjectTypeMask, const AActor* IgnoreActor = nullptr) const = 0;

	virtual bool SphereSweepShapeComponents(const FVector& Start, const FVector& Dir, float MaxDist, float Radius,
		FHitResult& OutHit,
		ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic,
		const AActor* IgnoreActor = nullptr) const = 0;

	virtual bool GetBodyTransform(const FBodyInstance* Body, FTransform& OutTransform) const = 0;
	virtual void SetBodyTransform(FBodyInstance* Body, const FTransform& Transform, bool bTeleport = true) = 0;
	virtual void SetKinematicTarget(FBodyInstance* Body, const FTransform& Transform) = 0;
	// Dynamic/Kinematic runtime switch. Passive ragdoll toggles instantiated bodies to Dynamic.
	// Static conversion would require actor recreation, so runtime implementations may ignore it.
	virtual void SetBodyType(FBodyInstance* Body, EPhysicsBodyType NewType) = 0;

	virtual void GetPhysicsStats(FPhysicsStats& OutStats) const = 0;
	virtual void ExtractPhysicsDebugLines(TArray<FPhysicsDebugLine>& OutLines) const = 0;
	virtual void ExtractVehicleDebugLines(TArray<FPhysicsDebugLine>& OutLines) const = 0;
};
