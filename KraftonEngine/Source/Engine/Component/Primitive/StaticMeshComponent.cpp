#include "Component/Primitive/StaticMeshComponent.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include "Object/Reflection/ObjectFactory.h"
#include "Core/Types/PropertyTypes.h"
#include "Engine/Platform/Paths.h"
#include "Collision/Ray/RayUtils.h"
#include "Mesh/Static/StaticMeshAsset.h"
#include "Engine/Runtime/Engine.h"
#include "Render/Shader/ShaderManager.h"
#include "Texture/Texture2D.h"
#include "Render/Proxy/StaticMeshSceneProxy.h"
#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Serialization/Archive.h"
#include "GameFramework/World.h"
#include "Physics/IPhysicsScene.h"
#include "Physics/Asset/BodySetup.h"
#include "GameFramework/AActor.h"
#include "Debug/DebugDrawQueue.h"
#include "Render/Scene/FScene.h"
#include "Math/Quat.h"
#include "Math/Vector.h"

#include <cstddef>

// ──────────────────────────────────────────────
// 로컬 와이어프레임 헬퍼 (SkeletalMeshComponent 와 동일 규약)
// ──────────────────────────────────────────────
namespace
{
using FDbgLineSink = std::function<void(const FVector&, const FVector&)>;

constexpr float kSMDbgPi  = 3.14159265f;
constexpr float kSMDbgPi2 = kSMDbgPi * 2.0f;

void SMDbgWireSphere(const FDbgLineSink& Emit, const FVector& C, float R)
{
	constexpr int32 Seg = 16;
	const FVector Ax[3] = { FVector(1,0,0), FVector(0,1,0), FVector(0,0,1) };
	for (int32 p = 0; p < 3; ++p)
	{
		const FVector U = Ax[p], V = Ax[(p + 1) % 3];
		FVector Prev = C + U * R;
		for (int32 i = 1; i <= Seg; ++i)
		{
			const float a = kSMDbgPi2 * i / Seg;
			const FVector Cur = C + (U * cosf(a) + V * sinf(a)) * R;
			Emit(Prev, Cur);
			Prev = Cur;
		}
	}
}

void SMDbgWireBox(const FDbgLineSink& Emit, const FVector& C, const FQuat& Rot, float HX, float HY, float HZ)
{
	const FVector L[8] = {
		{-HX,-HY,-HZ},{HX,-HY,-HZ},{HX,HY,-HZ},{-HX,HY,-HZ},
		{-HX,-HY, HZ},{HX,-HY, HZ},{HX,HY, HZ},{-HX,HY, HZ},
	};
	FVector P[8];
	for (int32 k = 0; k < 8; ++k) P[k] = C + Rot.RotateVector(L[k]);
	static const int32 E[12][2] = {
		{0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
	};
	for (int32 e = 0; e < 12; ++e) Emit(P[E[e][0]], P[E[e][1]]);
}

void SMDbgWireCapsule(const FDbgLineSink& Emit, const FVector& C, const FQuat& Rot, float Radius, float HalfH)
{
	constexpr int32 Seg = 16;
	const FVector Axis = Rot.RotateVector(FVector(1,0,0));
	const FVector U    = Rot.RotateVector(FVector(0,1,0));
	const FVector V    = Rot.RotateVector(FVector(0,0,1));
	const FVector TopC = C + Axis * HalfH, BotC = C - Axis * HalfH;

	// 양쪽 끝 반원 + 실린더 4개 세로 줄
	for (int32 i = 0; i < Seg; ++i)
	{
		const float a0 = kSMDbgPi2 * i / Seg;
		const float a1 = kSMDbgPi2 * (i + 1) / Seg;
		const FVector r0 = (U * cosf(a0) + V * sinf(a0)) * Radius;
		const FVector r1 = (U * cosf(a1) + V * sinf(a1)) * Radius;
		// 실린더 링
		Emit(TopC + r0, TopC + r1);
		Emit(BotC + r0, BotC + r1);
	}

	// 반구 (4분면 2개씩)
	for (int32 pass = 0; pass < 2; ++pass)
	{
		const FVector& Cap = (pass == 0) ? TopC : BotC;
		const float sign  = (pass == 0) ? 1.0f : -1.0f;
		const FVector UA = (pass == 0) ? U : U;
		const FVector VA = V;
		for (int32 i = 0; i < Seg / 2; ++i)
		{
			const float a0 = kSMDbgPi * i / (Seg / 2);
			const float a1 = kSMDbgPi * (i + 1) / (Seg / 2);
			// U-Axis 평면 반원
			{
				const FVector p0 = Cap + Axis * (sign * Radius * sinf(a0)) + UA * (Radius * cosf(a0));
				const FVector p1 = Cap + Axis * (sign * Radius * sinf(a1)) + UA * (Radius * cosf(a1));
				Emit(p0, p1);
			}
			// V-Axis 평면 반원
			{
				const FVector p0 = Cap + Axis * (sign * Radius * sinf(a0)) + VA * (Radius * cosf(a0));
				const FVector p1 = Cap + Axis * (sign * Radius * sinf(a1)) + VA * (Radius * cosf(a1));
				Emit(p0, p1);
			}
		}
	}

	// 세로줄 4개 (연결선)
	for (int32 i = 0; i < 4; ++i)
	{
		const float a = kSMDbgPi2 * i / 4;
		const FVector r = (U * cosf(a) + V * sinf(a)) * Radius;
		Emit(TopC + r, BotC + r);
	}
}
} // namespace

FPrimitiveSceneProxy* UStaticMeshComponent::CreateSceneProxy()
{
	return new FStaticMeshSceneProxy(this);
}

void UStaticMeshComponent::SetStaticMesh(UStaticMesh* InMesh)
{
	StaticMesh = InMesh;
	if (InMesh)
	{
		// 메시 에셋 PathFileName 은 Import 시점에 절대 경로로 들어올 수 있어
		// 컴포넌트 단계에서 프로젝트 상대 경로로 정규화한다 (씬 직렬화 안정성).
		StaticMeshPath = FPaths::MakeProjectRelative(InMesh->GetAssetPathFileName());
		const TArray<FStaticMaterial>& DefaultMaterials = StaticMesh->GetStaticMaterials();

		OverrideMaterials.resize(DefaultMaterials.size());
		MaterialSlots.resize(DefaultMaterials.size());

		for (int32 i = 0; i < (int32)DefaultMaterials.size(); ++i)
		{
			OverrideMaterials[i] = DefaultMaterials[i].MaterialInterface;

			if (OverrideMaterials[i])
				MaterialSlots[i] = OverrideMaterials[i]->GetAssetPathFileName();
			else
				MaterialSlots[i] = "None";
		}
	}
	else
	{
		StaticMeshPath = "None";
		OverrideMaterials.clear();
		MaterialSlots.clear();
	}
	CacheLocalBounds();

	MarkRenderStateDirty();
	MarkWorldBoundsDirty();
}

void UStaticMeshComponent::CacheLocalBounds()
{
	bHasValidBounds = false;
	if (!StaticMesh) return;
	FStaticMesh* Asset = StaticMesh->GetStaticMeshAsset();
	if (!Asset || Asset->Vertices.empty()) return;

	// FStaticMesh에 이미 계산된 바운드가 있으면 그대로 사용
	if (!Asset->bBoundsValid)
	{
		Asset->CacheBounds();
	}

	CachedLocalCenter = Asset->BoundsCenter;
	CachedLocalExtent = Asset->BoundsExtent;
	bHasValidBounds = Asset->bBoundsValid;
}

UStaticMesh* UStaticMeshComponent::GetStaticMesh() const
{
	return StaticMesh;
}

UBodySetup* UStaticMeshComponent::GetBodySetup() const
{
	return StaticMesh ? StaticMesh->GetBodySetup() : nullptr;
}

void UStaticMeshComponent::SetMobility(EComponentMobility InMobility)
{
	if (Mobility == InMobility) return;
	Mobility = InMobility;
	// bSimulatePhysics를 동기화하면 NotifyPhysicsBodyDirty가 PhysX Body를 재빌드
	SetSimulatePhysics(InMobility == EComponentMobility::Dynamic);
}

void UStaticMeshComponent::CreatePhysicsState()
{
	// Mobility → bSimulatePhysics (Static=false → PxRigidStatic, Dynamic=true → PxRigidDynamic)
	// AggregateGeom은 이미 에디터 편집 시 실시간으로 업데이트됨.
	bSimulatePhysics = (Mobility == EComponentMobility::Dynamic);

	// 3. Physics scene에 등록. BeginPlay 이후면 RebuildBody로 기존 actor 교체,
	//    이전이면 데이터 준비만 — BeginPlay에서 RegisterComponent가 자동 호출됨.
	if (!Owner) return;
	UWorld* World = Owner->GetWorld();
	if (!World) return;
	if (IPhysicsScene* Scene = World->GetPhysicsScene())
	{
		if (bComponentHasBegunPlay)
			Scene->RebuildBody(this);
		else if (IsCollisionEnabled())
			Scene->RegisterComponent(this);
	}
}

void UStaticMeshComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* InMaterial)
{
	if (ElementIndex >= 0 && ElementIndex < static_cast<int32>(OverrideMaterials.size()))
	{
		OverrideMaterials[ElementIndex] = InMaterial;

		// MaterialSlots 동기화 — 씬 저장 시 경로가 올바르게 직렬화되도록
		if (ElementIndex < static_cast<int32>(MaterialSlots.size()))
		{
			MaterialSlots[ElementIndex] = InMaterial
				? InMaterial->GetAssetPathFileName()
				: "None";
		}

		// 프록시에 Material dirty 전파
		MarkProxyDirty(EDirtyFlag::Material);
	}
}

UMaterialInterface* UStaticMeshComponent::GetMaterial(int32 ElementIndex) const
{
	if (ElementIndex >= 0 && ElementIndex < OverrideMaterials.size())
	{
		return OverrideMaterials[ElementIndex];
	}
	return nullptr;
}

FMeshBuffer* UStaticMeshComponent::GetMeshBuffer() const
{
	if (!StaticMesh) return nullptr;
	FStaticMesh* Asset = StaticMesh->GetStaticMeshAsset();
	if (!Asset || !Asset->RenderBuffer) return nullptr;
	return Asset->RenderBuffer.get();
}

FMeshDataView UStaticMeshComponent::GetMeshDataView() const
{
	if (!StaticMesh) return {};
	FStaticMesh* Asset = StaticMesh->GetStaticMeshAsset();
	if (!Asset || Asset->Vertices.empty()) return {};

	FMeshDataView View;
	View.VertexData  = Asset->Vertices.data();
	View.VertexCount = (uint32)Asset->Vertices.size();
	View.Stride      = sizeof(FNormalVertex);
	View.PositionOffset = offsetof(FNormalVertex, pos);
	View.NormalOffset   = offsetof(FNormalVertex, normal);
	View.ColorOffset    = offsetof(FNormalVertex, color);
	View.UVOffset       = offsetof(FNormalVertex, tex);
	View.TangentOffset  = offsetof(FNormalVertex, tangent);
	View.IndexData   = Asset->Indices.data();
	View.IndexCount  = (uint32)Asset->Indices.size();
	return View;
}

void UStaticMeshComponent::UpdateWorldAABB() const
{
	if (!bHasValidBounds)
	{
		UPrimitiveComponent::UpdateWorldAABB();
		return;
	}

	FVector WorldCenter = CachedWorldMatrix.TransformPositionWithW(CachedLocalCenter);

	float Ex = std::abs(CachedWorldMatrix.M[0][0]) * CachedLocalExtent.X
		+ std::abs(CachedWorldMatrix.M[1][0]) * CachedLocalExtent.Y
		+ std::abs(CachedWorldMatrix.M[2][0]) * CachedLocalExtent.Z;
	float Ey = std::abs(CachedWorldMatrix.M[0][1]) * CachedLocalExtent.X
		+ std::abs(CachedWorldMatrix.M[1][1]) * CachedLocalExtent.Y
		+ std::abs(CachedWorldMatrix.M[2][1]) * CachedLocalExtent.Z;
	float Ez = std::abs(CachedWorldMatrix.M[0][2]) * CachedLocalExtent.X
		+ std::abs(CachedWorldMatrix.M[1][2]) * CachedLocalExtent.Y
		+ std::abs(CachedWorldMatrix.M[2][2]) * CachedLocalExtent.Z;

	WorldAABBMinLocation = WorldCenter - FVector(Ex, Ey, Ez);
	WorldAABBMaxLocation = WorldCenter + FVector(Ex, Ey, Ez);
	bWorldAABBDirty = false;
	bHasValidWorldAABB = true;
}

bool UStaticMeshComponent::LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult)
{
	const FMatrix& WorldMatrix = GetWorldMatrix();
	const FMatrix& WorldInverse = GetWorldInverseMatrix();
	return LineTraceStaticMeshFast(Ray, WorldMatrix, WorldInverse, OutHitResult);
}

bool UStaticMeshComponent::LineTraceStaticMeshFast(
	const FRay& Ray,
	const FMatrix& WorldMatrix,
	const FMatrix& WorldInverse,
	FHitResult& OutHitResult)
{
	if (!StaticMesh) return false;

	FVector LocalOrigin = WorldInverse.TransformPositionWithW(Ray.Origin);
	FVector LocalDirection = WorldInverse.TransformVector(Ray.Direction);
	LocalDirection.Normalize();

	// Mesh BVH만 사용하는 전용 경로입니다. 월드 BVH는 이 함수를 직접 호출해 가상 호출 비용을 피합니다.
	if (StaticMesh->RaycastMeshTrianglesWithBVHLocal(LocalOrigin, LocalDirection, OutHitResult))
	{
		const FVector LocalHitPoint = LocalOrigin + LocalDirection * OutHitResult.Distance;
		const FVector WorldHitPoint = WorldMatrix.TransformPositionWithW(LocalHitPoint);
		OutHitResult.Distance = FVector::Distance(Ray.Origin, WorldHitPoint);
		OutHitResult.HitComponent = this;
		return true;
	}

	// 실패하면 기존 방식하던 걸 주석 처리. 성능개선이 일단 확인됨.
	//bool bHit = FRayUtils::RaycastTriangles(
	//	Ray, GetWorldMatrix(),
	//	GetWorldInverseMatrix(),
	//	&Asset->Vertices[0].pos,
	//	sizeof(FNormalVertex),
	//	Asset->Indices,
	//	OutHitResult);

	//if (bHit)
	//{
	//	OutHitResult.HitComponent = this;
	//}
	/*codex의 답변
왜 빨라졌냐면, 주석 처리된 Jungle_Week5_Team3/KraftonEngine/Source/Engine/Collision/RayUtils.cpp:60의
RaycastTriangles()는 BVH 없이 Indices를 처음부터 끝까지 3개씩 돌면서 모든 triangle에 IntersectTriangle()를 합니다.
즉 후보 메시에 대해 매번 풀 스캔입니다.
월드 단계에서 이미 Jungle_Week5_Team3/KraftonEngine/Source/Engine/Collision/WorldPrimitivePickingBVH.cpp:87가
primitive AABB 기준으로 후보만 추립니다.
그런데 AABB는 보수적이라 “박스는 맞았지만 실제 삼각형은 안 맞는” 후보가 꽤 나옵니다.
예전 코드에서는 이런 BVH miss 후보마다 fallback 전체 triangle 스캔이 한 번 더 돌았습니다.
즉 “안 맞은 객체”를 확인하는 비용이 너무 컸던 겁니다.
	*/
	return false; // bHit;
}

void UStaticMeshComponent::PostDuplicate()
{
	UMeshComponent::PostDuplicate();

	// 메시 에셋 재로딩
	if (!StaticMeshPath.empty() && StaticMeshPath != "None")
	{
		ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
		UStaticMesh* Loaded = FMeshManager::LoadStaticMesh(StaticMeshPath, Device);
		if (Loaded)
		{
			// SetStaticMesh는 MaterialSlots를 덮어쓰므로, 직렬화된 슬롯 정보를 백업·복원한다.
			TArray<FSoftObjectPtr> SavedSlots = MaterialSlots;
			SetStaticMesh(Loaded);

			// Override material 재로딩
			for (int32 i = 0; i < (int32)MaterialSlots.size() && i < (int32)SavedSlots.size(); ++i)
			{
				MaterialSlots[i] = SavedSlots[i];
				const FString& MatPath = MaterialSlots[i];
				if (MatPath.empty() || MatPath == "None")
				{
					OverrideMaterials[i] = nullptr;
				}
				else
				{
					UMaterial* LoadedMat = FMaterialManager::Get().GetOrCreateMaterial(MatPath);
					OverrideMaterials[i] = LoadedMat;
				}
			}
		}
	}

	CacheLocalBounds();
	MarkRenderStateDirty();
	MarkWorldBoundsDirty();
}

void UStaticMeshComponent::PostEditChangeProperty(const FPropertyChangedEvent& Event)
{
	const bool bMaterialSlotsChanged =
		(Event.PropertyName && strcmp(Event.PropertyName, "MaterialSlots") == 0)
		|| (Event.Property && Event.Property->Name && strcmp(Event.Property->Name, "MaterialSlots") == 0)
		|| Event.PropertyPath.rfind("MaterialSlots[", 0) == 0;

	if (bMaterialSlotsChanged && Event.ArrayIndex >= 0)
	{
		const int32 Index = Event.ArrayIndex;
		if (Index >= 0 && Index < static_cast<int32>(MaterialSlots.size()))
		{
			const FString& NewMatPath = MaterialSlots[Index];
			if (NewMatPath.empty() || NewMatPath == "None")
			{
				SetMaterial(Index, nullptr);
			}
			else if (UMaterial* LoadedMat = FMaterialManager::Get().GetOrCreateMaterial(NewMatPath))
			{
				SetMaterial(Index, LoadedMat);
			}
		}
		return;
	}

	UMeshComponent::PostEditChangeProperty(Event);
}

void UStaticMeshComponent::PostEditProperty(const char* PropertyName)
{
	UMeshComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "Mobility") == 0)
	{
		SetMobility(Mobility);
		return;
	}

	if (strcmp(PropertyName, "StaticMeshPath") == 0 || strcmp(PropertyName, "Static Mesh") == 0)
	{
		if (StaticMeshPath.empty() || StaticMeshPath == "None")
		{
			StaticMesh = nullptr;
		}
		else
		{
			ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
			UStaticMesh* Loaded = FMeshManager::LoadStaticMesh(StaticMeshPath, Device);
			SetStaticMesh(Loaded);
		}
		CacheLocalBounds();
		MarkWorldBoundsDirty();
	}

	if (strncmp(PropertyName, "Element ", 8) == 0)
	{
		// "Element 0"에서 8번째 인덱스부터 시작하는 숫자를 정수로 변환
		int32 Index = atoi(&PropertyName[8]);

		// 인덱스 범위 유효성 검사
		if (Index >= 0 && Index < (int32)MaterialSlots.size())
		{
			FString NewMatPath = MaterialSlots[Index];

			if (NewMatPath == "None" || NewMatPath.empty())
			{
				SetMaterial(Index, nullptr);
			}
			else
			{
				UMaterial* LoadedMat = FMaterialManager::Get().GetOrCreateMaterial(NewMatPath);
				if (LoadedMat)
				{
					SetMaterial(Index, LoadedMat);
				}
			}
		}
	}

	if (strcmp(PropertyName, "MaterialSlots") == 0 || strcmp(PropertyName, "Materials") == 0)
	{
		for (int32 Index = 0; Index < (int32)MaterialSlots.size(); ++Index)
		{
			const FString& NewMatPath = MaterialSlots[Index];
			if (NewMatPath == "None" || NewMatPath.empty())
			{
				SetMaterial(Index, nullptr);
			}
			else
			{
				UMaterial* LoadedMat = FMaterialManager::Get().GetOrCreateMaterial(NewMatPath);
				if (LoadedMat)
				{
					SetMaterial(Index, LoadedMat);
				}
			}
		}
	}
}

// ──────────────────────────────────────────────
// Physics body 디버그 와이어프레임
// ──────────────────────────────────────────────
void UStaticMeshComponent::BuildPhysicsBodyWireframe(
	const std::function<void(const FVector&, const FVector&)>& EmitLine) const
{
	if (!EmitLine) return;

	UBodySetup* BS = GetBodySetup();
	if (!BS) return;

	// 컴포넌트 월드 위치/회전 추출
	const FVector WorldPos  = GetWorldLocation();
	const FQuat   WorldRot  = CachedWorldMatrix.ToQuat();

	for (const FKSphereElem& E : BS->AggregateGeom.SphereElems)
		SMDbgWireSphere(EmitLine, WorldPos + WorldRot.RotateVector(E.Center), E.Radius);

	for (const FKBoxElem& E : BS->AggregateGeom.BoxElems)
		SMDbgWireBox(EmitLine, WorldPos + WorldRot.RotateVector(E.Center),
			WorldRot * E.Rotation, E.HalfX, E.HalfY, E.HalfZ);

	for (const FKCapsuleElem& E : BS->AggregateGeom.CapsuleElems)
		SMDbgWireCapsule(EmitLine, WorldPos + WorldRot.RotateVector(E.Center),
			WorldRot * E.Rotation, E.Radius, E.HalfHeight);
}

void UStaticMeshComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	if (!bShowPhysicsBodies) return;

	BuildPhysicsBodyWireframe([&Scene](const FVector& A, const FVector& B)
	{
		Scene.AddDebugLine(A, B, FColor::Green());
	});
}

void UStaticMeshComponent::DrawRuntimePhysicsBodies()
{
	if (!bShowPhysicsBodies) return;

	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay()) return;

	FDebugDrawQueue& Queue = World->GetScene().GetDebugDrawQueue();
	BuildPhysicsBodyWireframe([&Queue](const FVector& A, const FVector& B)
	{
		Queue.AddLine(A, B, FColor::Green(), 0.0f);
	});
}

void UStaticMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	DrawRuntimePhysicsBodies();
}
