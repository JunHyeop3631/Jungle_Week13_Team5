#pragma once

#include "Render/Proxy/PhysicsShapeDebugSceneProxy.h"

class UStaticMeshCollisionDebugComponent;

// FPhysicsShapeDebugSceneProxy를 상속하여 RebuildGeometry()만 오버라이드.
// 렌더 패스(PhysicsShapeDebug 플래그, PhysicsShapeSolid/Wire 버퍼)를 그대로 공유한다.
class FStaticMeshCollisionDebugSceneProxy : public FPhysicsShapeDebugSceneProxy
{
public:
	explicit FStaticMeshCollisionDebugSceneProxy(UStaticMeshCollisionDebugComponent* InComponent);
	~FStaticMeshCollisionDebugSceneProxy() override;

protected:
	void RebuildGeometry() override;
};
