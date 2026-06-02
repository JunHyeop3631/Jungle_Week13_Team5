#pragma once

#include "Component/MeshComponent.h"
#include "Core/Types/PropertyTypes.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Static/StaticMesh.h"
#include "Object/Ptr/ObjectPtr.h"
#include "Object/Ptr/SoftObjectPtr.h"

class UMaterialInterface;
class FPrimitiveSceneProxy;

namespace json { class JSON; }

// UStaticMeshComponent — 월드 배치 컴포넌트

#include "Source/Engine/Component/Primitive/StaticMeshComponent.generated.h"

enum class EStaticMeshPhysicsCollisionSource : uint8
{
	None,
	BoundsBox,
	AssetCollision,
};

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
	bool GetLocalBounds(FVector& OutCenter, FVector& OutExtent) const;
	void SetPhysicsCollisionSource(EStaticMeshPhysicsCollisionSource InSource);
	EStaticMeshPhysicsCollisionSource GetPhysicsCollisionSource() const { return PhysicsCollisionSource; }

	void SetMaterial(int32 ElementIndex, UMaterialInterface* InMaterial);
	UMaterialInterface* GetMaterial(int32 ElementIndex) const;
	const TArray<UMaterialInterface*>& GetOverrideMaterials() const { return OverrideMaterials; }
	UMaterialInterface* GetClothMaterial(int32 ElementIndex) const override { return GetMaterial(ElementIndex); }

	void PostDuplicate() override;

	// Property Editor 지원
	void PostEditChangeProperty(const FPropertyChangedEvent& Event) override;
	void PostEditProperty(const char* PropertyName) override;

	const FString& GetStaticMeshPath() const { return StaticMeshPath.ToString(); }

private:
	void CacheLocalBounds();

	TObjectPtr<UStaticMesh> StaticMesh;
	UPROPERTY(Edit, Save, Category="Mesh", DisplayName="Static Mesh", AssetType="StaticMesh")
	FSoftObjectPtr StaticMeshPath = "None";
	TArray<UMaterialInterface*> OverrideMaterials;
	UPROPERTY(Edit, Save, EditFixedSize, Category="Materials", DisplayName="Materials", AssetType="Material")
	TArray<FSoftObjectPtr> MaterialSlots;
	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Physics Collision Source", Enum=EStaticMeshPhysicsCollisionSource)
	EStaticMeshPhysicsCollisionSource PhysicsCollisionSource = EStaticMeshPhysicsCollisionSource::AssetCollision;

	FVector CachedLocalCenter = { 0, 0, 0 };
	FVector CachedLocalExtent = { 0.5f, 0.5f, 0.5f };
	bool bHasValidBounds = false;
};
