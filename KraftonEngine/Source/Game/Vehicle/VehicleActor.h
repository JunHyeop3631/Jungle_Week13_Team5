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
	void InitDefaultComponents();

protected:
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

	UPROPERTY(Save, Edit)
	FVector WheelOffset_FL = FVector(5.0f, -5.0f, 0.0f);
	UPROPERTY(Save, Edit)
	FVector WheelOffset_FR = FVector(5.0f, 5.0f, 0.0f);
	UPROPERTY(Save, Edit)
	FVector WheelOffset_BL = FVector(-5.0f, 5.0f, 0.0f);
	UPROPERTY(Save, Edit)
	FVector WheelOffset_BR = FVector(-5.0f, -5.0f, 0.0f);
};

