#include "Component/MeshComponent.h"

#include "Object/Reflection/ObjectFactory.h"

HIDE_FROM_COMPONENT_LIST(UMeshComponent)

void UMeshComponent::BeginPlay()
{
	UPrimitiveComponent::BeginPlay();
}

void UMeshComponent::EndPlay()
{
	UPrimitiveComponent::EndPlay();
}

void UMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UPrimitiveComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UMeshComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);
}
