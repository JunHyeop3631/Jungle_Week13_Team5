#include "Physics/PhysXRuntime.h"
#include "Physics/PhysXHelpers.h"

#include <algorithm>
#include <cmath>

using namespace PhysXHelpers;

namespace
{
	PxDefaultAllocator GAllocator;
	PxDefaultErrorCallback GErrorCallback;

	bool IsUsableDirection(const PxVec3& Direction)
	{
		return Direction.magnitudeSquared() > 1.0e-6f;
	}

	PxVec3 NormalizeOrFallback(const PxVec3& Direction, const PxVec3& Fallback)
	{
		return IsUsableDirection(Direction) ? Direction.getNormalized() : Fallback;
	}

	void AddDebugLine(TArray<FPhysicsDebugLine>& OutLines, const PxVec3& Start, const PxVec3& End, const FVector& Color)
	{
		FPhysicsDebugLine Line;
		Line.Start = ToFVector(Start);
		Line.End = ToFVector(End);
		Line.Color = Color;
		OutLines.push_back(Line);
	}

	void AddDebugCross(TArray<FPhysicsDebugLine>& OutLines, const PxVec3& Center, float Radius, const FVector& Color)
	{
		AddDebugLine(OutLines, Center - PxVec3(Radius, 0.0f, 0.0f), Center + PxVec3(Radius, 0.0f, 0.0f), Color);
		AddDebugLine(OutLines, Center - PxVec3(0.0f, Radius, 0.0f), Center + PxVec3(0.0f, Radius, 0.0f), Color);
		AddDebugLine(OutLines, Center - PxVec3(0.0f, 0.0f, Radius), Center + PxVec3(0.0f, 0.0f, Radius), Color);
	}

	static constexpr PxU32 NumVehicle4WWheels = 4;
	static constexpr PxU32 VehicleQuerySelfIdMask = 0x7fffffffu;

	PxU32 MakeVehicleQuerySelfId(uint64 Serial)
	{
		PxU32 Id = static_cast<PxU32>(Serial & VehicleQuerySelfIdMask);
		return Id != 0 ? Id : 1u;
	}

	PxVec3 ComputeBoxInertiaTensor(const FVector& HalfExtents, float Mass)
	{
		const float SizeX = HalfExtents.X * 2.0f;
		const float SizeY = HalfExtents.Y * 2.0f;
		const float SizeZ = HalfExtents.Z * 2.0f;
		const float Scale = Mass / 12.0f;
		return PxVec3(
			Scale * (SizeY * SizeY + SizeZ * SizeZ),
			Scale * (SizeX * SizeX + SizeZ * SizeZ),
			Scale * (SizeX * SizeX + SizeY * SizeY));
	}

	PxQueryHitType::Enum VehicleSuspensionRaycastPreFilter(
		PxFilterData FilterData0,
		PxFilterData FilterData1,
		const void*,
		PxU32,
		PxHitFlags&)
	{
		// word3 is reserved here as a lightweight self-filter id.
		if (FilterData0.word3 != 0 && FilterData0.word3 == FilterData1.word3)
		{
			return PxQueryHitType::eNONE;
		}

		return PxQueryHitType::eBLOCK;
	}

	struct FPhysXVehicle4WData
	{
		PxVehicleDrive4W* Vehicle = nullptr;
		PxBatchQuery* SuspensionBatchQuery = nullptr;
		TStaticArray<PxRaycastQueryResult, NumVehicle4WWheels> RaycastResults = {};
		TStaticArray<PxRaycastHit, NumVehicle4WWheels> RaycastHits = {};
		TStaticArray<PxWheelQueryResult, NumVehicle4WWheels> WheelQueryResults = {};
		PxVehicleDrivableSurfaceToTireFrictionPairs* FrictionPairs = nullptr;
		PxVehicleDrive4WRawInputData RawInput;
		PxVehicleKeySmoothingData KeySmoothingData = {};
		PxF32 SteerVsForwardSpeedData[16] =
		{
			0.0f, 0.75f,
			5.0f, 0.75f,
			30.0f, 0.125f,
			120.0f, 0.1f,
			PX_MAX_F32, PX_MAX_F32,
			PX_MAX_F32, PX_MAX_F32,
			PX_MAX_F32, PX_MAX_F32,
			PX_MAX_F32, PX_MAX_F32,
		};
		PxFixedSizeLookupTable<8> SteerVsForwardSpeedTable;

		FPhysXVehicle4WData()
			: SteerVsForwardSpeedTable(SteerVsForwardSpeedData, 4)
		{
			KeySmoothingData.mRiseRates[PxVehicleDrive4WControl::eANALOG_INPUT_ACCEL] = 3.0f;
			KeySmoothingData.mRiseRates[PxVehicleDrive4WControl::eANALOG_INPUT_BRAKE] = 3.0f;
			KeySmoothingData.mRiseRates[PxVehicleDrive4WControl::eANALOG_INPUT_HANDBRAKE] = 10.0f;
			KeySmoothingData.mRiseRates[PxVehicleDrive4WControl::eANALOG_INPUT_STEER_LEFT] = 2.5f;
			KeySmoothingData.mRiseRates[PxVehicleDrive4WControl::eANALOG_INPUT_STEER_RIGHT] = 2.5f;

			KeySmoothingData.mFallRates[PxVehicleDrive4WControl::eANALOG_INPUT_ACCEL] = 5.0f;
			KeySmoothingData.mFallRates[PxVehicleDrive4WControl::eANALOG_INPUT_BRAKE] = 5.0f;
			KeySmoothingData.mFallRates[PxVehicleDrive4WControl::eANALOG_INPUT_HANDBRAKE] = 10.0f;
			KeySmoothingData.mFallRates[PxVehicleDrive4WControl::eANALOG_INPUT_STEER_LEFT] = 5.0f;
			KeySmoothingData.mFallRates[PxVehicleDrive4WControl::eANALOG_INPUT_STEER_RIGHT] = 5.0f;
		}
	};

	FPhysXVehicle4WData* GetVehicleData(FVehicle4WInstance* Vehicle)
	{
		return Vehicle ? static_cast<FPhysXVehicle4WData*>(Vehicle->VehicleHandle.NativePtr) : nullptr;
	}

	const FPhysXVehicle4WData* GetVehicleData(const FVehicle4WInstance* Vehicle)
	{
		return Vehicle ? static_cast<const FPhysXVehicle4WData*>(Vehicle->VehicleHandle.NativePtr) : nullptr;
	}

	void ApplyVehicleInputToRawData(const FVehicle4WInput& Input, PxVehicleDrive4WRawInputData& OutRawInput)
	{
		OutRawInput.setDigitalAccel(Input.bAccelerate);
		OutRawInput.setDigitalBrake(Input.bBrake);
		OutRawInput.setDigitalSteerLeft(Input.bSteerLeft);
		OutRawInput.setDigitalSteerRight(Input.bSteerRight);
		OutRawInput.setDigitalHandbrake(Input.bHandbrake);
	}

	void UpdateCachedVehicleWheelTransforms(FVehicle4WInstance* Instance)
	{
		FPhysXVehicle4WData* Data = GetVehicleData(Instance);
		PxRigidDynamic* Actor = GetPxDynamic(Instance ? Instance->ChassisBody : nullptr);
		if (!Instance || !Data || !Data->Vehicle || !Actor)
		{
			return;
		}

		const PxU32 ShapeCount = Actor->getNbShapes();
		if (ShapeCount == 0)
		{
			return;
		}

		TArray<PxShape*> Shapes(ShapeCount);
		Actor->getShapes(Shapes.data(), ShapeCount);

		const PxTransform ActorPose = Actor->getGlobalPose();
		for (PxU32 WheelIndex = 0; WheelIndex < NumVehicle4WWheels; ++WheelIndex)
		{
			const PxI32 ShapeIndex = Data->Vehicle->mWheelsSimData.getWheelShapeMapping(WheelIndex);
			if (ShapeIndex < 0 || static_cast<PxU32>(ShapeIndex) >= ShapeCount || !Shapes[ShapeIndex])
			{
				continue;
			}

			Instance->WheelWorldTransforms[WheelIndex] = ToFTransform(ActorPose * Shapes[ShapeIndex]->getLocalPose());
		}
	}

	void AppendVehicleDebugLines(const FVehicle4WInstance* Instance, TArray<FPhysicsDebugLine>& OutLines)
	{
		const FPhysXVehicle4WData* Data = GetVehicleData(Instance);
		const PxRigidActor* RigidActor = GetPxActor(Instance ? Instance->ChassisBody : nullptr);
		const PxRigidDynamic* Actor = RigidActor ? RigidActor->is<PxRigidDynamic>() : nullptr;
		if (!Instance || !Data || !Data->Vehicle || !Actor)
		{
			return;
		}

		constexpr float WheelCrossRadius = 0.12f;
		constexpr float TireDirectionLength = 0.55f;
		constexpr float ContactNormalLength = 0.45f;
		const FVector WheelColor(1.0f, 1.0f, 1.0f);
		const FVector SuspensionColor(1.0f, 0.8f, 0.0f);
		const FVector AirSuspensionColor(1.0f, 0.35f, 0.0f);
		const FVector ContactColor(0.0f, 1.0f, 1.0f);
		const FVector LongitudinalColor(0.0f, 1.0f, 0.0f);
		const FVector LateralColor(1.0f, 0.0f, 1.0f);

		const PxTransform ActorPose = Actor->getGlobalPose();
		const PxVec3 ForwardFallback = ActorPose.q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
		const PxVec3 LateralFallback = ActorPose.q.rotate(PxVec3(0.0f, 1.0f, 0.0f));

		for (PxU32 WheelIndex = 0; WheelIndex < NumVehicle4WWheels; ++WheelIndex)
		{
			const PxWheelQueryResult& Query = Data->WheelQueryResults[WheelIndex];
			const PxTransform WheelPose = ActorPose * Query.localPose;
			const PxVec3 CachedWheelCenter = ToPxVec3(Instance->WheelWorldTransforms[WheelIndex].Location);
			const PxVec3 WheelCenter = IsUsableDirection(Query.localPose.p) ? WheelPose.p : CachedWheelCenter;

			AddDebugCross(OutLines, WheelCenter, WheelCrossRadius, WheelColor);

			if (Query.suspLineLength > 0.0f && IsUsableDirection(Query.suspLineDir))
			{
				const PxVec3 SuspEnd = Query.suspLineStart + Query.suspLineDir * Query.suspLineLength;
				AddDebugLine(OutLines, Query.suspLineStart, SuspEnd,
					Query.isInAir ? AirSuspensionColor : SuspensionColor);
			}
			else
			{
				AddDebugLine(OutLines, WheelCenter, WheelCenter + ActorPose.q.rotate(PxVec3(0.0f, 0.0f, -0.6f)),
					AirSuspensionColor);
			}

			const PxVec3 BasePoint = !Query.isInAir && Query.tireContactShape
				? Query.tireContactPoint
				: WheelCenter;

			if (!Query.isInAir && Query.tireContactShape && IsUsableDirection(Query.tireContactNormal))
			{
				const PxVec3 ContactNormal = Query.tireContactNormal.getNormalized();
				AddDebugCross(OutLines, Query.tireContactPoint, WheelCrossRadius, ContactColor);
				AddDebugLine(OutLines, Query.tireContactPoint,
					Query.tireContactPoint + ContactNormal * ContactNormalLength,
					ContactColor);
			}

			const PxVec3 LongitudinalDir = NormalizeOrFallback(Query.tireLongitudinalDir, ForwardFallback);
			const PxVec3 LateralDir = NormalizeOrFallback(Query.tireLateralDir, LateralFallback);
			AddDebugLine(OutLines, BasePoint, BasePoint + LongitudinalDir * TireDirectionLength, LongitudinalColor);
			AddDebugLine(OutLines, BasePoint, BasePoint + LateralDir * TireDirectionLength, LateralColor);
		}
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

	PxInitVehicleSDK(*Physics);
	PxVehicleSetBasisVectors(PxVec3(0.0f, 0.0f, 1.0f), PxVec3(1.0f, 0.0f, 0.0f));
	PxVehicleSetUpdateMode(PxVehicleUpdateMode::eVELOCITY_CHANGE);
	bVehicleSdkInitialized = true;

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
	while (!Vehicles.empty())
	{
		DestroyVehicle4W(Vehicles.back());
	}

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
		if (PxRigidActor* Actor = PhysXHelpers::GetPxActor(Body))
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
		if (bVehicleSdkInitialized)
		{
			PxCloseVehicleSDK();
			bVehicleSdkInitialized = false;
		}

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

	for (FVehicle4WInstance* VehicleInstance : Vehicles)
	{
		FPhysXVehicle4WData* Data = GetVehicleData(VehicleInstance);
		if (!VehicleInstance || !Data || !Data->Vehicle || !Data->SuspensionBatchQuery || !Data->FrictionPairs)
		{
			continue;
		}

		ApplyVehicleInputToRawData(VehicleInstance->LastInput, Data->RawInput);
		PxVehicleDrive4WSmoothDigitalRawInputsAndSetAnalogInputs(
			Data->KeySmoothingData,
			Data->SteerVsForwardSpeedTable,
			Data->RawInput,
			DeltaTime,
			false,
			*Data->Vehicle);

		if (PxRigidDynamic* Actor = GetPxDynamic(VehicleInstance->ChassisBody))
		{
			if (VehicleInstance->LastInput.bAccelerate || VehicleInstance->LastInput.bBrake ||
				VehicleInstance->LastInput.bSteerLeft || VehicleInstance->LastInput.bSteerRight ||
				VehicleInstance->LastInput.bHandbrake)
			{
				Actor->wakeUp();
			}
		}

		PxVehicleWheels* VehicleArray[1] = { Data->Vehicle };
		PxVehicleSuspensionRaycasts(
			Data->SuspensionBatchQuery,
			1,
			VehicleArray,
			NumVehicle4WWheels,
			Data->RaycastResults.data());

		PxVehicleWheelQueryResult VehicleQueryResults[1];
		VehicleQueryResults[0].wheelQueryResults = Data->WheelQueryResults.data();
		VehicleQueryResults[0].nbWheelQueryResults = NumVehicle4WWheels;

		PxVehicleUpdates(
			DeltaTime,
			Scene->getGravity(),
			*Data->FrictionPairs,
			1,
			VehicleArray,
			VehicleQueryResults);
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

	for (FVehicle4WInstance* VehicleInstance : Vehicles)
	{
		UpdateCachedVehicleWheelTransforms(VehicleInstance);
	}
}

FBodyInstance* FPhysXRuntime::CreateRigidBody(const FPhysicsBodyDesc& Desc)
{
	if (!Initialize() || !Physics || !Scene)
	{
		return nullptr;
	}

	const PxTransform Pose = PhysXHelpers::ToPxTransform(Desc.WorldTransform);
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

	if (PxRigidActor* Actor = PhysXHelpers::GetPxActor(Body))
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

	PxRigidActor* Actor = PhysXHelpers::GetPxActor(Body);
	if (!Actor)
	{
		return {};
	}

	PxGeometryHolder Geometry;
	if (!PhysXHelpers::BuildGeometry(Desc, Geometry))
	{
		return {};
	}

	PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Actor, Geometry.any(), *DefaultMaterial);
	if (!Shape)
	{
		return {};
	}

	Shape->setLocalPose(PhysXHelpers::ToPxTransform(Desc.LocalTransform));
	Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, Desc.bSimulationShape && !Desc.bTriggerShape);
	Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, Desc.bTriggerShape);
	Shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, Desc.bSceneQueryShape);
	Shape->userData = Body->OwnerComponent;

	FPhysicsShapeHandle Handle{ Shape, AllocateSerial() };
	Body->ShapeHandles.push_back(Handle);

	if (PxRigidDynamic* Dynamic = PhysXHelpers::GetPxDynamic(Body))
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

	PxRigidActor* ParentActor = PhysXHelpers::GetPxActor(Desc.ParentBody);
	PxRigidActor* ChildActor = PhysXHelpers::GetPxActor(Desc.ChildBody);
	if (!ParentActor || !ChildActor)
	{
		return nullptr;
	}

	PxD6Joint* Joint = PxD6JointCreate(
		*Physics,
		ParentActor,
		PhysXHelpers::ToPxTransform(Desc.ParentLocalFrame),
		ChildActor,
		PhysXHelpers::ToPxTransform(Desc.ChildLocalFrame));

	if (!Joint)
	{
		return nullptr;
	}

	Joint->setMotion(PxD6Axis::eX, PhysXHelpers::ToPxD6Motion(Desc.LinearX));
	Joint->setMotion(PxD6Axis::eY, PhysXHelpers::ToPxD6Motion(Desc.LinearY));
	Joint->setMotion(PxD6Axis::eZ, PhysXHelpers::ToPxD6Motion(Desc.LinearZ));
	Joint->setMotion(PxD6Axis::eTWIST, PhysXHelpers::ToPxD6Motion(Desc.Twist));
	Joint->setMotion(PxD6Axis::eSWING1, PhysXHelpers::ToPxD6Motion(Desc.Swing1));
	Joint->setMotion(PxD6Axis::eSWING2, PhysXHelpers::ToPxD6Motion(Desc.Swing2));
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

FVehicle4WInstance* FPhysXRuntime::CreateVehicle4W(const FVehicle4WDesc& Desc)
{
	if (!Initialize() || !Physics || !Scene || !DefaultMaterial || !bVehicleSdkInitialized)
	{
		return nullptr;
	}

	FVehicle4WInstance* Instance = new FVehicle4WInstance();
	Instance->Name = Desc.Name;
	Instance->VehicleHandle.Serial = AllocateSerial();

	const PxU32 VehicleSelfId = MakeVehicleQuerySelfId(Instance->VehicleHandle.Serial);
	PxFilterData VehicleQueryFilter;
	VehicleQueryFilter.word3 = VehicleSelfId;

	PxRigidDynamic* Actor = Physics->createRigidDynamic(ToPxTransform(Desc.WorldTransform));
	if (!Actor)
	{
		delete Instance;
		return nullptr;
	}

	Actor->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
	Actor->setLinearDamping(0.05f);
	Actor->setAngularDamping(0.35f);
	Actor->setMass(Desc.ChassisMass);
	Actor->setMassSpaceInertiaTensor(ComputeBoxInertiaTensor(Desc.ChassisHalfExtents, Desc.ChassisMass));
	Actor->setCMassLocalPose(PxTransform(ToPxVec3(Desc.ChassisCenterOfMassOffset), PxQuat(PxIdentity)));

	FBodyInstance* Body = new FBodyInstance();
	Body->BodyName = Desc.Name.empty() ? FString("Vehicle4W") : Desc.Name;
	Body->BodyType = EPhysicsBodyType::Dynamic;
	Body->ActorHandle = { Actor, AllocateSerial() };
	Body->CachedWorldTransform = Desc.WorldTransform;
	Body->bValid = true;
	Body->bSimulating = true;
	Actor->userData = Body;

	bool bShapeCreationFailed = false;
	for (PxU32 WheelIndex = 0; WheelIndex < NumVehicle4WWheels; ++WheelIndex)
	{
		PxShape* WheelShape = PxRigidActorExt::createExclusiveShape(
			*Actor,
			PxSphereGeometry(Desc.WheelRadius),
			*DefaultMaterial);
		if (!WheelShape)
		{
			bShapeCreationFailed = true;
			break;
		}

		WheelShape->setLocalPose(PxTransform(ToPxVec3(Desc.WheelCenterOffsets[WheelIndex]), PxQuat(PxIdentity)));
		WheelShape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
		WheelShape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
		WheelShape->setQueryFilterData(VehicleQueryFilter);
		WheelShape->userData = Body;
		Body->ShapeHandles.push_back({ WheelShape, AllocateSerial() });
	}

	PxShape* ChassisShape = nullptr;
	if (!bShapeCreationFailed)
	{
		ChassisShape = PxRigidActorExt::createExclusiveShape(
			*Actor,
			PxBoxGeometry(ToPxVec3(Desc.ChassisHalfExtents)),
			*DefaultMaterial);
		if (!ChassisShape)
		{
			bShapeCreationFailed = true;
		}
		else
		{
			ChassisShape->setQueryFilterData(VehicleQueryFilter);
			ChassisShape->userData = Body;
			Body->ShapeHandles.push_back({ ChassisShape, AllocateSerial() });
		}
	}

	if (bShapeCreationFailed)
	{
		Actor->release();
		delete Body;
		delete Instance;
		return nullptr;
	}

	Scene->addActor(*Actor);

	PxVehicleWheelsSimData* WheelsSimData = PxVehicleWheelsSimData::allocate(NumVehicle4WWheels);
	PxVehicleDrive4W* Drive4W = PxVehicleDrive4W::allocate(NumVehicle4WWheels);
	FPhysXVehicle4WData* RuntimeData = new FPhysXVehicle4WData();
	if (!WheelsSimData || !Drive4W || !RuntimeData)
	{
		if (WheelsSimData) WheelsSimData->free();
		if (Drive4W) Drive4W->free();
		delete RuntimeData;
		Scene->removeActor(*Actor);
		Actor->release();
		delete Body;
		delete Instance;
		return nullptr;
	}

	PxVec3 WheelCenterActorOffsets[NumVehicle4WWheels];
	PxVec3 WheelCenterCMOffsets[NumVehicle4WWheels];
	for (PxU32 WheelIndex = 0; WheelIndex < NumVehicle4WWheels; ++WheelIndex)
	{
		WheelCenterActorOffsets[WheelIndex] = ToPxVec3(Desc.WheelCenterOffsets[WheelIndex]);
		WheelCenterCMOffsets[WheelIndex] =
			WheelCenterActorOffsets[WheelIndex] - ToPxVec3(Desc.ChassisCenterOfMassOffset);
	}

	PxF32 SprungMasses[NumVehicle4WWheels];
	PxVehicleComputeSprungMasses(
		NumVehicle4WWheels,
		WheelCenterActorOffsets,
		ToPxVec3(Desc.ChassisCenterOfMassOffset),
		Desc.ChassisMass,
		2,
		SprungMasses);

	for (PxU32 WheelIndex = 0; WheelIndex < NumVehicle4WWheels; ++WheelIndex)
	{
		PxVehicleWheelData WheelData;
		WheelData.mMass = Desc.WheelMass;
		WheelData.mRadius = Desc.WheelRadius;
		WheelData.mWidth = Desc.WheelWidth;
		WheelData.mMOI = 0.5f * Desc.WheelMass * Desc.WheelRadius * Desc.WheelRadius;
		WheelData.mDampingRate = Desc.WheelDampingRate;
		WheelData.mMaxBrakeTorque = Desc.MaxBrakeTorque;

		if (WheelIndex == static_cast<PxU32>(EVehicle4WWheelIndex::FrontLeft) ||
			WheelIndex == static_cast<PxU32>(EVehicle4WWheelIndex::FrontRight))
		{
			WheelData.mMaxSteer = Desc.MaxSteerRadians;
		}
		else
		{
			WheelData.mMaxHandBrakeTorque = Desc.MaxHandbrakeTorque;
		}

		PxVehicleTireData TireData;
		TireData.mType = 0;

		PxVehicleSuspensionData SuspensionData;
		SuspensionData.mMaxCompression = Desc.SuspensionMaxCompression;
		SuspensionData.mMaxDroop = Desc.SuspensionMaxDroop;
		SuspensionData.mSpringStrength = Desc.SuspensionSpringStrength;
		SuspensionData.mSpringDamperRate = Desc.SuspensionSpringDamperRate;
		SuspensionData.mSprungMass = SprungMasses[WheelIndex];

		PxFilterData WheelRaycastFilter;
		WheelRaycastFilter.word3 = VehicleSelfId;

		WheelsSimData->setWheelData(WheelIndex, WheelData);
		WheelsSimData->setTireData(WheelIndex, TireData);
		WheelsSimData->setSuspensionData(WheelIndex, SuspensionData);
		WheelsSimData->setSuspTravelDirection(WheelIndex, PxVec3(0.0f, 0.0f, -1.0f));
		WheelsSimData->setWheelCentreOffset(WheelIndex, WheelCenterCMOffsets[WheelIndex]);
		WheelsSimData->setSuspForceAppPointOffset(WheelIndex, PxVec3(WheelCenterCMOffsets[WheelIndex].x, WheelCenterCMOffsets[WheelIndex].y, -0.30f));
		WheelsSimData->setTireForceAppPointOffset(WheelIndex, PxVec3(WheelCenterCMOffsets[WheelIndex].x, WheelCenterCMOffsets[WheelIndex].y, -0.30f));
		WheelsSimData->setWheelShapeMapping(WheelIndex, WheelIndex);
		WheelsSimData->setSceneQueryFilterData(WheelIndex, WheelRaycastFilter);
	}

	PxVehicleDriveSimData4W DriveData;
	PxVehicleDifferential4WData Differential;
	Differential.mType = PxVehicleDifferential4WData::eDIFF_TYPE_LS_4WD;
	DriveData.setDiffData(Differential);

	PxVehicleEngineData EngineData;
	EngineData.mPeakTorque = Desc.EnginePeakTorque;
	EngineData.mMaxOmega = Desc.EngineMaxOmega;
	EngineData.mTorqueCurve.addPair(0.0f, 0.80f);
	EngineData.mTorqueCurve.addPair(0.33f, 1.00f);
	EngineData.mTorqueCurve.addPair(1.0f, 0.80f);
	DriveData.setEngineData(EngineData);

	PxVehicleClutchData ClutchData;
	ClutchData.mStrength = Desc.ClutchStrength;
	DriveData.setClutchData(ClutchData);

	PxVehicleGearsData GearsData;
	DriveData.setGearsData(GearsData);

	PxVehicleAckermannGeometryData AckermannData;
	AckermannData.mAccuracy = 1.0f;
	AckermannData.mAxleSeparation =
		std::abs(Desc.WheelCenterOffsets[static_cast<PxU32>(EVehicle4WWheelIndex::FrontLeft)].X -
			Desc.WheelCenterOffsets[static_cast<PxU32>(EVehicle4WWheelIndex::RearLeft)].X);
	AckermannData.mFrontWidth =
		std::abs(Desc.WheelCenterOffsets[static_cast<PxU32>(EVehicle4WWheelIndex::FrontRight)].Y -
			Desc.WheelCenterOffsets[static_cast<PxU32>(EVehicle4WWheelIndex::FrontLeft)].Y);
	AckermannData.mRearWidth =
		std::abs(Desc.WheelCenterOffsets[static_cast<PxU32>(EVehicle4WWheelIndex::RearRight)].Y -
			Desc.WheelCenterOffsets[static_cast<PxU32>(EVehicle4WWheelIndex::RearLeft)].Y);
	DriveData.setAckermannGeometryData(AckermannData);

	Drive4W->setup(Physics, Actor, *WheelsSimData, DriveData, 0);
	Drive4W->setToRestState();
	WheelsSimData->free();

	PxVehicleDrivableSurfaceType SurfaceTypes[1];
	SurfaceTypes[0].mType = 0;
	const PxMaterial* SurfaceMaterials[1] = { DefaultMaterial };
	RuntimeData->FrictionPairs = PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(1, 1);
	if (!RuntimeData->FrictionPairs)
	{
		Drive4W->free();
		delete RuntimeData;
		Scene->removeActor(*Actor);
		Actor->release();
		delete Body;
		delete Instance;
		return nullptr;
	}
	RuntimeData->FrictionPairs->setup(1, 1, SurfaceMaterials, SurfaceTypes);
	RuntimeData->FrictionPairs->setTypePairFriction(0, 0, Desc.TireFriction);

	PxBatchQueryDesc BatchQueryDesc(NumVehicle4WWheels, 0, 0);
	BatchQueryDesc.queryMemory.userRaycastResultBuffer = RuntimeData->RaycastResults.data();
	BatchQueryDesc.queryMemory.userRaycastTouchBuffer = RuntimeData->RaycastHits.data();
	BatchQueryDesc.queryMemory.raycastTouchBufferSize = NumVehicle4WWheels;
	BatchQueryDesc.preFilterShader = VehicleSuspensionRaycastPreFilter;
	RuntimeData->SuspensionBatchQuery = Scene->createBatchQuery(BatchQueryDesc);
	if (!RuntimeData->SuspensionBatchQuery)
	{
		RuntimeData->FrictionPairs->release();
		Drive4W->free();
		delete RuntimeData;
		Scene->removeActor(*Actor);
		Actor->release();
		delete Body;
		delete Instance;
		return nullptr;
	}

	RuntimeData->Vehicle = Drive4W;
	Instance->VehicleHandle.NativePtr = RuntimeData;
	Instance->ChassisBody = Body;
	Instance->bValid = true;

	Bodies.push_back(Body);
	Vehicles.push_back(Instance);
	UpdateCachedVehicleWheelTransforms(Instance);

	return Instance;
}

void FPhysXRuntime::DestroyVehicle4W(FVehicle4WInstance* Vehicle)
{
	if (!Vehicle)
	{
		return;
	}

	Vehicles.erase(std::remove(Vehicles.begin(), Vehicles.end(), Vehicle), Vehicles.end());

	FPhysXVehicle4WData* Data = GetVehicleData(Vehicle);
	if (Data)
	{
		if (Data->SuspensionBatchQuery)
		{
			Data->SuspensionBatchQuery->release();
			Data->SuspensionBatchQuery = nullptr;
		}
		if (Data->FrictionPairs)
		{
			Data->FrictionPairs->release();
			Data->FrictionPairs = nullptr;
		}
		if (Data->Vehicle)
		{
			Data->Vehicle->free();
			Data->Vehicle = nullptr;
		}
		delete Data;
	}

	FBodyInstance* Body = Vehicle->ChassisBody;
	Vehicle->Reset();
	delete Vehicle;

	DestroyRigidBody(Body);
}

void FPhysXRuntime::SetVehicle4WInput(FVehicle4WInstance* Vehicle, const FVehicle4WInput& Input)
{
	if (!Vehicle || !Vehicle->bValid)
	{
		return;
	}

	Vehicle->LastInput = Input;
}

bool FPhysXRuntime::GetVehicle4WWheelTransforms(
	const FVehicle4WInstance* Vehicle,
	TStaticArray<FTransform, 4>& OutTransforms) const
{
	if (!Vehicle || !Vehicle->bValid)
	{
		return false;
	}

	OutTransforms = Vehicle->WheelWorldTransforms;
	return true;
}

bool FPhysXRuntime::GetBodyTransform(const FBodyInstance* Body, FTransform& OutTransform) const
{
	const PxRigidActor* Actor = PhysXHelpers::GetPxActor(Body);
	if (!Actor)
	{
		return false;
	}

	OutTransform = PhysXHelpers::ToFTransform(Actor->getGlobalPose());
	return true;
}

void FPhysXRuntime::SetBodyTransform(FBodyInstance* Body, const FTransform& Transform, bool bTeleport)
{
	PxRigidActor* Actor = PhysXHelpers::GetPxActor(Body);
	if (!Actor)
	{
		return;
	}

	if (!bTeleport)
	{
		SetKinematicTarget(Body, Transform);
		return;
	}

	Actor->setGlobalPose(PhysXHelpers::ToPxTransform(Transform));
	Body->CachedWorldTransform = Transform;
}

void FPhysXRuntime::SetKinematicTarget(FBodyInstance* Body, const FTransform& Transform)
{
	PxRigidDynamic* Dynamic = PhysXHelpers::GetPxDynamic(Body);
	if (!Dynamic)
	{
		return;
	}

	Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
	Dynamic->setKinematicTarget(PhysXHelpers::ToPxTransform(Transform));
	Body->BodyType = EPhysicsBodyType::Kinematic;
	Body->CachedWorldTransform = Transform;
}

void FPhysXRuntime::SetBodyType(FBodyInstance* Body, EPhysicsBodyType NewType)
{
	if (!Body)
	{
		return;
	}

	// PxRigidStatic↔PxRigidDynamic 전환은 actor 자체를 재생성해야 한다 — 이번 범위 밖.
	// Static 요청은 무시한다.
	if (NewType == EPhysicsBodyType::Static)
	{
		return;
	}

	PxRigidDynamic* Dynamic = GetPxDynamic(Body);
	if (!Dynamic)
	{
		return;
	}

	if (Body->BodyType == NewType)
	{
		// 이미 같은 타입이라도 Dynamic 진입 시점에 잠들어 있을 수 있으니 wake만 보장.
		if (NewType == EPhysicsBodyType::Dynamic)
		{
			Dynamic->wakeUp();
		}
		return;
	}

	const bool bWantKinematic = (NewType == EPhysicsBodyType::Kinematic);
	Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, bWantKinematic);
	Body->BodyType = NewType;
	Body->bSimulating = (NewType == EPhysicsBodyType::Dynamic);

	if (NewType == EPhysicsBodyType::Dynamic)
	{
		// kinematic → dynamic 전환은 즉시 wake — 잠든 상태로 두면 중력/조인트가 적용되지 않는다.
		Dynamic->wakeUp();
	}
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
	OutStats.NumVehicles = static_cast<uint32>(Vehicles.size());

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
	const PxU32 NumTriangles = RenderBuffer.getNbTriangles();
	const PxDebugTriangle* Triangles = RenderBuffer.getTriangles();

	OutLines.reserve(NumLines + NumTriangles * 3);
	for (PxU32 Index = 0; Index < NumLines; ++Index)
	{
		FPhysicsDebugLine Line;
		Line.Start = PhysXHelpers::ToFVector(Lines[Index].pos0);
		Line.End = PhysXHelpers::ToFVector(Lines[Index].pos1);
		Line.Color = PhysXHelpers::DecodeDebugColor(Lines[Index].color0);
		OutLines.push_back(Line);
	}

	for (PxU32 Index = 0; Index < NumTriangles; ++Index)
	{
		const PxDebugTriangle& Triangle = Triangles[Index];
		const FVector Color = DecodeDebugColor(Triangle.color0);
		AddDebugLine(OutLines, Triangle.pos0, Triangle.pos1, Color);
		AddDebugLine(OutLines, Triangle.pos1, Triangle.pos2, Color);
		AddDebugLine(OutLines, Triangle.pos2, Triangle.pos0, Color);
	}
}

void FPhysXRuntime::ExtractVehicleDebugLines(TArray<FPhysicsDebugLine>& OutLines) const
{
	OutLines.clear();

	for (const FVehicle4WInstance* Vehicle : Vehicles)
	{
		AppendVehicleDebugLines(Vehicle, OutLines);
	}
}
