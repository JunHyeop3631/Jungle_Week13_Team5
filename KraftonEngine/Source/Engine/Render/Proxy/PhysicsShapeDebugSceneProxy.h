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

private:
	void RebuildGeometry();

	TArray<FColoredVertex> CachedSolid;	// 반투명 셰이딩 솔리드 (AlphaBlend, NoDepth)
};
