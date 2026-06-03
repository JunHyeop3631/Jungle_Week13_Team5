#pragma once

#include "Component/PrimitiveComponent.h"

#include "Source/Engine/Component/MeshComponent.generated.h"

UCLASS()
class UMeshComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()
	UMeshComponent() = default;
	~UMeshComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;
	void PostEditProperty(const char* PropertyName) override;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
};
