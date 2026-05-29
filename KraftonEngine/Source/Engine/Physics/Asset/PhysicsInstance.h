#pragma once
#include "Core/Types/CoreTypes.h"

namespace physx 
{ 
	class PxRigdActor;
	class PxD6Joint;
}

struct FBodyInstance
{
	FString BoneName;

	physx::PxRigdActor* ActorHandle = nullptr;

	bool bSimulatePhysics = false;
	bool bEnableGravity = false;
};

struct FConstraintInstance
{
	FString ParentBoneName;
	FString ChildBoneName;

	physx::PxD6Joint* JointHandle = nullptr;

	bool bConstraintActive = true;
};