#include "Physics/Cloth/NvClothScene.h"

#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "Component/ShapeComponent.h"
#include "NvCloth/Callbacks.h"
#include "NvCloth/Cloth.h"
#include "NvCloth/Fabric.h"
#include "NvCloth/Factory.h"
#include "NvCloth/PhaseConfig.h"
#include "NvCloth/Solver.h"
#include "NvClothExt/ClothFabricCooker.h"
#include "NvClothExt/ClothMeshDesc.h"
#include "Core/Logging/Log.h"
#include "foundation/PxAllocatorCallback.h"
#include "foundation/PxErrorCallback.h"
#include "foundation/PxQuat.h"
#include "foundation/PxVec3.h"
#include "foundation/PxVec4.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <malloc.h>

namespace
{
class FNvClothAllocator final : public physx::PxAllocatorCallback
{
public:
	void* allocate(size_t Size, const char*, const char*, int) override
	{
		return _aligned_malloc(Size, 16);
	}

	void deallocate(void* Ptr) override
	{
		_aligned_free(Ptr);
	}
};

class FNvClothErrorCallback final : public physx::PxErrorCallback
{
public:
	void reportError(physx::PxErrorCode::Enum, const char*, const char*, int) override
	{
	}
};

class FNvClothAssertHandler final : public nv::cloth::PxAssertHandler
{
public:
	void operator()(const char*, const char*, int, bool& Ignore) override
	{
		Ignore = true;
	}
};

FNvClothAllocator GClothAllocator;
FNvClothErrorCallback GClothErrorCallback;
FNvClothAssertHandler GClothAssertHandler;
bool GClothCallbacksInitialized = false;
constexpr int CudaSuccess = 0;
constexpr float ClothSphereCollisionMargin = 1.0f;
constexpr float ClothCapsuleCollisionMargin = 1.0f;
constexpr float ClothBoxCollisionMargin = 1.0f;
constexpr float ClothTeleportDistanceThreshold = 5.0f;
constexpr float ClothTeleportReferenceDeltaTime = 1.0f / 60.0f;
constexpr float ClothTeleportSpeedThreshold = ClothTeleportDistanceThreshold / ClothTeleportReferenceDeltaTime;
constexpr float ClothTeleportRotationDotThreshold = 0.5f;

struct FNvClothCudaDriver
{
	using CUdevice = int;
	using CUresult = int;
	using CuInitFn = CUresult(WINAPI*)(unsigned int);
	using CuDeviceGetCountFn = CUresult(WINAPI*)(int*);
	using CuDeviceGetFn = CUresult(WINAPI*)(CUdevice*, int);
	using CuCtxCreateFn = CUresult(WINAPI*)(CUcontext*, unsigned int, CUdevice);
	using CuCtxDestroyFn = CUresult(WINAPI*)(CUcontext);
	using CuCtxSetCurrentFn = CUresult(WINAPI*)(CUcontext);

	HMODULE Module = nullptr;
	CuInitFn CuInit = nullptr;
	CuDeviceGetCountFn CuDeviceGetCount = nullptr;
	CuDeviceGetFn CuDeviceGet = nullptr;
	CuCtxCreateFn CuCtxCreate = nullptr;
	CuCtxDestroyFn CuCtxDestroy = nullptr;
	CuCtxSetCurrentFn CuCtxSetCurrent = nullptr;

	bool Load()
	{
		Module = ::LoadLibraryA("nvcuda.dll");
		if (!Module)
		{
			return false;
		}

		CuInit = reinterpret_cast<CuInitFn>(::GetProcAddress(Module, "cuInit"));
		CuDeviceGetCount = reinterpret_cast<CuDeviceGetCountFn>(::GetProcAddress(Module, "cuDeviceGetCount"));
		CuDeviceGet = reinterpret_cast<CuDeviceGetFn>(::GetProcAddress(Module, "cuDeviceGet"));
		CuCtxCreate = reinterpret_cast<CuCtxCreateFn>(::GetProcAddress(Module, "cuCtxCreate_v2"));
		if (!CuCtxCreate)
		{
			CuCtxCreate = reinterpret_cast<CuCtxCreateFn>(::GetProcAddress(Module, "cuCtxCreate"));
		}
		CuCtxDestroy = reinterpret_cast<CuCtxDestroyFn>(::GetProcAddress(Module, "cuCtxDestroy_v2"));
		if (!CuCtxDestroy)
		{
			CuCtxDestroy = reinterpret_cast<CuCtxDestroyFn>(::GetProcAddress(Module, "cuCtxDestroy"));
		}
		CuCtxSetCurrent = reinterpret_cast<CuCtxSetCurrentFn>(::GetProcAddress(Module, "cuCtxSetCurrent"));

		if (!CuInit || !CuDeviceGetCount || !CuDeviceGet || !CuCtxCreate || !CuCtxDestroy || !CuCtxSetCurrent)
		{
			Unload();
			return false;
		}

		return true;
	}

	void Unload()
	{
		CuInit = nullptr;
		CuDeviceGetCount = nullptr;
		CuDeviceGet = nullptr;
		CuCtxCreate = nullptr;
		CuCtxDestroy = nullptr;
		CuCtxSetCurrent = nullptr;
		if (Module)
		{
			::FreeLibrary(Module);
			Module = nullptr;
		}
	}
};

nv::cloth::Factory* TryCreateCudaFactory(CUcontext Context)
{
#if defined(_MSC_VER)
	__try
	{
		return NvClothCreateFactoryCUDA(Context);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}
#else
	return NvClothCreateFactoryCUDA(Context);
#endif
}

nv::cloth::Solver* TryCreateSolver(nv::cloth::Factory* InFactory)
{
#if defined(_MSC_VER)
	__try
	{
		return InFactory ? InFactory->createSolver() : nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}
#else
	return InFactory ? InFactory->createSolver() : nullptr;
#endif
}

void TryDestroyFactory(nv::cloth::Factory* InFactory)
{
	if (!InFactory)
	{
		return;
	}

#if defined(_MSC_VER)
	__try
	{
		NvClothDestroyFactory(InFactory);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
#else
	NvClothDestroyFactory(InFactory);
#endif
}

physx::PxVec3 ToPxVec3(const FVector& Value)
{
	return physx::PxVec3(Value.X, Value.Y, Value.Z);
}

physx::PxQuat ToPxQuat(const FQuat& Value)
{
	return physx::PxQuat(Value.X, Value.Y, Value.Z, Value.W);
}

physx::PxVec4 ToPxParticle(const FClothParticle& Particle)
{
	return physx::PxVec4(Particle.Position.X, Particle.Position.Y, Particle.Position.Z, Particle.InvMass);
}

float GetClothMaxTranslationStep(float DeltaTime)
{
	if (std::isfinite(DeltaTime) && DeltaTime > 1.0e-6f)
	{
		return ClothTeleportSpeedThreshold * DeltaTime;
	}

	return ClothTeleportDistanceThreshold;
}

FVector ClampClothTranslationStep(const FVector& CurrentLocation, const FVector& TargetLocation, float DeltaTime)
{
	const FVector DeltaLocation = TargetLocation - CurrentLocation;
	const float MaxStep = GetClothMaxTranslationStep(DeltaTime);
	const float MaxStepSquared = MaxStep * MaxStep;
	const float DeltaLengthSquared = DeltaLocation.LengthSquared();
	if (DeltaLengthSquared <= MaxStepSquared)
	{
		return TargetLocation;
	}

	const float DeltaLength = std::sqrt(DeltaLengthSquared);
	return DeltaLength > 1.0e-6f ? CurrentLocation + DeltaLocation * (MaxStep / DeltaLength) : CurrentLocation;
}

void TeleportClothWithoutInertia(nv::cloth::Cloth& Cloth, const FVector& Location, const FQuat& Rotation)
{
	Cloth.teleportToLocation(ToPxVec3(Location), ToPxQuat(Rotation));
	Cloth.clearInertia();
	Cloth.ignoreVelocityDiscontinuity();
}

bool ResetPreviousParticlesToCurrent(nv::cloth::Cloth& Cloth)
{
	nv::cloth::MappedRange<physx::PxVec4> CurrentParticles = Cloth.getCurrentParticles();
	nv::cloth::MappedRange<physx::PxVec4> PreviousParticles = Cloth.getPreviousParticles();
	if (CurrentParticles.size() != PreviousParticles.size())
	{
		return false;
	}

	for (uint32 Index = 0; Index < CurrentParticles.size(); ++Index)
	{
		PreviousParticles[Index] = CurrentParticles[Index];
	}

	return true;
}

physx::PxVec4 ToPxSphere(const FClothCollisionSphere& Sphere)
{
	return physx::PxVec4(Sphere.Center.X, Sphere.Center.Y, Sphere.Center.Z, Sphere.Radius);
}

physx::PxVec4 ToPxPlane(const FClothCollisionPlane& Plane)
{
	return physx::PxVec4(Plane.Normal.X, Plane.Normal.Y, Plane.Normal.Z, Plane.Distance);
}

physx::PxVec4 ToPxMotionConstraint(const FClothMotionConstraint& Constraint)
{
	return physx::PxVec4(Constraint.Center.X, Constraint.Center.Y, Constraint.Center.Z, Constraint.Radius);
}

physx::PxVec4 ToPxSeparationConstraint(const FClothSeparationConstraint& Constraint)
{
	return physx::PxVec4(Constraint.Center.X, Constraint.Center.Y, Constraint.Center.Z, Constraint.Radius);
}

FVector ToFVector(const physx::PxVec4& Value)
{
	return FVector(Value.x, Value.y, Value.z);
}

physx::PxVec3 NormalizeOrDefault(const FVector& Value, const physx::PxVec3& Fallback)
{
	const float LengthSquared = Value.X * Value.X + Value.Y * Value.Y + Value.Z * Value.Z;
	if (LengthSquared <= 1.0e-8f)
	{
		return Fallback;
	}

	const float InvLength = 1.0f / std::sqrt(LengthSquared);
	return physx::PxVec3(Value.X * InvLength, Value.Y * InvLength, Value.Z * InvLength);
}

template <typename T>
nv::cloth::Range<const T> MakeConstRange(const TArray<T>& Values)
{
	if (Values.empty())
	{
		return nv::cloth::Range<const T>();
	}

	return nv::cloth::Range<const T>(Values.data(), Values.data() + Values.size());
}

template <typename T>
nv::cloth::Range<T> MakeRange(TArray<T>& Values)
{
	if (Values.empty())
	{
		return nv::cloth::Range<T>();
	}

	return nv::cloth::Range<T>(Values.data(), Values.data() + Values.size());
}

bool ValidateCapsules(const TArray<FClothCollisionCapsule>& Capsules, uint32 NumSpheres)
{
	for (const FClothCollisionCapsule& Capsule : Capsules)
	{
		if (Capsule.SphereA >= NumSpheres || Capsule.SphereB >= NumSpheres)
		{
			return false;
		}
	}

	return true;
}

FVector NormalizeOrFallback(const FVector& Value, const FVector& Fallback)
{
	if (Value.LengthSquared() <= 1.0e-8f)
	{
		return Fallback;
	}

	return Value.Normalized();
}

uint64 MakeClothEdgeKey(uint32 A, uint32 B)
{
	if (A > B)
	{
		std::swap(A, B);
	}
	return (static_cast<uint64>(A) << 32) | static_cast<uint64>(B);
}

bool AddUniqueClothEdge(TSet<uint64>& Edges, uint32 A, uint32 B)
{
	if (A == B)
	{
		return false;
	}
	return Edges.insert(MakeClothEdgeKey(A, B)).second;
}

uint32 CountFabricConstraintEdges(const TArray<uint32>& Indices)
{
	TSet<uint64> Edges;
	Edges.reserve(Indices.size());

	for (uint32 Index = 0; Index + 2 < Indices.size(); Index += 3)
	{
		const uint32 I0 = Indices[Index + 0];
		const uint32 I1 = Indices[Index + 1];
		const uint32 I2 = Indices[Index + 2];
		AddUniqueClothEdge(Edges, I0, I1);
		AddUniqueClothEdge(Edges, I1, I2);
		AddUniqueClothEdge(Edges, I2, I0);
	}

	return static_cast<uint32>(Edges.size());
}

void AddClothDebugLine(TArray<FPhysicsDebugLine>& OutLines, const FVector& Start, const FVector& End, const FVector& Color)
{
	FPhysicsDebugLine Line;
	Line.Start = Start;
	Line.End = End;
	Line.Color = Color;
	OutLines.push_back(Line);
}

void AddClothDebugCross(TArray<FPhysicsDebugLine>& OutLines, const FVector& Center, float Radius, const FVector& Color)
{
	const float R = (std::max)(0.05f, Radius);
	AddClothDebugLine(OutLines, Center - FVector(R, 0.0f, 0.0f), Center + FVector(R, 0.0f, 0.0f), Color);
	AddClothDebugLine(OutLines, Center - FVector(0.0f, R, 0.0f), Center + FVector(0.0f, R, 0.0f), Color);
	AddClothDebugLine(OutLines, Center - FVector(0.0f, 0.0f, R), Center + FVector(0.0f, 0.0f, R), Color);
}

void BuildPerpendicularAxes(const FVector& Axis, FVector& OutA, FVector& OutB)
{
	const FVector Normal = NormalizeOrFallback(Axis, FVector::UpVector);
	const FVector Seed = std::abs(Normal.Z) < 0.9f ? FVector::UpVector : FVector::RightVector;

	OutA = Normal.Cross(Seed);
	if (OutA.LengthSquared() <= 1.0e-8f)
	{
		OutA = FVector::RightVector;
	}
	else
	{
		OutA.Normalize();
	}

	OutB = Normal.Cross(OutA);
	if (OutB.LengthSquared() <= 1.0e-8f)
	{
		OutB = FVector::ForwardVector;
	}
	else
	{
		OutB.Normalize();
	}
}

void AddClothDebugCircle(
	TArray<FPhysicsDebugLine>& OutLines,
	const FVector& Center,
	const FVector& AxisA,
	const FVector& AxisB,
	float Radius,
	const FVector& Color)
{
	if (Radius <= 0.0f)
	{
		return;
	}

	constexpr uint32 SegmentCount = 20;
	constexpr float TwoPi = 6.28318530717958647692f;
	for (uint32 Segment = 0; Segment < SegmentCount; ++Segment)
	{
		const float A0 = TwoPi * static_cast<float>(Segment) / static_cast<float>(SegmentCount);
		const float A1 = TwoPi * static_cast<float>(Segment + 1) / static_cast<float>(SegmentCount);
		const FVector P0 = Center + AxisA * (std::cos(A0) * Radius) + AxisB * (std::sin(A0) * Radius);
		const FVector P1 = Center + AxisA * (std::cos(A1) * Radius) + AxisB * (std::sin(A1) * Radius);
		AddClothDebugLine(OutLines, P0, P1, Color);
	}
}

void AddClothDebugSphere(TArray<FPhysicsDebugLine>& OutLines, const FVector& Center, float Radius, const FVector& Color)
{
	AddClothDebugCircle(OutLines, Center, FVector::RightVector, FVector::ForwardVector, Radius, Color);
	AddClothDebugCircle(OutLines, Center, FVector::RightVector, FVector::UpVector, Radius, Color);
	AddClothDebugCircle(OutLines, Center, FVector::ForwardVector, FVector::UpVector, Radius, Color);
}

void AddClothDebugCapsule(
	TArray<FPhysicsDebugLine>& OutLines,
	const FClothCollisionSphere& SphereA,
	const FClothCollisionSphere& SphereB,
	const FVector& Color)
{
	const FVector Delta = SphereB.Center - SphereA.Center;
	if (Delta.LengthSquared() <= 1.0e-8f)
	{
		return;
	}

	FVector AxisA;
	FVector AxisB;
	BuildPerpendicularAxes(Delta, AxisA, AxisB);

	AddClothDebugLine(OutLines, SphereA.Center + AxisA * SphereA.Radius, SphereB.Center + AxisA * SphereB.Radius, Color);
	AddClothDebugLine(OutLines, SphereA.Center - AxisA * SphereA.Radius, SphereB.Center - AxisA * SphereB.Radius, Color);
	AddClothDebugLine(OutLines, SphereA.Center + AxisB * SphereA.Radius, SphereB.Center + AxisB * SphereB.Radius, Color);
	AddClothDebugLine(OutLines, SphereA.Center - AxisB * SphereA.Radius, SphereB.Center - AxisB * SphereB.Radius, Color);
}

bool IsGridParticlePinned(EClothPinMode PinMode, uint32 Row, uint32 Column, uint32 NumRows, uint32 NumColumns);

bool BuildGridDescriptions(const FClothGridDesc& Desc, FClothFabricDesc& OutFabricDesc, FClothInstanceDesc& OutInstanceDesc)
{
	if (Desc.NumColumns < 2 || Desc.NumRows < 2 || Desc.Spacing <= 0.0f)
	{
		return false;
	}

	const FVector AxisX = NormalizeOrFallback(Desc.AxisX, FVector::RightVector);
	const FVector AxisY = NormalizeOrFallback(Desc.AxisY, FVector::DownVector);
	const uint32 ParticleCount = Desc.NumColumns * Desc.NumRows;

	OutFabricDesc = FClothFabricDesc();
	OutFabricDesc.Name = Desc.Name + "_Fabric";
	OutFabricDesc.GravityDirection = Desc.Settings.Gravity;
	OutFabricDesc.Particles.reserve(ParticleCount);
	OutFabricDesc.UVs.reserve(ParticleCount);

	OutInstanceDesc = FClothInstanceDesc();
	OutInstanceDesc.Name = Desc.Name;
	OutInstanceDesc.Settings = Desc.Settings;
	OutInstanceDesc.Collision = Desc.Collision;
	OutInstanceDesc.InitialParticles.reserve(ParticleCount);

	for (uint32 Row = 0; Row < Desc.NumRows; ++Row)
	{
		for (uint32 Column = 0; Column < Desc.NumColumns; ++Column)
		{
			const float U = Desc.NumColumns > 1 ? static_cast<float>(Column) / static_cast<float>(Desc.NumColumns - 1) : 0.0f;
			const float V = Desc.NumRows > 1 ? static_cast<float>(Row) / static_cast<float>(Desc.NumRows - 1) : 0.0f;
			const FVector Position = Desc.Origin + AxisX * (Column * Desc.Spacing) + AxisY * (Row * Desc.Spacing);
			const bool bPinned = IsGridParticlePinned(Desc.PinMode, Row, Column, Desc.NumRows, Desc.NumColumns);

			FClothParticle Particle;
			Particle.Position = Position;
			Particle.InvMass = bPinned ? 0.0f : 1.0f;

			OutFabricDesc.Particles.emplace_back(Particle);
			OutFabricDesc.UVs.emplace_back(U, V);
			OutInstanceDesc.InitialParticles.emplace_back(Particle);
		}
	}

	OutFabricDesc.Indices.reserve((Desc.NumColumns - 1) * (Desc.NumRows - 1) * 6);
	for (uint32 Row = 0; Row + 1 < Desc.NumRows; ++Row)
	{
		for (uint32 Column = 0; Column + 1 < Desc.NumColumns; ++Column)
		{
			const uint32 I0 = Row * Desc.NumColumns + Column;
			const uint32 I1 = I0 + 1;
			const uint32 I2 = I0 + Desc.NumColumns;
			const uint32 I3 = I2 + 1;

			OutFabricDesc.Indices.emplace_back(I0);
			OutFabricDesc.Indices.emplace_back(I2);
			OutFabricDesc.Indices.emplace_back(I1);

			OutFabricDesc.Indices.emplace_back(I1);
			OutFabricDesc.Indices.emplace_back(I2);
			OutFabricDesc.Indices.emplace_back(I3);
		}
	}

	return true;
}

bool IsGridParticlePinned(EClothPinMode PinMode, uint32 Row, uint32 Column, uint32 NumRows, uint32 NumColumns)
{
	const bool bTop = Row == 0;
	const bool bBottom = Row + 1 == NumRows;
	const bool bLeft = Column == 0;
	const bool bRight = Column + 1 == NumColumns;

	switch (PinMode)
	{
	case EClothPinMode::TopRow:
		return bTop;
	case EClothPinMode::BottomRow:
		return bBottom;
	case EClothPinMode::LeftRow:
		return bLeft;
	case EClothPinMode::RightRow:
		return bRight;
	case EClothPinMode::TopLeft:
		return bTop && bLeft;
	case EClothPinMode::TopRight:
		return bTop && bRight;
	case EClothPinMode::BottomLeft:
		return bBottom && bLeft;
	case EClothPinMode::BottomRight:
		return bBottom && bRight;
	case EClothPinMode::TopCorners:
		return bTop && (bLeft || bRight);
	case EClothPinMode::BottomCorners:
		return bBottom && (bLeft || bRight);
	case EClothPinMode::LeftCorners:
		return bLeft && (bTop || bBottom);
	case EClothPinMode::RightCorners:
		return bRight && (bTop || bBottom);
	case EClothPinMode::AllCorners:
		return (bTop || bBottom) && (bLeft || bRight);
	case EClothPinMode::None:
	default:
		return false;
	}
}
}

struct FNvClothFabricRecord
{
	FClothFabricHandle Handle;
	nv::cloth::Fabric* Fabric = nullptr;
	uint32 NumParticles = 0;
	FString Name;
	TArray<uint32> Indices;
	TArray<FVector2> UVs;
};

struct FNvClothInstanceRecord
{
	FClothInstance Instance;
	FClothCollisionDesc UserCollision;
	FMatrix ClothWorldMatrix = FMatrix::Identity;
	FVector LastClothInertiaLocation = FVector::ZeroVector;
	FQuat LastClothWorldRotation = FQuat::Identity;
	bool bHasClothWorldTransform = false;
	bool bUseRegisteredShapeCollision = false;
	bool bResetParticleHistoryBeforeSim = false;
	nv::cloth::Cloth* Cloth = nullptr;
	FNvClothFabricRecord* FabricRecord = nullptr;
	TArray<nv::cloth::PhaseConfig> PhaseConfigs;
	TArray<physx::PxVec4> SphereScratch;
	TArray<uint32_t> CapsuleScratch;
	TArray<physx::PxVec4> PlaneScratch;
	TArray<uint32_t> ConvexScratch;
};

namespace
{
FVector TransformPoint(const FMatrix& Matrix, const FVector& Point)
{
	return Matrix.TransformPositionWithW(Point);
}

bool HasBoundsPoints(const FBoundingBox& Bounds)
{
	return Bounds.Min.X <= Bounds.Max.X
		&& Bounds.Min.Y <= Bounds.Max.Y
		&& Bounds.Min.Z <= Bounds.Max.Z;
}

bool BoundsIntersect(const FBoundingBox& A, const FBoundingBox& B)
{
	return HasBoundsPoints(A) && HasBoundsPoints(B) && A.IsIntersected(B);
}

FBoundingBox ExpandBounds(const FBoundingBox& Bounds, float Padding)
{
	if (!HasBoundsPoints(Bounds))
	{
		return Bounds;
	}

	const FVector Delta(Padding, Padding, Padding);
	return FBoundingBox(Bounds.Min - Delta, Bounds.Max + Delta);
}

float GetWorldToClothRadiusScale(const FMatrix& ClothWorldMatrix)
{
	const FVector Scale = ClothWorldMatrix.GetScale();
	const float MaxScale = (std::max)({ std::abs(Scale.X), std::abs(Scale.Y), std::abs(Scale.Z) });
	return MaxScale > 1.0e-4f ? 1.0f / MaxScale : 1.0f;
}

FVector SafeNormal(const FVector& Value, const FVector& Fallback)
{
	if (Value.LengthSquared() <= 1.0e-8f)
	{
		return Fallback;
	}
	return Value.Normalized();
}

FClothCollisionPlane MakePlaneFromPointNormal(const FVector& Point, const FVector& Normal)
{
	const FVector N = SafeNormal(Normal, FVector::UpVector);
	FClothCollisionPlane Plane;
	Plane.Normal = N;
	Plane.Distance = -N.Dot(Point);
	return Plane;
}

void AppendBoxConvexToClothCollision(
	const UBoxComponent* Box,
	const FMatrix& ClothWorldInverse,
	float Margin,
	FClothCollisionDesc& OutCollision)
{
	if (!Box)
	{
		return;
	}

	constexpr uint32 PlanesPerBox = 6;
	const uint32 PlaneBase = static_cast<uint32>(OutCollision.Planes.size());
	if (PlaneBase + PlanesPerBox > 32)
	{
		return;
	}

	const FVector Center = Box->GetWorldLocation();
	const FVector Extent = Box->GetScaledBoxExtent();
	const FVector WorldAxes[3] =
	{
		SafeNormal(Box->GetForwardVector(), FVector::XAxisVector),
		SafeNormal(Box->GetRightVector(), FVector::YAxisVector),
		SafeNormal(Box->GetUpVector(), FVector::ZAxisVector),
	};
	const float WorldExtents[3] = { Extent.X, Extent.Y, Extent.Z };

	uint32 Mask = 0;
	for (uint32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		const FVector Axis = WorldAxes[AxisIndex];
		const float AxisExtent = WorldExtents[AxisIndex];

		const FVector PositivePointWorld = Center + Axis * (AxisExtent + Margin);
		const FVector NegativePointWorld = Center - Axis * (AxisExtent + Margin);

		const FVector PositivePointLocal = TransformPoint(ClothWorldInverse, PositivePointWorld);
		const FVector PositiveNormalLocal = SafeNormal(
			TransformPoint(ClothWorldInverse, PositivePointWorld + Axis) - PositivePointLocal,
			FVector::XAxisVector);
		OutCollision.Planes.push_back(MakePlaneFromPointNormal(PositivePointLocal, PositiveNormalLocal));
		Mask |= (1u << (PlaneBase + AxisIndex * 2u));

		const FVector NegativePointLocal = TransformPoint(ClothWorldInverse, NegativePointWorld);
		const FVector NegativeNormalLocal = SafeNormal(
			TransformPoint(ClothWorldInverse, NegativePointWorld - Axis) - NegativePointLocal,
			FVector::XAxisVector);
		OutCollision.Planes.push_back(MakePlaneFromPointNormal(NegativePointLocal, NegativeNormalLocal));
		Mask |= (1u << (PlaneBase + AxisIndex * 2u + 1u));
	}

	FClothCollisionConvex Convex;
	Convex.PlaneMask = Mask;
	OutCollision.Convexes.push_back(Convex);
}

FBoundingBox BuildClothWorldBounds(const FNvClothInstanceRecord& Record)
{
	FBoundingBox Bounds;
	if (!Record.Cloth)
	{
		return Bounds;
	}

	auto CurrentParticles = Record.Cloth->getCurrentParticles();
	for (uint32 Index = 0; Index < CurrentParticles.size(); ++Index)
	{
		const physx::PxVec4& Particle = CurrentParticles[Index];
		Bounds.Expand(TransformPoint(Record.ClothWorldMatrix, FVector(Particle.x, Particle.y, Particle.z)));
	}
	return Bounds;
}

bool AppendShapeColliderToClothCollision(
	const UShapeComponent* Shape,
	const FMatrix& ClothWorldInverse,
	float RadiusScale,
	FClothCollisionDesc& OutCollision)
{
	if (!Shape || !Shape->IsCollisionEnabled())
	{
		return false;
	}

	if (const USphereComponent* Sphere = Cast<USphereComponent>(Shape))
	{
		const float Radius = (Sphere->GetScaledSphereRadius() + ClothSphereCollisionMargin) * RadiusScale;
		if (Radius <= 0.0f)
		{
			return false;
		}

		FClothCollisionSphere ClothSphere;
		ClothSphere.Center = TransformPoint(ClothWorldInverse, Sphere->GetWorldLocation());
		ClothSphere.Radius = Radius;
		OutCollision.Spheres.push_back(ClothSphere);
		return true;
	}

	if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Shape))
	{
		const float WorldRadius = Capsule->GetScaledCapsuleRadius();
		const float WorldHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		const float Radius = (WorldRadius + ClothCapsuleCollisionMargin) * RadiusScale;
		const float SegmentHalfLength = (std::max)(0.0f, WorldHalfHeight - WorldRadius);
		if (Radius <= 0.0f || WorldHalfHeight <= 0.0f)
		{
			return false;
		}

		FVector Axis = Capsule->GetUpVector();
		if (Axis.Length() <= 1.0e-4f)
		{
			Axis = FVector::ZAxisVector;
		}
		Axis.Normalize();

		const FVector Center = Capsule->GetWorldLocation();
		const uint32 SphereBase = static_cast<uint32>(OutCollision.Spheres.size());

		FClothCollisionSphere SphereA;
		SphereA.Center = TransformPoint(ClothWorldInverse, Center + Axis * SegmentHalfLength);
		SphereA.Radius = Radius;
		OutCollision.Spheres.push_back(SphereA);

		FClothCollisionSphere SphereB;
		SphereB.Center = TransformPoint(ClothWorldInverse, Center - Axis * SegmentHalfLength);
		SphereB.Radius = Radius;
		OutCollision.Spheres.push_back(SphereB);

		FClothCollisionCapsule ClothCapsule;
		ClothCapsule.SphereA = SphereBase;
		ClothCapsule.SphereB = SphereBase + 1;
		OutCollision.Capsules.push_back(ClothCapsule);
		return true;
	}

	if (const UBoxComponent* Box = Cast<UBoxComponent>(Shape))
	{
		AppendBoxConvexToClothCollision(Box, ClothWorldInverse, ClothBoxCollisionMargin, OutCollision);
		return true;
	}

	return false;
}

uint32 CountPinnedParticles(const FNvClothInstanceRecord& Record)
{
	if (!Record.Cloth)
	{
		return 0;
	}

	uint32 Count = 0;
	const nv::cloth::Cloth* Cloth = Record.Cloth;
	nv::cloth::MappedRange<const physx::PxVec4> CurrentParticles = Cloth->getCurrentParticles();
	for (uint32 Index = 0; Index < CurrentParticles.size(); ++Index)
	{
		if (CurrentParticles[Index].w <= 0.0f)
		{
			++Count;
		}
	}
	return Count;
}

void AccumulateRecordStats(const FNvClothInstanceRecord& Record, FClothStats& OutStats)
{
	++OutStats.NumCloths;
	OutStats.NumParticles += Record.Instance.NumParticles;
	OutStats.NumPinnedParticles += CountPinnedParticles(Record);
	OutStats.NumMotionConstraints += static_cast<uint32>(Record.Instance.Constraints.MotionConstraints.size());
	OutStats.NumSeparationConstraints += static_cast<uint32>(Record.Instance.Constraints.SeparationConstraints.size());
	OutStats.NumCollisionSpheres += static_cast<uint32>(Record.Instance.Collision.Spheres.size());
	OutStats.NumCollisionCapsules += static_cast<uint32>(Record.Instance.Collision.Capsules.size());
	OutStats.NumCollisionPlanes += static_cast<uint32>(Record.Instance.Collision.Planes.size());
	OutStats.NumCollisionConvexes += static_cast<uint32>(Record.Instance.Collision.Convexes.size());

	if (Record.FabricRecord)
	{
		OutStats.NumConstraints += CountFabricConstraintEdges(Record.FabricRecord->Indices);
	}
}

void AppendRecordParticleDebugLines(
	const FNvClothInstanceRecord& Record,
	TArray<FPhysicsDebugLine>& OutLines,
	const FClothDebugDrawOptions& Options)
{
	if (!Record.Cloth)
	{
		return;
	}

	const nv::cloth::Cloth* Cloth = Record.Cloth;
	nv::cloth::MappedRange<const physx::PxVec4> CurrentParticles = Cloth->getCurrentParticles();
	const uint32 ParticleCount = static_cast<uint32>(CurrentParticles.size());
	const uint32 MaxParticles = Options.MaxDebugParticles > 0
		? (std::min)(ParticleCount, Options.MaxDebugParticles)
		: ParticleCount;

	const FVector FreeParticleColor(0.0f, 0.72f, 1.0f);
	const FVector PinnedParticleColor(1.0f, 0.18f, 0.12f);
	for (uint32 Index = 0; Index < MaxParticles; ++Index)
	{
		const physx::PxVec4& Particle = CurrentParticles[Index];
		const FVector Color = Particle.w <= 0.0f ? PinnedParticleColor : FreeParticleColor;
		AddClothDebugCross(OutLines, ToFVector(Particle), Options.ParticlePointSize, Color);
	}
}

void AppendRecordConstraintDebugLines(
	const FNvClothInstanceRecord& Record,
	TArray<FPhysicsDebugLine>& OutLines,
	const FClothDebugDrawOptions& Options)
{
	if (!Record.Cloth || !Record.FabricRecord)
	{
		return;
	}

	const nv::cloth::Cloth* Cloth = Record.Cloth;
	nv::cloth::MappedRange<const physx::PxVec4> CurrentParticles = Cloth->getCurrentParticles();
	const uint32 ParticleCount = static_cast<uint32>(CurrentParticles.size());
	const TArray<uint32>& Indices = Record.FabricRecord->Indices;

	TSet<uint64> Edges;
	Edges.reserve(Indices.size());

	const FVector ConstraintColor(0.15f, 1.0f, 0.32f);
	uint32 EmittedLines = 0;
	for (uint32 Index = 0; Index + 2 < Indices.size(); Index += 3)
	{
		const uint32 Tri[3] = { Indices[Index + 0], Indices[Index + 1], Indices[Index + 2] };
		for (uint32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
		{
			const uint32 A = Tri[EdgeIndex];
			const uint32 B = Tri[(EdgeIndex + 1) % 3];
			if (A >= ParticleCount || B >= ParticleCount || !AddUniqueClothEdge(Edges, A, B))
			{
				continue;
			}

			if (Options.MaxDebugConstraintLines > 0 && EmittedLines >= Options.MaxDebugConstraintLines)
			{
				return;
			}

			AddClothDebugLine(OutLines, ToFVector(CurrentParticles[A]), ToFVector(CurrentParticles[B]), ConstraintColor);
			++EmittedLines;
		}
	}
}

void AppendRecordCollisionDebugLines(const FNvClothInstanceRecord& Record, TArray<FPhysicsDebugLine>& OutLines)
{
	const FVector SphereColor(1.0f, 0.9f, 0.0f);
	const FVector CapsuleColor(1.0f, 0.45f, 0.08f);

	for (const FClothCollisionSphere& Sphere : Record.Instance.Collision.Spheres)
	{
		AddClothDebugSphere(OutLines, Sphere.Center, Sphere.Radius, SphereColor);
	}

	for (const FClothCollisionCapsule& Capsule : Record.Instance.Collision.Capsules)
	{
		if (Capsule.SphereA >= Record.Instance.Collision.Spheres.size() ||
			Capsule.SphereB >= Record.Instance.Collision.Spheres.size())
		{
			continue;
		}

		const FClothCollisionSphere& SphereA = Record.Instance.Collision.Spheres[Capsule.SphereA];
		const FClothCollisionSphere& SphereB = Record.Instance.Collision.Spheres[Capsule.SphereB];
		AddClothDebugCapsule(OutLines, SphereA, SphereB, CapsuleColor);
	}
}

void AppendRecordDebugLines(
	const FNvClothInstanceRecord& Record,
	TArray<FPhysicsDebugLine>& OutLines,
	const FClothDebugDrawOptions& Options)
{
	if (Options.bParticles)
	{
		AppendRecordParticleDebugLines(Record, OutLines, Options);
	}

	if (Options.bConstraints)
	{
		AppendRecordConstraintDebugLines(Record, OutLines, Options);
	}

	if (Options.bCollision)
	{
		AppendRecordCollisionDebugLines(Record, OutLines);
	}
}
}

FNvClothScene::FNvClothScene() = default;

FNvClothScene::~FNvClothScene()
{
	Shutdown();
}

bool FNvClothScene::Initialize(EClothBackend Backend)
{
	if (Factory && Solver)
	{
		return true;
	}

	if (!GClothCallbacksInitialized)
	{
		nv::cloth::InitializeNvCloth(&GClothAllocator, &GClothErrorCallback, &GClothAssertHandler, nullptr);
		GClothCallbacksInitialized = true;
	}

	if (Backend == EClothBackend::CUDA)
	{
		FNvClothCudaDriver Driver;
		if (Driver.Load())
		{
			int DeviceCount = 0;
			FNvClothCudaDriver::CUdevice Device = 0;
			CUcontext CreatedContext = nullptr;
			if (Driver.CuInit(0) == CudaSuccess
				&& Driver.CuDeviceGetCount(&DeviceCount) == CudaSuccess
				&& DeviceCount > 0
				&& Driver.CuDeviceGet(&Device, 0) == CudaSuccess)
			{
				Driver.CuCtxCreate(&CreatedContext, 0, Device);
			}

			if (CreatedContext && Driver.CuCtxSetCurrent(CreatedContext) == CudaSuccess)
			{
				Factory = TryCreateCudaFactory(CreatedContext);
				if (Factory)
				{
					Solver = TryCreateSolver(Factory);
					if (Solver)
					{
						CudaDriverModule = Driver.Module;
						CudaContext = CreatedContext;
						Driver.Module = nullptr;
						Stats = FClothStats();
						UE_LOG("[NvCloth] CUDA backend initialized.");
						return true;
					}

					TryDestroyFactory(Factory);
					Factory = nullptr;
				}
			}

			if (CreatedContext)
			{
				Driver.CuCtxSetCurrent(nullptr);
				Driver.CuCtxDestroy(CreatedContext);
			}

			Driver.Unload();
		}

		UE_LOG("[NvCloth] CUDA backend unavailable; falling back to CPU.");
	}
	else if (Backend != EClothBackend::CPU)
	{
		return false;
	}

	Factory = NvClothCreateFactoryCPU();
	if (!Factory)
	{
		return false;
	}

	Solver = TryCreateSolver(Factory);
	if (!Solver)
	{
		TryDestroyFactory(Factory);
		Factory = nullptr;
		return false;
	}

	Stats = FClothStats();
	UE_LOG("[NvCloth] CPU backend initialized.");
	return true;
}

void FNvClothScene::Shutdown()
{
	for (std::unique_ptr<FNvClothInstanceRecord>& Record : Instances)
	{
		if (Record && Record->Cloth)
		{
			if (Solver)
			{
				Solver->removeCloth(Record->Cloth);
			}
			delete Record->Cloth;
			Record->Cloth = nullptr;
			Record->Instance.Reset();
		}
	}
	Instances.clear();

	for (std::unique_ptr<FNvClothFabricRecord>& Record : Fabrics)
	{
		if (Record && Record->Fabric)
		{
			Record->Fabric->decRefCount();
			Record->Fabric = nullptr;
		}
	}
	Fabrics.clear();

	if (Solver)
	{
		delete Solver;
		Solver = nullptr;
	}

	if (Factory)
	{
		TryDestroyFactory(Factory);
		Factory = nullptr;
	}

	if (CudaContext)
	{
		HMODULE Module = static_cast<HMODULE>(CudaDriverModule);
		if (Module)
		{
			using CuCtxDestroyFn = int(WINAPI*)(CUcontext);
			using CuCtxSetCurrentFn = int(WINAPI*)(CUcontext);
			CuCtxSetCurrentFn CuCtxSetCurrent = reinterpret_cast<CuCtxSetCurrentFn>(::GetProcAddress(Module, "cuCtxSetCurrent"));
			CuCtxDestroyFn CuCtxDestroy = reinterpret_cast<CuCtxDestroyFn>(::GetProcAddress(Module, "cuCtxDestroy_v2"));
			if (!CuCtxDestroy)
			{
				CuCtxDestroy = reinterpret_cast<CuCtxDestroyFn>(::GetProcAddress(Module, "cuCtxDestroy"));
			}

			if (CuCtxSetCurrent)
			{
				CuCtxSetCurrent(nullptr);
			}

			if (CuCtxDestroy)
			{
				CuCtxDestroy(static_cast<CUcontext>(CudaContext));
			}
		}
		CudaContext = nullptr;
	}

	if (CudaDriverModule)
	{
		::FreeLibrary(static_cast<HMODULE>(CudaDriverModule));
		CudaDriverModule = nullptr;
	}

	Stats = FClothStats();
}

FClothFabricHandle FNvClothScene::CreateClothFabric(const FClothFabricDesc& Desc)
{
	if (!Factory || Desc.Particles.size() < 3 || Desc.Indices.size() < 3 || (Desc.Indices.size() % 3) != 0)
	{
		return {};
	}

	if (!Desc.UVs.empty() && Desc.UVs.size() != Desc.Particles.size())
	{
		return {};
	}

	for (uint32 Index : Desc.Indices)
	{
		if (Index >= Desc.Particles.size())
		{
			return {};
		}
	}

	TArray<physx::PxVec3> Points;
	TArray<float> InvMasses;
	Points.reserve(Desc.Particles.size());
	InvMasses.reserve(Desc.Particles.size());

	for (const FClothParticle& Particle : Desc.Particles)
	{
		Points.emplace_back(Particle.Position.X, Particle.Position.Y, Particle.Position.Z);
		InvMasses.emplace_back(Particle.InvMass);
	}

	nv::cloth::ClothMeshDesc MeshDesc;
	MeshDesc.points.data = Points.data();
	MeshDesc.points.count = static_cast<physx::PxU32>(Points.size());
	MeshDesc.points.stride = sizeof(physx::PxVec3);
	MeshDesc.invMasses.data = InvMasses.data();
	MeshDesc.invMasses.count = static_cast<physx::PxU32>(InvMasses.size());
	MeshDesc.invMasses.stride = sizeof(float);
	MeshDesc.triangles.data = Desc.Indices.data();
	MeshDesc.triangles.count = static_cast<physx::PxU32>(Desc.Indices.size() / 3);
	MeshDesc.triangles.stride = sizeof(uint32) * 3;

	nv::cloth::Vector<int32_t>::Type PhaseTypes;
	nv::cloth::Fabric* Fabric = NvClothCookFabricFromMesh(
		Factory,
		MeshDesc,
		NormalizeOrDefault(Desc.GravityDirection, physx::PxVec3(0.0f, 0.0f, -1.0f)),
		&PhaseTypes,
		Desc.bUseGeodesicTether);

	if (!Fabric)
	{
		return {};
	}

	std::unique_ptr<FNvClothFabricRecord> Record = std::make_unique<FNvClothFabricRecord>();
	Record->Fabric = Fabric;
	Record->NumParticles = static_cast<uint32>(Desc.Particles.size());
	Record->Name = Desc.Name;
	Record->Indices = Desc.Indices;
	Record->UVs = Desc.UVs;
	Record->Handle = { Fabric, NextFabricSerial++ };

	const FClothFabricHandle Handle = Record->Handle;
	Fabrics.emplace_back(std::move(Record));
	return Handle;
}

void FNvClothScene::DestroyClothFabric(FClothFabricHandle Fabric)
{
	if (!Fabric.IsValid())
	{
		return;
	}

	const bool bInUse = std::any_of(Instances.begin(), Instances.end(),
		[Fabric](const std::unique_ptr<FNvClothInstanceRecord>& Record)
		{
			return Record && Record->Instance.FabricHandle.NativePtr == Fabric.NativePtr &&
				Record->Instance.FabricHandle.Serial == Fabric.Serial;
		});

	if (bInUse)
	{
		return;
	}

	auto It = std::find_if(Fabrics.begin(), Fabrics.end(),
		[Fabric](const std::unique_ptr<FNvClothFabricRecord>& Record)
		{
			return Record && Record->Handle.NativePtr == Fabric.NativePtr && Record->Handle.Serial == Fabric.Serial;
		});

	if (It == Fabrics.end())
	{
		return;
	}

	if ((*It)->Fabric)
	{
		(*It)->Fabric->decRefCount();
		(*It)->Fabric = nullptr;
	}

	Fabrics.erase(It);
}

FClothInstance* FNvClothScene::CreateClothInstance(const FClothInstanceDesc& Desc)
{
	if (!Factory || !Solver || !Desc.Fabric.IsValid())
	{
		return nullptr;
	}

	FNvClothFabricRecord* FabricRecord = FindFabricRecord(Desc.Fabric);
	if (!FabricRecord || !FabricRecord->Fabric || Desc.InitialParticles.size() != FabricRecord->NumParticles)
	{
		return nullptr;
	}

	TArray<physx::PxVec4> Particles;
	Particles.reserve(Desc.InitialParticles.size());
	for (const FClothParticle& Particle : Desc.InitialParticles)
	{
		Particles.emplace_back(ToPxParticle(Particle));
	}

	nv::cloth::Cloth* Cloth = Factory->createCloth(MakeConstRange(Particles), *FabricRecord->Fabric);
	if (!Cloth)
	{
		return nullptr;
	}

	std::unique_ptr<FNvClothInstanceRecord> Record = std::make_unique<FNvClothInstanceRecord>();
	Record->Cloth = Cloth;
	Record->FabricRecord = FabricRecord;
	Record->Instance.Name = Desc.Name;
	Record->Instance.FabricHandle = Desc.Fabric;
	Record->Instance.InstanceHandle = { Cloth, NextInstanceSerial++ };
	Record->Instance.Settings = Desc.Settings;
	Record->Instance.NumParticles = FabricRecord->NumParticles;
	Record->Instance.bValid = true;
	Record->UserCollision = Desc.Collision;

	ApplyClothSettings(*Record);

	if (!ApplyClothConstraints(*Record, Desc.Constraints) || !ApplyClothCollision(*Record, Desc.Collision))
	{
		delete Cloth;
		return nullptr;
	}

	Record->Instance.Constraints = Desc.Constraints;
	Record->Instance.Collision = Desc.Collision;
	Solver->addCloth(Cloth);

	FClothInstance* Instance = &Record->Instance;
	Instances.emplace_back(std::move(Record));
	return Instance;
}

FClothInstance* FNvClothScene::CreateGridCloth(const FClothGridDesc& Desc, FClothFabricHandle* OutFabric)
{
	FClothFabricDesc FabricDesc;
	FClothInstanceDesc InstanceDesc;
	if (!BuildGridDescriptions(Desc, FabricDesc, InstanceDesc))
	{
		return nullptr;
	}

	const FClothFabricHandle Fabric = CreateClothFabric(FabricDesc);
	if (!Fabric.IsValid())
	{
		return nullptr;
	}

	InstanceDesc.Fabric = Fabric;
	FClothInstance* Instance = CreateClothInstance(InstanceDesc);
	if (!Instance)
	{
		DestroyClothFabric(Fabric);
		return nullptr;
	}

	if (OutFabric)
	{
		*OutFabric = Fabric;
	}

	return Instance;
}

void FNvClothScene::DestroyClothInstance(FClothInstance* Instance)
{
	if (!Instance)
	{
		return;
	}

	auto It = std::find_if(Instances.begin(), Instances.end(),
		[Instance](const std::unique_ptr<FNvClothInstanceRecord>& Record)
		{
			return Record && &Record->Instance == Instance;
		});

	if (It == Instances.end())
	{
		return;
	}

	if ((*It)->Cloth)
	{
		if (Solver)
		{
			Solver->removeCloth((*It)->Cloth);
		}
		delete (*It)->Cloth;
		(*It)->Cloth = nullptr;
	}

	(*It)->Instance.Reset();
	Instances.erase(It);
}

bool FNvClothScene::SetClothParticles(FClothInstance* Instance, const TArray<FClothParticle>& Particles)
{
	FNvClothInstanceRecord* Record = FindInstanceRecord(Instance);
	if (!Record || !Record->Cloth || Particles.size() != Instance->NumParticles)
	{
		return false;
	}

	nv::cloth::MappedRange<physx::PxVec4> CurrentParticles = Record->Cloth->getCurrentParticles();
	nv::cloth::MappedRange<physx::PxVec4> PreviousParticles = Record->Cloth->getPreviousParticles();
	if (CurrentParticles.size() != Particles.size() || PreviousParticles.size() != Particles.size())
	{
		return false;
	}

	for (uint32 Index = 0; Index < Particles.size(); ++Index)
	{
		const physx::PxVec4 Particle = ToPxParticle(Particles[Index]);
		CurrentParticles[Index] = Particle;
		PreviousParticles[Index] = Particle;
	}

	return true;
}

bool FNvClothScene::SetPinnedParticlePositions(FClothInstance* Instance, const TArray<FClothPinnedParticle>& Pins, bool bResetPreviousParticles)
{
	FNvClothInstanceRecord* Record = FindInstanceRecord(Instance);
	if (!Record || !Record->Cloth)
	{
		return false;
	}

	for (const FClothPinnedParticle& Pin : Pins)
	{
		if (Pin.ParticleIndex >= Instance->NumParticles)
		{
			return false;
		}
	}

	nv::cloth::MappedRange<physx::PxVec4> CurrentParticles = Record->Cloth->getCurrentParticles();
	if (CurrentParticles.size() != Instance->NumParticles)
	{
		return false;
	}

	if (bResetPreviousParticles)
	{
		nv::cloth::MappedRange<physx::PxVec4> PreviousParticles = Record->Cloth->getPreviousParticles();
		if (PreviousParticles.size() != Instance->NumParticles)
		{
			return false;
		}

		for (const FClothPinnedParticle& Pin : Pins)
		{
			const physx::PxVec4 PinnedParticle(Pin.Position.X, Pin.Position.Y, Pin.Position.Z, 0.0f);
			CurrentParticles[Pin.ParticleIndex] = PinnedParticle;
			PreviousParticles[Pin.ParticleIndex] = PinnedParticle;
		}
	}
	else
	{
		for (const FClothPinnedParticle& Pin : Pins)
		{
			CurrentParticles[Pin.ParticleIndex] = physx::PxVec4(Pin.Position.X, Pin.Position.Y, Pin.Position.Z, 0.0f);
		}
	}

	return true;
}

bool FNvClothScene::SetClothSettings(FClothInstance* Instance, const FClothSettings& Settings)
{
	FNvClothInstanceRecord* Record = FindInstanceRecord(Instance);
	if (!Record || !Record->Cloth)
	{
		return false;
	}

	Instance->Settings = Settings;
	ApplyClothSettings(*Record);
	return true;
}

bool FNvClothScene::SetClothConstraints(FClothInstance* Instance, const FClothConstraintDesc& Constraints)
{
	FNvClothInstanceRecord* Record = FindInstanceRecord(Instance);
	if (!Record || !ApplyClothConstraints(*Record, Constraints))
	{
		return false;
	}

	Instance->Constraints = Constraints;
	return true;
}

bool FNvClothScene::SetClothCollisionSpheres(FClothInstance* Instance, const TArray<FClothCollisionSphere>& Spheres)
{
	FNvClothInstanceRecord* Record = FindInstanceRecord(Instance);
	if (!Record || !Record->Cloth || !ValidateCapsules(Record->UserCollision.Capsules, static_cast<uint32>(Spheres.size())))
	{
		return false;
	}

	FClothCollisionDesc Collision = Record->UserCollision;
	Collision.Spheres = Spheres;

	if (!ApplyClothCollision(*Record, Collision))
	{
		return false;
	}

	Record->UserCollision = Collision;
	Instance->Collision = Collision;
	return true;
}

bool FNvClothScene::SetClothCollisionCapsules(FClothInstance* Instance, const TArray<FClothCollisionCapsule>& Capsules)
{
	FNvClothInstanceRecord* Record = FindInstanceRecord(Instance);
	if (!Record || !Record->Cloth || !ValidateCapsules(Capsules, static_cast<uint32>(Record->UserCollision.Spheres.size())))
	{
		return false;
	}

	FClothCollisionDesc Collision = Record->UserCollision;
	Collision.Capsules = Capsules;

	if (!ApplyClothCollision(*Record, Collision))
	{
		return false;
	}

	Record->UserCollision = Collision;
	Instance->Collision = Collision;
	return true;
}

bool FNvClothScene::SetClothCollision(FClothInstance* Instance, const FClothCollisionDesc& Collision)
{
	FNvClothInstanceRecord* Record = FindInstanceRecord(Instance);
	if (!Record || !Record->Cloth || !ApplyClothCollision(*Record, Collision))
	{
		return false;
	}

	Record->UserCollision = Collision;
	Instance->Collision = Collision;
	return true;
}

bool FNvClothScene::SetClothWorldMatrix(FClothInstance* Instance, const FMatrix& WorldMatrix, float DeltaTime)
{
	FNvClothInstanceRecord* Record = FindInstanceRecord(Instance);
	if (!Record)
	{
		return false;
	}

	const FVector NewLocation = WorldMatrix.GetLocation();
	const FQuat NewRotation = WorldMatrix.ToQuat().GetNormalized();
	if (Record->Cloth)
	{
		if (!Record->bHasClothWorldTransform)
		{
			TeleportClothWithoutInertia(*Record->Cloth, NewLocation, NewRotation);
			Record->bResetParticleHistoryBeforeSim = true;
			Record->bHasClothWorldTransform = true;
			Record->LastClothInertiaLocation = NewLocation;
		}
		else
		{
			const float RotationDot = std::abs(
				NewRotation.X * Record->LastClothWorldRotation.X
				+ NewRotation.Y * Record->LastClothWorldRotation.Y
				+ NewRotation.Z * Record->LastClothWorldRotation.Z
				+ NewRotation.W * Record->LastClothWorldRotation.W);
			const bool bLargeRotation = RotationDot < ClothTeleportRotationDotThreshold;

			if (bLargeRotation)
			{
				const FVector ClampedLocation = ClampClothTranslationStep(Record->LastClothInertiaLocation, NewLocation, DeltaTime);
				TeleportClothWithoutInertia(*Record->Cloth, ClampedLocation, NewRotation);
				Record->bResetParticleHistoryBeforeSim = true;
				Record->LastClothInertiaLocation = ClampedLocation;
			}
			else
			{
				const FVector ClampedLocation = ClampClothTranslationStep(Record->LastClothInertiaLocation, NewLocation, DeltaTime);
				Record->Cloth->setTranslation(ToPxVec3(ClampedLocation));
				Record->Cloth->setRotation(ToPxQuat(NewRotation));
				Record->LastClothInertiaLocation = ClampedLocation;
			}
		}
	}

	Record->ClothWorldMatrix = WorldMatrix;
	Record->LastClothWorldRotation = NewRotation;
	Record->bUseRegisteredShapeCollision = true;
	return true;
}

void FNvClothScene::RegisterShapeCollider(UShapeComponent* ShapeComponent)
{
	if (!ShapeComponent)
	{
		return;
	}

	if (std::find(ShapeColliders.begin(), ShapeColliders.end(), ShapeComponent) == ShapeColliders.end())
	{
		ShapeColliders.push_back(ShapeComponent);
	}
}

void FNvClothScene::UnregisterShapeCollider(UShapeComponent* ShapeComponent)
{
	if (!ShapeComponent)
	{
		return;
	}

	ShapeColliders.erase(
		std::remove(ShapeColliders.begin(), ShapeColliders.end(), ShapeComponent),
		ShapeColliders.end());
}

void FNvClothScene::SimulateCloth(float DeltaTime)
{
	if (!Solver || DeltaTime <= 0.0f)
	{
		return;
	}

	DeltaTime = std::min(DeltaTime, 1.0f / 30.0f);
	const auto StartTime = std::chrono::high_resolution_clock::now();

	for (std::unique_ptr<FNvClothInstanceRecord>& Record : Instances)
	{
		if (Record && Record->Cloth && Record->bResetParticleHistoryBeforeSim)
		{
			ResetPreviousParticlesToCurrent(*Record->Cloth);
			Record->bResetParticleHistoryBeforeSim = false;
		}
	}

	for (std::unique_ptr<FNvClothInstanceRecord>& Record : Instances)
	{
		if (!Record || !Record->Cloth || !Record->bUseRegisteredShapeCollision)
		{
			continue;
		}

		FClothCollisionDesc Collision;
		if (BuildCollisionFromRegisteredShapes(*Record, Collision))
		{
			ApplyClothCollision(*Record, Collision);
			Record->Instance.Collision = Collision;
		}
	}

	Stats.NumSolverChunks = 0;
	if (Solver->beginSimulation(DeltaTime))
	{
		const int ChunkCount = Solver->getSimulationChunkCount();
		Stats.NumSolverChunks = static_cast<uint32>((std::max)(ChunkCount, 0));
		for (int ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
		{
			Solver->simulateChunk(ChunkIndex);
		}
		Solver->endSimulation();
	}

	const auto EndTime = std::chrono::high_resolution_clock::now();
	Stats.LastSimulationMs = std::chrono::duration<float, std::milli>(EndTime - StartTime).count();
	Stats.bSolverError = Solver->hasError();
}

bool FNvClothScene::GetClothParticlePositions(const FClothInstance* Instance, TArray<FVector>& OutPositions) const
{
	const FNvClothInstanceRecord* Record = FindInstanceRecord(Instance);
	if (!Record || !Record->Cloth)
	{
		return false;
	}

	const nv::cloth::Cloth* Cloth = Record->Cloth;
	nv::cloth::MappedRange<const physx::PxVec4> CurrentParticles = Cloth->getCurrentParticles();
	OutPositions.clear();
	OutPositions.reserve(CurrentParticles.size());
	for (uint32 Index = 0; Index < CurrentParticles.size(); ++Index)
	{
		OutPositions.emplace_back(ToFVector(CurrentParticles[Index]));
	}

	return true;
}

bool FNvClothScene::GetClothRenderData(const FClothInstance* Instance, FClothRenderData& OutRenderData) const
{
	const FNvClothInstanceRecord* Record = FindInstanceRecord(Instance);
	if (!Record || !Record->Cloth || !Record->FabricRecord)
	{
		OutRenderData.Reset();
		return false;
	}

	const nv::cloth::Cloth* Cloth = Record->Cloth;
	nv::cloth::MappedRange<const physx::PxVec4> CurrentParticles = Cloth->getCurrentParticles();
	if (CurrentParticles.size() != Record->Instance.NumParticles)
	{
		OutRenderData.Reset();
		return false;
	}

	OutRenderData.Reset();
	OutRenderData.Vertices.resize(CurrentParticles.size());
	OutRenderData.Indices = Record->FabricRecord->Indices;

	for (uint32 Index = 0; Index < CurrentParticles.size(); ++Index)
	{
		FClothRenderVertex& Vertex = OutRenderData.Vertices[Index];
		Vertex.Position = ToFVector(CurrentParticles[Index]);
		Vertex.Normal = FVector::ZeroVector;
		Vertex.UV = Index < Record->FabricRecord->UVs.size() ? Record->FabricRecord->UVs[Index] : FVector2();
	}

	for (uint32 Index = 0; Index + 2 < OutRenderData.Indices.size(); Index += 3)
	{
		const uint32 I0 = OutRenderData.Indices[Index + 0];
		const uint32 I1 = OutRenderData.Indices[Index + 1];
		const uint32 I2 = OutRenderData.Indices[Index + 2];
		if (I0 >= OutRenderData.Vertices.size() || I1 >= OutRenderData.Vertices.size() || I2 >= OutRenderData.Vertices.size())
		{
			continue;
		}

		const FVector& P0 = OutRenderData.Vertices[I0].Position;
		const FVector& P1 = OutRenderData.Vertices[I1].Position;
		const FVector& P2 = OutRenderData.Vertices[I2].Position;
		FVector FaceNormal = (P1 - P0).Cross(P2 - P0);
		if (FaceNormal.LengthSquared() <= 1.0e-8f)
		{
			continue;
		}

		FaceNormal.Normalize();
		OutRenderData.Vertices[I0].Normal += FaceNormal;
		OutRenderData.Vertices[I1].Normal += FaceNormal;
		OutRenderData.Vertices[I2].Normal += FaceNormal;
	}

	for (FClothRenderVertex& Vertex : OutRenderData.Vertices)
	{
		if (Vertex.Normal.LengthSquared() <= 1.0e-8f)
		{
			Vertex.Normal = FVector::UpVector;
		}
		else
		{
			Vertex.Normal.Normalize();
		}
	}

	return true;
}

void FNvClothScene::GetClothStats(FClothStats& OutStats) const
{
	OutStats = Stats;
	OutStats.NumFabrics = static_cast<uint32>(Fabrics.size());
	OutStats.NumCloths = 0;
	OutStats.NumParticles = 0;
	OutStats.NumPinnedParticles = 0;
	OutStats.NumConstraints = 0;
	OutStats.NumMotionConstraints = 0;
	OutStats.NumSeparationConstraints = 0;
	OutStats.NumCollisionSpheres = 0;
	OutStats.NumCollisionCapsules = 0;

	for (const std::unique_ptr<FNvClothInstanceRecord>& Record : Instances)
	{
		if (Record)
		{
			AccumulateRecordStats(*Record, OutStats);
		}
	}

	if (Solver)
	{
		OutStats.bSolverError = Solver->hasError();
	}
}

void FNvClothScene::GetClothStats(const FClothInstance* Instance, FClothStats& OutStats) const
{
	OutStats = Stats;
	OutStats.NumFabrics = 0;
	OutStats.NumCloths = 0;
	OutStats.NumParticles = 0;
	OutStats.NumPinnedParticles = 0;
	OutStats.NumConstraints = 0;
	OutStats.NumMotionConstraints = 0;
	OutStats.NumSeparationConstraints = 0;
	OutStats.NumCollisionSpheres = 0;
	OutStats.NumCollisionCapsules = 0;

	const FNvClothInstanceRecord* Record = FindInstanceRecord(Instance);
	if (Record)
	{
		OutStats.NumFabrics = Record->FabricRecord ? 1u : 0u;
		AccumulateRecordStats(*Record, OutStats);
	}

	if (Solver)
	{
		OutStats.bSolverError = Solver->hasError();
	}
}

void FNvClothScene::ExtractClothDebugLines(
	const FClothInstance* Instance,
	TArray<FPhysicsDebugLine>& OutLines,
	const FClothDebugDrawOptions& Options) const
{
	OutLines.clear();

	if (Instance)
	{
		const FNvClothInstanceRecord* Record = FindInstanceRecord(Instance);
		if (Record)
		{
			AppendRecordDebugLines(*Record, OutLines, Options);
		}
		return;
	}

	for (const std::unique_ptr<FNvClothInstanceRecord>& Record : Instances)
	{
		if (Record)
		{
			AppendRecordDebugLines(*Record, OutLines, Options);
		}
	}
}

FNvClothFabricRecord* FNvClothScene::FindFabricRecord(FClothFabricHandle Handle)
{
	return const_cast<FNvClothFabricRecord*>(static_cast<const FNvClothScene*>(this)->FindFabricRecord(Handle));
}

const FNvClothFabricRecord* FNvClothScene::FindFabricRecord(FClothFabricHandle Handle) const
{
	for (const std::unique_ptr<FNvClothFabricRecord>& Record : Fabrics)
	{
		if (Record && Record->Handle.NativePtr == Handle.NativePtr && Record->Handle.Serial == Handle.Serial)
		{
			return Record.get();
		}
	}

	return nullptr;
}

FNvClothInstanceRecord* FNvClothScene::FindInstanceRecord(FClothInstance* Instance)
{
	return const_cast<FNvClothInstanceRecord*>(static_cast<const FNvClothScene*>(this)->FindInstanceRecord(Instance));
}

const FNvClothInstanceRecord* FNvClothScene::FindInstanceRecord(const FClothInstance* Instance) const
{
	if (!Instance || !Instance->InstanceHandle.IsValid())
	{
		return nullptr;
	}

	for (const std::unique_ptr<FNvClothInstanceRecord>& Record : Instances)
	{
		if (Record && &Record->Instance == Instance &&
			Record->Instance.InstanceHandle.NativePtr == Instance->InstanceHandle.NativePtr &&
			Record->Instance.InstanceHandle.Serial == Instance->InstanceHandle.Serial)
		{
			return Record.get();
		}
	}

	return nullptr;
}

void FNvClothScene::ApplyClothSettings(FNvClothInstanceRecord& Record)
{
	if (!Record.Cloth || !Record.FabricRecord || !Record.FabricRecord->Fabric)
	{
		return;
	}

	const FClothSettings& Settings = Record.Instance.Settings;
	Record.Cloth->setGravity(ToPxVec3(Settings.Gravity));
	Record.Cloth->setDamping(physx::PxVec3(Settings.Damping, Settings.Damping, Settings.Damping));
	Record.Cloth->setWindVelocity(ToPxVec3(Settings.WindVelocity));
	Record.Cloth->setDragCoefficient(Settings.DragCoefficient);
	Record.Cloth->setLiftCoefficient(Settings.LiftCoefficient);
	Record.Cloth->setLinearInertia(physx::PxVec3(Settings.LinearInertia, Settings.LinearInertia, Settings.LinearInertia));
	Record.Cloth->setAngularInertia(physx::PxVec3(Settings.AngularInertia, Settings.AngularInertia, Settings.AngularInertia));
	Record.Cloth->setCentrifugalInertia(physx::PxVec3(Settings.CentrifugalInertia, Settings.CentrifugalInertia, Settings.CentrifugalInertia));
	Record.Cloth->setSolverFrequency(std::max(Settings.SolverFrequency, 1.0f));
	Record.Cloth->setStiffnessFrequency(std::max(Settings.StiffnessFrequency, 1.0f));
	Record.Cloth->setTetherConstraintScale(Settings.TetherScale);
	Record.Cloth->setTetherConstraintStiffness(Settings.TetherStiffness);
	Record.Cloth->setFriction(Settings.Friction);
	Record.Cloth->enableContinuousCollision(Settings.bContinuousCollision);

	const uint32 NumPhases = Record.FabricRecord->Fabric->getNumPhases();
	Record.PhaseConfigs.clear();
	Record.PhaseConfigs.reserve(NumPhases);

	for (uint32 PhaseIndex = 0; PhaseIndex < NumPhases; ++PhaseIndex)
	{
		nv::cloth::PhaseConfig Config(static_cast<uint16_t>(PhaseIndex));
		Config.mStiffness = Settings.PhaseStiffness;
		Config.mStiffnessMultiplier = Settings.PhaseStiffnessMultiplier;
		Config.mCompressionLimit = Settings.CompressionLimit;
		Config.mStretchLimit = Settings.StretchLimit;
		Record.PhaseConfigs.emplace_back(Config);
	}

	if (!Record.PhaseConfigs.empty())
	{
		Record.Cloth->setPhaseConfig(MakeConstRange(Record.PhaseConfigs));
	}
}

bool FNvClothScene::ApplyClothConstraints(FNvClothInstanceRecord& Record, const FClothConstraintDesc& Constraints)
{
	FClothInstance& Instance = Record.Instance;
	if (!Record.Cloth)
	{
		return false;
	}

	if (!Constraints.MotionConstraints.empty() && Constraints.MotionConstraints.size() != Instance.NumParticles)
	{
		return false;
	}

	if (!Constraints.SeparationConstraints.empty() && Constraints.SeparationConstraints.size() != Instance.NumParticles)
	{
		return false;
	}

	if (Constraints.MotionConstraints.empty())
	{
		Record.Cloth->clearMotionConstraints();
	}
	else
	{
		nv::cloth::Range<physx::PxVec4> MotionRange = Record.Cloth->getMotionConstraints();
		if (MotionRange.size() != Constraints.MotionConstraints.size())
		{
			return false;
		}

		for (uint32 Index = 0; Index < Constraints.MotionConstraints.size(); ++Index)
		{
			MotionRange[Index] = ToPxMotionConstraint(Constraints.MotionConstraints[Index]);
		}
	}

	Record.Cloth->setMotionConstraintScaleBias(Constraints.MotionConstraintScale, Constraints.MotionConstraintBias);
	Record.Cloth->setMotionConstraintStiffness(Constraints.MotionConstraintStiffness);

	if (Constraints.SeparationConstraints.empty())
	{
		Record.Cloth->clearSeparationConstraints();
	}
	else
	{
		nv::cloth::Range<physx::PxVec4> SeparationRange = Record.Cloth->getSeparationConstraints();
		if (SeparationRange.size() != Constraints.SeparationConstraints.size())
		{
			return false;
		}

		for (uint32 Index = 0; Index < Constraints.SeparationConstraints.size(); ++Index)
		{
			SeparationRange[Index] = ToPxSeparationConstraint(Constraints.SeparationConstraints[Index]);
		}
	}

	Record.Cloth->clearInterpolation();
	return true;
}

bool FNvClothScene::ApplyClothCollision(FNvClothInstanceRecord& Record, const FClothCollisionDesc& Collision)
{
	if (!Record.Cloth || !ValidateCapsules(Collision.Capsules, static_cast<uint32>(Collision.Spheres.size())))
	{
		return false;
	}

	const uint32_t ExistingCapsules = Record.Cloth->getNumCapsules();
	if (ExistingCapsules > 0)
	{
		Record.Cloth->setCapsules(nv::cloth::Range<const uint32_t>(), 0, ExistingCapsules);
	}

	const uint32_t ExistingSpheres = Record.Cloth->getNumSpheres();

	Record.SphereScratch.clear();
	Record.SphereScratch.reserve(Collision.Spheres.size());
	for (const FClothCollisionSphere& Sphere : Collision.Spheres)
	{
		Record.SphereScratch.emplace_back(ToPxSphere(Sphere));
	}

	Record.Cloth->setSpheres(MakeConstRange(Record.SphereScratch), 0, ExistingSpheres);

	Record.CapsuleScratch.clear();
	Record.CapsuleScratch.reserve(Collision.Capsules.size() * 2);
	for (const FClothCollisionCapsule& Capsule : Collision.Capsules)
	{
		Record.CapsuleScratch.emplace_back(Capsule.SphereA);
		Record.CapsuleScratch.emplace_back(Capsule.SphereB);
	}

	if (!Record.CapsuleScratch.empty())
	{
		Record.Cloth->setCapsules(MakeConstRange(Record.CapsuleScratch), 0, 0);
	}

	const uint32_t ExistingConvexes = Record.Cloth->getNumConvexes();
	if (ExistingConvexes > 0)
	{
		Record.Cloth->setConvexes(nv::cloth::Range<const uint32_t>(), 0, ExistingConvexes);
	}

	const uint32_t ExistingPlanes = Record.Cloth->getNumPlanes();

	Record.PlaneScratch.clear();
	Record.PlaneScratch.reserve(Collision.Planes.size());
	for (const FClothCollisionPlane& Plane : Collision.Planes)
	{
		Record.PlaneScratch.emplace_back(ToPxPlane(Plane));
	}

	Record.Cloth->setPlanes(MakeConstRange(Record.PlaneScratch), 0, ExistingPlanes);

	Record.ConvexScratch.clear();
	Record.ConvexScratch.reserve(Collision.Convexes.size());
	for (const FClothCollisionConvex& Convex : Collision.Convexes)
	{
		if (Convex.PlaneMask != 0)
		{
			Record.ConvexScratch.emplace_back(Convex.PlaneMask);
		}
	}

	if (!Record.ConvexScratch.empty())
	{
		Record.Cloth->setConvexes(MakeConstRange(Record.ConvexScratch), 0, 0);
	}

	Record.Cloth->clearInterpolation();
	return true;
}

bool FNvClothScene::BuildCollisionFromRegisteredShapes(FNvClothInstanceRecord& Record, FClothCollisionDesc& OutCollision) const
{
	OutCollision = Record.UserCollision;
	if (!Record.Cloth)
	{
		return false;
	}

	FBoundingBox ClothBounds = BuildClothWorldBounds(Record);
	if (!HasBoundsPoints(ClothBounds))
	{
		return true;
	}

	ClothBounds = ExpandBounds(ClothBounds, 25.0f);
	const FMatrix ClothWorldInverse = Record.ClothWorldMatrix.GetInverse();
	const float RadiusScale = GetWorldToClothRadiusScale(Record.ClothWorldMatrix);

	for (UShapeComponent* Shape : ShapeColliders)
	{
		if (!Shape || !Shape->IsCollisionEnabled())
		{
			continue;
		}

		if (!BoundsIntersect(ClothBounds, Shape->GetWorldBoundingBox()))
		{
			continue;
		}

		AppendShapeColliderToClothCollision(Shape, ClothWorldInverse, RadiusScale, OutCollision);
	}

	return true;
}
