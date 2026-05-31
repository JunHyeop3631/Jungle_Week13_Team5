#include "VehicleActor.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "GameFramework/World.h"
#include "Input/InputSystem.h"
#include "Physics/IPhysicsScene.h"

namespace
{
	FTransform MakeRelativeTransform(const FTransform& WorldTransform, const FTransform& ParentWorldTransform)
	{
		return FTransform(WorldTransform.ToMatrix() * ParentWorldTransform.ToMatrix().GetInverse());
	}
}

void AVehicleActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	(void)DeltaTime;

	if (!PhysicsSceneOwner || !VehicleInstance)
	{
		return;
	}

	PhysicsSceneOwner->SetVehicle4WInput(VehicleInstance, BuildVehicleInput());
	ApplyVehicleTransforms(*PhysicsSceneOwner);
}

void AVehicleActor::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	PhysicsSceneOwner = World ? World->GetPhysicsScene() : nullptr;
	if (!PhysicsSceneOwner)
	{
		return;
	}

	FVehicle4WDesc VehicleDesc;
	VehicleDesc.Name = GetName();
	VehicleDesc.WorldTransform = FTransform(GetActorLocation(), GetActorRotation(), FVector(1.0f, 1.0f, 1.0f));
	VehicleDesc.ChassisMass = 1500.0f;
	VehicleDesc.WheelMass = 20.0f;
	VehicleDesc.WheelCenterOffsets[static_cast<size_t>(EVehicle4WWheelIndex::FrontLeft)] = WheelOffset_FL;
	VehicleDesc.WheelCenterOffsets[static_cast<size_t>(EVehicle4WWheelIndex::FrontRight)] = WheelOffset_FR;
	VehicleDesc.WheelCenterOffsets[static_cast<size_t>(EVehicle4WWheelIndex::RearLeft)] = WheelOffset_BL;
	VehicleDesc.WheelCenterOffsets[static_cast<size_t>(EVehicle4WWheelIndex::RearRight)] = WheelOffset_BR;

	VehicleInstance = PhysicsSceneOwner->CreateVehicle4W(VehicleDesc);
	if (!VehicleInstance)
	{
		PhysicsSceneOwner = nullptr;
		return;
	}

	ApplyVehicleTransforms(*PhysicsSceneOwner);
}

void AVehicleActor::EndPlay()
{
	if (PhysicsSceneOwner && VehicleInstance)
	{
		PhysicsSceneOwner->DestroyVehicle4W(VehicleInstance);
	}
	VehicleInstance = nullptr;
	PhysicsSceneOwner = nullptr;

	Super::EndPlay();
}

void AVehicleActor::InitDefaultComponents()
{
	BodyMeshComponent = AddComponent<UStaticMeshComponent>();
	SetRootComponent(BodyMeshComponent);

	WheelMeshComponent_FL = AddComponent<UStaticMeshComponent>();
	WheelMeshComponent_FR = AddComponent<UStaticMeshComponent>();
	WheelMeshComponent_BL = AddComponent<UStaticMeshComponent>();
	WheelMeshComponent_BR = AddComponent<UStaticMeshComponent>();

	WheelMeshComponent_FL->SetParent(BodyMeshComponent);
	WheelMeshComponent_FL->SetRelativeLocation(WheelOffset_FL);

	WheelMeshComponent_FR->SetParent(BodyMeshComponent);
	WheelMeshComponent_FR->SetRelativeLocation(WheelOffset_FR);

	WheelMeshComponent_BL->SetParent(BodyMeshComponent);
	WheelMeshComponent_BL->SetRelativeLocation(WheelOffset_BL);

	WheelMeshComponent_BR->SetParent(BodyMeshComponent);
	WheelMeshComponent_BR->SetRelativeLocation(WheelOffset_BR);
}

FVehicle4WInput AVehicleActor::BuildVehicleInput() const
{
	FVehicle4WInput Input;
	Input.bAccelerate = bAutoAccelerate;

	if (bUseKeyboardInput)
	{
		const InputSystem& In = InputSystem::Get();
		Input.bAccelerate = Input.bAccelerate || In.GetKey('W') || In.GetKey(VK_UP);
		Input.bBrake = In.GetKey('S') || In.GetKey(VK_DOWN);
		Input.bSteerLeft = In.GetKey('A') || In.GetKey(VK_LEFT);
		Input.bSteerRight = In.GetKey('D') || In.GetKey(VK_RIGHT);
		Input.bHandbrake = In.GetKey(VK_SPACE);
	}

	return Input;
}

void AVehicleActor::ApplyVehicleTransforms(IPhysicsScene& PhysicsScene)
{
	if (!VehicleInstance || !VehicleInstance->ChassisBody)
	{
		return;
	}

	FTransform ChassisWorld;
	if (!PhysicsScene.GetBodyTransform(VehicleInstance->ChassisBody, ChassisWorld))
	{
		return;
	}

	SetActorLocation(ChassisWorld.Location);
	SetActorRotation(ChassisWorld.Rotation.ToRotator());

	TStaticArray<FTransform, 4> WheelWorldTransforms;
	if (!PhysicsScene.GetVehicle4WWheelTransforms(VehicleInstance, WheelWorldTransforms))
	{
		return;
	}

	TStaticArray<UStaticMeshComponent*, 4> WheelComponents =
	{
		WheelMeshComponent_FL,
		WheelMeshComponent_FR,
		WheelMeshComponent_BL,
		WheelMeshComponent_BR,
	};

	for (size_t WheelIndex = 0; WheelIndex < WheelComponents.size(); ++WheelIndex)
	{
		UStaticMeshComponent* WheelComponent = WheelComponents[WheelIndex];
		if (!WheelComponent)
		{
			continue;
		}

		WheelComponent->SetRelativeTransform(MakeRelativeTransform(WheelWorldTransforms[WheelIndex], ChassisWorld));
	}
}
