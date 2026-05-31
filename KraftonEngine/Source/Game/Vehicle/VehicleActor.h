#pragma once
#include "GameFramework/AActor.h"
#include "Physics/PhysicsTypes.h"

class IPhysicsScene;
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
	void EndPlay() override;
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

	FVehicle4WInput BuildVehicleInput() const;
	void ApplyVehicleTransforms(IPhysicsScene& PhysicsScene);

	IPhysicsScene* PhysicsSceneOwner = nullptr;
	FVehicle4WInstance* VehicleInstance = nullptr;

	UPROPERTY(Edit, Save, Category="Vehicle Input", DisplayName="Keyboard Input")
	bool bUseKeyboardInput = true;
	UPROPERTY(Edit, Save, Category="Vehicle Input", DisplayName="Auto Accelerate")
	bool bAutoAccelerate = false;

	UPROPERTY(Save, Edit)
	FVector WheelOffset_FL = FVector(1.30f, -0.80f, -0.35f);
	UPROPERTY(Save, Edit)
	FVector WheelOffset_FR = FVector(1.30f, 0.80f, -0.35f);
	UPROPERTY(Save, Edit)
	FVector WheelOffset_BL = FVector(-1.30f, -0.80f, -0.35f);
	UPROPERTY(Save, Edit)
	FVector WheelOffset_BR = FVector(-1.30f, 0.80f, -0.35f);
};

