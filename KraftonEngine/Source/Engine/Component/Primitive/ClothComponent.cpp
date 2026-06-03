#include "Component/Primitive/ClothComponent.h"

#include "GameFramework/World.h"
#include "Physics/Cloth/IClothScene.h"
#include "Render/Proxy/ClothSceneProxy.h"

#include <algorithm>
#include <cstring>
#include <cmath>

namespace
{
bool IsFiniteVector(const FVector& Value)
{
	return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
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
}

UClothComponent::UClothComponent()
{
	bCastShadow = false;
	BuildPreviewRenderData();
}

UClothComponent::~UClothComponent()
{
	DestroyCloth();
}

void UClothComponent::BeginPlay()
{
	UMeshComponent::BeginPlay();

	if (bSimulateCloth)
	{
		RecreateCloth();
	}
}

void UClothComponent::EndPlay()
{
	DestroyCloth();
	UMeshComponent::EndPlay();
}

void UClothComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bSimulateCloth && ClothInstance)
	{
		ApplyClothSettings();
		if (UWorld* World = GetWorld())
		{
			if (IClothScene* Scene = World->GetClothScene())
			{
				Scene->SetClothWorldMatrix(ClothInstance, GetWorldMatrix(), DeltaTime);
			}
		}
	}
}

void UClothComponent::PostEditProperty(const char* PropertyName)
{
	UMeshComponent::PostEditProperty(PropertyName);

	if (!PropertyName)
	{
		return;
	}

	if (std::strcmp(PropertyName, "bRenderTwoSided") == 0 || std::strcmp(PropertyName, "Two Sided Cloth") == 0)
	{
		MarkProxyDirty(EDirtyFlag::Material);
		return;
	}

	if (std::strcmp(PropertyName, "RenderColor") == 0 || std::strcmp(PropertyName, "Cloth Color") == 0)
	{
		MarkProxyDirty(EDirtyFlag::Mesh);
		return;
	}

	MarkClothRenderDataDirty();
	if (bComponentHasBegunPlay && bSimulateCloth)
	{
		if (std::strcmp(PropertyName, "Gravity") == 0
			|| std::strcmp(PropertyName, "Damping") == 0
			|| std::strcmp(PropertyName, "SolverFrequency") == 0
			|| std::strcmp(PropertyName, "StiffnessFrequency") == 0
			|| std::strcmp(PropertyName, "bContinuousCollision") == 0
			|| std::strcmp(PropertyName, "PhaseStiffness") == 0
			|| std::strcmp(PropertyName, "TetherScale") == 0
			|| std::strcmp(PropertyName, "TetherStiffness") == 0
			|| std::strcmp(PropertyName, "Friction") == 0
			|| std::strcmp(PropertyName, "WindVelocity") == 0
			|| std::strcmp(PropertyName, "WindDragCoefficient") == 0
			|| std::strcmp(PropertyName, "WindLiftCoefficient") == 0)
		{
			ApplyClothSettings();
		}
		else
		{
			RecreateCloth();
		}
	}
}

FPrimitiveSceneProxy* UClothComponent::CreateSceneProxy()
{
	return new FClothSceneProxy(this);
}

FClothGridDesc UClothComponent::BuildGridDesc() const
{
	FClothGridDesc Desc;
	Desc.Name = GetName();
	Desc.NumColumns = static_cast<uint32>((std::max)(NumColumns, 2));
	Desc.NumRows = static_cast<uint32>((std::max)(NumRows, 2));
	Desc.Spacing = (std::max)(Spacing, 0.001f);
	Desc.Origin = ClothOrigin;
	Desc.AxisX = FVector::RightVector;
	Desc.AxisY = FVector::DownVector;
	Desc.PinMode = PinMode;
	Desc.Settings = BuildClothSettings();
	return Desc;
}

FClothSettings UClothComponent::BuildClothSettings() const
{
	FClothSettings Settings;
	Settings.Gravity = Gravity;
	Settings.Damping = (std::max)(Damping, 0.0f);
	Settings.SolverFrequency = (std::max)(SolverFrequency, 1.0f);
	Settings.StiffnessFrequency = (std::max)(StiffnessFrequency, 1.0f);
	Settings.bContinuousCollision = bContinuousCollision;
	Settings.PhaseStiffness = (std::min)((std::max)(PhaseStiffness, 0.0f), 1.0f);
	Settings.TetherScale = (std::max)(TetherScale, 0.0f);
	Settings.TetherStiffness = (std::min)((std::max)(TetherStiffness, 0.0f), 1.0f);
	Settings.Friction = (std::min)((std::max)(Friction, 0.0f), 1.0f);
	Settings.WindVelocity = ClampVectorLength(WindVelocity, 50.0f);
	Settings.DragCoefficient = (std::min)((std::max)(WindDragCoefficient, 0.0f), 0.05f);
	Settings.LiftCoefficient = (std::min)((std::max)(WindLiftCoefficient, 0.0f), 0.02f);
	Settings.LinearInertia = 0.0f;
	Settings.AngularInertia = 0.0f;
	Settings.CentrifugalInertia = 0.0f;
	return Settings;
}

bool UClothComponent::ApplyClothSettings()
{
	UWorld* World = GetWorld();
	IClothScene* Scene = World ? World->GetClothScene() : nullptr;
	if (!Scene || !ClothInstance)
	{
		return false;
	}

	return Scene->SetClothSettings(ClothInstance, BuildClothSettings());
}

bool UClothComponent::RecreateCloth()
{
	DestroyCloth();

	UWorld* World = GetWorld();
	IClothScene* Scene = World ? World->GetClothScene() : nullptr;
	if (!Scene)
	{
		return false;
	}

	ClothFabric = {};
	ClothInstance = Scene->CreateGridCloth(BuildGridDesc(), &ClothFabric);
	if (!ClothInstance)
	{
		ClothFabric = {};
		return false;
	}

	CachedRenderData.Reset();
	CachedMeshData.Vertices.clear();
	CachedMeshData.Indices.clear();
	Scene->SetClothWorldMatrix(ClothInstance, GetWorldMatrix());
	ApplyClothSettings();
	MarkWorldBoundsDirty();
	MarkProxyDirty(EDirtyFlag::Mesh);
	return true;
}

void UClothComponent::DestroyCloth()
{
	UWorld* World = GetWorld();
	IClothScene* Scene = World ? World->GetClothScene() : nullptr;
	if (Scene)
	{
		if (ClothInstance)
		{
			Scene->DestroyClothInstance(ClothInstance);
		}
		if (ClothFabric.IsValid())
		{
			Scene->DestroyClothFabric(ClothFabric);
		}
	}

	ClothInstance = nullptr;
	ClothFabric = {};
}

bool UClothComponent::GetClothRenderData(FClothRenderData& OutRenderData) const
{
	UWorld* World = GetWorld();
	IClothScene* Scene = World ? World->GetClothScene() : nullptr;
	if (Scene && ClothInstance)
	{
		FClothRenderData CurrentRenderData;
		if (Scene->GetClothRenderData(ClothInstance, CurrentRenderData) && IsValidClothRenderData(CurrentRenderData))
		{
			CachedRenderData = CurrentRenderData;
			UpdateCachedMeshDataFromRenderData(CachedRenderData);
			OutRenderData = CachedRenderData;
			return true;
		}

		if (IsValidClothRenderData(CachedRenderData))
		{
			UpdateCachedMeshDataFromRenderData(CachedRenderData);
			OutRenderData = CachedRenderData;
			return true;
		}
	}

	BuildPreviewRenderData();
	OutRenderData = CachedRenderData;
	return !OutRenderData.Vertices.empty() && !OutRenderData.Indices.empty();
}

bool UClothComponent::GetClothStats(FClothStats& OutStats) const
{
	UWorld* World = GetWorld();
	IClothScene* Scene = World ? World->GetClothScene() : nullptr;
	if (!Scene || !ClothInstance)
	{
		OutStats = {};
		return false;
	}

	Scene->GetClothStats(ClothInstance, OutStats);
	return OutStats.NumCloths > 0;
}

bool UClothComponent::ExtractClothDebugLines(TArray<FPhysicsDebugLine>& OutLines, const FClothDebugDrawOptions& Options) const
{
	UWorld* World = GetWorld();
	IClothScene* Scene = World ? World->GetClothScene() : nullptr;
	if (!Scene || !ClothInstance)
	{
		return false;
	}

	const size_t OldCount = OutLines.size();
	TArray<FPhysicsDebugLine> LocalLines;
	Scene->ExtractClothDebugLines(ClothInstance, LocalLines, Options);
	if (LocalLines.empty())
	{
		return false;
	}

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

const TMeshData<FVertex>& UClothComponent::GetCachedMeshData() const
{
	BuildPreviewRenderData();
	return CachedMeshData;
}

FMeshDataView UClothComponent::GetMeshDataView() const
{
	FClothRenderData RenderData;
	GetClothRenderData(RenderData);
	return FMeshDataView::FromMeshData(CachedMeshData);
}

void UClothComponent::UpdateWorldAABB() const
{
	FClothRenderData RenderData;
	GetClothRenderData(RenderData);

	const FMatrix WorldMatrix = GetWorldMatrix();
	bool bHasPoint = false;
	FVector Min;
	FVector Max;

	for (const FVertex& Vertex : CachedMeshData.Vertices)
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
			Min = WorldPos;
			Max = WorldPos;
			bHasPoint = true;
		}
		else
		{
			Min.X = (std::min)(Min.X, WorldPos.X);
			Min.Y = (std::min)(Min.Y, WorldPos.Y);
			Min.Z = (std::min)(Min.Z, WorldPos.Z);
			Max.X = (std::max)(Max.X, WorldPos.X);
			Max.Y = (std::max)(Max.Y, WorldPos.Y);
			Max.Z = (std::max)(Max.Z, WorldPos.Z);
		}
	}

	if (!bHasPoint)
	{
		UPrimitiveComponent::UpdateWorldAABB();
		return;
	}

	WorldAABBMinLocation = Min;
	WorldAABBMaxLocation = Max;
	bWorldAABBDirty = false;
	bHasValidWorldAABB = true;
}

void UClothComponent::BuildPreviewRenderData() const
{
	if (!bCachedPreviewDirty && !CachedRenderData.Vertices.empty() && !CachedRenderData.Indices.empty())
	{
		return;
	}

	const FClothGridDesc Desc = BuildGridDesc();
	const uint32 Columns = (std::max)(Desc.NumColumns, 2u);
	const uint32 Rows = (std::max)(Desc.NumRows, 2u);

	CachedRenderData.Reset();
	CachedRenderData.Vertices.reserve(static_cast<size_t>(Columns) * Rows);
	CachedRenderData.Indices.reserve(static_cast<size_t>(Columns - 1) * (Rows - 1) * 6);

	for (uint32 Row = 0; Row < Rows; ++Row)
	{
		for (uint32 Col = 0; Col < Columns; ++Col)
		{
			FClothRenderVertex Vertex;
			Vertex.Position = Desc.Origin + Desc.AxisX * (static_cast<float>(Col) * Desc.Spacing)
				+ Desc.AxisY * (static_cast<float>(Row) * Desc.Spacing);
			Vertex.Normal = FVector::UpVector;
			Vertex.UV = FVector2(
				Columns > 1 ? static_cast<float>(Col) / static_cast<float>(Columns - 1) : 0.0f,
				Rows > 1 ? static_cast<float>(Row) / static_cast<float>(Rows - 1) : 0.0f);
			CachedRenderData.Vertices.push_back(Vertex);
		}
	}

	for (uint32 Row = 0; Row + 1 < Rows; ++Row)
	{
		for (uint32 Col = 0; Col + 1 < Columns; ++Col)
		{
			const uint32 I0 = Row * Columns + Col;
			const uint32 I1 = I0 + 1;
			const uint32 I2 = I0 + Columns;
			const uint32 I3 = I2 + 1;

			CachedRenderData.Indices.push_back(I0);
			CachedRenderData.Indices.push_back(I2);
			CachedRenderData.Indices.push_back(I1);
			CachedRenderData.Indices.push_back(I1);
			CachedRenderData.Indices.push_back(I2);
			CachedRenderData.Indices.push_back(I3);
		}
	}

	UpdateCachedMeshDataFromRenderData(CachedRenderData);
	bCachedPreviewDirty = false;
}

void UClothComponent::UpdateCachedMeshDataFromRenderData(const FClothRenderData& RenderData) const
{
	CachedMeshData.Vertices.resize(RenderData.Vertices.size());
	for (size_t Index = 0; Index < RenderData.Vertices.size(); ++Index)
	{
		CachedMeshData.Vertices[Index].Position = RenderData.Vertices[Index].Position;
		CachedMeshData.Vertices[Index].Color = RenderColor;
		CachedMeshData.Vertices[Index].SubID = 0;
	}
	CachedMeshData.Indices = RenderData.Indices;
}

void UClothComponent::MarkClothRenderDataDirty()
{
	bCachedPreviewDirty = true;
	CachedRenderData.Reset();
	CachedMeshData.Vertices.clear();
	CachedMeshData.Indices.clear();
	MarkWorldBoundsDirty();
	MarkProxyDirty(EDirtyFlag::Mesh);
}
