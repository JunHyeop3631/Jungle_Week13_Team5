#include "StaticMeshCollisionDebugComponent.h"

#include "Object/Reflection/ObjectFactory.h"
#include "Render/Proxy/StaticMeshCollisionDebugSceneProxy.h"

UStaticMeshCollisionDebugComponent::UStaticMeshCollisionDebugComponent()
{
}

UStaticMeshCollisionDebugComponent::~UStaticMeshCollisionDebugComponent()
{
}

FPrimitiveSceneProxy* UStaticMeshCollisionDebugComponent::CreateSceneProxy()
{
	if (!StaticMesh) return nullptr;
	return new FStaticMeshCollisionDebugSceneProxy(this);
}
