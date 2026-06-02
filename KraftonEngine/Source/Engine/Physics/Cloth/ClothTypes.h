#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Object/Reflection/ObjectMacros.h"

enum class EClothBackend : uint8
{
	CPU,
	CUDA,
	DX11,
};

UENUM()
enum class EClothPinMode : uint8
{
	None,
	TopRow,
	BottomRow,
	LeftRow,
	RightRow,
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight,
	TopCorners,
	BottomCorners,
	LeftCorners,
	RightCorners,
	AllCorners,
};

struct FClothFabricHandle
{
	void* NativePtr = nullptr;
	uint64 Serial = 0;

	bool IsValid() const { return NativePtr != nullptr; }
};

struct FClothInstanceHandle
{
	void* NativePtr = nullptr;
	uint64 Serial = 0;

	bool IsValid() const { return NativePtr != nullptr; }
};

struct FClothParticle
{
	FVector Position;
	float InvMass = 1.0f;
};

struct FClothRenderVertex
{
	FVector Position;
	FVector Normal = FVector::UpVector;
	FVector2 UV;
};

struct FClothRenderData
{
	TArray<FClothRenderVertex> Vertices;
	TArray<uint32> Indices;

	void Reset()
	{
		Vertices.clear();
		Indices.clear();
	}
};

struct FClothDebugDrawOptions
{
	bool bParticles = true;
	bool bConstraints = true;
	bool bCollision = true;

	float ParticlePointSize = 2.0f;
	uint32 MaxDebugParticles = 4096;
	uint32 MaxDebugConstraintLines = 16384;
};

struct FClothFabricDesc
{
	FString Name;
	TArray<FClothParticle> Particles;
	TArray<uint32> Indices;
	TArray<FVector2> UVs;
	FVector GravityDirection = FVector(0.0f, 0.0f, -1.0f);
	bool bUseGeodesicTether = true;
};

struct FClothMotionConstraint
{
	FVector Center;
	float Radius = 0.0f;
};

struct FClothSeparationConstraint
{
	FVector Center;
	float Radius = 0.0f;
};

struct FClothConstraintDesc
{
	TArray<FClothMotionConstraint> MotionConstraints;
	TArray<FClothSeparationConstraint> SeparationConstraints;

	float MotionConstraintScale = 1.0f;
	float MotionConstraintBias = 0.0f;
	float MotionConstraintStiffness = 1.0f;
};

struct FClothCollisionSphere
{
	FVector Center;
	float Radius = 1.0f;
};

struct FClothCollisionCapsule
{
	uint32 SphereA = 0;
	uint32 SphereB = 0;
};

struct FClothCollisionPlane
{
	FVector Normal = FVector::UpVector;
	float Distance = 0.0f;
};

struct FClothCollisionConvex
{
	uint32 PlaneMask = 0;
};

struct FClothCollisionDesc
{
	TArray<FClothCollisionSphere> Spheres;
	TArray<FClothCollisionCapsule> Capsules;
	TArray<FClothCollisionPlane> Planes;
	TArray<FClothCollisionConvex> Convexes;
};

struct FClothPinnedParticle
{
	uint32 ParticleIndex = 0;
	FVector Position;
};

struct FClothSettings
{
	FVector Gravity = FVector(0.0f, 0.0f, -980.0f);
	FVector WindVelocity = FVector::ZeroVector;

	float SolverFrequency = 120.0f;
	float StiffnessFrequency = 60.0f;
	float Damping = 0.1f;
	float DragCoefficient = 0.0f;
	float LiftCoefficient = 0.0f;
	float LinearInertia = 1.0f;
	float AngularInertia = 1.0f;
	float CentrifugalInertia = 1.0f;

	float PhaseStiffness = 1.0f;
	float PhaseStiffnessMultiplier = 1.0f;
	float CompressionLimit = 1.0f;
	float StretchLimit = 1.0f;

	float TetherScale = 1.0f;
	float TetherStiffness = 1.0f;
	float Friction = 0.0f;
	bool bContinuousCollision = false;
};

struct FClothInstanceDesc
{
	FString Name;
	FClothFabricHandle Fabric;
	TArray<FClothParticle> InitialParticles;
	FClothSettings Settings;
	FClothConstraintDesc Constraints;
	FClothCollisionDesc Collision;
};

struct FClothGridDesc
{
	FString Name = "GridCloth";

	uint32 NumColumns = 20;
	uint32 NumRows = 20;
	float Spacing = 10.0f;

	FVector Origin = FVector(0.0f, 0.0f, 200.0f);
	FVector AxisX = FVector::RightVector;
	FVector AxisY = FVector::DownVector;

	EClothPinMode PinMode = EClothPinMode::TopRow;
	FClothSettings Settings;
	FClothCollisionDesc Collision;
};

struct FClothInstance
{
	FString Name;
	FClothFabricHandle FabricHandle;
	FClothInstanceHandle InstanceHandle;
	FClothSettings Settings;
	FClothConstraintDesc Constraints;
	FClothCollisionDesc Collision;
	uint32 NumParticles = 0;
	bool bValid = false;

	void Reset()
	{
		Name.clear();
		FabricHandle = {};
		InstanceHandle = {};
		Settings = FClothSettings();
		Constraints = FClothConstraintDesc();
		Collision = FClothCollisionDesc();
		NumParticles = 0;
		bValid = false;
	}
};

struct FClothStats
{
	uint32 NumFabrics = 0;
	uint32 NumCloths = 0;
	uint32 NumParticles = 0;
	uint32 NumPinnedParticles = 0;
	uint32 NumConstraints = 0;
	uint32 NumMotionConstraints = 0;
	uint32 NumSeparationConstraints = 0;
	uint32 NumCollisionSpheres = 0;
	uint32 NumCollisionCapsules = 0;
	uint32 NumCollisionPlanes = 0;
	uint32 NumCollisionConvexes = 0;
	uint32 NumSolverChunks = 0;
	float LastSimulationMs = 0.0f;
	bool bSolverError = false;

	void Accumulate(const FClothStats& Other)
	{
		NumFabrics += Other.NumFabrics;
		NumCloths += Other.NumCloths;
		NumParticles += Other.NumParticles;
		NumPinnedParticles += Other.NumPinnedParticles;
		NumConstraints += Other.NumConstraints;
		NumMotionConstraints += Other.NumMotionConstraints;
		NumSeparationConstraints += Other.NumSeparationConstraints;
		NumCollisionSpheres += Other.NumCollisionSpheres;
		NumCollisionCapsules += Other.NumCollisionCapsules;
		NumCollisionPlanes += Other.NumCollisionPlanes;
		NumCollisionConvexes += Other.NumCollisionConvexes;
		NumSolverChunks = (NumSolverChunks > Other.NumSolverChunks) ? NumSolverChunks : Other.NumSolverChunks;
		LastSimulationMs = (LastSimulationMs > Other.LastSimulationMs) ? LastSimulationMs : Other.LastSimulationMs;
		bSolverError = bSolverError || Other.bSolverError;
	}
};
