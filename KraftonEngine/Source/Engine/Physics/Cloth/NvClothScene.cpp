#include "Physics/Cloth/NvClothScene.h"

#include "NvCloth/Callbacks.h"
#include "NvCloth/Cloth.h"
#include "NvCloth/Fabric.h"
#include "NvCloth/Factory.h"
#include "NvCloth/PhaseConfig.h"
#include "NvCloth/Solver.h"
#include "NvClothExt/ClothFabricCooker.h"
#include "NvClothExt/ClothMeshDesc.h"
#include "foundation/PxAllocatorCallback.h"
#include "foundation/PxErrorCallback.h"
#include "foundation/PxVec3.h"
#include "foundation/PxVec4.h"

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

physx::PxVec3 ToPxVec3(const FVector& Value)
{
	return physx::PxVec3(Value.X, Value.Y, Value.Z);
}

physx::PxVec4 ToPxParticle(const FClothParticle& Particle)
{
	return physx::PxVec4(Particle.Position.X, Particle.Position.Y, Particle.Position.Z, Particle.InvMass);
}

physx::PxVec4 ToPxSphere(const FClothCollisionSphere& Sphere)
{
	return physx::PxVec4(Sphere.Center.X, Sphere.Center.Y, Sphere.Center.Z, Sphere.Radius);
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
			const bool bPinned = Desc.bPinTopRow && Row == 0;

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
	nv::cloth::Cloth* Cloth = nullptr;
	FNvClothFabricRecord* FabricRecord = nullptr;
	TArray<nv::cloth::PhaseConfig> PhaseConfigs;
	TArray<physx::PxVec4> SphereScratch;
	TArray<uint32_t> CapsuleScratch;
};

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

	if (Backend != EClothBackend::CPU)
	{
		return false;
	}

	Factory = NvClothCreateFactoryCPU();
	if (!Factory)
	{
		return false;
	}

	Solver = Factory->createSolver();
	if (!Solver)
	{
		NvClothDestroyFactory(Factory);
		Factory = nullptr;
		return false;
	}

	Stats = FClothStats();
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
		NvClothDestroyFactory(Factory);
		Factory = nullptr;
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
	if (!Record || !Record->Cloth || !ValidateCapsules(Instance->Collision.Capsules, static_cast<uint32>(Spheres.size())))
	{
		return false;
	}

	FClothCollisionDesc Collision = Instance->Collision;
	Collision.Spheres = Spheres;

	if (!ApplyClothCollision(*Record, Collision))
	{
		return false;
	}

	Instance->Collision = Collision;
	return true;
}

bool FNvClothScene::SetClothCollisionCapsules(FClothInstance* Instance, const TArray<FClothCollisionCapsule>& Capsules)
{
	FNvClothInstanceRecord* Record = FindInstanceRecord(Instance);
	if (!Record || !Record->Cloth || !ValidateCapsules(Capsules, static_cast<uint32>(Instance->Collision.Spheres.size())))
	{
		return false;
	}

	FClothCollisionDesc Collision = Instance->Collision;
	Collision.Capsules = Capsules;

	if (!ApplyClothCollision(*Record, Collision))
	{
		return false;
	}

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

	Instance->Collision = Collision;
	return true;
}

void FNvClothScene::SimulateCloth(float DeltaTime)
{
	if (!Solver || DeltaTime <= 0.0f)
	{
		return;
	}

	DeltaTime = std::min(DeltaTime, 1.0f / 30.0f);
	const auto StartTime = std::chrono::high_resolution_clock::now();

	if (Solver->beginSimulation(DeltaTime))
	{
		const int ChunkCount = Solver->getSimulationChunkCount();
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
	OutStats.NumCloths = static_cast<uint32>(Instances.size());
	OutStats.NumParticles = 0;

	for (const std::unique_ptr<FNvClothInstanceRecord>& Record : Instances)
	{
		if (Record)
		{
			OutStats.NumParticles += Record->Instance.NumParticles;
		}
	}

	if (Solver)
	{
		OutStats.bSolverError = Solver->hasError();
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

	Record.Cloth->clearInterpolation();
	return true;
}
