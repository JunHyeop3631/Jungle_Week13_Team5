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
    ~UPhysicsAsset()
    {
        for (UBodySetup* BS : BodySetups) { delete BS; }
        for (UPhysicsConstraintSetup* CS : ConstraintSetups) { delete CS; }
    }

    FString AssetPathFileName;

    UBodySetup* FindBodySetup(const FString& BoneName) const
    {
        for (UBodySetup* BS : BodySetups)
            if (BS && BS->BoneName == BoneName) return BS;
        return nullptr;
    }

    UPhysicsConstraintSetup* FindConstraintSetup(const FString& Parent, const FString& Child) const
    {
        for (UPhysicsConstraintSetup* CS : ConstraintSetups)
            if (CS && CS->ParentBoneName == Parent && CS->ChildBoneName == Child) return CS;
        return nullptr;
    }

    UBodySetup* GetOrCreateBodySetup(const FString& BoneName)
    {
        if (UBodySetup* E = FindBodySetup(BoneName)) return E;
        UBodySetup* BS = new UBodySetup();
        BS->BoneName = BoneName;
        BodySetups.push_back(BS);
        return BS;
    }

    UPhysicsConstraintSetup* GetOrCreateConstraintSetup(const FString& Parent, const FString& Child)
    {
        if (UPhysicsConstraintSetup* E = FindConstraintSetup(Parent, Child)) return E;
        UPhysicsConstraintSetup* CS = new UPhysicsConstraintSetup();
        CS->ParentBoneName = Parent;
        CS->ChildBoneName  = Child;
        ConstraintSetups.push_back(CS);
        return CS;
    }

    void RemoveBodySetup(const FString& BoneName)
    {
        for (auto It = BodySetups.begin(); It != BodySetups.end(); ++It)
        {
            if (*It && (*It)->BoneName == BoneName) { delete *It; BodySetups.erase(It); return; }
        }
    }

    void RemoveConstraintSetup(const FString& Parent, const FString& Child)
    {
        for (auto It = ConstraintSetups.begin(); It != ConstraintSetups.end(); ++It)
        {
            if (*It && (*It)->ParentBoneName == Parent && (*It)->ChildBoneName == Child)
            {
                delete *It; ConstraintSetups.erase(It); return;
            }
        }
    }

    const TArray<UBodySetup*>&              GetBodySetups()          const { return BodySetups; }
    const TArray<UPhysicsConstraintSetup*>& GetConstraints()         const { return ConstraintSetups; }
    TArray<UBodySetup*>&                    GetBodySetupsMutable()         { return BodySetups; }
    TArray<UPhysicsConstraintSetup*>&       GetConstraintsMutable()        { return ConstraintSetups; }

    // ── 바디쌍 충돌 비활성화 ──────────────────────────────────
    // 래그돌에서 인접한 두 바디(예: 위팔-아래팔)가 서로 부딪혀 떨리는 것을 막기 위해
    // 특정 본 쌍의 충돌을 꺼둔다. 본 이름 순서와 무관하게 동작한다.
    bool IsCollisionDisabled(const FString& BoneA, const FString& BoneB) const
    {
        for (const FDisabledCollisionPair& P : DisabledCollisionPairs)
            if ((P.BoneA == BoneA && P.BoneB == BoneB) || (P.BoneA == BoneB && P.BoneB == BoneA))
                return true;
        return false;
    }

    void SetCollisionDisabled(const FString& BoneA, const FString& BoneB, bool bDisabled)
    {
        if (BoneA == BoneB) return;
        const bool bAlready = IsCollisionDisabled(BoneA, BoneB);
        if (bDisabled && !bAlready)
        {
            DisabledCollisionPairs.push_back(FDisabledCollisionPair{ BoneA, BoneB });
        }
        else if (!bDisabled && bAlready)
        {
            for (auto It = DisabledCollisionPairs.begin(); It != DisabledCollisionPairs.end(); ++It)
            {
                if ((It->BoneA == BoneA && It->BoneB == BoneB) || (It->BoneA == BoneB && It->BoneB == BoneA))
                {
                    DisabledCollisionPairs.erase(It);
                    return;
                }
            }
        }
    }

    int32 GetNumDisabledCollisionPairs() const { return (int32)DisabledCollisionPairs.size(); }

    void Serialize(FArchive& Ar)
    {
        Ar << AssetPathFileName;

        // ── BodySetups ────────────────────────────────────────
        uint32 BodyCount = (uint32)BodySetups.size();
        Ar << BodyCount;
        if (Ar.IsLoading())
        {
            for (UBodySetup* BS : BodySetups) delete BS;
            BodySetups.clear();
            for (uint32 i = 0; i < BodyCount; ++i)
                BodySetups.push_back(new UBodySetup());
        }
        for (UBodySetup* BS : BodySetups)
            if (BS) BS->Serialize(Ar);

        // ── ConstraintSetups ──────────────────────────────────
        uint32 ConstraintCount = (uint32)ConstraintSetups.size();
        Ar << ConstraintCount;
        if (Ar.IsLoading())
        {
            for (UPhysicsConstraintSetup* CS : ConstraintSetups) delete CS;
            ConstraintSetups.clear();
            for (uint32 i = 0; i < ConstraintCount; ++i)
                ConstraintSetups.push_back(new UPhysicsConstraintSetup());
        }
        for (UPhysicsConstraintSetup* CS : ConstraintSetups)
            if (CS) CS->Serialize(Ar);

        // ── DisabledCollisionPairs ────────────────────────────
        // (이후 버전에서 추가됨. 구버전 에셋은 재저장 필요)
        uint32 PairCount = (uint32)DisabledCollisionPairs.size();
        Ar << PairCount;
        if (Ar.IsLoading())
        {
            DisabledCollisionPairs.clear();
            DisabledCollisionPairs.resize(PairCount);
        }
        for (FDisabledCollisionPair& P : DisabledCollisionPairs)
        {
            Ar << P.BoneA;
            Ar << P.BoneB;
        }
    }

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
