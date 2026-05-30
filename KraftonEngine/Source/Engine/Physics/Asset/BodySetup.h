#pragma once

#include "Object/Object.h"
#include "Core/Types/CoreTypes.h"
#include "Physics/Asset/PhysicsGeometry.h"
#include "Source/Engine/Physics/Asset/BodySetup.generated.h"

// 이 바디가 충돌 시스템에서 어떻게 쓰이는지 (언리얼 ECollisionEnabled 대응)
enum class EBodyCollisionEnabled : uint8
{
    NoCollision     = 0,   // 충돌 안 함
    QueryOnly       = 1,   // 레이캐스트/오버랩 등 쿼리에만 사용
    PhysicsOnly     = 2,   // 물리 시뮬레이션 충돌에만 사용
    QueryAndPhysics = 3,   // 둘 다
};

// 이 바디의 시뮬레이션 방식 (언리얼 EPhysicsType 대응)
enum class EBodyPhysicsType : uint8
{
    Default   = 0,   // 시뮬레이션 켜지면 물리로 구동 (래그돌)
    Kinematic = 1,   // 애니메이션이 구동, 다른 바디를 밀어냄
};

// UBodySetup  —  본 단위 물리 설계도
UCLASS()
class UBodySetup : public UObject
{
public:
	GENERATED_BODY()
    UBodySetup() = default;

    FString         BoneName;
    FKAggregateGeom AggregateGeom;

    float Mass            = 1.0f;
    float LinearDamping   = 0.01f;
    float AngularDamping  = 0.05f;
    float Friction        = 0.7f;
    float Restitution     = 0.3f;
    bool  bSimulatePhysics = true;

    // 충돌 / 시뮬레이션 용도 구분
    bool                  bEnableGravity   = true;
    EBodyPhysicsType      PhysicsType      = EBodyPhysicsType::Default;
    EBodyCollisionEnabled CollisionEnabled = EBodyCollisionEnabled::QueryAndPhysics;

    void Serialize(FArchive& Ar)
    {
        Ar << BoneName;
        AggregateGeom.Serialize(Ar);
        Ar << Mass;
        Ar << LinearDamping;
        Ar << AngularDamping;
        Ar << Friction;
        Ar << Restitution;
        Ar << bSimulatePhysics;
        // 아래 필드는 이후 버전에서 추가됨. (구버전 에셋은 재저장 필요)
        Ar << bEnableGravity;
        Ar << PhysicsType;
        Ar << CollisionEnabled;
    }
};
