#pragma once

#include "Core/Types/CoreTypes.h"
#include "Core/Types/CollisionTypes.h"
#include "Math/Transform.h"
#include "Math/Vector.h"

class UPrimitiveComponent;
struct FBodyInstance;
struct FConstraintInstance;

static constexpr float PhysicsPi = 3.14159265358979323846f;

enum class EPhysicsBodyType : uint8
{
	Static,
	Dynamic,
	Kinematic,
};

enum class EPhysicsShapeType : uint8
{
	Box,
	Sphere,
	Capsule,
	Convex,
	TriangleMesh,
};

enum class EPhysicsMotionType : uint8
{
	Locked,
	Limited,
	Free,
};

struct FPhysicsActorHandle
{
	void* NativePtr = nullptr;
	uint64 Serial = 0;

	bool IsValid() const { return NativePtr != nullptr; }
};

struct FPhysicsShapeHandle
{
	void* NativePtr = nullptr;
	uint64 Serial = 0;

	bool IsValid() const { return NativePtr != nullptr; }
};

struct FPhysicsTriangleMeshHandle
{
	void* NativePtr = nullptr;
	uint64 Serial = 0;

	bool IsValid() const { return NativePtr != nullptr; }
};

struct FPhysicsJointHandle
{
	void* NativePtr = nullptr;
	uint64 Serial = 0;

	bool IsValid() const { return NativePtr != nullptr; }
};

struct FPhysicsVehicleHandle
{
	void* NativePtr = nullptr;
	uint64 Serial = 0;

	bool IsValid() const { return NativePtr != nullptr; }
};

struct FPhysicsAggregateHandle
{
	void* NativePtr = nullptr;
	uint64 Serial = 0;

	bool IsValid() const { return NativePtr != nullptr; }
};

enum class EVehicle4WWheelIndex : uint8
{
	FrontLeft = 0,
	FrontRight,
	RearLeft,
	RearRight,
	Count,
};

struct FPhysicalMaterialDesc
{
	float StaticFriction = 0.5f;
	float DynamicFriction = 0.5f;
	float Restitution = 0.3f;
	float Density = 1.0f;
};

struct FPhysicsShapeDesc
{
	FString Name;
	EPhysicsShapeType ShapeType = EPhysicsShapeType::Box;
	FTransform LocalTransform;

	FVector HalfExtent = FVector(50.0f, 50.0f, 50.0f);
	float Radius = 50.0f;
	float HalfHeight = 100.0f;
	TArray<FVector> ConvexVertices;
	TArray<FVector> TriangleMeshVertices;
	TArray<uint32> TriangleMeshIndices;
	TArray<uint8> CookedTriangleMeshData;
	FVector TriangleMeshScale = FVector(1.0f, 1.0f, 1.0f);

	bool bSimulationShape = true;
	bool bTriggerShape = false;
	bool bSceneQueryShape = true;

	// 세팅하면 OwnerComponent 대신 아래 값으로 FilterData를 직접 지정한다.
	// 컴포넌트 없이 생성되는 바디(에디터 바닥 등)에서 명시적으로 사용.
	bool bOverrideFilterData = false;
	ECollisionChannel OverrideObjectType = ECollisionChannel::WorldStatic;
	FCollisionResponseContainer OverrideResponses; // 기본값: 전 채널 Block

	FPhysicalMaterialDesc Material;
};

struct FPhysicsBodyDesc
{
	UPrimitiveComponent* OwnerComponent = nullptr;

	FString BodyName;
	FString BoneName;
	int32 BoneIndex = -1;

	EPhysicsBodyType BodyType = EPhysicsBodyType::Static;
	FTransform WorldTransform;

	float Mass = 1.0f;
	float LinearDamping = 0.01f;
	float AngularDamping = 0.05f;

	bool bUseGravity = true;
	bool bEnableCCD = false;
	bool bStartAwake = true;

	// 유효하면 이 바디를 해당 PxAggregate 에 추가(씬 직접 추가 대신). 래그돌 자기충돌 일괄 제어용.
	FPhysicsAggregateHandle Aggregate;

	TArray<FPhysicsShapeDesc> Shapes;
};

struct FPhysicsConstraintDesc
{
	FString ConstraintName;

	FBodyInstance* ParentBody = nullptr;
	FBodyInstance* ChildBody = nullptr;

	FTransform ParentLocalFrame;
	FTransform ChildLocalFrame;

	EPhysicsMotionType LinearX = EPhysicsMotionType::Locked;
	EPhysicsMotionType LinearY = EPhysicsMotionType::Locked;
	EPhysicsMotionType LinearZ = EPhysicsMotionType::Locked;

	EPhysicsMotionType Twist = EPhysicsMotionType::Limited;
	EPhysicsMotionType Swing1 = EPhysicsMotionType::Limited;
	EPhysicsMotionType Swing2 = EPhysicsMotionType::Limited;

	float TwistLimitRadiansMin = -PhysicsPi * 0.25f;
	float TwistLimitRadiansMax = PhysicsPi * 0.25f;
	float Swing1LimitRadians = PhysicsPi / 6.0f;
	float Swing2LimitRadians = PhysicsPi / 6.0f;

	bool bBreakable = false;
	float BreakForce = 0.0f;
	float BreakTorque = 0.0f;
};

struct FPhysicsStats
{
	uint32 NumActors = 0;
	uint32 NumRigidStatics = 0;
	uint32 NumRigidDynamics = 0;
	uint32 NumActiveBodies = 0;
	uint32 NumShapes = 0;
	uint32 NumJoints = 0;
	uint32 NumVehicles = 0;
	uint32 NumContactPairs = 0;
	float LastSimulationMs = 0.0f;
};

struct FPhysicsDebugLine
{
	FVector Start;
	FVector End;
	FVector Color = FVector(0.0f, 1.0f, 0.0f);
};

struct FVehicle4WInput
{
	bool bMoveForward = false;
	bool bMoveBackward = false;
	bool bSteerLeft = false;
	bool bSteerRight = false;
	bool bHandbrake = false;
};

struct FVehicle4WDesc
{
	FString Name;
	FTransform WorldTransform;

	FVector ChassisHalfExtents = FVector(1.6f, 0.8f, 0.45f);
	FVector ChassisCenterOfMassOffset = FVector(0.0f, 0.0f, -0.35f);
	float ChassisMass = 1200.0f;

	float WheelMass = 20.0f;
	float WheelRadius = 0.35f;
	float WheelWidth = 0.25f;
	float WheelDampingRate = 0.25f;
	float MaxSteerRadians = PhysicsPi * 0.333f;
	float MaxBrakeTorque = 1500.0f;
	float MaxHandbrakeTorque = 4000.0f;

	float SuspensionMaxCompression = 0.30f;
	float SuspensionMaxDroop = 0.10f;
	float SuspensionSpringStrength = 35000.0f;
	float SuspensionSpringDamperRate = 4500.0f;

	float EnginePeakTorque = 500.0f;
	float EngineMaxOmega = 600.0f;
	float ClutchStrength = 10.0f;
	float TireFriction = 1.0f;

	// Actor-local offsets. This engine uses +X forward, +Y right, +Z up.
	TStaticArray<FVector, 4> WheelCenterOffsets =
	{
		FVector( 1.30f, -0.80f, -0.35f),
		FVector( 1.30f,  0.80f, -0.35f),
		FVector(-1.30f, -0.80f, -0.35f),
		FVector(-1.30f,  0.80f, -0.35f),
	};
};

struct FVehicle4WInstance
{
	FString Name;
	FPhysicsVehicleHandle VehicleHandle;
	FBodyInstance* ChassisBody = nullptr;
	TStaticArray<FTransform, 4> WheelWorldTransforms;
	FVehicle4WInput LastInput;
	bool bValid = false;

	void Reset()
	{
		Name.clear();
		VehicleHandle = {};
		ChassisBody = nullptr;
		WheelWorldTransforms = {};
		LastInput = FVehicle4WInput();
		bValid = false;
	}
};


struct RagdoleBone
{
	FString name;
	FVector offset;
	FVector halfSize;
	int parentIndex;
	FBodyInstance* body = nullptr;
	FConstraintInstance* ConstraintInstance = nullptr;

};
