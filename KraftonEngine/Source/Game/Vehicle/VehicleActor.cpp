#include "VehicleActor.h"
#include "Component/Primitive/StaticMeshComponent.h"

#include "Physics/PhysXHelpers.h"

void AVehicleActor::BeginPlay()
{
	BodyMeshComponent = AddComponent<UStaticMeshComponent>();
	SetRootComponent(BodyMeshComponent);

	WheelMeshComponent_FL = AddComponent<UStaticMeshComponent>();
	WheelMeshComponent_FR = AddComponent<UStaticMeshComponent>();
	WheelMeshComponent_BL = AddComponent<UStaticMeshComponent>();
	WheelMeshComponent_BR = AddComponent<UStaticMeshComponent>();

	WheelMeshComponent_BL->SetParent(BodyMeshComponent);
	WheelMeshComponent_BR->SetParent(BodyMeshComponent);
	WheelMeshComponent_FL->SetParent(BodyMeshComponent);
	WheelMeshComponent_FR->SetParent(BodyMeshComponent);

}

void AVehicleActor::Tick(float DeltaTime)
{
	AActor::Tick(DeltaTime);

	if (!Vehicle || !VehicleRigidActor)
		return;

	const physx::PxTransform Pose = VehicleRigidActor->getGlobalPose();

	// 1. PhysX rigid body pose -> Actor/Body mesh transform
	SetActorLocation(PhysXHelpers::ToFVector(Pose.p));
	SetActorRotation(FRotator::FromQuaternion(PhysXHelpers::ToFQuat(Pose.q)));

	// 2. PhysX wheel pose -> wheel mesh transform
	const physx::PxVehicleWheelsDynData& WheelDynData = Vehicle->mWheelsDynData;

	for (int32 i = 0; i < 4; ++i)
	{
		float RotationAngle = WheelDynData.getWheelRotationAngle(i);
		float SteerAngle = WheelDynData.getWheelRotationAngle(i);

		// wheel mesh에 회전 적용
	}
}
