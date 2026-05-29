#include "Physics/PhysXRuntime.h"

#include <PxPhysicsAPI.h>
#include <algorithm>

using namespace physx;

namespace
{
	PxDefaultAllocator GAllocator;
	PxDefaultErrorCallback GErrorCallback;

	PxVec3 ToPxVec3(const FVector& V)
	{
		return PxVec3(V.X, V.Y, V.Z);
	}

	PxQuat ToPxQuat(const FQuat& Q)
	{
		return PxQuat(Q.X, Q.Y, Q.Z, Q.W);
	}

	FVector ToFVector(const PxVec3& V)
	{
		return FVector(V.x, V.y, V.z);
	}

	FQuat ToFQuat(const PxQuat& Q)
	{
		return FQuat(Q.x, Q.y, Q.z, Q.w);
	}

	PxTransform ToPxTransform(const FTransform& Transform)
	{
		return PxTransform(ToPxVec3(Transform.Location), ToPxQuat(Transform.Rotation));
	}

	FTransform ToFTransform(const PxTransform& Transform)
	{
		return FTransform(ToFVector(Transform.p), ToFQuat(Transform.q), FVector(1.0f, 1.0f, 1.0f));
	}

	PxD6Motion::Enum ToPxD6Motion(EPhysicsMotionType Motion)
	{
		switch (Motion)
		{
		case EPhysicsMotionType::Free:
			return PxD6Motion::eFREE;
		case EPhysicsMotionType::Limited:
			return PxD6Motion::eLIMITED;
		case EPhysicsMotionType::Locked:
		default:
			return PxD6Motion::eLOCKED;
		}
	}

	PxRigidActor* GetPxActor(FBodyInstance* Body)
	{
		return Body ? static_cast<PxRigidActor*>(Body->ActorHandle.NativePtr) : nullptr;
	}

	const PxRigidActor* GetPxActor(const FBodyInstance* Body)
	{
		return Body ? static_cast<const PxRigidActor*>(Body->ActorHandle.NativePtr) : nullptr;
	}

	PxRigidDynamic* GetPxDynamic(FBodyInstance* Body)
	{
		PxRigidActor* Actor = GetPxActor(Body);
		return Actor ? Actor->is<PxRigidDynamic>() : nullptr;
	}

	bool BuildGeometry(const FPhysicsShapeDesc& Desc, PxGeometryHolder& OutGeometry)
	{
		switch (Desc.ShapeType)
		{
		case EPhysicsShapeType::Box:
			OutGeometry = PxBoxGeometry(Desc.HalfExtent.X, Desc.HalfExtent.Y, Desc.HalfExtent.Z);
			return true;
		case EPhysicsShapeType::Sphere:
			OutGeometry = PxSphereGeometry(Desc.Radius);
			return true;
		case EPhysicsShapeType::Capsule:
			OutGeometry = PxCapsuleGeometry(Desc.Radius, Desc.HalfHeight);
			return true;
		case EPhysicsShapeType::Convex:
		case EPhysicsShapeType::TriangleMesh:
		default:
			return false;
		}
	}

	FVector DecodeDebugColor(PxU32 Color)
	{
		const float R = static_cast<float>((Color >> 16) & 0xff) / 255.0f;
		const float G = static_cast<float>((Color >> 8) & 0xff) / 255.0f;
		const float B = static_cast<float>(Color & 0xff) / 255.0f;
		return FVector(R, G, B);
	}
}

FPhysXRuntime::~FPhysXRuntime()
{
	Shutdown();
}

bool FPhysXRuntime::Initialize()
{
	if (Foundation || Physics || Scene)
	{
		return true;
	}

	Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, GAllocator, GErrorCallback);
	if (!Foundation)
	{
		return false;
	}

	Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *Foundation, PxTolerancesScale());
	if (!Physics)
	{
		Shutdown();
		return false;
	}

	bExtensionsInitialized = PxInitExtensions(*Physics, nullptr);
	if (!bExtensionsInitialized)
	{
		Shutdown();
		return false;
	}

	Dispatcher = PxDefaultCpuDispatcherCreate(4);
	if (!Dispatcher)
	{
		Shutdown();
		return false;
	}

	PxSceneDesc SceneDesc(Physics->getTolerancesScale());
	SceneDesc.gravity = PxVec3(0.0f, 0.0f, -9.81f);
	SceneDesc.cpuDispatcher = Dispatcher;
	SceneDesc.filterShader = PxDefaultSimulationFilterShader;
	SceneDesc.flags |= PxSceneFlag::eENABLE_ACTIVE_ACTORS;
	SceneDesc.flags |= PxSceneFlag::eENABLE_CCD;
	SceneDesc.flags |= PxSceneFlag::eENABLE_PCM;

	Scene = Physics->createScene(SceneDesc);
	if (!Scene)
	{
		Shutdown();
		return false;
	}

	DefaultMaterial = Physics->createMaterial(0.5f, 0.5f, 0.3f);
	if (!DefaultMaterial)
	{
		Shutdown();
		return false;
	}

	return true;
}

void FPhysXRuntime::Shutdown()
{
	for (FConstraintInstance* Joint : Joints)
	{
		if (!Joint) continue;
		if (PxJoint* PxJointPtr = static_cast<PxJoint*>(Joint->JointHandle.NativePtr))
		{
			PxJointPtr->release();
		}
		delete Joint;
	}
	Joints.clear();

	for (FBodyInstance* Body : Bodies)
	{
		if (!Body) continue;
		if (PxRigidActor* Actor = GetPxActor(Body))
		{
			Actor->release();
		}
		delete Body;
	}
	Bodies.clear();

	if (DefaultMaterial)
	{
		DefaultMaterial->release();
		DefaultMaterial = nullptr;
	}

	if (Scene)
	{
		Scene->release();
		Scene = nullptr;
	}

	if (Dispatcher)
	{
		Dispatcher->release();
		Dispatcher = nullptr;
	}

	if (Physics)
	{
		if (bExtensionsInitialized)
		{
			PxCloseExtensions();
			bExtensionsInitialized = false;
		}
		Physics->release();
		Physics = nullptr;
	}

	if (Foundation)
	{
		Foundation->release();
		Foundation = nullptr;
	}

	NextSerial = 1;
}

void FPhysXRuntime::Simulate(float DeltaTime)
{
	if (!Scene || DeltaTime <= 0.0f)
	{
		return;
	}

	Scene->simulate(DeltaTime);
	Scene->fetchResults(true);

	for (FBodyInstance* Body : Bodies)
	{
		if (!Body) continue;
		FTransform Transform;
		if (GetBodyTransform(Body, Transform))
		{
			Body->CachedWorldTransform = Transform;
		}
	}
}

FBodyInstance* FPhysXRuntime::CreateRigidBody(const FPhysicsBodyDesc& Desc)
{
	if (!Initialize() || !Physics || !Scene)
	{
		return nullptr;
	}

	const PxTransform Pose = ToPxTransform(Desc.WorldTransform);
	PxRigidActor* Actor = nullptr;

	if (Desc.BodyType == EPhysicsBodyType::Static)
	{
		Actor = Physics->createRigidStatic(Pose);
	}
	else
	{
		PxRigidDynamic* Dynamic = Physics->createRigidDynamic(Pose);
		if (Dynamic && Desc.BodyType == EPhysicsBodyType::Kinematic)
		{
			Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
		}
		if (Dynamic)
		{
			Dynamic->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !Desc.bUseGravity);
			Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, Desc.bEnableCCD);
			Dynamic->setLinearDamping(Desc.LinearDamping);
			Dynamic->setAngularDamping(Desc.AngularDamping);
		}
		Actor = Dynamic;
	}

	if (!Actor)
	{
		return nullptr;
	}

	FBodyInstance* Body = new FBodyInstance();
	Body->OwnerComponent = Desc.OwnerComponent;
	Body->BodyName = Desc.BodyName;
	Body->BoneName = Desc.BoneName;
	Body->BoneIndex = Desc.BoneIndex;
	Body->BodyType = Desc.BodyType;
	Body->ActorHandle = { Actor, AllocateSerial() };
	Body->CachedWorldTransform = Desc.WorldTransform;
	Body->bValid = true;
	Body->bSimulating = Desc.BodyType == EPhysicsBodyType::Dynamic;
	Actor->userData = Body;

	for (const FPhysicsShapeDesc& ShapeDesc : Desc.Shapes)
	{
		CreateShape(Body, ShapeDesc);
	}

	if (PxRigidDynamic* Dynamic = Actor->is<PxRigidDynamic>())
	{
		PxRigidBodyExt::updateMassAndInertia(*Dynamic, Desc.Mass);
		if (!Desc.bStartAwake)
		{
			Dynamic->putToSleep();
		}
	}

	Scene->addActor(*Actor);
	Bodies.push_back(Body);
	return Body;
}

void FPhysXRuntime::DestroyRigidBody(FBodyInstance* Body)
{
	if (!Body)
	{
		return;
	}

	Bodies.erase(std::remove(Bodies.begin(), Bodies.end(), Body), Bodies.end());

	if (PxRigidActor* Actor = GetPxActor(Body))
	{
		Actor->release();
	}

	delete Body;
}

FPhysicsShapeHandle FPhysXRuntime::CreateShape(FBodyInstance* Body, const FPhysicsShapeDesc& Desc)
{
	if (!Physics || !Body)
	{
		return {};
	}

	PxRigidActor* Actor = GetPxActor(Body);
	if (!Actor)
	{
		return {};
	}

	PxGeometryHolder Geometry;
	if (!BuildGeometry(Desc, Geometry))
	{
		return {};
	}

	PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Actor, Geometry.any(), *DefaultMaterial);
	if (!Shape)
	{
		return {};
	}

	Shape->setLocalPose(ToPxTransform(Desc.LocalTransform));
	Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, Desc.bSimulationShape && !Desc.bTriggerShape);
	Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, Desc.bTriggerShape);
	Shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, Desc.bSceneQueryShape);
	Shape->userData = Body->OwnerComponent;

	FPhysicsShapeHandle Handle{ Shape, AllocateSerial() };
	Body->ShapeHandles.push_back(Handle);

	if (PxRigidDynamic* Dynamic = GetPxDynamic(Body))
	{
		PxRigidBodyExt::updateMassAndInertia(*Dynamic, Desc.Material.Density);
	}

	return Handle;
}

FConstraintInstance* FPhysXRuntime::CreateD6Joint(const FPhysicsConstraintDesc& Desc)
{
	if (!Physics || !Desc.ParentBody || !Desc.ChildBody)
	{
		return nullptr;
	}

	PxRigidActor* ParentActor = GetPxActor(Desc.ParentBody);
	PxRigidActor* ChildActor = GetPxActor(Desc.ChildBody);
	if (!ParentActor || !ChildActor)
	{
		return nullptr;
	}

	PxD6Joint* Joint = PxD6JointCreate(
		*Physics,
		ParentActor,
		ToPxTransform(Desc.ParentLocalFrame),
		ChildActor,
		ToPxTransform(Desc.ChildLocalFrame));

	if (!Joint)
	{
		return nullptr;
	}

	Joint->setMotion(PxD6Axis::eX, ToPxD6Motion(Desc.LinearX));
	Joint->setMotion(PxD6Axis::eY, ToPxD6Motion(Desc.LinearY));
	Joint->setMotion(PxD6Axis::eZ, ToPxD6Motion(Desc.LinearZ));
	Joint->setMotion(PxD6Axis::eTWIST, ToPxD6Motion(Desc.Twist));
	Joint->setMotion(PxD6Axis::eSWING1, ToPxD6Motion(Desc.Swing1));
	Joint->setMotion(PxD6Axis::eSWING2, ToPxD6Motion(Desc.Swing2));
	Joint->setTwistLimit(PxJointAngularLimitPair(Desc.TwistLimitRadiansMin, Desc.TwistLimitRadiansMax));
	Joint->setSwingLimit(PxJointLimitCone(Desc.Swing1LimitRadians, Desc.Swing2LimitRadians));

	if (Desc.bBreakable)
	{
		Joint->setBreakForce(Desc.BreakForce, Desc.BreakTorque);
	}

	FConstraintInstance* Constraint = new FConstraintInstance();
	Constraint->ConstraintName = Desc.ConstraintName;
	Constraint->ParentBody = Desc.ParentBody;
	Constraint->ChildBody = Desc.ChildBody;
	Constraint->JointHandle = { Joint, AllocateSerial() };
	Constraint->Desc = Desc;
	Constraint->bValid = true;

	Joints.push_back(Constraint);
	return Constraint;
}

void FPhysXRuntime::DestroyJoint(FConstraintInstance* Joint)
{
	if (!Joint)
	{
		return;
	}

	Joints.erase(std::remove(Joints.begin(), Joints.end(), Joint), Joints.end());

	if (PxJoint* PxJointPtr = static_cast<PxJoint*>(Joint->JointHandle.NativePtr))
	{
		PxJointPtr->release();
	}

	delete Joint;
}

bool FPhysXRuntime::GetBodyTransform(const FBodyInstance* Body, FTransform& OutTransform) const
{
	const PxRigidActor* Actor = GetPxActor(Body);
	if (!Actor)
	{
		return false;
	}

	OutTransform = ToFTransform(Actor->getGlobalPose());
	return true;
}

void FPhysXRuntime::SetBodyTransform(FBodyInstance* Body, const FTransform& Transform, bool bTeleport)
{
	PxRigidActor* Actor = GetPxActor(Body);
	if (!Actor)
	{
		return;
	}

	if (!bTeleport)
	{
		SetKinematicTarget(Body, Transform);
		return;
	}

	Actor->setGlobalPose(ToPxTransform(Transform));
	Body->CachedWorldTransform = Transform;
}

void FPhysXRuntime::SetKinematicTarget(FBodyInstance* Body, const FTransform& Transform)
{
	PxRigidDynamic* Dynamic = GetPxDynamic(Body);
	if (!Dynamic)
	{
		return;
	}

	Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
	Dynamic->setKinematicTarget(ToPxTransform(Transform));
	Body->BodyType = EPhysicsBodyType::Kinematic;
	Body->CachedWorldTransform = Transform;
}

void FPhysXRuntime::GetPhysicsStats(FPhysicsStats& OutStats) const
{
	OutStats = FPhysicsStats();
	if (!Scene)
	{
		return;
	}

	OutStats.NumRigidStatics = Scene->getNbActors(PxActorTypeFlag::eRIGID_STATIC);
	OutStats.NumRigidDynamics = Scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC);
	OutStats.NumActors = OutStats.NumRigidStatics + OutStats.NumRigidDynamics;
	OutStats.NumJoints = static_cast<uint32>(Joints.size());

	for (const FBodyInstance* Body : Bodies)
	{
		if (!Body) continue;
		OutStats.NumShapes += static_cast<uint32>(Body->ShapeHandles.size());
	}

	PxSimulationStatistics SimStats;
	Scene->getSimulationStatistics(SimStats);
	OutStats.NumContactPairs = SimStats.nbDiscreteContactPairsTotal;
}

void FPhysXRuntime::ExtractPhysicsDebugLines(TArray<FPhysicsDebugLine>& OutLines) const
{
	OutLines.clear();
	if (!Scene)
	{
		return;
	}

	const PxRenderBuffer& RenderBuffer = Scene->getRenderBuffer();
	const PxU32 NumLines = RenderBuffer.getNbLines();
	const PxDebugLine* Lines = RenderBuffer.getLines();

	OutLines.reserve(NumLines);
	for (PxU32 Index = 0; Index < NumLines; ++Index)
	{
		FPhysicsDebugLine Line;
		Line.Start = ToFVector(Lines[Index].pos0);
		Line.End = ToFVector(Lines[Index].pos1);
		Line.Color = DecodeDebugColor(Lines[Index].color0);
		OutLines.push_back(Line);
	}
}
