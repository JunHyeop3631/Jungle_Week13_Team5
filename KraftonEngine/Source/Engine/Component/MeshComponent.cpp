#include "Component/MeshComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "GameFramework/World.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Physics/Cloth/IClothScene.h"

HIDE_FROM_COMPONENT_LIST(UMeshComponent)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>

namespace
{
bool IsFiniteVector(const FVector& Value)
{
	return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
}

bool IsFiniteVector2(const FVector2& Value)
{
	return std::isfinite(Value.X) && std::isfinite(Value.Y);
}

struct FMeshClothWeldKey
{
	int64 X = 0;
	int64 Y = 0;
	int64 Z = 0;

	bool operator==(const FMeshClothWeldKey& Other) const
	{
		return X == Other.X && Y == Other.Y && Z == Other.Z;
	}
};

struct FMeshClothWeldKeyHash
{
	size_t operator()(const FMeshClothWeldKey& Key) const
	{
		uint64 Hash = 1469598103934665603ull;
		auto Mix = [&Hash](int64 Value)
		{
			Hash ^= static_cast<uint64>(Value);
			Hash *= 1099511628211ull;
		};

		Mix(Key.X);
		Mix(Key.Y);
		Mix(Key.Z);
		return static_cast<size_t>(Hash);
	}
};

int64 QuantizeMeshClothCoordinate(float Value, float Tolerance)
{
	return static_cast<int64>(std::llround(static_cast<double>(Value) / static_cast<double>(Tolerance)));
}

FMeshClothWeldKey MakeMeshClothWeldKey(const FVector& Position, float Tolerance)
{
	return {
		QuantizeMeshClothCoordinate(Position.X, Tolerance),
		QuantizeMeshClothCoordinate(Position.Y, Tolerance),
		QuantizeMeshClothCoordinate(Position.Z, Tolerance)
	};
}

void ExpandBounds(FVector& Min, FVector& Max, const FVector& Position)
{
	Min.X = (std::min)(Min.X, Position.X);
	Min.Y = (std::min)(Min.Y, Position.Y);
	Min.Z = (std::min)(Min.Z, Position.Z);
	Max.X = (std::max)(Max.X, Position.X);
	Max.Y = (std::max)(Max.Y, Position.Y);
	Max.Z = (std::max)(Max.Z, Position.Z);
}

float GetAxisValue(const FVector& Position, uint32 Axis)
{
	switch (Axis)
	{
	case 0:
		return Position.X;
	case 1:
		return Position.Y;
	default:
		return Position.Z;
	}
}

FVector2 BuildPlanarMeshClothUV(const FVector& Position, const FVector& Min, const FVector& Max)
{
	const float Extents[3] = {
		(std::max)(0.0f, Max.X - Min.X),
		(std::max)(0.0f, Max.Y - Min.Y),
		(std::max)(0.0f, Max.Z - Min.Z)
	};
	uint32 AxisU = 0;
	uint32 AxisV = 1;
	for (uint32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Extents[Axis] > Extents[AxisU])
		{
			AxisV = AxisU;
			AxisU = Axis;
		}
		else if (Axis != AxisU && Extents[Axis] > Extents[AxisV])
		{
			AxisV = Axis;
		}
	}

	const float MinValues[3] = { Min.X, Min.Y, Min.Z };
	const float U = Extents[AxisU] > 1.0e-6f ? (GetAxisValue(Position, AxisU) - MinValues[AxisU]) / Extents[AxisU] : 0.0f;
	const float V = Extents[AxisV] > 1.0e-6f ? (GetAxisValue(Position, AxisV) - MinValues[AxisV]) / Extents[AxisV] : 0.0f;
	return FVector2(U, V);
}

FVector ClampVectorLength(const FVector& Value, float MaxLength)
{
	if (!IsFiniteVector(Value))
	{
		return FVector::ZeroVector;
	}

	const float LengthSquared = Value.LengthSquared();
	if (LengthSquared <= MaxLength * MaxLength)
	{
		return Value;
	}

	const float Length = std::sqrt(LengthSquared);
	return Length > 1.0e-6f ? Value * (MaxLength / Length) : FVector::ZeroVector;
}

float AxisTolerance(float MinValue, float MaxValue, float Ratio)
{
	const float Extent = (std::max)(0.0f, MaxValue - MinValue);
	return (std::max)(0.001f, Extent * (std::max)(0.0f, Ratio));
}

bool IsNearMin(float Value, float MinValue, float MaxValue, float Ratio)
{
	return Value <= MinValue + AxisTolerance(MinValue, MaxValue, Ratio);
}

bool IsNearMax(float Value, float MinValue, float MaxValue, float Ratio)
{
	return Value >= MaxValue - AxisTolerance(MinValue, MaxValue, Ratio);
}

bool IsMeshClothParticlePinned(const FVector& Position, const FVector& Min, const FVector& Max,
	EClothPinMode PinMode, float ToleranceRatio)
{
	const bool bTop = IsNearMax(Position.Z, Min.Z, Max.Z, ToleranceRatio);
	const bool bBottom = IsNearMin(Position.Z, Min.Z, Max.Z, ToleranceRatio);
	const bool bLeft = IsNearMin(Position.Y, Min.Y, Max.Y, ToleranceRatio);
	const bool bRight = IsNearMax(Position.Y, Min.Y, Max.Y, ToleranceRatio);

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

bool IsValidClothRenderData(const FClothRenderData& RenderData)
{
	if (RenderData.Vertices.empty() || RenderData.Indices.empty())
	{
		return false;
	}

	for (const FClothRenderVertex& Vertex : RenderData.Vertices)
	{
		if (!IsFiniteVector(Vertex.Position) || Vertex.Position.LengthSquared() > 1.0e12f)
		{
			return false;
		}
	}

	return true;
}

void RecomputeClothRenderNormals(FClothRenderData& RenderData)
{
	for (FClothRenderVertex& Vertex : RenderData.Vertices)
	{
		Vertex.Normal = FVector::ZeroVector;
	}

	for (uint32 Index = 0; Index + 2 < RenderData.Indices.size(); Index += 3)
	{
		const uint32 I0 = RenderData.Indices[Index + 0];
		const uint32 I1 = RenderData.Indices[Index + 1];
		const uint32 I2 = RenderData.Indices[Index + 2];
		if (I0 >= RenderData.Vertices.size() || I1 >= RenderData.Vertices.size() || I2 >= RenderData.Vertices.size())
		{
			continue;
		}

		const FVector& P0 = RenderData.Vertices[I0].Position;
		const FVector& P1 = RenderData.Vertices[I1].Position;
		const FVector& P2 = RenderData.Vertices[I2].Position;
		FVector FaceNormal = (P1 - P0).Cross(P2 - P0);
		if (FaceNormal.LengthSquared() <= 1.0e-8f)
		{
			continue;
		}

		FaceNormal.Normalize();
		RenderData.Vertices[I0].Normal += FaceNormal;
		RenderData.Vertices[I1].Normal += FaceNormal;
		RenderData.Vertices[I2].Normal += FaceNormal;
	}

	for (FClothRenderVertex& Vertex : RenderData.Vertices)
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
}
}

UMeshComponent::~UMeshComponent()
{
	DestroyMeshCloth();
}

void UMeshComponent::BeginPlay()
{
	UPrimitiveComponent::BeginPlay();

	if (bSimulateMeshCloth)
	{
		RecreateMeshCloth();
	}
}

void UMeshComponent::EndPlay()
{
	DestroyMeshCloth();
	UPrimitiveComponent::EndPlay();
}

void UMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UPrimitiveComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bSimulateMeshCloth && MeshClothInstance)
	{
		ApplyMeshClothSettings();
		if (UWorld* World = GetWorld())
		{
			if (IClothScene* ClothScene = World->GetClothScene())
			{
				ClothScene->SetClothWorldMatrix(MeshClothInstance, GetWorldMatrix(), DeltaTime);
			}
		}
		MarkWorldBoundsDirty();
	}
}

void UMeshComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);
	if (!PropertyName)
	{
		return;
	}

	const bool bClothToggleChanged =
		std::strcmp(PropertyName, "bSimulateMeshCloth") == 0 ||
		std::strcmp(PropertyName, "Simulate Mesh Cloth") == 0;

	const bool bClothShapeChanged =
		bClothToggleChanged ||
		std::strcmp(PropertyName, "MeshClothPinMode") == 0 ||
		std::strcmp(PropertyName, "Mesh Cloth Pin Mode") == 0 ||
		std::strcmp(PropertyName, "MeshClothPinTolerance") == 0 ||
		std::strcmp(PropertyName, "Mesh Cloth Pin Tolerance") == 0 ||
		std::strcmp(PropertyName, "MeshClothWeldTolerance") == 0 ||
		std::strcmp(PropertyName, "Mesh Cloth Weld Tolerance") == 0;

	const bool bClothSettingsChanged =
		std::strcmp(PropertyName, "MeshClothGravity") == 0 ||
		std::strcmp(PropertyName, "Mesh Cloth Gravity") == 0 ||
		std::strcmp(PropertyName, "MeshClothDamping") == 0 ||
		std::strcmp(PropertyName, "Mesh Cloth Damping") == 0 ||
		std::strcmp(PropertyName, "MeshClothWindVelocity") == 0 ||
		std::strcmp(PropertyName, "Mesh Cloth Wind Velocity") == 0 ||
		std::strcmp(PropertyName, "MeshClothWindDragCoefficient") == 0 ||
		std::strcmp(PropertyName, "Mesh Cloth Wind Drag") == 0 ||
		std::strcmp(PropertyName, "MeshClothWindLiftCoefficient") == 0 ||
		std::strcmp(PropertyName, "Mesh Cloth Wind Lift") == 0;

	const bool bClothRenderChanged =
		std::strcmp(PropertyName, "MeshClothRenderColor") == 0 ||
		std::strcmp(PropertyName, "Mesh Cloth Color") == 0 ||
		std::strcmp(PropertyName, "bMeshClothTwoSided") == 0 ||
		std::strcmp(PropertyName, "Mesh Cloth Two Sided") == 0;

	if (bClothShapeChanged)
	{
		ResetMeshClothCache();
		if (bComponentHasBegunPlay)
		{
			if (bSimulateMeshCloth)
			{
				RecreateMeshCloth();
			}
			else
			{
				DestroyMeshCloth();
			}
		}
		MarkRenderStateDirty();
		MarkWorldBoundsDirty();
		return;
	}

	if (bClothSettingsChanged)
	{
		if (bComponentHasBegunPlay && MeshClothInstance)
		{
			ApplyMeshClothSettings();
		}
		return;
	}

	if (bClothRenderChanged)
	{
		MarkProxyDirty(EDirtyFlag::Material);
		MarkProxyDirty(EDirtyFlag::Mesh);
	}
}

FClothSettings UMeshComponent::BuildMeshClothSettings() const
{
	FClothSettings Settings;
	Settings.Gravity = MeshClothGravity;
	Settings.Damping = (std::max)(MeshClothDamping, 0.0f);
	Settings.WindVelocity = ClampVectorLength(MeshClothWindVelocity, 50.0f);
	Settings.DragCoefficient = (std::min)((std::max)(MeshClothWindDragCoefficient, 0.0f), 0.05f);
	Settings.LiftCoefficient = (std::min)((std::max)(MeshClothWindLiftCoefficient, 0.0f), 0.02f);
	return Settings;
}

bool UMeshComponent::BuildMeshClothDescriptions(FClothFabricDesc& OutFabricDesc, FClothInstanceDesc& OutInstanceDesc) const
{
	const FMeshDataView View = GetMeshDataView();
	if (!View.IsValid() || View.VertexCount == 0 || !View.IndexData || !View.HasPosition())
	{
		return false;
	}

	const uint32 InvalidIndex = (std::numeric_limits<uint32>::max)();
	const float RequestedWeldTolerance = (std::max)(MeshClothWeldTolerance, 0.0f);
	const bool bUseWelding = RequestedWeldTolerance > 0.0f;
	const float WeldTolerance = bUseWelding ? (std::max)(RequestedWeldTolerance, 1.0e-6f) : 0.0f;
	const bool bHasSourceUV = View.HasUV();

	TArray<uint32> SourceToWelded;
	SourceToWelded.resize(View.VertexCount, InvalidIndex);

	TArray<FClothParticle> WeldedParticles;
	TArray<FVector2> WeldedUVs;
	WeldedParticles.reserve(View.VertexCount);
	WeldedUVs.reserve(View.VertexCount);

	std::unordered_map<FMeshClothWeldKey, uint32, FMeshClothWeldKeyHash> WeldMap;
	if (bUseWelding)
	{
		WeldMap.reserve(View.VertexCount);
	}

	auto GetOrAddWeldedParticle = [&](uint32 SourceIndex, uint32& OutParticleIndex) -> bool
	{
		if (SourceIndex >= View.VertexCount)
		{
			return false;
		}

		uint32& CachedIndex = SourceToWelded[SourceIndex];
		if (CachedIndex != InvalidIndex)
		{
			OutParticleIndex = CachedIndex;
			return true;
		}

		const FVector& Position = View.GetPosition(SourceIndex);
		if (!IsFiniteVector(Position))
		{
			return false;
		}

		if (bUseWelding)
		{
			const FMeshClothWeldKey Key = MakeMeshClothWeldKey(Position, WeldTolerance);
			const auto Existing = WeldMap.find(Key);
			if (Existing != WeldMap.end())
			{
				CachedIndex = Existing->second;
				OutParticleIndex = CachedIndex;
				return true;
			}
		}

		FClothParticle Particle;
		Particle.Position = Position;
		Particle.InvMass = 1.0f;

		FVector2 UV = bHasSourceUV ? View.GetUV(SourceIndex) : FVector2();
		if (!IsFiniteVector2(UV))
		{
			UV = FVector2();
		}

		const uint32 NewParticleIndex = static_cast<uint32>(WeldedParticles.size());
		WeldedParticles.push_back(Particle);
		WeldedUVs.push_back(UV);

		if (bUseWelding)
		{
			WeldMap.emplace(MakeMeshClothWeldKey(Position, WeldTolerance), NewParticleIndex);
		}

		CachedIndex = NewParticleIndex;
		OutParticleIndex = NewParticleIndex;
		return true;
	};

	TArray<uint32> RemappedIndices;
	RemappedIndices.reserve(View.IndexCount);

	for (uint32 Index = 0; Index + 2 < View.IndexCount; Index += 3)
	{
		const uint32 SourceI0 = View.IndexData[Index + 0];
		const uint32 SourceI1 = View.IndexData[Index + 1];
		const uint32 SourceI2 = View.IndexData[Index + 2];

		uint32 I0 = InvalidIndex;
		uint32 I1 = InvalidIndex;
		uint32 I2 = InvalidIndex;
		if (!GetOrAddWeldedParticle(SourceI0, I0) ||
			!GetOrAddWeldedParticle(SourceI1, I1) ||
			!GetOrAddWeldedParticle(SourceI2, I2))
		{
			return false;
		}

		if (I0 == I1 || I1 == I2 || I2 == I0)
		{
			continue;
		}

		RemappedIndices.push_back(I0);
		RemappedIndices.push_back(I1);
		RemappedIndices.push_back(I2);
	}

	if (WeldedParticles.size() < 3 || RemappedIndices.empty())
	{
		return false;
	}

	TArray<uint32> CompactRemap;
	CompactRemap.resize(WeldedParticles.size(), InvalidIndex);

	TArray<FClothParticle> CompactParticles;
	TArray<FVector2> CompactUVs;
	CompactParticles.reserve(WeldedParticles.size());
	CompactUVs.reserve(WeldedUVs.size());

	for (uint32& Index : RemappedIndices)
	{
		if (Index >= WeldedParticles.size())
		{
			return false;
		}

		uint32& CompactIndex = CompactRemap[Index];
		if (CompactIndex == InvalidIndex)
		{
			CompactIndex = static_cast<uint32>(CompactParticles.size());
			CompactParticles.push_back(WeldedParticles[Index]);
			CompactUVs.push_back(Index < WeldedUVs.size() ? WeldedUVs[Index] : FVector2());
		}
		Index = CompactIndex;
	}

	if (CompactParticles.size() < 3)
	{
		return false;
	}

	FVector Min = CompactParticles[0].Position;
	FVector Max = Min;
	for (const FClothParticle& Particle : CompactParticles)
	{
		ExpandBounds(Min, Max, Particle.Position);
	}

	for (uint32 Index = 0; Index < CompactParticles.size(); ++Index)
	{
		FClothParticle& Particle = CompactParticles[Index];
		Particle.InvMass = IsMeshClothParticlePinned(Particle.Position, Min, Max, MeshClothPinMode, MeshClothPinTolerance)
			? 0.0f
			: 1.0f;

		if (!bHasSourceUV && Index < CompactUVs.size())
		{
			CompactUVs[Index] = BuildPlanarMeshClothUV(Particle.Position, Min, Max);
		}
	}

	OutFabricDesc = FClothFabricDesc();
	OutFabricDesc.Name = GetName() + "_MeshClothFabric";
	OutFabricDesc.Particles = std::move(CompactParticles);
	OutFabricDesc.UVs = std::move(CompactUVs);
	OutFabricDesc.Indices = std::move(RemappedIndices);

	OutInstanceDesc = FClothInstanceDesc();
	OutInstanceDesc.Name = GetName() + "_MeshCloth";
	OutInstanceDesc.InitialParticles = OutFabricDesc.Particles;
	OutInstanceDesc.Settings = BuildMeshClothSettings();
	return true;
}

bool UMeshComponent::RecreateMeshCloth()
{
	DestroyMeshCloth();

	UWorld* World = GetWorld();
	IClothScene* ClothScene = World ? World->GetClothScene() : nullptr;
	if (!ClothScene)
	{
		return false;
	}

	FClothFabricDesc FabricDesc;
	FClothInstanceDesc InstanceDesc;
	if (!BuildMeshClothDescriptions(FabricDesc, InstanceDesc))
	{
		return false;
	}

	MeshClothFabric = ClothScene->CreateClothFabric(FabricDesc);
	if (!MeshClothFabric.IsValid())
	{
		return false;
	}

	InstanceDesc.Fabric = MeshClothFabric;
	MeshClothInstance = ClothScene->CreateClothInstance(InstanceDesc);
	if (!MeshClothInstance || !MeshClothInstance->bValid)
	{
		DestroyMeshCloth();
		return false;
	}

	ResetMeshClothCache();
	ClothScene->SetClothWorldMatrix(MeshClothInstance, GetWorldMatrix());
	ApplyMeshClothSettings();
	MarkWorldBoundsDirty();
	MarkRenderStateDirty();
	return true;
}

void UMeshComponent::DestroyMeshCloth()
{
	const bool bHadCloth = MeshClothInstance != nullptr || MeshClothFabric.IsValid();

	UWorld* World = GetWorld();
	IClothScene* ClothScene = World ? World->GetClothScene() : nullptr;
	if (ClothScene)
	{
		if (MeshClothInstance)
		{
			ClothScene->DestroyClothInstance(MeshClothInstance);
		}
		if (MeshClothFabric.IsValid())
		{
			ClothScene->DestroyClothFabric(MeshClothFabric);
		}
	}

	MeshClothInstance = nullptr;
	MeshClothFabric = {};
	ResetMeshClothCache();

	if (bHadCloth)
	{
		MarkWorldBoundsDirty();
		MarkRenderStateDirty();
	}
}

bool UMeshComponent::ApplyMeshClothSettings()
{
	UWorld* World = GetWorld();
	IClothScene* ClothScene = World ? World->GetClothScene() : nullptr;
	if (!ClothScene || !MeshClothInstance)
	{
		return false;
	}

	return ClothScene->SetClothSettings(MeshClothInstance, BuildMeshClothSettings());
}

bool UMeshComponent::BuildMeshClothPreviewRenderData(FClothRenderData& OutRenderData) const
{
	FClothFabricDesc FabricDesc;
	FClothInstanceDesc InstanceDesc;
	if (!BuildMeshClothDescriptions(FabricDesc, InstanceDesc))
	{
		OutRenderData.Reset();
		return false;
	}

	OutRenderData.Reset();
	OutRenderData.Vertices.reserve(FabricDesc.Particles.size());
	for (size_t Index = 0; Index < FabricDesc.Particles.size(); ++Index)
	{
		FClothRenderVertex Vertex;
		Vertex.Position = FabricDesc.Particles[Index].Position;
		Vertex.UV = Index < FabricDesc.UVs.size() ? FabricDesc.UVs[Index] : FVector2();
		OutRenderData.Vertices.push_back(Vertex);
	}
	OutRenderData.Indices = FabricDesc.Indices;
	RecomputeClothRenderNormals(OutRenderData);
	return IsValidClothRenderData(OutRenderData);
}

bool UMeshComponent::GetClothRenderData(FClothRenderData& OutRenderData) const
{
	UWorld* World = GetWorld();
	IClothScene* ClothScene = World ? World->GetClothScene() : nullptr;
	if (ClothScene && MeshClothInstance)
	{
		FClothRenderData CurrentRenderData;
		if (ClothScene->GetClothRenderData(MeshClothInstance, CurrentRenderData) && IsValidClothRenderData(CurrentRenderData))
		{
			CachedMeshClothRenderData = CurrentRenderData;
			OutRenderData = CachedMeshClothRenderData;
			return true;
		}
	}

	if (IsValidClothRenderData(CachedMeshClothRenderData))
	{
		OutRenderData = CachedMeshClothRenderData;
		return true;
	}

	if (BuildMeshClothPreviewRenderData(CachedMeshClothRenderData))
	{
		OutRenderData = CachedMeshClothRenderData;
		return true;
	}

	OutRenderData.Reset();
	return false;
}

bool UMeshComponent::GetClothStats(FClothStats& OutStats) const
{
	OutStats = FClothStats();
	UWorld* World = GetWorld();
	IClothScene* ClothScene = World ? World->GetClothScene() : nullptr;
	if (!ClothScene || !MeshClothInstance)
	{
		return false;
	}

	ClothScene->GetClothStats(MeshClothInstance, OutStats);
	return OutStats.NumCloths > 0;
}

bool UMeshComponent::ExtractClothDebugLines(TArray<FPhysicsDebugLine>& OutLines, const FClothDebugDrawOptions& Options) const
{
	UWorld* World = GetWorld();
	IClothScene* ClothScene = World ? World->GetClothScene() : nullptr;
	if (!ClothScene || !MeshClothInstance)
	{
		return false;
	}

	TArray<FPhysicsDebugLine> LocalLines;
	ClothScene->ExtractClothDebugLines(MeshClothInstance, LocalLines, Options);
	if (LocalLines.empty())
	{
		return false;
	}

	const size_t OldCount = OutLines.size();
	const FMatrix WorldMatrix = GetWorldMatrix();
	OutLines.reserve(OutLines.size() + LocalLines.size());
	for (const FPhysicsDebugLine& LocalLine : LocalLines)
	{
		FPhysicsDebugLine WorldLine;
		WorldLine.Start = WorldMatrix.TransformPositionWithW(LocalLine.Start);
		WorldLine.End = WorldMatrix.TransformPositionWithW(LocalLine.End);
		WorldLine.Color = LocalLine.Color;
		OutLines.push_back(WorldLine);
	}

	return OutLines.size() != OldCount;
}

bool UMeshComponent::UpdateMeshClothWorldAABB() const
{
	FClothRenderData RenderData;
	if (!GetClothRenderData(RenderData) || RenderData.Vertices.empty())
	{
		return false;
	}

	const FMatrix& WorldMatrix = CachedWorldMatrix;
	bool bHasPoint = false;
	FVector WorldMin;
	FVector WorldMax;

	for (const FClothRenderVertex& Vertex : RenderData.Vertices)
	{
		if (!IsFiniteVector(Vertex.Position))
		{
			continue;
		}

		const FVector WorldPos = WorldMatrix.TransformPositionWithW(Vertex.Position);
		if (!IsFiniteVector(WorldPos))
		{
			continue;
		}

		if (!bHasPoint)
		{
			WorldMin = WorldPos;
			WorldMax = WorldPos;
			bHasPoint = true;
		}
		else
		{
			WorldMin.X = (std::min)(WorldMin.X, WorldPos.X);
			WorldMin.Y = (std::min)(WorldMin.Y, WorldPos.Y);
			WorldMin.Z = (std::min)(WorldMin.Z, WorldPos.Z);
			WorldMax.X = (std::max)(WorldMax.X, WorldPos.X);
			WorldMax.Y = (std::max)(WorldMax.Y, WorldPos.Y);
			WorldMax.Z = (std::max)(WorldMax.Z, WorldPos.Z);
		}
	}

	if (!bHasPoint)
	{
		return false;
	}

	WorldAABBMinLocation = WorldMin;
	WorldAABBMaxLocation = WorldMax;
	bWorldAABBDirty = false;
	bHasValidWorldAABB = true;
	return true;
}

void UMeshComponent::ResetMeshClothCache() const
{
	CachedMeshClothRenderData.Reset();
}
