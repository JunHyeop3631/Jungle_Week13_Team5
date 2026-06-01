#pragma once

#include "Component/PrimitiveComponent.h"

#include "Source/Engine/Component/Debug/PhysicsShapeDebugComponent.generated.h"

class USkeletalMeshComponent;
class UPhysicsAsset;

// ============================================================
// UPhysicsShapeDebugComponent — 피직스 에디터 콜리전 셰이프 와이어프레임 컴포넌트
//
// UBoneDebugComponent 와 동일한 패턴(프리뷰 액터에 부착 → 씬 프록시 등록).
// 본 디버그가 메시에 가려지지 않도록 NoDepth 로 그리는 것처럼,
// 셰이프도 NoDepth(항상 위)로 그려 언리얼 PhAT 스타일을 따른다.
// 선택 셰이프 인덱스는 하이라이트 색상에 쓰인다.
//   SelKind: 0 = None, 1 = Sphere, 2 = Box, 3 = Capsule
// ============================================================
UCLASS()
class UPhysicsShapeDebugComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()
	UPhysicsShapeDebugComponent();
	~UPhysicsShapeDebugComponent() override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;

	USkeletalMeshComponent* GetTargetMeshComponent() const { return TargetMeshComponent; }
	void SetTargetMeshComponent(USkeletalMeshComponent* InMeshComponent) { TargetMeshComponent = InMeshComponent; }

	UPhysicsAsset* GetPhysicsAsset() const { return PhysicsAsset; }
	void SetPhysicsAsset(UPhysicsAsset* InAsset) { PhysicsAsset = InAsset; }

	int32 GetSelBodyIndex() const { return SelBodyIndex; }
	int32 GetSelKind() const { return SelKind; }
	int32 GetSelElemIndex() const { return SelElemIndex; }
	void SetSelection(int32 BodyIndex, int32 Kind, int32 ElemIndex)
	{
		SelBodyIndex = BodyIndex;
		SelKind = Kind;
		SelElemIndex = ElemIndex;
	}

	// 디버그 표시 토글 (솔리드/와이어 독립). 프록시가 RebuildGeometry 에서 읽어 캐싱.
	bool GetShowSolid() const { return bShowSolid; }
	bool GetShowWire()  const { return bShowWire; }
	void SetShowFlags(bool bInSolid, bool bInWire) { bShowSolid = bInSolid; bShowWire = bInWire; }

private:
	USkeletalMeshComponent* TargetMeshComponent = nullptr;
	UPhysicsAsset* PhysicsAsset = nullptr;

	int32 SelBodyIndex = -1;
	int32 SelKind = 0;
	int32 SelElemIndex = -1;

	bool bShowSolid = true;
	bool bShowWire  = true;
};
