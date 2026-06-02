#pragma once

#include "Component/PrimitiveComponent.h"

#include "Source/Engine/Component/Debug/StaticMeshCollisionDebugComponent.generated.h"

class UStaticMesh;

UCLASS()
class UStaticMeshCollisionDebugComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()
	UStaticMeshCollisionDebugComponent();
	~UStaticMeshCollisionDebugComponent() override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;

	UStaticMesh* GetStaticMesh() const { return StaticMesh; }
	void SetStaticMesh(UStaticMesh* InMesh) { StaticMesh = InMesh; MarkRenderStateDirty(); }

	// shape 데이터만 바뀐 경우 — 프록시를 재생성하지 않고 UpdateTransform으로 geometry만 재빌드
	void MarkGeometryDirty() { MarkRenderTransformDirty(); }

private:
	UStaticMesh* StaticMesh = nullptr;
};
