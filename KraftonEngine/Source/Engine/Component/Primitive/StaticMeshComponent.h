#pragma once

#include "Component/MeshComponent.h"
#include "Core/Types/PropertyTypes.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Static/StaticMesh.h"
#include "Object/Ptr/ObjectPtr.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include <functional>

class UMaterialInterface;
class UBodySetup;
class FPrimitiveSceneProxy;
class FScene;

namespace json { class JSON; }

UENUM()
enum class EComponentMobility : uint8
{
	Static,   // PxRigidStatic — 질량 연산 없는 고정 바디
	Dynamic,  // PxRigidDynamic — 중력/힘 적용, BodySetup 물성값 주입
};

// UStaticMeshComponent — 월드 배치 컴포넌트

#include "Source/Engine/Component/Primitive/StaticMeshComponent.generated.h"

UCLASS()
class UStaticMeshComponent : public UMeshComponent
{
public:
	GENERATED_BODY()
	UStaticMeshComponent() = default;
	~UStaticMeshComponent() override = default;

	FMeshBuffer* GetMeshBuffer() const override;
	FMeshDataView GetMeshDataView() const override;
	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override;
	bool LineTraceStaticMeshFast(const FRay& Ray, const FMatrix& WorldMatrix, const FMatrix& WorldInverse, FHitResult& OutHitResult);
	void UpdateWorldAABB() const override;

	// 구체 프록시 생성 (FStaticMeshSceneProxy)
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	void SetStaticMesh(UStaticMesh* InMesh);
	UStaticMesh* GetStaticMesh() const;
	UBodySetup* GetBodySetup() const;

	EComponentMobility GetMobility() const { return Mobility; }
	void SetMobility(EComponentMobility InMobility);

	// CollisionShapes → AggregateGeom 동기화 후 PhysicsScene에 Static/Dynamic 바디 생성.
	// BeginPlay 이전이면 데이터 준비만 하고 BeginPlay에서 자동 등록.
	void CreatePhysicsState();

	void SetMaterial(int32 ElementIndex, UMaterialInterface* InMaterial);
	UMaterialInterface* GetMaterial(int32 ElementIndex) const;
	const TArray<UMaterialInterface*>& GetOverrideMaterials() const { return OverrideMaterials; }

	void PostDuplicate() override;

	// Property Editor 지원
	void PostEditChangeProperty(const FPropertyChangedEvent& Event) override;
	void PostEditProperty(const char* PropertyName) override;

	// 에디터 선택 시 physics body 와이어프레임 표시
	void ContributeSelectedVisuals(FScene& Scene) const override;

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	const FString& GetStaticMeshPath() const { return StaticMeshPath.ToString(); }

private:
	void CacheLocalBounds();
	void BuildPhysicsBodyWireframe(const std::function<void(const FVector&, const FVector&)>& EmitLine) const;
	void DrawRuntimePhysicsBodies();

	TObjectPtr<UStaticMesh> StaticMesh;
	UPROPERTY(Edit, Save, Category="Mesh", DisplayName="Static Mesh", AssetType="StaticMesh")
	FSoftObjectPtr StaticMeshPath = "None";
	UPROPERTY(Edit, Save, Category="Physics", DisplayName="Mobility", Enum=EComponentMobility)
	EComponentMobility Mobility = EComponentMobility::Static;

	UPROPERTY(Edit, Save, Category="Physics", DisplayName="Show Physics Bodies")
	bool bShowPhysicsBodies = false;

	TArray<UMaterialInterface*> OverrideMaterials;
	UPROPERTY(Edit, Save, EditFixedSize, Category="Materials", DisplayName="Materials", AssetType="Material")
	TArray<FSoftObjectPtr> MaterialSlots;

	FVector CachedLocalCenter = { 0, 0, 0 };
	FVector CachedLocalExtent = { 0.5f, 0.5f, 0.5f };
	bool bHasValidBounds = false;
};
