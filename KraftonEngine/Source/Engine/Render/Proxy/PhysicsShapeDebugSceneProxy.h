#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Math/Vector.h"

class UPhysicsShapeDebugComponent;

// 셰이딩(가짜 명암)이 구워진 솔리드 삼각형 버텍스. 연속 3개 = 삼각형 1개.
struct FColoredVertex
{
	FVector  Pos;
	FVector4 Color;	// 알파 포함 (반투명)
};

// 와이어프레임 라인 세그먼트 (셰이프별 단일 색, 셰이딩 없음). 2점 = 라인 1개.
struct FColoredLine
{
	FVector  Start;
	FVector  End;
	FVector4 Color;
};

// ============================================================
// FPhysicsShapeDebugSceneProxy — 피직스 콜리전 셰이프 반투명 솔리드 프록시
//
// FBoneDebugSceneProxy 와 동일 구조. 본 월드 트랜스폼 × 셰이프 로컬로
// sphere/box/capsule 의 셰이딩된 삼각형 메시를 캐싱하고, DrawCommandBuilder 가
// PhysicsShapeSolid(AlphaBlend·NoDepth) 버퍼에 병합한다. (언리얼 PhAT 스타일)
// ============================================================
class FPhysicsShapeDebugSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FPhysicsShapeDebugSceneProxy(UPhysicsShapeDebugComponent* InComponent);
	~FPhysicsShapeDebugSceneProxy() override;

	void UpdateTransform() override;

	const TArray<FColoredVertex>& GetCachedSolid() const { return CachedSolid; }
	const TArray<FColoredLine>&   GetCachedWire()  const { return CachedWire; }
	bool ShouldDrawSolid() const { return bShowSolid; }
	bool ShouldDrawWire()  const { return bShowWire; }

	// asset/mesh 변경 시 본 인덱스 캐시 무효화
	void InvalidateBoneCache() { bBoneCacheDirty = true; }

private:
	void RebuildGeometry();
	void RebuildBoneCache(class UPhysicsShapeDebugComponent* Comp);

	TArray<FColoredVertex> CachedSolid;	// 반투명 셰이딩 솔리드 (AlphaBlend, NoDepth)
	TArray<FColoredLine>   CachedWire;	// 와이어프레임 (EditorLines LINELIST, NoDepth)
	bool bShowSolid = true;
	bool bShowWire  = true;

	// 본 이름 → 인덱스 캐시 (asset/mesh 변경 시에만 재빌드)
	TArray<int32> CachedBoneIndices;	// BodySetups 와 1:1 대응
	bool bBoneCacheDirty = true;

};
