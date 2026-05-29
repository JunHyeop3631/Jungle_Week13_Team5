#pragma once

#include "Physics/IPhysicsRuntime.h"

namespace physx
{
	class PxDefaultCpuDispatcher;
	class PxFoundation;
	class PxMaterial;
	class PxPhysics;
	class PxScene;
}

class FPhysXRuntime : public IPhysicsRuntime
{
public:
	FPhysXRuntime() = default;
	~FPhysXRuntime() override;

	bool Initialize();
	void Shutdown();
	void Simulate(float DeltaTime);

	FBodyInstance* CreateRigidBody(const FPhysicsBodyDesc& Desc) override;
	void DestroyRigidBody(FBodyInstance* Body) override;

	FPhysicsShapeHandle CreateShape(FBodyInstance* Body, const FPhysicsShapeDesc& Desc) override;

	FConstraintInstance* CreateD6Joint(const FPhysicsConstraintDesc& Desc) override;
	void DestroyJoint(FConstraintInstance* Joint) override;

	bool GetBodyTransform(const FBodyInstance* Body, FTransform& OutTransform) const override;
	void SetBodyTransform(FBodyInstance* Body, const FTransform& Transform, bool bTeleport = true) override;
	void SetKinematicTarget(FBodyInstance* Body, const FTransform& Transform) override;

	void GetPhysicsStats(FPhysicsStats& OutStats) const override;
	void ExtractPhysicsDebugLines(TArray<FPhysicsDebugLine>& OutLines) const override;

private:
	physx::PxFoundation* Foundation = nullptr;
	physx::PxPhysics* Physics = nullptr;
	physx::PxScene* Scene = nullptr;
	physx::PxDefaultCpuDispatcher* Dispatcher = nullptr;
	physx::PxMaterial* DefaultMaterial = nullptr;
	bool bExtensionsInitialized = false;

	TArray<FBodyInstance*> Bodies;
	TArray<FConstraintInstance*> Joints;
	uint64 NextSerial = 1;

	uint64 AllocateSerial() { return NextSerial++; }
};
