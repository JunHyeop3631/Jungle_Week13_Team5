#pragma once

#include "Object/Object.h"
#include "Core/Types/CoreTypes.h"
#include "Physics/Asset/BodySetup.h"
#include "Physics/Asset/PhysicsConstraintSetup.h"
#include "Source/Engine/Physics/Asset/PhysicsAsset.generated.h"

// UPhysicsAsset  —  한 캐릭터의 모든 물리 설정을 담는 최상위 자산
UCLASS()
class UPhysicsAsset : public UObject
{
public:
	GENERATED_BODY()
    UPhysicsAsset() = default;
    ~UPhysicsAsset();

    FString AssetPathFileName;

    // 래그돌 자기충돌(같은 메시 내 바디끼리) 일괄 on/off.
    //   false → InstantiatePhysicsAssetBodies 가 모든 바디를 PxAggregate(enableSelfCollision=false)로 묶어 끔(월드와는 충돌).
    //   true  → 종전처럼 모든 바디가 서로 충돌(조인트 직결 쌍만 PhysX 기본으로 제외).
    UPROPERTY(Edit, Save, Category="Physics", DisplayName="Enable Self Collision")
    bool bEnableSelfCollision = false;

    UBodySetup* FindBodySetup(const FString& BoneName) const;
    UPhysicsConstraintSetup* FindConstraintSetup(const FString& Parent, const FString& Child) const;

    UBodySetup* GetOrCreateBodySetup(const FString& BoneName);
    UPhysicsConstraintSetup* GetOrCreateConstraintSetup(const FString& Parent, const FString& Child);

    void RemoveBodySetup(const FString& BoneName);
    void RemoveConstraintSetup(const FString& Parent, const FString& Child);

    const TArray<UBodySetup*>&              GetBodySetups()          const { return BodySetups; }
    const TArray<UPhysicsConstraintSetup*>& GetConstraints()         const { return ConstraintSetups; }
    TArray<UBodySetup*>&                    GetBodySetupsMutable()         { return BodySetups; }
    TArray<UPhysicsConstraintSetup*>&       GetConstraintsMutable()        { return ConstraintSetups; }

    // ── 바디쌍 충돌 비활성화 ──────────────────────────────────
    // 래그돌에서 인접한 두 바디(예: 위팔-아래팔)가 서로 부딪혀 떨리는 것을 막기 위해
    // 특정 본 쌍의 충돌을 꺼둔다. 본 이름 순서와 무관하게 동작한다.
    bool IsCollisionDisabled(const FString& BoneA, const FString& BoneB) const;
    void SetCollisionDisabled(const FString& BoneA, const FString& BoneB, bool bDisabled);
    int32 GetNumDisabledCollisionPairs() const { return (int32)DisabledCollisionPairs.size(); }
    void  ClearDisabledCollisionPairs() { DisabledCollisionPairs.clear(); }   // 전체 재생성(②) 시 일괄 비움

    void Serialize(FArchive& Ar);

private:
    struct FDisabledCollisionPair
    {
        FString BoneA;
        FString BoneB;
    };

    TArray<UBodySetup*>              BodySetups;
    TArray<UPhysicsConstraintSetup*> ConstraintSetups;
    TArray<FDisabledCollisionPair>  DisabledCollisionPairs;
};
