#pragma once

#include "Physics/IPhysicsScene.h"

namespace physx
{
	class PxAggregate;
	class PxDefaultCpuDispatcher;
	class PxFoundation;
	class PxMaterial;
	class PxPhysics;
	class PxScene;
}

class FPhysXSimulationEventCallback;

class FPhysXRuntime : public IPhysicsScene
{
public:
	FPhysXRuntime() = default;
	~FPhysXRuntime() override;

	bool Initialize() override;
	void Shutdown() override;
	void Simulate(float DeltaTime) override;

	void RegisterComponent(UPrimitiveComponent* Comp) override;
	void UnregisterComponent(UPrimitiveComponent* Comp) override;
	void RebuildBody(UPrimitiveComponent* Comp) override;

	FBodyInstance* CreateRigidBody(const FPhysicsBodyDesc& Desc) override;
	void DestroyRigidBody(FBodyInstance* Body) override;

	FPhysicsAggregateHandle CreateAggregate(int32 MaxActors, bool bEnableSelfCollision) override;
	void DestroyAggregate(const FPhysicsAggregateHandle& Handle) override;

	FPhysicsShapeHandle CreateShape(FBodyInstance* Body, const FPhysicsShapeDesc& Desc) override;

	FConstraintInstance* CreateD6Joint(const FPhysicsConstraintDesc& Desc) override;
	void DestroyJoint(FConstraintInstance* Joint) override;

	FVehicle4WInstance* CreateVehicle4W(const FVehicle4WDesc& Desc) override;
	void DestroyVehicle4W(FVehicle4WInstance* Vehicle) override;
	void SetVehicle4WInput(FVehicle4WInstance* Vehicle, const FVehicle4WInput& Input) override;
	bool GetVehicle4WWheelTransforms(const FVehicle4WInstance* Vehicle, TStaticArray<FTransform, 4>& OutTransforms) const override;

	void AddForce(UPrimitiveComponent* Comp, const FVector& Force) override;
	void AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation) override;
	void AddTorque(UPrimitiveComponent* Comp, const FVector& Torque) override;

	FVector GetLinearVelocity(UPrimitiveComponent* Comp) const override;
	void SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel) override;
	FVector GetAngularVelocity(UPrimitiveComponent* Comp) const override;
	void SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel) override;

	void SetMass(UPrimitiveComponent* Comp, float Mass) override;
	float GetMass(UPrimitiveComponent* Comp) const override;
	void SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset) override;
	FVector GetCenterOfMass(UPrimitiveComponent* Comp) const override;

	bool Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic,
		const AActor* IgnoreActor = nullptr) const override;

	bool RaycastByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		uint32 ObjectTypeMask, const AActor* IgnoreActor = nullptr) const override;

	bool SphereSweepShapeComponents(const FVector& Start, const FVector& Dir, float MaxDist, float Radius,
		FHitResult& OutHit,
		ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic,
		const AActor* IgnoreActor = nullptr) const override;

	bool GetBodyTransform(const FBodyInstance* Body, FTransform& OutTransform) const override;
	void SetBodyTransform(FBodyInstance* Body, const FTransform& Transform, bool bTeleport = true) override;
	void SetKinematicTarget(FBodyInstance* Body, const FTransform& Transform) override;
	void SetBodyType(FBodyInstance* Body, EPhysicsBodyType NewType) override;
	void SetRagdollBodyFilter(FBodyInstance* Body, uint32 GroupId, uint32 BodyIndex, uint32 IgnoreMask) override;

	void GetPhysicsStats(FPhysicsStats& OutStats) const override;
	void ExtractPhysicsDebugLines(TArray<FPhysicsDebugLine>& OutLines) const override;
	void ExtractVehicleDebugLines(TArray<FPhysicsDebugLine>& OutLines) const override;

private:
	physx::PxFoundation* Foundation = nullptr;
	physx::PxPhysics* Physics = nullptr;
	physx::PxScene* Scene = nullptr;
	physx::PxDefaultCpuDispatcher* Dispatcher = nullptr;
	physx::PxMaterial* DefaultMaterial = nullptr;
	FPhysXSimulationEventCallback* EventCallback = nullptr;
	bool bVehicleSdkAvailable = false;

	TArray<FBodyInstance*> Bodies;
	TArray<FConstraintInstance*> Joints;
	TArray<FVehicle4WInstance*> Vehicles;
	TArray<physx::PxAggregate*> Aggregates;
	uint64 NextSerial = 1;

	uint64 AllocateSerial() { return NextSerial++; }
	FBodyInstance* FindBodyByComponent(const UPrimitiveComponent* Comp) const;
	bool BuildBodyDescFromComponent(UPrimitiveComponent* Comp, FPhysicsBodyDesc& OutDesc) const;
	FPhysicsShapeHandle CreateShape_AssumesLocked(FBodyInstance* Body, const FPhysicsShapeDesc& Desc);
};
