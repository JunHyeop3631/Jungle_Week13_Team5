#pragma once
#include "GameFramework/AActor.h"

#include "PxPhysicsAPI.h"
#include "vehicle/PxVehicleSDK.h"
#include "vehicle/PxVehicleDrive4W.h"
#include "vehicle/PxVehicleUtil.h"
#include "vehicle/PxVehicleUtilSetup.h"
#include "vehicle/PxVehicleUtilControl.h"
#include "vehicle/PxVehicleUpdate.h"
class UStaticMeshComponent;

#include "Source/Game/Vehicle/VehicleActor.generated.h"

UCLASS()
class AVehicleActor : public AActor
{
public:
	GENERATED_BODY()
	AVehicleActor() = default;
	~AVehicleActor() override = default;
	void BeginPlay() override;
	void Tick(float DeltaTime) override;

private:

	//Render
	UStaticMeshComponent* BodyMeshComponent = nullptr;

	UStaticMeshComponent* WheelMeshComponent_FL = nullptr;
	UStaticMeshComponent* WheelMeshComponent_FR = nullptr;
	UStaticMeshComponent* WheelMeshComponent_BL = nullptr;
	UStaticMeshComponent* WheelMeshComponent_BR = nullptr;

	//PhysX Vehicle
	physx::PxRigidDynamic* VehicleRigidActor = nullptr;
	physx::PxVehicleDrive4W* Vehicle = nullptr;

	//Input
	physx::PxVehicleDrive4WRawInputData VehicleInputData;

	FVector WheelOffset_FL;
	FVector WheelOffset_FR;
	FVector WheelOffset_BL;
	FVector WheelOffset_BR;
};

