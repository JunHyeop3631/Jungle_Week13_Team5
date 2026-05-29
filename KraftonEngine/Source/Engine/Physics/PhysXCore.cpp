#include "Physics/PhysXCore.h"
#include "Core/Logging/Log.h"

#include <PxPhysicsAPI.h>

using namespace physx;

#ifndef WITH_EDITOR
#define WITH_EDITOR 0
#endif

#if defined(_DEBUG) && !defined(NDEBUG)
#define KRAFTON_PHYSX_DEVELOPMENT_FEATURES 1
#else
#define KRAFTON_PHYSX_DEVELOPMENT_FEATURES 0
#endif

#if defined(_DEBUG) || WITH_EDITOR
#define KRAFTON_PHYSX_REQUIRE_RW_LOCK 1
#else
#define KRAFTON_PHYSX_REQUIRE_RW_LOCK 0
#endif

class FPhysXErrorCallback : public PxErrorCallback
{
public:
	void reportError(PxErrorCode::Enum Code, const char* Message, const char* File, int Line) override
	{
		const char* Severity = "Info";
		if (Code == PxErrorCode::eABORT || Code == PxErrorCode::eOUT_OF_MEMORY)
		{
			Severity = "Fatal";
		}
		else if (Code == PxErrorCode::eINTERNAL_ERROR || Code == PxErrorCode::eINVALID_OPERATION)
		{
			Severity = "Error";
		}
		else if (Code == PxErrorCode::eINVALID_PARAMETER ||
			Code == PxErrorCode::ePERF_WARNING ||
			Code == PxErrorCode::eDEBUG_WARNING)
		{
			Severity = "Warning";
		}

		UE_LOG("[PhysX %s] %s (%s:%d)", Severity, Message, File, Line);
	}
};

static FPhysXErrorCallback GPhysXErrorCallback;
static PxDefaultAllocator GPhysXAllocator;

static PxFoundation* GSharedFoundation = nullptr;
static PxPhysics* GSharedPhysics = nullptr;
static PxPvd* GSharedPvd = nullptr;
static PxPvdTransport* GSharedPvdTransport = nullptr;
static int GSharedRefCount = 0;
static bool GPhysXExtensionsInitialized = false;
static bool GPhysXPvdConnected = false;

static void ReleasePhysXCoreObjects()
{
	if (GPhysXExtensionsInitialized)
	{
		PxCloseExtensions();
		GPhysXExtensionsInitialized = false;
	}

	if (GSharedPhysics)
	{
		GSharedPhysics->release();
		GSharedPhysics = nullptr;
	}

	if (GSharedPvd)
	{
		GSharedPvd->release();
		GSharedPvd = nullptr;
		GPhysXPvdConnected = false;
	}

	if (GSharedPvdTransport)
	{
		GSharedPvdTransport->release();
		GSharedPvdTransport = nullptr;
	}

	if (GSharedFoundation)
	{
		GSharedFoundation->release();
		GSharedFoundation = nullptr;
	}
}

static bool CreateSharedPhysXCore()
{
	GSharedFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, GPhysXAllocator, GPhysXErrorCallback);
	if (!GSharedFoundation)
	{
		UE_LOG("[PhysX] Failed to create Foundation");
		return false;
	}

#if KRAFTON_PHYSX_DEVELOPMENT_FEATURES
	GSharedPvd = PxCreatePvd(*GSharedFoundation);
	if (GSharedPvd)
	{
		GSharedPvdTransport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
		if (GSharedPvdTransport)
		{
			GPhysXPvdConnected = GSharedPvd->connect(*GSharedPvdTransport, PxPvdInstrumentationFlag::eALL);
			UE_LOG("[PhysX] PVD %s (127.0.0.1:5425)",
				GPhysXPvdConnected ? "connected" : "connection pending/failed");
		}
	}
#endif

	const bool bTrackOutstandingAllocations = KRAFTON_PHYSX_DEVELOPMENT_FEATURES != 0;
	GSharedPhysics = PxCreatePhysics(
		PX_PHYSICS_VERSION,
		*GSharedFoundation,
		PxTolerancesScale(),
		bTrackOutstandingAllocations,
		GSharedPvd);

	if (!GSharedPhysics)
	{
		UE_LOG("[PhysX] Failed to create Physics");
		ReleasePhysXCoreObjects();
		return false;
	}

	GPhysXExtensionsInitialized = PxInitExtensions(*GSharedPhysics, GSharedPvd);
	if (!GPhysXExtensionsInitialized)
	{
		UE_LOG("[PhysX] PxInitExtensions failed");
		ReleasePhysXCoreObjects();
		return false;
	}

	return true;
}

FPhysXCoreHandles AcquireSharedPhysXCore()
{
	if (GSharedRefCount == 0 && !CreateSharedPhysXCore())
	{
		return {};
	}

	++GSharedRefCount;

	FPhysXCoreHandles Handles;
	Handles.Foundation = GSharedFoundation;
	Handles.Physics = GSharedPhysics;
	Handles.Pvd = GSharedPvd;
	Handles.bDevelopmentFeaturesEnabled = KRAFTON_PHYSX_DEVELOPMENT_FEATURES != 0;
	Handles.bPvdConnected = GPhysXPvdConnected;
	Handles.bExtensionsInitialized = GPhysXExtensionsInitialized;
	return Handles;
}

void ReleaseSharedPhysXCore()
{
	if (GSharedRefCount <= 0)
	{
		return;
	}

	--GSharedRefCount;
	if (GSharedRefCount == 0)
	{
		ReleasePhysXCoreObjects();
	}
}

void ConfigurePhysXSceneDesc(PxSceneDesc& SceneDesc)
{
	SceneDesc.flags |= PxSceneFlag::eENABLE_ACTIVE_ACTORS;
	SceneDesc.flags |= PxSceneFlag::eENABLE_CCD;
	SceneDesc.flags |= PxSceneFlag::eENABLE_PCM;

#if KRAFTON_PHYSX_REQUIRE_RW_LOCK
	SceneDesc.flags |= PxSceneFlag::eREQUIRE_RW_LOCK;
#endif
}

void ConfigurePhysXPvdSceneClient(PxScene* Scene)
{
#if KRAFTON_PHYSX_DEVELOPMENT_FEATURES
	if (!Scene)
	{
		return;
	}

	if (PxPvdSceneClient* PvdClient = Scene->getScenePvdClient())
	{
		PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
		PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
		PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
	}
#else
	(void)Scene;
#endif
}
