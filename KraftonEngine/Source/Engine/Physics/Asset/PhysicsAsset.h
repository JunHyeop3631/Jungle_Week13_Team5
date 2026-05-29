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

    const TArray<UBodySetup*>& GetBodySetups()  const { return BodySetups; }
    const TArray<UPhysicsConstraintSetup*>& GetConstraints() const { return ConstraintSetups; }
    TArray<UBodySetup*>& GetBodySetupsMutable()  { return BodySetups; }
    TArray<UPhysicsConstraintSetup*>& GetConstraintsMutable() { return ConstraintSetups; }

private:
    TArray<UBodySetup*> BodySetups;
    TArray<UPhysicsConstraintSetup*> ConstraintSetups;
};
