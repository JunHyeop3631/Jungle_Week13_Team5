#include "PhysicsShapeDebugComponent.h"

#include "Object/Reflection/ObjectFactory.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Render/Proxy/PhysicsShapeDebugSceneProxy.h"

UPhysicsShapeDebugComponent::UPhysicsShapeDebugComponent()
{
}

UPhysicsShapeDebugComponent::~UPhysicsShapeDebugComponent()
{
}

FPrimitiveSceneProxy* UPhysicsShapeDebugComponent::CreateSceneProxy()
{
	return new FPhysicsShapeDebugSceneProxy(this);
}
