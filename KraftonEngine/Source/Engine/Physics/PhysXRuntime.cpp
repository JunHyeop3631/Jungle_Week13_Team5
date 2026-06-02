#include "Physics/PhysXRuntime.h"
#include "Physics/PhysXCore.h"
#include "Physics/PhysXHelpers.h"
#include "Physics/PhysXSceneLock.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "GameFramework/AActor.h"
#include "Object/Object.h"
#include "Core/Logging/Log.h"
#include "Physics/Asset/BodySetup.h"

#include <algorithm>
#include <cmath>

using namespace PhysXHelpers;

namespace
{
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

	bool HasAnyBlockResponse(const UPrimitiveComponent* Comp)
	{
		if (!Comp)
		{
			return false;
		}

		for (int32 Channel = 0; Channel < static_cast<int32>(ECollisionChannel::ActiveCount); ++Channel)
		{
			if (Comp->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Channel)) == ECollisionResponse::Block)
			{
				return true;
			}
		}
		return false;
	}

	float GetMaxAbsScale(const FVector& Scale)
	{
		return (std::max)((std::max)(std::abs(Scale.X), std::abs(Scale.Y)), std::abs(Scale.Z));
	}

	bool IsBodyQueryCollisionEnabled(const UBodySetup& BodySetup)
	{
		return BodySetup.CollisionEnabled == EBodyCollisionEnabled::QueryOnly
			|| BodySetup.CollisionEnabled == EBodyCollisionEnabled::QueryAndPhysics;
	}

	bool IsBodyPhysicsCollisionEnabled(const UBodySetup& BodySetup)
	{
		return BodySetup.CollisionEnabled == EBodyCollisionEnabled::PhysicsOnly
			|| BodySetup.CollisionEnabled == EBodyCollisionEnabled::QueryAndPhysics;
	}

	void ApplyBodyMaterial(const UBodySetup& BodySetup, FPhysicsShapeDesc& ShapeDesc)
	{
		ShapeDesc.Material.StaticFriction = BodySetup.Friction;
		ShapeDesc.Material.DynamicFriction = BodySetup.Friction;
		ShapeDesc.Material.Restitution = BodySetup.Restitution;
	}

	void ApplyCollisionFlags(const UPrimitiveComponent* Comp, const UBodySetup& BodySetup, FPhysicsShapeDesc& ShapeDesc)
	{
		const bool bBodyQuery = IsBodyQueryCollisionEnabled(BodySetup);
		const bool bBodyPhysics = IsBodyPhysicsCollisionEnabled(BodySetup);
		const bool bHasBlockResponse = HasAnyBlockResponse(Comp);
		const bool bTrigger = Comp && Comp->GetGenerateOverlapEvents()
			&& (!Comp->IsPhysicsCollisionEnabled() || !bBodyPhysics || !bHasBlockResponse);

		ShapeDesc.bTriggerShape = bTrigger;
		ShapeDesc.bSimulationShape = !bTrigger && bBodyPhysics && Comp && Comp->IsPhysicsCollisionEnabled();
		ShapeDesc.bSceneQueryShape = bBodyQuery && Comp && Comp->IsQueryCollisionEnabled();
	}

	void ApplyComponentCollisionFlags(const UPrimitiveComponent* Comp, FPhysicsShapeDesc& ShapeDesc)
	{
		const bool bHasBlockResponse = HasAnyBlockResponse(Comp);
		const bool bTrigger = Comp && Comp->GetGenerateOverlapEvents()
			&& (!Comp->IsPhysicsCollisionEnabled() || !bHasBlockResponse);

		ShapeDesc.bTriggerShape = bTrigger;
		ShapeDesc.bSimulationShape = !bTrigger && Comp && Comp->IsPhysicsCollisionEnabled();
		ShapeDesc.bSceneQueryShape = Comp && Comp->IsQueryCollisionEnabled();
	}

	void AppendStaticMeshBodySetupShapes(const UStaticMeshComponent* Comp, const UBodySetup& BodySetup, FPhysicsBodyDesc& BodyDesc)
	{
		const FVector Scale = Comp ? Comp->GetWorldScale() : FVector(1.0f, 1.0f, 1.0f);
		const FVector AbsScale(std::abs(Scale.X), std::abs(Scale.Y), std::abs(Scale.Z));
		const float UniformScale = (std::max)(GetMaxAbsScale(AbsScale), 0.001f);

		for (const FKSphereElem& Sphere : BodySetup.AggregateGeom.SphereElems)
		{
			FPhysicsShapeDesc ShapeDesc;
			ShapeDesc.Name = BodySetup.BoneName + "_Sphere";
			ShapeDesc.ShapeType = EPhysicsShapeType::Sphere;
			ShapeDesc.LocalTransform = FTransform(
				FVector(Sphere.Center.X * Scale.X, Sphere.Center.Y * Scale.Y, Sphere.Center.Z * Scale.Z),
				FQuat::Identity,
				FVector(1.0f, 1.0f, 1.0f));
			ShapeDesc.Radius = (std::max)(Sphere.Radius * UniformScale, 0.001f);
			ApplyBodyMaterial(BodySetup, ShapeDesc);
			ApplyCollisionFlags(Comp, BodySetup, ShapeDesc);
			BodyDesc.Shapes.push_back(ShapeDesc);
		}

		for (const FKBoxElem& Box : BodySetup.AggregateGeom.BoxElems)
		{
			FPhysicsShapeDesc ShapeDesc;
			ShapeDesc.Name = BodySetup.BoneName + "_Box";
			ShapeDesc.ShapeType = EPhysicsShapeType::Box;
			ShapeDesc.LocalTransform = FTransform(
				FVector(Box.Center.X * Scale.X, Box.Center.Y * Scale.Y, Box.Center.Z * Scale.Z),
				Box.Rotation,
				FVector(1.0f, 1.0f, 1.0f));
			ShapeDesc.HalfExtent = FVector(
				(std::max)(Box.HalfX * AbsScale.X, 0.001f),
				(std::max)(Box.HalfY * AbsScale.Y, 0.001f),
				(std::max)(Box.HalfZ * AbsScale.Z, 0.001f));
			ApplyBodyMaterial(BodySetup, ShapeDesc);
			ApplyCollisionFlags(Comp, BodySetup, ShapeDesc);
			BodyDesc.Shapes.push_back(ShapeDesc);
		}

		for (const FKCapsuleElem& Capsule : BodySetup.AggregateGeom.CapsuleElems)
		{
			FPhysicsShapeDesc ShapeDesc;
			ShapeDesc.Name = BodySetup.BoneName + "_Capsule";
			ShapeDesc.ShapeType = EPhysicsShapeType::Capsule;
			ShapeDesc.LocalTransform = FTransform(
				FVector(Capsule.Center.X * Scale.X, Capsule.Center.Y * Scale.Y, Capsule.Center.Z * Scale.Z),
				Capsule.Rotation,
				FVector(1.0f, 1.0f, 1.0f));
			ShapeDesc.Radius = (std::max)(Capsule.Radius * UniformScale, 0.001f);
			ShapeDesc.HalfHeight = (std::max)(0.0f, Capsule.HalfHeight - Capsule.Radius) * UniformScale;
			ApplyBodyMaterial(BodySetup, ShapeDesc);
			ApplyCollisionFlags(Comp, BodySetup, ShapeDesc);
			BodyDesc.Shapes.push_back(ShapeDesc);
		}
	}

	bool AppendStaticMeshTriangleMeshShape(const UStaticMeshComponent* Comp, FPhysicsBodyDesc& BodyDesc)
	{
		if (!Comp)
		{
			return false;
		}

		UStaticMesh* StaticMesh = Comp->GetStaticMesh();
		if (!StaticMesh || !StaticMesh->HasTriangleMeshCollision())
		{
			return false;
		}

		const FStaticMesh* MeshAsset = StaticMesh->GetStaticMeshAsset();
		if (!MeshAsset || MeshAsset->Vertices.empty() || MeshAsset->Indices.size() < 3 || MeshAsset->Indices.size() % 3 != 0)
		{
			return false;
		}

		FPhysicsShapeDesc ShapeDesc;
		ShapeDesc.Name = "StaticMesh_TriangleMesh";
		ShapeDesc.ShapeType = EPhysicsShapeType::TriangleMesh;
		ShapeDesc.LocalTransform = FTransform();
		ApplyComponentCollisionFlags(Comp, ShapeDesc);
		if (ShapeDesc.bTriggerShape)
		{
			ShapeDesc.bTriggerShape = false;
			ShapeDesc.bSimulationShape = false;
		}

		const FVector Scale = Comp->GetWorldScale();
		ShapeDesc.TriangleMeshVertices.reserve(MeshAsset->Vertices.size());
		for (const FNormalVertex& Vertex : MeshAsset->Vertices)
		{
			ShapeDesc.TriangleMeshVertices.push_back(FVector(Vertex.pos.X * Scale.X, Vertex.pos.Y * Scale.Y, Vertex.pos.Z * Scale.Z));
		}
		ShapeDesc.TriangleMeshIndices = MeshAsset->Indices;

		BodyDesc.Shapes.push_back(ShapeDesc);
		return true;
	}

	PxTriangleMesh* CookTriangleMesh(PxFoundation* Foundation, PxPhysics* Physics, const FPhysicsShapeDesc& Desc)
	{
		if (!Foundation || !Physics || Desc.TriangleMeshVertices.empty() || Desc.TriangleMeshIndices.size() < 3 || Desc.TriangleMeshIndices.size() % 3 != 0)
		{
			return nullptr;
		}

		PxTriangleMeshDesc MeshDesc;
		MeshDesc.points.count = static_cast<PxU32>(Desc.TriangleMeshVertices.size());
		MeshDesc.points.stride = sizeof(FVector);
		MeshDesc.points.data = Desc.TriangleMeshVertices.data();
		MeshDesc.triangles.count = static_cast<PxU32>(Desc.TriangleMeshIndices.size() / 3);
		MeshDesc.triangles.stride = sizeof(uint32) * 3;
		MeshDesc.triangles.data = Desc.TriangleMeshIndices.data();

		PxCookingParams Params(Physics->getTolerancesScale());
		Params.meshPreprocessParams |= PxMeshPreprocessingFlag::eWELD_VERTICES;
		Params.meshWeldTolerance = 0.001f;

		PxCooking* Cooking = PxCreateCooking(PX_PHYSICS_VERSION, *Foundation, Params);
		if (!Cooking)
		{
			UE_LOG("[PhysXRuntime] Failed to create PhysX cooking interface for triangle mesh collision");
			return nullptr;
		}

		PxDefaultMemoryOutputStream CookedData;
		if (!Cooking->cookTriangleMesh(MeshDesc, CookedData))
		{
			UE_LOG("[PhysXRuntime] Failed to cook triangle mesh collision (%u vertices, %u triangles)",
				MeshDesc.points.count,
				MeshDesc.triangles.count);
			Cooking->release();
			return nullptr;
		}

		PxDefaultMemoryInputData InputData(CookedData.getData(), CookedData.getSize());
		PxTriangleMesh* TriangleMesh = Physics->createTriangleMesh(InputData);
		Cooking->release();
		return TriangleMesh;
	}

	PxFilterData BuildComponentFilterData(const UPrimitiveComponent* Comp)
	{
		PxFilterData Filter;
		if (!Comp)
		{
			return Filter;
		}

		Filter.word0 = static_cast<PxU32>(Comp->GetCollisionObjectType());
		Filter.word1 = 0;
		Filter.word2 = 0;
		Filter.word3 = Comp->GetOwner() ? Comp->GetOwner()->GetUUID() : 0;

		for (int32 Channel = 0; Channel < static_cast<int32>(ECollisionChannel::ActiveCount); ++Channel)
		{
			const ECollisionResponse Response = Comp->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Channel));
			if (Response == ECollisionResponse::Block)
			{
				Filter.word1 |= (1u << Channel);
			}
			else if (Response == ECollisionResponse::Overlap)
			{
				Filter.word2 |= (1u << Channel);
			}
		}
		return Filter;
	}

	void ApplyComponentFilterData(PxShape* Shape, const UPrimitiveComponent* Comp)
	{
		if (!Shape || !Comp)
		{
			return;
		}

		const PxFilterData Filter = BuildComponentFilterData(Comp);
		Shape->setSimulationFilterData(Filter);
		Shape->setQueryFilterData(Filter);
	}

	// 래그돌 자기충돌 per-pair 비활성용 시뮬레이션 필터 데이터 태그.
	// object-type enum 은 bit31 을 절대 쓰지 않으므로 센티넬로 사용한다.
	constexpr PxU32 RAGDOLL_FILTER_TAG = 0x80000000u;

	ECollisionResponse GetResponseFromFilter(const PxFilterData& Source, PxU32 TargetObjectType)
	{
		if (TargetObjectType >= 32)
		{
			return ECollisionResponse::Ignore;
		}

		const PxU32 TargetBit = 1u << TargetObjectType;
		if ((Source.word1 & TargetBit) != 0)
		{
			return ECollisionResponse::Block;
		}
		if ((Source.word2 & TargetBit) != 0)
		{
			return ECollisionResponse::Overlap;
		}
		return ECollisionResponse::Ignore;
	}

	ECollisionResponse GetPairResponseFromFilter(const PxFilterData& FilterData0, const PxFilterData& FilterData1)
	{
		const ECollisionResponse Response0 = GetResponseFromFilter(FilterData0, FilterData1.word0);
		const ECollisionResponse Response1 = GetResponseFromFilter(FilterData1, FilterData0.word0);
		return (Response0 < Response1) ? Response0 : Response1;
	}

	// 커스텀 시뮬레이션 필터 셰이더.
	//   - 같은 래그돌 그룹(word0 하위비트 일치)인 두 바디가 서로의 ignore-mask 에 들어 있으면 eKILL.
	//   - 그 외 모든 쌍은 PxDefaultSimulationFilterShader 에 그대로 위임 → 비-래그돌 동작 무변경.
	// 시뮬 필터 데이터만 사용하므로 query(레이캐스트/스윕) 채널 로직에는 영향이 없다.
	PxFilterFlags KraftonRagdollFilterShader(
		PxFilterObjectAttributes Attributes0, PxFilterData FilterData0,
		PxFilterObjectAttributes Attributes1, PxFilterData FilterData1,
		PxPairFlags& PairFlags, const void* ConstantBlock, PxU32 ConstantBlockSize)
	{
		(void)ConstantBlock;
		(void)ConstantBlockSize;

		const bool bRagdoll0 = (FilterData0.word0 & RAGDOLL_FILTER_TAG) != 0;
		const bool bRagdoll1 = (FilterData1.word0 & RAGDOLL_FILTER_TAG) != 0;

		// 같은 액터(word3 = owner actor UUID 일치) 쌍은 충돌 제외 — 한 액터의 컴포넌트끼리는 물리 충돌
		// 안 한다(이동용 캡슐/박스 ↔ 래그돌 바디 중복 충돌로 인한 튕김 방지). 단 래그돌-래그돌 자기충돌은
		// aggregate(self-collision off) / 아래 ragdoll 경로가 전담하므로 여기선 제외한다.
		if (FilterData0.word3 != 0 && FilterData0.word3 == FilterData1.word3 && !(bRagdoll0 && bRagdoll1)
			&& !PxFilterObjectIsTrigger(Attributes0) && !PxFilterObjectIsTrigger(Attributes1))
		{
			return PxFilterFlag::eSUPPRESS;
		}

		// 래그돌 바디가 하나라도 끼는 쌍은 여기서 직접 판정한다.
		// 래그돌 shape 의 "시뮬" 필터 데이터는 (태그|그룹 / 인덱스 / 마스크)로 재해석되므로,
		// 그룹 시스템 기반 기본 셰이더에 그대로 넘기면 오해석된다 → 비-래그돌 쌍만 위임한다.
		if (bRagdoll0 || bRagdoll1)
		{
			// 트리거는 기본 셰이더와 동일하게 트리거 이벤트로 처리(접촉 생성 금지).
			if (PxFilterObjectIsTrigger(Attributes0) || PxFilterObjectIsTrigger(Attributes1))
			{
				PairFlags = PxPairFlag::eTRIGGER_DEFAULT;
				return PxFilterFlags();
			}

			// 같은 래그돌 그룹의 두 바디가 서로의 ignore-mask 에 들어 있으면 충돌 제거(eKILL).
			//   word1 = 자기 인덱스 비트, word2 = 충돌 비활성 대상 마스크. 양방향 검사.
			if (bRagdoll0 && bRagdoll1 &&
				((FilterData0.word0 & ~RAGDOLL_FILTER_TAG) == (FilterData1.word0 & ~RAGDOLL_FILTER_TAG)) &&
				((FilterData0.word2 & FilterData1.word1) != 0 || (FilterData1.word2 & FilterData0.word1) != 0))
			{
				return PxFilterFlag::eKILL;
			}

			// 그 외(래그돌-월드, 래그돌-래그돌 비-ignore)는 기본 접촉.
			PairFlags = PxPairFlag::eCONTACT_DEFAULT;
			return PxFilterFlags();
		}

		if (PxFilterObjectIsTrigger(Attributes0) || PxFilterObjectIsTrigger(Attributes1))
		{
			PairFlags = PxPairFlag::eTRIGGER_DEFAULT;
			return PxFilterFlags();
		}

		const ECollisionResponse PairResponse = GetPairResponseFromFilter(FilterData0, FilterData1);
		if (PairResponse == ECollisionResponse::Ignore)
		{
			return PxFilterFlag::eSUPPRESS;
		}

		if (PairResponse == ECollisionResponse::Overlap)
		{
			PairFlags = PxPairFlag::eDETECT_DISCRETE_CONTACT
				| PxPairFlag::eNOTIFY_TOUCH_FOUND
				| PxPairFlag::eNOTIFY_TOUCH_LOST;
			return PxFilterFlags();
		}

		PairFlags = PxPairFlag::eCONTACT_DEFAULT
			| PxPairFlag::eNOTIFY_TOUCH_FOUND
			| PxPairFlag::eNOTIFY_TOUCH_LOST
			| PxPairFlag::eNOTIFY_CONTACT_POINTS;
		return PxFilterFlags();
	}

	FBodyInstance* GetBodyFromActor(const PxRigidActor* Actor)
	{
		return Actor ? static_cast<FBodyInstance*>(Actor->userData) : nullptr;
	}

	UPrimitiveComponent* GetComponentFromActor(const PxRigidActor* Actor)
	{
		FBodyInstance* Body = GetBodyFromActor(Actor);
		return Body ? Body->OwnerComponent : nullptr;
	}

	bool IsTriggerShape(const PxShape* Shape)
	{
		return Shape && Shape->getFlags().isSet(PxShapeFlag::eTRIGGER_SHAPE);
	}

	void FillHitResult(const PxLocationHit& Hit, FHitResult& OutHit)
	{
		OutHit = FHitResult();
		OutHit.bHit = true;
		OutHit.Distance = Hit.distance;
		OutHit.WorldHitLocation = ToFVector(Hit.position);
		OutHit.WorldNormal = ToFVector(Hit.normal);
		OutHit.ImpactNormal = OutHit.WorldNormal;
		OutHit.FaceIndex = static_cast<int>(Hit.faceIndex);

		if (UPrimitiveComponent* HitComp = GetComponentFromActor(Hit.actor))
		{
			OutHit.HitComponent = HitComp;
			OutHit.HitActor = HitComp->GetOwner();
		}
	}

	static constexpr PxU32 NumVehicle4WWheels = 4;
	static constexpr PxU32 VehicleQuerySelfIdMask = 0x7fffffffu;

	PxU32 MakeVehicleQuerySelfId(uint64 Serial)
	{
		PxU32 Id = static_cast<PxU32>(Serial & VehicleQuerySelfIdMask);
		return Id != 0 ? Id : 1u;
	}

	PxFilterData BuildVehicleChassisFilterData(PxU32 VehicleSelfId)
	{
		PxFilterData Filter;
		Filter.word0 = static_cast<PxU32>(ECollisionChannel::WorldDynamic);
		Filter.word1 = 0;
		Filter.word2 = 0;
		Filter.word3 = VehicleSelfId;

		for (PxU32 Channel = 0; Channel < static_cast<PxU32>(ECollisionChannel::ActiveCount); ++Channel)
		{
			Filter.word1 |= (1u << Channel);
		}
		return Filter;
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

		if (FilterData0.word1 != 0)
		{
			const PxU32 ObjectBit = 1u << FilterData1.word0;
			if ((FilterData0.word1 & ObjectBit) == 0)
			{
				return PxQueryHitType::eNONE;
			}
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

	void ApplyVehicleInputToRawData(
		FVehicle4WInstance* Instance,
		FPhysXVehicle4WData* Data,
		PxRigidDynamic* Actor, 
		PxVehicleDrive4WRawInputData& OutRawInput)
	{
		if (!Instance || !Data || !Data->Vehicle || !Actor)
			return;

		PxTransform ChassisTransform = Actor->getGlobalPose();

		PxVec3 ForwardDir = ChassisTransform.q.rotate(PxVec3(1.0f, 0.0f, 0.0f));
		float ForwardSpeed = Actor->getLinearVelocity().dot(ForwardDir);

		const float StopThreshold = 1.0f;
		const FVehicle4WInput& Input = Instance->LastInput;

		OutRawInput.setDigitalSteerLeft(Input.bSteerLeft);
		OutRawInput.setDigitalSteerRight(Input.bSteerRight);
		OutRawInput.setDigitalHandbrake(Input.bHandbrake);

		if (Input.bMoveBackward) // 사용자가 후진 키(S)를 누름
		{
			if (ForwardSpeed > StopThreshold)
			{
				// 아직 앞으로 달리는 중 -> 브레이크를 밟아 감속
				OutRawInput.setDigitalAccel(false);
				OutRawInput.setDigitalBrake(true);
			}
			else
			{
				// 거의 멈췄거나 이미 후진 중 -> 후진 기어를 넣고 후진 가속
				Data->Vehicle->mDriveDynData.setTargetGear(PxVehicleGearsData::eREVERSE);
				OutRawInput.setDigitalAccel(true);
				OutRawInput.setDigitalBrake(false);
			}
		}
		else if (Input.bMoveForward) // 사용자가 전진 키(W)를 누름
		{
			if (ForwardSpeed < -StopThreshold)
			{
				// 뒤로 가고 있는 중 -> 브레이크를 밟아 감속
				OutRawInput.setDigitalAccel(false);
				OutRawInput.setDigitalBrake(true);
			}
			else
			{
				// 거의 멈췄거나 전진 중 -> 1단 기어를 넣고 전진 가속
				Data->Vehicle->mDriveDynData.setTargetGear(PxVehicleGearsData::eFIRST);
				OutRawInput.setDigitalAccel(true);
				OutRawInput.setDigitalBrake(false);
			}
		}
		else
		{
			// 아무것도 누르지 않음 (감속 페달 모두 오프)
			OutRawInput.setDigitalAccel(false);
			OutRawInput.setDigitalBrake(false);
		}
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

class FPhysXSimulationEventCallback final : public PxSimulationEventCallback
{
public:
	void Clear()
	{
		Events.clear();
	}

	void Dispatch()
	{
		TArray<FPendingEvent> PendingEvents;
		PendingEvents.swap(Events);

		for (const FPendingEvent& Event : PendingEvents)
		{
			if (!IsValid(Event.A) || !IsValid(Event.B))
			{
				continue;
			}

			AActor* ActorA = Event.A->GetOwner();
			AActor* ActorB = Event.B->GetOwner();
			if (Event.Type == EEventType::BeginOverlap)
			{
				Event.A->NotifyComponentBeginOverlap(Event.A, ActorB, Event.B, 0, false, Event.HitForA);
				Event.B->NotifyComponentBeginOverlap(Event.B, ActorA, Event.A, 0, false, Event.HitForB);
			}
			else if (Event.Type == EEventType::EndOverlap)
			{
				Event.A->NotifyComponentEndOverlap(Event.A, ActorB, Event.B, 0);
				Event.B->NotifyComponentEndOverlap(Event.B, ActorA, Event.A, 0);
			}
			else if (Event.Type == EEventType::BeginHit)
			{
				Event.A->NotifyComponentHit(Event.A, ActorB, Event.B, Event.ImpulseForA, Event.HitForA);
				Event.B->NotifyComponentHit(Event.B, ActorA, Event.A, Event.ImpulseForB, Event.HitForB);
			}
			else if (Event.Type == EEventType::EndHit)
			{
				Event.A->NotifyComponentEndHit(Event.A, ActorB, Event.B);
				Event.B->NotifyComponentEndHit(Event.B, ActorA, Event.A);
			}
		}
	}

	void onContact(const PxContactPairHeader& PairHeader, const PxContactPair* Pairs, PxU32 NumPairs) override
	{
		// 이번 스텝에 actor 가 제거되면(예: SetCollisionEnabled(NoCollision)→DestroyRigidBody, 래그돌 토글)
		// PairHeader.actors[] 는 이미 해제된 dangling 포인터 → GetComponentFromActor 의 userData deref 가 UAF.
		// deref 전에 removed 플래그를 먼저 확인해 skip 한다 (아래 onTrigger 의 eREMOVED_SHAPE 가드와 동형).
		if (PairHeader.flags & (PxContactPairHeaderFlag::eREMOVED_ACTOR_0 | PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
		{
			return;
		}

		UPrimitiveComponent* CompA = GetComponentFromActor(PairHeader.actors[0]);
		UPrimitiveComponent* CompB = GetComponentFromActor(PairHeader.actors[1]);
		if (!CompA || !CompB)
		{
			return;
		}

		const ECollisionResponse Response = UPrimitiveComponent::GetMinResponse(CompA, CompB);
		if (Response == ECollisionResponse::Ignore)
		{
			return;
		}
		if (Response == ECollisionResponse::Overlap && !WantsOverlapEvent(CompA, CompB))
		{
			return;
		}

		for (PxU32 PairIndex = 0; PairIndex < NumPairs; ++PairIndex)
		{
			const PxContactPair& Pair = Pairs[PairIndex];
			// 제거된 shape 가 낀 쌍도 skip — shape/actor 포인터가 dangling 일 수 있다.
			if (Pair.flags & (PxContactPairFlag::eREMOVED_SHAPE_0 | PxContactPairFlag::eREMOVED_SHAPE_1))
			{
				continue;
			}
			const bool bTouchFound = Pair.events.isSet(PxPairFlag::eNOTIFY_TOUCH_FOUND);
			const bool bTouchLost = Pair.events.isSet(PxPairFlag::eNOTIFY_TOUCH_LOST);
			if (!bTouchFound && !bTouchLost)
			{
				continue;
			}

			if (Response == ECollisionResponse::Overlap)
			{
				if (bTouchFound)
				{
					QueueOverlap(EEventType::BeginOverlap, CompA, CompB);
				}
				if (bTouchLost)
				{
					QueueOverlap(EEventType::EndOverlap, CompA, CompB);
				}
			}
			else
			{
				if (bTouchFound)
				{
					QueueHit(CompA, CompB, Pair);
				}
				if (bTouchLost)
				{
					QueueSimple(EEventType::EndHit, CompA, CompB);
				}
			}
		}
	}

	void onTrigger(PxTriggerPair* Pairs, PxU32 Count) override
	{
		for (PxU32 PairIndex = 0; PairIndex < Count; ++PairIndex)
		{
			const PxTriggerPair& Pair = Pairs[PairIndex];
			if (Pair.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
			{
				continue;
			}

			UPrimitiveComponent* TriggerComp = GetComponentFromActor(Pair.triggerActor);
			UPrimitiveComponent* OtherComp = GetComponentFromActor(Pair.otherActor);
			if (!TriggerComp || !OtherComp || !WantsOverlapEvent(TriggerComp, OtherComp))
			{
				continue;
			}

			if (Pair.status & PxPairFlag::eNOTIFY_TOUCH_FOUND)
			{
				QueueOverlap(EEventType::BeginOverlap, TriggerComp, OtherComp);
			}
			if (Pair.status & PxPairFlag::eNOTIFY_TOUCH_LOST)
			{
				QueueOverlap(EEventType::EndOverlap, TriggerComp, OtherComp);
			}
		}
	}

	void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
	void onWake(PxActor**, PxU32) override {}
	void onSleep(PxActor**, PxU32) override {}
	void onAdvance(const PxRigidBody* const*, const PxTransform*, const PxU32) override {}

private:
	enum class EEventType : uint8
	{
		BeginOverlap,
		EndOverlap,
		BeginHit,
		EndHit,
	};

	struct FPendingEvent
	{
		EEventType Type = EEventType::BeginOverlap;
		UPrimitiveComponent* A = nullptr;
		UPrimitiveComponent* B = nullptr;
		FHitResult HitForA;
		FHitResult HitForB;
		FVector ImpulseForA = FVector(0.0f, 0.0f, 0.0f);
		FVector ImpulseForB = FVector(0.0f, 0.0f, 0.0f);
	};

	static bool WantsOverlapEvent(const UPrimitiveComponent* A, const UPrimitiveComponent* B)
	{
		return (A && A->GetGenerateOverlapEvents()) || (B && B->GetGenerateOverlapEvents());
	}

	static FHitResult MakeHitResult(UPrimitiveComponent* OtherComp, const PxVec3& Position, const PxVec3& Normal, float PenetrationDepth = 0.0f)
	{
		FHitResult Hit;
		Hit.bHit = true;
		Hit.HitComponent = OtherComp;
		Hit.HitActor = OtherComp ? OtherComp->GetOwner() : nullptr;
		Hit.WorldHitLocation = ToFVector(Position);
		Hit.WorldNormal = ToFVector(Normal);
		Hit.ImpactNormal = Hit.WorldNormal;
		Hit.PenetrationDepth = PenetrationDepth;
		return Hit;
	}

	void QueueSimple(EEventType Type, UPrimitiveComponent* A, UPrimitiveComponent* B)
	{
		FPendingEvent Event;
		Event.Type = Type;
		Event.A = A;
		Event.B = B;
		Events.push_back(Event);
	}

	void QueueOverlap(EEventType Type, UPrimitiveComponent* A, UPrimitiveComponent* B)
	{
		FPendingEvent Event;
		Event.Type = Type;
		Event.A = A;
		Event.B = B;
		const PxVec3 MidPoint = (ToPxVec3(A->GetWorldLocation()) + ToPxVec3(B->GetWorldLocation())) * 0.5f;
		Event.HitForA = MakeHitResult(B, MidPoint, PxVec3(0.0f, 0.0f, 0.0f));
		Event.HitForB = MakeHitResult(A, MidPoint, PxVec3(0.0f, 0.0f, 0.0f));
		Events.push_back(Event);
	}

	void QueueHit(UPrimitiveComponent* A, UPrimitiveComponent* B, const PxContactPair& Pair)
	{
		FPendingEvent Event;
		Event.Type = EEventType::BeginHit;
		Event.A = A;
		Event.B = B;

		PxContactPairPoint ContactPoint;
		if (Pair.contactCount > 0 && Pair.extractContacts(&ContactPoint, 1) > 0)
		{
			Event.HitForA = MakeHitResult(B, ContactPoint.position, ContactPoint.normal, -ContactPoint.separation);
			Event.HitForB = MakeHitResult(A, ContactPoint.position, -ContactPoint.normal, -ContactPoint.separation);
			Event.ImpulseForA = ToFVector(ContactPoint.impulse);
			Event.ImpulseForB = ToFVector(-ContactPoint.impulse);
		}
		else
		{
			const PxVec3 MidPoint = (ToPxVec3(A->GetWorldLocation()) + ToPxVec3(B->GetWorldLocation())) * 0.5f;
			Event.HitForA = MakeHitResult(B, MidPoint, PxVec3(0.0f, 0.0f, 0.0f));
			Event.HitForB = MakeHitResult(A, MidPoint, PxVec3(0.0f, 0.0f, 0.0f));
		}

		Events.push_back(Event);
	}

	TArray<FPendingEvent> Events;
};

FPhysXRuntime::~FPhysXRuntime()
{
	Shutdown();
}

bool FPhysXRuntime::Initialize()
{
	if (Foundation && Physics && Scene)
	{
		return true;
	}

	if (Foundation || Physics || Scene)
	{
		Shutdown();
	}

	FPhysXCoreHandles Core = AcquireSharedPhysXCore();
	Foundation = Core.Foundation;
	Physics = Core.Physics;
	bVehicleSdkAvailable = Core.bVehicleSdkInitialized;
	if (!Foundation || !Physics)
	{
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
	SceneDesc.filterShader = KraftonRagdollFilterShader;
	EventCallback = new FPhysXSimulationEventCallback();
	SceneDesc.simulationEventCallback = EventCallback;
	ConfigurePhysXSceneDesc(SceneDesc);

	Scene = Physics->createScene(SceneDesc);
	if (!Scene)
	{
		Shutdown();
		return false;
	}
	ConfigurePhysXPvdSceneClient(Scene);

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

	while (!Joints.empty())
	{
		FConstraintInstance* Joint = Joints.back();
		if (!Joint)
		{
			Joints.pop_back();
			continue;
		}
		DestroyJoint(Joint);
	}
	Joints.clear();

	while (!Bodies.empty())
	{
		FBodyInstance* Body = Bodies.back();
		if (!Body)
		{
			Bodies.pop_back();
			continue;
		}
		DestroyRigidBody(Body);
	}
	Bodies.clear();

	// 바디(actor) release 후 남은 aggregate 정리(이미 비어 있음). Scene->release 이전에 수행.
	for (PxAggregate* Aggregate : Aggregates)
	{
		if (Aggregate) Aggregate->release();
	}
	Aggregates.clear();

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

	if (EventCallback)
	{
		EventCallback->Clear();
		delete EventCallback;
		EventCallback = nullptr;
	}

	if (Dispatcher)
	{
		Dispatcher->release();
		Dispatcher = nullptr;
	}

	if (Foundation || Physics)
	{
		Foundation = nullptr;
		Physics = nullptr;
		bVehicleSdkAvailable = false;
		ReleaseSharedPhysXCore();
	}

	NextSerial = 1;
}

FBodyInstance* FPhysXRuntime::FindBodyByComponent(const UPrimitiveComponent* Comp) const
{
	if (!Comp)
	{
		return nullptr;
	}

	for (FBodyInstance* Body : Bodies)
	{
		if (Body && Body->OwnerComponent == Comp)
		{
			return Body;
		}
	}
	return nullptr;
}

bool FPhysXRuntime::BuildBodyDescFromComponent(UPrimitiveComponent* Comp, FPhysicsBodyDesc& OutDesc) const
{
	if (!Comp || !Comp->IsCollisionEnabled())
	{
		return false;
	}

	FPhysicsShapeDesc ShapeDesc;
	if (UBoxComponent* Box = Cast<UBoxComponent>(Comp))
	{
		ShapeDesc.ShapeType = EPhysicsShapeType::Box;
		ShapeDesc.HalfExtent = Box->GetScaledBoxExtent();
	}
	else if (USphereComponent* Sphere = Cast<USphereComponent>(Comp))
	{
		ShapeDesc.ShapeType = EPhysicsShapeType::Sphere;
		ShapeDesc.Radius = Sphere->GetScaledSphereRadius();
	}
	else if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Comp))
	{
		const float Radius = Capsule->GetScaledCapsuleRadius();
		const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		ShapeDesc.ShapeType = EPhysicsShapeType::Capsule;
		ShapeDesc.Radius = Radius;
		ShapeDesc.HalfHeight = (std::max)(0.0f, HalfHeight - Radius);
		// PhysX capsules are X-axis aligned; engine capsules use local Z as their long axis.
		ShapeDesc.LocalTransform.Rotation = FQuat::FromAxisAngle(FVector(0.0f, 1.0f, 0.0f), -PhysicsPi * 0.5f);
	}
	else if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Comp))
	{
		UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh();
		if (StaticMesh && StaticMesh->GetCollisionMode() == EStaticMeshCollisionMode::TriangleMesh)
		{
			if (Comp->GetSimulatePhysics())
			{
				UE_LOG("[PhysXRuntime] Triangle mesh collision is supported only for static static-mesh components");
				return false;
			}

			OutDesc = FPhysicsBodyDesc();
			OutDesc.OwnerComponent = Comp;
			OutDesc.BodyName = "StaticMeshTriangleMesh";
			OutDesc.BodyType = EPhysicsBodyType::Static;
			OutDesc.WorldTransform = FTransform(Comp->GetWorldLocation(), Comp->GetWorldRotation(), FVector(1.0f, 1.0f, 1.0f));
			OutDesc.Mass = std::max(0.001f, Comp->GetMass());
			OutDesc.bUseGravity = false;
			OutDesc.bEnableCCD = false;
			AppendStaticMeshTriangleMeshShape(StaticMeshComp, OutDesc);
			return !OutDesc.Shapes.empty();
		}

		UBodySetup* BodySetup = StaticMeshComp->GetBodySetup();
		if (!BodySetup || BodySetup->AggregateGeom.IsEmpty() || BodySetup->CollisionEnabled == EBodyCollisionEnabled::NoCollision)
		{
			return false;
		}

		OutDesc = FPhysicsBodyDesc();
		OutDesc.OwnerComponent = Comp;
		OutDesc.BodyName = BodySetup->BoneName.empty() ? FString("StaticMesh") : BodySetup->BoneName;
		OutDesc.BodyType = Comp->GetSimulatePhysics() ? EPhysicsBodyType::Dynamic : EPhysicsBodyType::Static;
		OutDesc.WorldTransform = FTransform(Comp->GetWorldLocation(), Comp->GetWorldRotation(), FVector(1.0f, 1.0f, 1.0f));
		OutDesc.Mass = std::max(0.001f, Comp->GetMass() > 0.0f ? Comp->GetMass() : BodySetup->Mass);
		OutDesc.LinearDamping  = Comp->GetLinearDamping();
		OutDesc.AngularDamping = Comp->GetAngularDamping();
		OutDesc.bUseGravity    = Comp->IsGravityEnabled();
		OutDesc.bEnableCCD = Comp->GetSimulatePhysics();

		AppendStaticMeshBodySetupShapes(StaticMeshComp, *BodySetup, OutDesc);
		return !OutDesc.Shapes.empty();
	}
	else
	{
		return false;
	}

	const bool bHasBlockResponse = HasAnyBlockResponse(Comp);
	const bool bTrigger = Comp->GetGenerateOverlapEvents()
		&& (!Comp->IsPhysicsCollisionEnabled() || !bHasBlockResponse);
	ShapeDesc.bTriggerShape = bTrigger;
	ShapeDesc.bSimulationShape = !bTrigger && Comp->IsPhysicsCollisionEnabled();
	ShapeDesc.bSceneQueryShape = Comp->IsQueryCollisionEnabled();

	OutDesc = FPhysicsBodyDesc();
	OutDesc.OwnerComponent = Comp;
	OutDesc.BodyName = "PrimitiveComponent";
	OutDesc.BodyType = Comp->GetSimulatePhysics() ? EPhysicsBodyType::Dynamic : EPhysicsBodyType::Static;
	OutDesc.WorldTransform = FTransform(Comp->GetWorldLocation(), Comp->GetWorldRotation(), FVector(1.0f, 1.0f, 1.0f));
	OutDesc.Mass = Comp->GetMass();
	OutDesc.bUseGravity = true;
	OutDesc.bEnableCCD = Comp->GetSimulatePhysics();
	OutDesc.Shapes.push_back(ShapeDesc);
	return true;
}

void FPhysXRuntime::RegisterComponent(UPrimitiveComponent* Comp)
{
	if (!Comp)
	{
		return;
	}

	UnregisterComponent(Comp);

	FPhysicsBodyDesc Desc;
	if (!BuildBodyDescFromComponent(Comp, Desc))
	{
		return;
	}

	CreateRigidBody(Desc);
	SetCenterOfMass(Comp, Comp->GetCenterOfMass());
}

void FPhysXRuntime::UnregisterComponent(UPrimitiveComponent* Comp)
{
	if (FBodyInstance* Body = FindBodyByComponent(Comp))
	{
		DestroyRigidBody(Body);
	}
}

void FPhysXRuntime::RebuildBody(UPrimitiveComponent* Comp)
{
	if (!Comp || !Comp->IsCollisionEnabled())
	{
		UnregisterComponent(Comp);
		return;
	}

	RegisterComponent(Comp);
}

void FPhysXRuntime::Simulate(float DeltaTime)
{
	if (!Scene || DeltaTime <= 0.0f)
	{
		return;
	}

	struct FPendingBodySync
	{
		FBodyInstance* Body = nullptr;
		UPrimitiveComponent* OwnerComponent = nullptr;
		FTransform Transform;
		bool bApplyToComponent = false;
	};

	TArray<FPendingBodySync> KinematicSyncs;
	KinematicSyncs.reserve(Bodies.size());
	for (FBodyInstance* Body : Bodies)
	{
		if (!Body || !Body->OwnerComponent || Body->BoneIndex >= 0 || Body->BodyType == EPhysicsBodyType::Dynamic)
		{
			continue;
		}

		FPendingBodySync Sync;
		Sync.Body = Body;
		Sync.OwnerComponent = Body->OwnerComponent;
		Sync.Transform = FTransform(
			Body->OwnerComponent->GetWorldLocation(),
			Body->OwnerComponent->GetWorldRotation(),
			FVector(1.0f, 1.0f, 1.0f));
		KinematicSyncs.push_back(Sync);
	}

	{
		PHYSX_SCENE_WRITE_LOCK(Scene);

		for (const FPendingBodySync& Sync : KinematicSyncs)
		{
			if (PxRigidActor* Actor = GetPxActor(Sync.Body))
			{
				Actor->setGlobalPose(ToPxTransform(Sync.Transform));
			}
		}

		for (FVehicle4WInstance* VehicleInstance : Vehicles)
		{
			FPhysXVehicle4WData* Data = GetVehicleData(VehicleInstance);
			PxRigidDynamic* Actor = GetPxDynamic(VehicleInstance->ChassisBody);

			if (!VehicleInstance || !Data || !Data->Vehicle || !Data->SuspensionBatchQuery || !Data->FrictionPairs || !Actor)
			{
				continue;
			}

			ApplyVehicleInputToRawData(VehicleInstance, Data, Actor, Data->RawInput);

			PxVehicleDrive4WSmoothDigitalRawInputsAndSetAnalogInputs(
				Data->KeySmoothingData,
				Data->SteerVsForwardSpeedTable,
				Data->RawInput,
				DeltaTime,
				false,
				*Data->Vehicle);

			if (VehicleInstance->LastInput.bMoveForward || VehicleInstance->LastInput.bMoveBackward ||
				VehicleInstance->LastInput.bSteerLeft || VehicleInstance->LastInput.bSteerRight ||
				VehicleInstance->LastInput.bHandbrake)
			{
				Actor->wakeUp();
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
	}

	if (EventCallback)
	{
		EventCallback->Dispatch();
	}

	TArray<FPendingBodySync> BodySyncs;
	BodySyncs.reserve(Bodies.size());
	{
		PHYSX_SCENE_READ_LOCK(Scene);

		for (FBodyInstance* Body : Bodies)
		{
			if (!Body)
			{
				continue;
			}

			const PxRigidActor* Actor = GetPxActor(Body);
			if (!Actor)
			{
				continue;
			}

			FPendingBodySync Sync;
			Sync.Body = Body;
			Sync.OwnerComponent = Body->OwnerComponent;
			Sync.Transform = ToFTransform(Actor->getGlobalPose());
			if (Body->OwnerComponent && Body->BoneIndex < 0 && Body->BodyType == EPhysicsBodyType::Dynamic)
			{
				const PxRigidDynamic* Dynamic = GetPxDynamic(Body);
				Sync.bApplyToComponent = Dynamic && !Dynamic->isSleeping();
			}
			BodySyncs.push_back(Sync);
		}

		for (FVehicle4WInstance* VehicleInstance : Vehicles)
		{
			UpdateCachedVehicleWheelTransforms(VehicleInstance);
		}
	}

	for (const FPendingBodySync& Sync : BodySyncs)
	{
		if (Sync.Body)
		{
			Sync.Body->CachedWorldTransform = Sync.Transform;
		}

		if (Sync.bApplyToComponent && Sync.OwnerComponent)
		{
			Sync.OwnerComponent->SetWorldLocation(Sync.Transform.Location);
			Sync.OwnerComponent->SetRelativeRotation(Sync.Transform.Rotation);
		}
	}
}

FBodyInstance* FPhysXRuntime::CreateRigidBody(const FPhysicsBodyDesc& Desc)
{
	if (!Initialize() || !Physics || !Scene)
	{
		return nullptr;
	}

	PHYSX_SCENE_WRITE_LOCK(Scene);

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
			// 깊이침투(depenetration) 분리 속도 상한. 미설정 시 PhysX 기본값은 사실상 무제한이라,
			// 래그돌 진입 순간 인접 바디/바닥과의 겹침이 한 스텝에 폭발적으로 풀리며 액터가 솟아오른다.
			// 월드 단위는 미터(TolerancesScale.length=1, gravity=-9.81)이므로 1 m/s 로 클램프해
			// 튐을 막는다(5cm 겹침이 ~3프레임에 해소). 값은 솟구침/끼임 사이에서 튜닝 가능.
			Dynamic->setMaxDepenetrationVelocity(1.0f);
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
		CreateShape_AssumesLocked(Body, ShapeDesc);
	}

	if (PxRigidDynamic* Dynamic = Actor->is<PxRigidDynamic>())
	{
		PxRigidBodyExt::updateMassAndInertia(*Dynamic, Desc.Mass);
		if (!Desc.bStartAwake)
		{
			Dynamic->putToSleep();
		}
	}

	if (PxAggregate* Aggregate = static_cast<PxAggregate*>(Desc.Aggregate.NativePtr))
	{
		// aggregate 가 씬에 속해 있으면 addActor 가 actor 를 씬에도 추가한다(PxAggregate 규약).
		Aggregate->addActor(*Actor);
	}
	else
	{
		Scene->addActor(*Actor);
	}
	Bodies.push_back(Body);
	return Body;
}

void FPhysXRuntime::DestroyRigidBody(FBodyInstance* Body)
{
	if (!Body)
	{
		return;
	}

	// freed-safe 멤버십 가드: 이미 파기되어 추적 목록에 없는 바디면(예: Shutdown 이 먼저 전부
	// 해제한 뒤 컴포넌트 소멸자가 dangling FBodyInstance* 로 재호출하는 경우) deref 전에 안전하게
	// 빠진다. std::find 는 포인터 "주소" 비교만 하므로 freed 메모리를 deref 하지 않는다.
	auto BodyIt = std::find(Bodies.begin(), Bodies.end(), Body);
	if (BodyIt == Bodies.end())
	{
		return;
	}
	Bodies.erase(BodyIt);

	if (PxRigidActor* Actor = PhysXHelpers::GetPxActor(Body))
	{
		if (Scene)
		{
			PHYSX_SCENE_WRITE_LOCK(Scene);
			Actor->release();
		}
		else
		{
			Actor->release();
		}
	}

	for (FPhysicsTriangleMeshHandle& MeshHandle : Body->TriangleMeshHandles)
	{
		if (PxTriangleMesh* TriangleMesh = static_cast<PxTriangleMesh*>(MeshHandle.NativePtr))
		{
			TriangleMesh->release();
		}
	}
	Body->TriangleMeshHandles.clear();

	delete Body;
}

FPhysicsAggregateHandle FPhysXRuntime::CreateAggregate(int32 MaxActors, bool bEnableSelfCollision)
{
	if (!Physics || !Scene || MaxActors <= 0)
	{
		return {};
	}

	PHYSX_SCENE_WRITE_LOCK(Scene);

	// PhysX 4.1 PxAggregate 의 maxSize 상한은 128. 초과하면 createAggregate 가 실패(null)하므로 호출측에서 폴백한다.
	PxAggregate* Aggregate = Physics->createAggregate(static_cast<PxU32>(MaxActors), bEnableSelfCollision);
	if (!Aggregate)
	{
		return {};
	}

	Scene->addAggregate(*Aggregate);
	Aggregates.push_back(Aggregate);
	return { Aggregate, AllocateSerial() };
}

void FPhysXRuntime::DestroyAggregate(const FPhysicsAggregateHandle& Handle)
{
	PxAggregate* Aggregate = static_cast<PxAggregate*>(Handle.NativePtr);
	if (!Aggregate)
	{
		return;
	}

	// freed-safe 멤버십 가드: 추적 목록에 없는 aggregate(이미 Shutdown 이 release)면 재release 하지
	// 않는다. 컴포넌트는 PhysicsAggregate 핸들의 stale NativePtr 복사본을 들고 있을 수 있다.
	auto AggIt = std::find(Aggregates.begin(), Aggregates.end(), Aggregate);
	if (AggIt == Aggregates.end())
	{
		return;
	}
	Aggregates.erase(AggIt);

	// 내부 actor 는 DestroyRigidBody 로 이미 release 되어 비어 있어야 한다.
	// (남은 actor 가 있으면 release 시 PhysX 가 씬으로 재삽입하므로 호출 순서를 지킨다.)
	if (Scene)
	{
		PHYSX_SCENE_WRITE_LOCK(Scene);
		Aggregate->release();
	}
	else
	{
		Aggregate->release();
	}
}

FPhysicsShapeHandle FPhysXRuntime::CreateShape(FBodyInstance* Body, const FPhysicsShapeDesc& Desc)
{
	if (!Scene)
	{
		return {};
	}

	PHYSX_SCENE_WRITE_LOCK(Scene);
	return CreateShape_AssumesLocked(Body, Desc);
}

FPhysicsShapeHandle FPhysXRuntime::CreateShape_AssumesLocked(FBodyInstance* Body, const FPhysicsShapeDesc& Desc)
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
	PxTriangleMesh* CookedTriangleMesh = nullptr;
	PxTriangleMeshGeometry TriangleMeshGeometry;
	const PxGeometry* GeometryPtr = nullptr;
	if (Desc.ShapeType == EPhysicsShapeType::TriangleMesh)
	{
		if (Body->BodyType != EPhysicsBodyType::Static && Desc.bSimulationShape)
		{
			UE_LOG("[PhysXRuntime] Triangle mesh simulation shape can be attached only to static actors");
			return {};
		}

		CookedTriangleMesh = CookTriangleMesh(Foundation, Physics, Desc);
		if (!CookedTriangleMesh)
		{
			return {};
		}

		TriangleMeshGeometry = PxTriangleMeshGeometry(CookedTriangleMesh);
		GeometryPtr = &TriangleMeshGeometry;
	}
	else
	{
		if (!PhysXHelpers::BuildGeometry(Desc, Geometry))
		{
			return {};
		}
		GeometryPtr = &Geometry.any();
	}

	PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Actor, *GeometryPtr, *DefaultMaterial);
	if (!Shape)
	{
		if (CookedTriangleMesh)
		{
			CookedTriangleMesh->release();
		}
		return {};
	}

	Shape->setLocalPose(PhysXHelpers::ToPxTransform(Desc.LocalTransform));
	Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, Desc.bSimulationShape && !Desc.bTriggerShape);
	Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, Desc.bTriggerShape);
	Shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, Desc.bSceneQueryShape);
	Shape->userData = Body->OwnerComponent;
	if (Desc.bOverrideFilterData)
	{
		PxFilterData Filter;
		Filter.word0 = static_cast<PxU32>(Desc.OverrideObjectType);
		Filter.word1 = 0;
		Filter.word2 = 0;
		Filter.word3 = (Body->OwnerComponent && Body->OwnerComponent->GetOwner()) ? Body->OwnerComponent->GetOwner()->GetUUID() : 0;  // 같은 액터 self-filter id: 형제 콜라이더 ↔ 래그돌 바디 충돌 제외용
		for (int32 Ch = 0; Ch < static_cast<int32>(ECollisionChannel::ActiveCount); ++Ch)
		{
			const ECollisionResponse R = Desc.OverrideResponses.GetResponse(static_cast<ECollisionChannel>(Ch));
			if (R == ECollisionResponse::Block)   Filter.word1 |= (1u << Ch);
			else if (R == ECollisionResponse::Overlap) Filter.word2 |= (1u << Ch);
		}
		Shape->setSimulationFilterData(Filter);
		Shape->setQueryFilterData(Filter);
	}
	else
	{
		ApplyComponentFilterData(Shape, Body->OwnerComponent);
	}

	FPhysicsShapeHandle Handle{ Shape, AllocateSerial() };
	Body->ShapeHandles.push_back(Handle);
	if (CookedTriangleMesh)
	{
		Body->TriangleMeshHandles.push_back({ CookedTriangleMesh, AllocateSerial() });
	}

	if (PxRigidDynamic* Dynamic = PhysXHelpers::GetPxDynamic(Body))
	{
		PxRigidBodyExt::updateMassAndInertia(*Dynamic, Desc.Material.Density);
	}

	return Handle;
}

FConstraintInstance* FPhysXRuntime::CreateD6Joint(const FPhysicsConstraintDesc& Desc)
{
	if (!Physics || !Scene || !Desc.ParentBody || !Desc.ChildBody)
	{
		return nullptr;
	}

	PxRigidActor* ParentActor = PhysXHelpers::GetPxActor(Desc.ParentBody);
	PxRigidActor* ChildActor = PhysXHelpers::GetPxActor(Desc.ChildBody);
	if (!ParentActor || !ChildActor)
	{
		return nullptr;
	}

	PHYSX_SCENE_WRITE_LOCK(Scene);

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

	// freed-safe 멤버십 가드: 추적 목록에 없는 조인트(이미 Shutdown 이 해제)면 dangling
	// FConstraintInstance* deref 전에 안전하게 빠진다. (주소 비교만 수행)
	auto JointIt = std::find(Joints.begin(), Joints.end(), Joint);
	if (JointIt == Joints.end())
	{
		return;
	}
	Joints.erase(JointIt);

	if (PxJoint* PxJointPtr = static_cast<PxJoint*>(Joint->JointHandle.NativePtr))
	{
		if (Scene)
		{
			PHYSX_SCENE_WRITE_LOCK(Scene);
			PxJointPtr->release();
		}
		else
		{
			PxJointPtr->release();
		}
	}

	delete Joint;
}

FVehicle4WInstance* FPhysXRuntime::CreateVehicle4W(const FVehicle4WDesc& Desc)
{
	if (!Initialize() || !Physics || !Scene || !DefaultMaterial || !bVehicleSdkAvailable)
	{
		return nullptr;
	}

	PHYSX_SCENE_WRITE_LOCK(Scene);

	FVehicle4WInstance* Instance = new FVehicle4WInstance();
	Instance->Name = Desc.Name;
	Instance->VehicleHandle.Serial = AllocateSerial();

	const PxU32 VehicleSelfId = MakeVehicleQuerySelfId(Instance->VehicleHandle.Serial);
	PxFilterData VehicleQueryFilter;
	VehicleQueryFilter.word3 = VehicleSelfId;
	const PxFilterData VehicleChassisFilter = BuildVehicleChassisFilterData(VehicleSelfId);

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
			ChassisShape->setSimulationFilterData(VehicleChassisFilter);
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
		WheelRaycastFilter.word1 = ObjectTypeBit(ECollisionChannel::WorldStatic);
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
	Drive4W->mDriveDynData.setUseAutoGears(true);
	Drive4W->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
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

	// freed-safe 멤버십 가드: 추적 목록에 없는 vehicle(예: Shutdown 이 먼저 전부 해제한 뒤
	// AVehicleActor::EndPlay 가 dangling VehicleInstance 로 재호출하는 경우)이면 deref/delete 전에
	// 안전하게 빠진다. std::find 는 포인터 "주소" 비교만 하므로 freed 메모리를 deref 하지 않는다.
	// (Vehicle->ChassisBody deref 와 delete Vehicle 의 double-free 를 차단)
	auto VehicleIt = std::find(Vehicles.begin(), Vehicles.end(), Vehicle);
	if (VehicleIt == Vehicles.end())
	{
		return;
	}
	Vehicles.erase(VehicleIt);

	FPhysXVehicle4WData* Data = GetVehicleData(Vehicle);
	if (Data)
	{
		auto ReleaseVehicleData = [Data]()
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
		};

		if (Scene)
		{
			PHYSX_SCENE_WRITE_LOCK(Scene);
			ReleaseVehicleData();
		}
		else
		{
			ReleaseVehicleData();
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

void FPhysXRuntime::AddForce(UPrimitiveComponent* Comp, const FVector& Force)
{
	PxRigidDynamic* Dynamic = GetPxDynamic(FindBodyByComponent(Comp));
	if (Scene && Dynamic)
	{
		PHYSX_SCENE_WRITE_LOCK(Scene);
		Dynamic->addForce(ToPxVec3(Force));
	}
}

void FPhysXRuntime::AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation)
{
	PxRigidDynamic* Dynamic = GetPxDynamic(FindBodyByComponent(Comp));
	if (Scene && Dynamic)
	{
		PHYSX_SCENE_WRITE_LOCK(Scene);
		PxRigidBodyExt::addForceAtPos(*Dynamic, ToPxVec3(Force), ToPxVec3(WorldLocation));
	}
}

void FPhysXRuntime::AddTorque(UPrimitiveComponent* Comp, const FVector& Torque)
{
	PxRigidDynamic* Dynamic = GetPxDynamic(FindBodyByComponent(Comp));
	if (Scene && Dynamic)
	{
		PHYSX_SCENE_WRITE_LOCK(Scene);
		Dynamic->addTorque(ToPxVec3(Torque));
	}
}

FVector FPhysXRuntime::GetLinearVelocity(UPrimitiveComponent* Comp) const
{
	const PxRigidDynamic* Dynamic = GetPxDynamic(FindBodyByComponent(Comp));
	if (Scene && Dynamic)
	{
		PHYSX_SCENE_READ_LOCK(Scene);
		return ToFVector(Dynamic->getLinearVelocity());
	}
	return FVector(0.0f, 0.0f, 0.0f);
}

void FPhysXRuntime::SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel)
{
	PxRigidDynamic* Dynamic = GetPxDynamic(FindBodyByComponent(Comp));
	if (Scene && Dynamic)
	{
		PHYSX_SCENE_WRITE_LOCK(Scene);
		Dynamic->setLinearVelocity(ToPxVec3(Vel));
	}
}

FVector FPhysXRuntime::GetAngularVelocity(UPrimitiveComponent* Comp) const
{
	const PxRigidDynamic* Dynamic = GetPxDynamic(FindBodyByComponent(Comp));
	if (Scene && Dynamic)
	{
		PHYSX_SCENE_READ_LOCK(Scene);
		return ToFVector(Dynamic->getAngularVelocity());
	}
	return FVector(0.0f, 0.0f, 0.0f);
}

void FPhysXRuntime::SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel)
{
	PxRigidDynamic* Dynamic = GetPxDynamic(FindBodyByComponent(Comp));
	if (Scene && Dynamic)
	{
		PHYSX_SCENE_WRITE_LOCK(Scene);
		Dynamic->setAngularVelocity(ToPxVec3(Vel));
	}
}

void FPhysXRuntime::SetMass(UPrimitiveComponent* Comp, float Mass)
{
	PxRigidDynamic* Dynamic = GetPxDynamic(FindBodyByComponent(Comp));
	if (Scene && Dynamic)
	{
		PHYSX_SCENE_WRITE_LOCK(Scene);
		PxRigidBodyExt::updateMassAndInertia(*Dynamic, (std::max)(0.001f, Mass));
	}
}

float FPhysXRuntime::GetMass(UPrimitiveComponent* Comp) const
{
	const PxRigidDynamic* Dynamic = GetPxDynamic(FindBodyByComponent(Comp));
	if (Scene && Dynamic)
	{
		PHYSX_SCENE_READ_LOCK(Scene);
		return Dynamic->getMass();
	}
	return 0.0f;
}

void FPhysXRuntime::SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset)
{
	PxRigidDynamic* Dynamic = GetPxDynamic(FindBodyByComponent(Comp));
	if (Scene && Dynamic)
	{
		PHYSX_SCENE_WRITE_LOCK(Scene);
		Dynamic->setCMassLocalPose(PxTransform(ToPxVec3(LocalOffset)));
	}
}

FVector FPhysXRuntime::GetCenterOfMass(UPrimitiveComponent* Comp) const
{
	const PxRigidDynamic* Dynamic = GetPxDynamic(FindBodyByComponent(Comp));
	if (Scene && Dynamic)
	{
		PHYSX_SCENE_READ_LOCK(Scene);
		return ToFVector(Dynamic->getCMassLocalPose().p);
	}
	return FVector(0.0f, 0.0f, 0.0f);
}

bool FPhysXRuntime::Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
	ECollisionChannel TraceChannel, const AActor* IgnoreActor) const
{
	OutHit = FHitResult();
	if (!Scene || MaxDist <= 0.0f)
	{
		return false;
	}

	const PxVec3 PxDir = ToPxVec3(Dir);
	if (!IsUsableDirection(PxDir))
	{
		return false;
	}

	struct FChannelRaycastFilter : PxQueryFilterCallback
	{
		const AActor* IgnoreActor = nullptr;
		PxU32 TraceBit = 0;

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (IsTriggerShape(Shape))
			{
				return PxQueryHitType::eNONE;
			}

			UPrimitiveComponent* Comp = GetComponentFromActor(Actor);
			if (!Comp || (IgnoreActor && Comp->GetOwner() == IgnoreActor))
			{
				return PxQueryHitType::eNONE;
			}

			const PxFilterData Filter = Shape ? Shape->getQueryFilterData() : PxFilterData();
			return (Filter.word1 & TraceBit) ? PxQueryHitType::eBLOCK : PxQueryHitType::eNONE;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	} FilterCallback;

	FilterCallback.IgnoreActor = IgnoreActor;
	FilterCallback.TraceBit = 1u << static_cast<PxU32>(TraceChannel);

	PxRaycastBuffer Hit;
	{
		PHYSX_SCENE_READ_LOCK(Scene);
		const bool bStatus = Scene->raycast(ToPxVec3(Start), PxDir.getNormalized(), MaxDist, Hit,
			PxHitFlag::eDEFAULT, PxQueryFilterData(), &FilterCallback);
		if (!bStatus || !Hit.hasBlock)
		{
			return false;
		}

		FillHitResult(Hit.block, OutHit);
	}
	return OutHit.bHit;
}

bool FPhysXRuntime::RaycastByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
	uint32 ObjectTypeMask, const AActor* IgnoreActor) const
{
	OutHit = FHitResult();
	if (!Scene || MaxDist <= 0.0f || ObjectTypeMask == 0)
	{
		return false;
	}

	const PxVec3 PxDir = ToPxVec3(Dir);
	if (!IsUsableDirection(PxDir))
	{
		return false;
	}

	struct FObjectTypeRaycastFilter : PxQueryFilterCallback
	{
		const AActor* IgnoreActor = nullptr;
		PxU32 ObjectTypeMask = 0;

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (IsTriggerShape(Shape))
			{
				return PxQueryHitType::eNONE;
			}

			UPrimitiveComponent* Comp = GetComponentFromActor(Actor);
			if (!Comp || (IgnoreActor && Comp->GetOwner() == IgnoreActor))
			{
				return PxQueryHitType::eNONE;
			}

			const PxFilterData Filter = Shape ? Shape->getQueryFilterData() : PxFilterData();
			const PxU32 ObjectBit = 1u << Filter.word0;
			return (ObjectTypeMask & ObjectBit) ? PxQueryHitType::eBLOCK : PxQueryHitType::eNONE;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	} FilterCallback;

	FilterCallback.IgnoreActor = IgnoreActor;
	FilterCallback.ObjectTypeMask = ObjectTypeMask;

	PxRaycastBuffer Hit;
	{
		PHYSX_SCENE_READ_LOCK(Scene);
		const bool bStatus = Scene->raycast(ToPxVec3(Start), PxDir.getNormalized(), MaxDist, Hit,
			PxHitFlag::eDEFAULT, PxQueryFilterData(), &FilterCallback);
		if (!bStatus || !Hit.hasBlock)
		{
			return false;
		}

		FillHitResult(Hit.block, OutHit);
	}
	return OutHit.bHit;
}

bool FPhysXRuntime::SphereSweepShapeComponents(const FVector& Start, const FVector& Dir, float MaxDist, float Radius,
	FHitResult& OutHit, ECollisionChannel TraceChannel, const AActor* IgnoreActor) const
{
	OutHit = FHitResult();
	if (!Scene || MaxDist <= 0.0f || Radius <= 0.0f)
	{
		return false;
	}

	const PxVec3 PxDir = ToPxVec3(Dir);
	if (!IsUsableDirection(PxDir))
	{
		return false;
	}

	struct FSweepFilter : PxQueryFilterCallback
	{
		const AActor* IgnoreActor = nullptr;
		PxU32 TraceBit = 0;

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (IsTriggerShape(Shape))
			{
				return PxQueryHitType::eNONE;
			}

			UPrimitiveComponent* Comp = GetComponentFromActor(Actor);
			if (!Comp || (IgnoreActor && Comp->GetOwner() == IgnoreActor))
			{
				return PxQueryHitType::eNONE;
			}

			const PxFilterData Filter = Shape ? Shape->getQueryFilterData() : PxFilterData();
			return (Filter.word1 & TraceBit) ? PxQueryHitType::eBLOCK : PxQueryHitType::eNONE;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	} FilterCallback;

	FilterCallback.IgnoreActor = IgnoreActor;
	FilterCallback.TraceBit = 1u << static_cast<PxU32>(TraceChannel);

	PxSweepBuffer Hit;
	const PxSphereGeometry SweepGeometry(Radius);
	{
		PHYSX_SCENE_READ_LOCK(Scene);
		const bool bStatus = Scene->sweep(SweepGeometry, PxTransform(ToPxVec3(Start)), PxDir.getNormalized(), MaxDist, Hit,
			PxHitFlag::eDEFAULT, PxQueryFilterData(), &FilterCallback);
		if (!bStatus || !Hit.hasBlock)
		{
			return false;
		}

		FillHitResult(Hit.block, OutHit);
	}
	return OutHit.bHit;
}

int32 FPhysXRuntime::OverlapSphere(const FVector& Center, float Radius, uint32 ObjectTypeMask,
	TArray<FOverlapResult>& OutOverlaps, const AActor* IgnoreActor) const
{
	OutOverlaps.clear();
	if (!Scene || Radius <= 0.0f || ObjectTypeMask == 0)
	{
		return 0;
	}

	struct FObjectTypeOverlapFilter : PxQueryFilterCallback
	{
		const AActor* IgnoreActor = nullptr;
		PxU32 ObjectTypeMask = 0;

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (IsTriggerShape(Shape))
			{
				return PxQueryHitType::eNONE;
			}

			UPrimitiveComponent* Comp = GetComponentFromActor(Actor);
			if (!Comp || (IgnoreActor && Comp->GetOwner() == IgnoreActor))
			{
				return PxQueryHitType::eNONE;
			}

			const PxFilterData Filter = Shape ? Shape->getQueryFilterData() : PxFilterData();
			const PxU32 ObjectBit = 1u << Filter.word0;
			return (ObjectTypeMask & ObjectBit) ? PxQueryHitType::eTOUCH : PxQueryHitType::eNONE;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eTOUCH;
		}
	} FilterCallback;

	FilterCallback.IgnoreActor = IgnoreActor;
	FilterCallback.ObjectTypeMask = ObjectTypeMask;

	// 다중 touch 수집 — eTOUCH 필터 + 고정 버퍼. (블로킹 쿼리 아님)
	constexpr PxU32 kMaxTouch = 64;
	PxOverlapHit TouchBuffer[kMaxTouch];
	PxOverlapBuffer Hit(TouchBuffer, kMaxTouch);
	{
		PHYSX_SCENE_READ_LOCK(Scene);
		Scene->overlap(PxSphereGeometry(Radius), PxTransform(ToPxVec3(Center)), Hit,
			PxQueryFilterData(PxQueryFlag::eDYNAMIC | PxQueryFlag::eSTATIC | PxQueryFlag::ePREFILTER),
			&FilterCallback);

		const PxU32 NumTouches = Hit.getNbTouches();
		for (PxU32 i = 0; i < NumTouches; ++i)
		{
			if (UPrimitiveComponent* Comp = GetComponentFromActor(Hit.getTouch(i).actor))
			{
				FOverlapResult Result;
				Result.OverlapComponent = Comp;
				Result.OverlapActor = Comp->GetOwner();
				OutOverlaps.push_back(Result);
			}
		}

		// silent cap 금지 — 버퍼 포화 시 잘림 가능성을 노출.
		if (NumTouches >= kMaxTouch)
		{
			UE_LOG("[OverlapSphere] touch buffer saturated (%u) — results may be truncated", kMaxTouch);
		}
	}
	return static_cast<int32>(OutOverlaps.size());
}

bool FPhysXRuntime::GetBodyTransform(const FBodyInstance* Body, FTransform& OutTransform) const
{
	const PxRigidActor* Actor = PhysXHelpers::GetPxActor(Body);
	if (!Scene || !Actor)
	{
		return false;
	}

	PHYSX_SCENE_READ_LOCK(Scene);
	OutTransform = PhysXHelpers::ToFTransform(Actor->getGlobalPose());
	return true;
}

void FPhysXRuntime::SetBodyTransform(FBodyInstance* Body, const FTransform& Transform, bool bTeleport)
{
	PxRigidActor* Actor = PhysXHelpers::GetPxActor(Body);
	if (!Scene || !Actor)
	{
		return;
	}

	if (!bTeleport)
	{
		SetKinematicTarget(Body, Transform);
		return;
	}

	PHYSX_SCENE_WRITE_LOCK(Scene);
	Actor->setGlobalPose(PhysXHelpers::ToPxTransform(Transform));
	Body->CachedWorldTransform = Transform;
}

void FPhysXRuntime::SetKinematicTarget(FBodyInstance* Body, const FTransform& Transform)
{
	PxRigidDynamic* Dynamic = PhysXHelpers::GetPxDynamic(Body);
	if (!Scene || !Dynamic)
	{
		return;
	}

	PHYSX_SCENE_WRITE_LOCK(Scene);
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
	if (!Scene || !Dynamic)
	{
		return;
	}

	PHYSX_SCENE_WRITE_LOCK(Scene);
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

void FPhysXRuntime::SetRagdollBodyFilter(FBodyInstance* Body, uint32 GroupId, uint32 BodyIndex, uint32 IgnoreMask)
{
	if (!Scene || !Body || GroupId == 0 || BodyIndex > 31)
	{
		return;
	}

	PxFilterData Sim;
	Sim.word0 = RAGDOLL_FILTER_TAG | (static_cast<PxU32>(GroupId) & ~RAGDOLL_FILTER_TAG);
	Sim.word1 = (1u << BodyIndex);
	Sim.word2 = static_cast<PxU32>(IgnoreMask);
	Sim.word3 = 0;

	PHYSX_SCENE_WRITE_LOCK(Scene);
	for (const FPhysicsShapeHandle& Handle : Body->ShapeHandles)
	{
		// 시뮬 필터 데이터만 갱신 — query 필터(채널/레이캐스트)는 그대로 둔다.
		if (PxShape* Shape = static_cast<PxShape*>(Handle.NativePtr))
		{
			Shape->setSimulationFilterData(Sim);
		}
	}
}

void FPhysXRuntime::GetPhysicsStats(FPhysicsStats& OutStats) const
{
	OutStats = FPhysicsStats();
	if (!Scene)
	{
		return;
	}

	PHYSX_SCENE_READ_LOCK(Scene);
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

	PHYSX_SCENE_READ_LOCK(Scene);
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
	if (!Scene)
	{
		return;
	}

	PHYSX_SCENE_READ_LOCK(Scene);
	for (const FVehicle4WInstance* Vehicle : Vehicles)
	{
		AppendVehicleDebugLines(Vehicle, OutLines);
	}
}
