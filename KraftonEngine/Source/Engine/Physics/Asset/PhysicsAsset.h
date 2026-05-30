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
