#pragma once

#include "Physics/BodyInstance.h"

struct FConstraintInstance
{
	FString ConstraintName;

	FBodyInstance* ParentBody = nullptr;
	FBodyInstance* ChildBody = nullptr;

	FPhysicsJointHandle JointHandle;
	FPhysicsConstraintDesc Desc;

	bool bValid = false;

	void Reset()
	{
		ConstraintName.clear();
		ParentBody = nullptr;
		ChildBody = nullptr;
		JointHandle = {};
		Desc = FPhysicsConstraintDesc();
		bValid = false;
	}
};
