#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Proxy/ShapeDebugGeometryUtils.h"

class UPhysicsShapeDebugComponent;

// ============================================================
// FPhysicsShapeDebugSceneProxy — 피직스 콜리전 셰이프 반투명 솔리드 프록시
//
// FBoneDebugSceneProxy 와 동일 구조. 본 월드 트랜스폼 × 셰이프 로컬로
// sphere/box/capsule 의 셰이딩된 삼각형 메시를 캐싱하고, DrawCommandBuilder 가
// PhysicsShapeSolid(AlphaBlend·NoDepth) 버퍼에 병합한다. (언리얼 PhAT 스타일)
//
// 서브클래스는 protected 생성자 + RebuildGeometry() 오버라이드로 확장 가능.
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

	void InvalidateBoneCache() { bBoneCacheDirty = true; }

protected:
	// 서브클래스용 생성자 — RebuildGeometry()를 호출하지 않아 vtable이 완전히 구성된 후
	// 서브클래스 생성자에서 직접 호출할 수 있다.
	explicit FPhysicsShapeDebugSceneProxy(UPrimitiveComponent* InComponent, bool /*bSubclass*/);

	virtual void RebuildGeometry();

	TArray<FColoredVertex> CachedSolid;
	TArray<FColoredLine>   CachedWire;
	bool bShowSolid = true;
	bool bShowWire  = true;

private:
	void RebuildBoneCache(UPhysicsShapeDebugComponent* Comp);

	TArray<int32> CachedBoneIndices;
	bool bBoneCacheDirty = true;
};
