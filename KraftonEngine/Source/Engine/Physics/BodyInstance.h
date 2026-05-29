#pragma once

#include "Physics/PhysicsTypes.h"

struct FBodyInstance
{
	UPrimitiveComponent* OwnerComponent = nullptr;

	FString BodyName;
	FString BoneName;
	int32 BoneIndex = -1;

	EPhysicsBodyType BodyType = EPhysicsBodyType::Static;

	FPhysicsActorHandle ActorHandle;
	TArray<FPhysicsShapeHandle> ShapeHandles;

	FTransform CachedWorldTransform;

	bool bValid = false;
	bool bSimulating = false;

	void Reset()
	{
		OwnerComponent = nullptr;
		BodyName.clear();
		BoneName.clear();
		BoneIndex = -1;
		BodyType = EPhysicsBodyType::Static;
		ActorHandle = {};
		ShapeHandles.clear();
		CachedWorldTransform = FTransform();
		bValid = false;
		bSimulating = false;
	}
};
