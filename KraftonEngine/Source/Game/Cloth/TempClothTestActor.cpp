#include "TempClothTestActor.h"

#include "Component/SceneComponent.h"
#include "Core/Logging/Log.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/World.h"
#include "Physics/Cloth/NvClothScene.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr uint32 TempClothColumns = 16;
	constexpr uint32 TempClothRows = 12;
	constexpr float TempClothSpacing = 14.0f;
	constexpr float TempClothDebugDuration = 0.0f;

	bool IsFiniteVector(const FVector& Value)
	{
		return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
	}
}

ATempClothTestActor::ATempClothTestActor()
{
	// TEMP CLOTH TEST - DELETE LATER.
	bTickInEditor = true;
	bNeedsTick = true;
}

ATempClothTestActor::~ATempClothTestActor()
{
	DestroyCloth();
}

void ATempClothTestActor::InitDefaultComponents()
{
	USceneComponent* SceneRoot = AddComponent<USceneComponent>();
	SetRootComponent(SceneRoot);
}

void ATempClothTestActor::BeginPlay()
{
	Super::BeginPlay();
	EnsureClothCreated();
}

void ATempClothTestActor::EndPlay()
{
	DestroyCloth();
	Super::EndPlay();
}

void ATempClothTestActor::PostDuplicate()
{
	DestroyCloth();
}

void ATempClothTestActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!EnsureClothCreated())
	{
		return;
	}

	ElapsedTime += DeltaTime;
	UpdatePins();

	if (ClothScene && ClothInstance)
	{
		ClothScene->SimulateCloth(DeltaTime);
		ClothScene->GetClothRenderData(ClothInstance, RenderData);
	}

	DrawClothDebug();
	LogClothStats(DeltaTime);
}

bool ATempClothTestActor::EnsureClothCreated()
{
	if (bInitialized)
	{
		return ClothScene && ClothInstance;
	}

	bInitialized = true;
	ClothScene = std::make_unique<FNvClothScene>();

	if (!ClothScene->Initialize(EClothBackend::CPU))
	{
		UE_LOG("[TEMP CLOTH TEST] Failed to initialize CPU NvCloth scene");
		DestroyCloth();
		return false;
	}

	FClothGridDesc GridDesc;
	GridDesc.Name = "TEMP_DELETE_LATER_GridCloth";
	GridDesc.NumColumns = TempClothColumns;
	GridDesc.NumRows = TempClothRows;
	GridDesc.Spacing = TempClothSpacing;
	GridDesc.Origin = GetTestOrigin() + FVector(-95.0f, -105.0f, 220.0f);
	GridDesc.AxisX = FVector::RightVector;
	GridDesc.AxisY = FVector::ForwardVector;
	GridDesc.bPinTopRow = true;
	GridDesc.Settings.Gravity = FVector(0.0f, 0.0f, -980.0f);
	GridDesc.Settings.Damping = 0.18f;
	GridDesc.Settings.WindVelocity = FVector::ZeroVector;
	GridDesc.Settings.DragCoefficient = 0.0f;
	GridDesc.Settings.LiftCoefficient = 0.0f;
	GridDesc.Settings.bContinuousCollision = false;

	const FVector CollisionCenter = GridDesc.Origin + FVector(75.0f, 105.0f, -55.0f);
	GridDesc.Collision.Spheres.push_back({ CollisionCenter, 46.0f });

	ClothInstance = ClothScene->CreateGridCloth(GridDesc, &FabricHandle);
	if (!ClothInstance)
	{
		UE_LOG("[TEMP CLOTH TEST] Failed to create grid cloth");
		DestroyCloth();
		return false;
	}

	Pins.clear();
	for (uint32 Column = 0; Column < TempClothColumns; ++Column)
	{
		FClothPinnedParticle Pin;
		Pin.ParticleIndex = Column;
		Pin.Position = GridDesc.Origin + FVector::RightVector * (static_cast<float>(Column) * TempClothSpacing);
		Pins.push_back(Pin);
	}
	UpdatePins();

	UE_LOG("[TEMP CLOTH TEST] Created CPU cloth: %u x %u particles=%u",
		TempClothColumns,
		TempClothRows,
		ClothInstance->NumParticles);

	return true;
}

FVector ATempClothTestActor::GetTestOrigin() const
{
	return RootComponent ? GetActorLocation() : PendingActorLocation;
}

void ATempClothTestActor::DestroyCloth()
{
	if (ClothScene && ClothInstance)
	{
		ClothScene->DestroyClothInstance(ClothInstance);
	}

	if (ClothScene && FabricHandle.IsValid())
	{
		ClothScene->DestroyClothFabric(FabricHandle);
	}

	ClothInstance = nullptr;
	FabricHandle = {};
	RenderData.Reset();
	Pins.clear();
	ClothScene.reset();
	bInitialized = false;
	ElapsedTime = 0.0f;
	LogAccumulator = 0.0f;
}

void ATempClothTestActor::UpdatePins()
{
	if (!ClothScene || !ClothInstance || Pins.empty())
	{
		return;
	}

	const FVector Origin = GetTestOrigin() + FVector(-95.0f, -105.0f, 220.0f);
	const float Wave = std::sinf(ElapsedTime * 1.5f) * 5.0f;

	for (uint32 Column = 0; Column < TempClothColumns; ++Column)
	{
		FClothPinnedParticle& Pin = Pins[Column];
		Pin.Position = Origin
			+ FVector::RightVector * (static_cast<float>(Column) * TempClothSpacing)
			+ FVector(0.0f, 0.0f, Wave);
	}

	ClothScene->SetPinnedParticlePositions(ClothInstance, Pins, true);
}

void ATempClothTestActor::DrawClothDebug()
{
	UWorld* World = GetWorld();
	if (!World || RenderData.Vertices.empty())
	{
		return;
	}

	const FColor ClothColor(80, 190, 255);
	const FColor PinColor = FColor::Yellow();
	const FColor CollisionColor = FColor::Red();

	for (size_t Index = 0; Index + 2 < RenderData.Indices.size(); Index += 3)
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
		if (!IsFiniteVector(P0) || !IsFiniteVector(P1) || !IsFiniteVector(P2))
		{
			continue;
		}

		DrawDebugLine(World, P0, P1, ClothColor, TempClothDebugDuration);
		DrawDebugLine(World, P1, P2, ClothColor, TempClothDebugDuration);
		DrawDebugLine(World, P2, P0, ClothColor, TempClothDebugDuration);
	}

	for (const FClothPinnedParticle& Pin : Pins)
	{
		DrawDebugPoint(World, Pin.Position, 2.0f, PinColor, TempClothDebugDuration);
	}

	if (ClothInstance)
	{
		for (const FClothCollisionSphere& Sphere : ClothInstance->Collision.Spheres)
		{
			DrawDebugSphere(World, Sphere.Center, Sphere.Radius, 16, CollisionColor, TempClothDebugDuration);
		}
	}
}

void ATempClothTestActor::LogClothStats(float DeltaTime)
{
	LogAccumulator += DeltaTime;
	if (LogAccumulator < 1.0f || RenderData.Vertices.empty())
	{
		return;
	}
	LogAccumulator = 0.0f;

	float MinZ = FLT_MAX;
	float MaxZ = -FLT_MAX;
	uint32 InvalidVertexCount = 0;
	for (const FClothRenderVertex& Vertex : RenderData.Vertices)
	{
		if (!IsFiniteVector(Vertex.Position))
		{
			++InvalidVertexCount;
			continue;
		}

		MinZ = (std::min)(MinZ, Vertex.Position.Z);
		MaxZ = (std::max)(MaxZ, Vertex.Position.Z);
	}

	FClothStats Stats;
	if (ClothScene)
	{
		ClothScene->GetClothStats(Stats);
	}

	UE_LOG("[TEMP CLOTH TEST] verts=%zu tris=%zu invalid=%u minZ=%.2f maxZ=%.2f simMs=%.3f",
		RenderData.Vertices.size(),
		RenderData.Indices.size() / 3,
		InvalidVertexCount,
		MinZ,
		MaxZ,
		Stats.LastSimulationMs);
}
