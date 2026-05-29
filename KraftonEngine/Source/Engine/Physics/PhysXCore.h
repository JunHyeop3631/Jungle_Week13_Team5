#pragma once

namespace physx
{
	class PxFoundation;
	class PxPhysics;
	class PxPvd;
	class PxScene;
	class PxSceneDesc;
}

struct FPhysXCoreHandles
{
	physx::PxFoundation* Foundation = nullptr;
	physx::PxPhysics* Physics = nullptr;
	physx::PxPvd* Pvd = nullptr;
	bool bDevelopmentFeaturesEnabled = false;
	bool bPvdConnected = false;
	bool bExtensionsInitialized = false;
};

FPhysXCoreHandles AcquireSharedPhysXCore();
void ReleaseSharedPhysXCore();

void ConfigurePhysXSceneDesc(physx::PxSceneDesc& SceneDesc);
void ConfigurePhysXPvdSceneClient(physx::PxScene* Scene);
