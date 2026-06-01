#include "Physics/Asset/PhysicsAsset.h"
#include "Serialization/Archive.h"

UPhysicsAsset::~UPhysicsAsset()
{
    for (UBodySetup* BS : BodySetups) { delete BS; }
    for (UPhysicsConstraintSetup* CS : ConstraintSetups) { delete CS; }
}

UBodySetup* UPhysicsAsset::FindBodySetup(const FString& BoneName) const
{
    for (UBodySetup* BS : BodySetups)
        if (BS && BS->BoneName == BoneName) return BS;
    return nullptr;
}

UPhysicsConstraintSetup* UPhysicsAsset::FindConstraintSetup(const FString& Parent, const FString& Child) const
{
    for (UPhysicsConstraintSetup* CS : ConstraintSetups)
        if (CS && CS->ParentBoneName == Parent && CS->ChildBoneName == Child) return CS;
    return nullptr;
}

UBodySetup* UPhysicsAsset::GetOrCreateBodySetup(const FString& BoneName)
{
    if (UBodySetup* E = FindBodySetup(BoneName)) return E;
    UBodySetup* BS = new UBodySetup();
    BS->BoneName = BoneName;
    BodySetups.push_back(BS);
    return BS;
}

UPhysicsConstraintSetup* UPhysicsAsset::GetOrCreateConstraintSetup(const FString& Parent, const FString& Child)
{
    if (UPhysicsConstraintSetup* E = FindConstraintSetup(Parent, Child)) return E;
    UPhysicsConstraintSetup* CS = new UPhysicsConstraintSetup();
    CS->ParentBoneName = Parent;
    CS->ChildBoneName  = Child;
    ConstraintSetups.push_back(CS);
    return CS;
}

void UPhysicsAsset::RemoveBodySetup(const FString& BoneName)
{
    for (auto It = BodySetups.begin(); It != BodySetups.end(); ++It)
    {
        if (*It && (*It)->BoneName == BoneName) { delete *It; BodySetups.erase(It); return; }
    }
}

void UPhysicsAsset::RemoveConstraintSetup(const FString& Parent, const FString& Child)
{
    for (auto It = ConstraintSetups.begin(); It != ConstraintSetups.end(); ++It)
    {
        if (*It && (*It)->ParentBoneName == Parent && (*It)->ChildBoneName == Child)
        {
            delete *It; ConstraintSetups.erase(It); return;
        }
    }
}

bool UPhysicsAsset::IsCollisionDisabled(const FString& BoneA, const FString& BoneB) const
{
    for (const FDisabledCollisionPair& P : DisabledCollisionPairs)
        if ((P.BoneA == BoneA && P.BoneB == BoneB) || (P.BoneA == BoneB && P.BoneB == BoneA))
            return true;
    return false;
}

void UPhysicsAsset::SetCollisionDisabled(const FString& BoneA, const FString& BoneB, bool bDisabled)
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

void UPhysicsAsset::Serialize(FArchive& Ar)
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

    // ── bEnableSelfCollision (이후 버전 추가) ──────────────────
    // 구버전 에셋엔 이 바이트가 없음 → 로드 시 ifstream.read 가 EOF 에서 버퍼를 건드리지 않아
    // 멤버 기본값(false)이 유지된다. 재저장하면 이후 정상 기록/로드.
    Ar << bEnableSelfCollision;
}
