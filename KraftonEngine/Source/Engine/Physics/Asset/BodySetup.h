#pragma once

#include "Object/Object.h"
#include "Core/Types/CoreTypes.h"
#include "Physics/Asset/PhysicsGeometry.h"
#include "Source/Engine/Physics/Asset/BodySetup.generated.h"

// UBodySetup  —  본 단위 물리 설계도
UCLASS()
class UBodySetup : public UObject
{
public:
	GENERATED_BODY()
    UBodySetup() = default;

    FString BoneName;
    FKAggregateGeom AggregateGeom;

    float Mass = 1.0f;
    float LinearDamping = 0.01f;
    float AngularDamping = 0.05f;
    float Friction = 0.7f;
    float Restitution = 0.3f;
    bool  bSimulatePhysics = true;
};
