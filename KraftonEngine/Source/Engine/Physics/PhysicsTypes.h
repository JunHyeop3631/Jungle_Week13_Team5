#pragma once

#include "Core/Types/CoreTypes.h"
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

struct FPhysicsJointHandle
{
	void* NativePtr = nullptr;
	uint64 Serial = 0;

	bool IsValid() const { return NativePtr != nullptr; }
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

	bool bSimulationShape = true;
	bool bTriggerShape = false;
	bool bSceneQueryShape = true;

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
	uint32 NumContactPairs = 0;
	float LastSimulationMs = 0.0f;
};

struct FPhysicsDebugLine
{
	FVector Start;
	FVector End;
	FVector Color = FVector(0.0f, 1.0f, 0.0f);
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