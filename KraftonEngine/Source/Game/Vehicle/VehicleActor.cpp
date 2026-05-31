#include "VehicleActor.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "GameFramework/World.h"
#include "Physics/IPhysicsScene.h"

#include "Physics/PhysXHelpers.h"

void AVehicleActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

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

void AVehicleActor::BeginPlay()
{
	Super::BeginPlay();

	IPhysicsScene* PhysicsScene = GetWorld()->GetPhysicsScene();

	//Create Body
	FPhysicsBodyDesc PhysicsBodyDesc;
	PhysicsBodyDesc.BodyType = EPhysicsBodyType::Dynamic;
	PhysicsBodyDesc.Mass = 1500.0f;
	FBodyInstance* VehicleBody = PhysicsScene->CreateRigidBody(PhysicsBodyDesc);
	VehicleRigidActor = PhysXHelpers::GetPxDynamic(VehicleBody);

	//Create Vehicle
	FVehicle4WDesc VehicleDesc;
	VehicleDesc.ChassisMass = PhysicsBodyDesc.Mass;
	VehicleDesc.WheelMass = 20.0f;
	VehicleDesc.WheelCenterOffsets[0] = WheelOffset_FL;
	VehicleDesc.WheelCenterOffsets[1] = WheelOffset_FR;
	VehicleDesc.WheelCenterOffsets[2] = WheelOffset_BL;
	VehicleDesc.WheelCenterOffsets[3] = WheelOffset_BR;

	FVehicle4WInstance* VehicleInstance = PhysicsScene->CreateVehicle4W(VehicleDesc);
	Vehicle = PhysXHelpers::GetPxVehicleDrive4W(VehicleInstance);
}

void AVehicleActor::InitDefaultComponents()
{
	BodyMeshComponent = AddComponent<UStaticMeshComponent>();
	SetRootComponent(BodyMeshComponent);

	WheelMeshComponent_FL = AddComponent<UStaticMeshComponent>();
	WheelMeshComponent_FR = AddComponent<UStaticMeshComponent>();
	WheelMeshComponent_BL = AddComponent<UStaticMeshComponent>();
	WheelMeshComponent_BR = AddComponent<UStaticMeshComponent>();

	WheelMeshComponent_BL->SetParent(BodyMeshComponent);
	WheelMeshComponent_BL->SetRelativeLocation(WheelOffset_BL);

	WheelMeshComponent_BR->SetParent(BodyMeshComponent);
	WheelMeshComponent_BR->SetRelativeLocation(WheelOffset_BR);

	WheelMeshComponent_FL->SetParent(BodyMeshComponent);
	WheelMeshComponent_FL->SetRelativeLocation(WheelOffset_FL);

	WheelMeshComponent_FR->SetParent(BodyMeshComponent);
	WheelMeshComponent_FR->SetRelativeLocation(WheelOffset_FR);



}
