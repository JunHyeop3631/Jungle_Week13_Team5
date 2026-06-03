#include "SkeletalMeshComponent.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"

#include "Animation/AnimationManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Animation/Sequence/AnimSequenceBase.h"
#include "Animation/Instance/AnimSingleNodeInstance.h"
#include "Animation/PoseContext.h"
#include "Asset/AssetRegistry.h"
#include "Core/Logging/Log.h"
#include "Component/Movement/MovementComponent.h"
#include "Component/Movement/CharacterMovementComponent.h"
#include "Component/PrimitiveComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Object/Object.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/Reflection/UClass.h"
#include "Physics/Asset/BodySetup.h"
#include "Physics/Asset/PhysicsAsset.h"
#include "Physics/Asset/PhysicsConstraintSetup.h"
#include "Physics/Cloth/IClothScene.h"
#include "Physics/IPhysicsScene.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Render/Scene/FScene.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    constexpr float DegreesToRadians(float Degrees)
    {
        return Degrees * PhysicsPi / 180.0f;
    }

    EPhysicsMotionType ToPhysicsMotion(EConstraintMotion Motion)
    {
        switch (Motion)
        {
        case EConstraintMotion::Free:
            return EPhysicsMotionType::Free;
        case EConstraintMotion::Limited:
            return EPhysicsMotionType::Limited;
        case EConstraintMotion::Locked:
        default:
            return EPhysicsMotionType::Locked;
        }
    }

    FTransform MakeUnitScaleTransform(const FVector& Location, const FQuat& Rotation = FQuat::Identity)
    {
        return FTransform(Location, Rotation, FVector(1.0f, 1.0f, 1.0f));
    }

    FTransform MakeRelativeTransform(const FTransform& WorldTransform, const FTransform& ParentWorldTransform)
    {
        return FTransform(WorldTransform.ToMatrix() * ParentWorldTransform.ToMatrix().GetInverse());
    }

    FTransform MakeWorldTransform(const FTransform& LocalTransform, const FTransform& ParentWorldTransform)
    {
        return FTransform(LocalTransform.ToMatrix() * ParentWorldTransform.ToMatrix());
    }

    void ApplyBodyMaterial(const UBodySetup& BodySetup, FPhysicsShapeDesc& ShapeDesc)
    {
        ShapeDesc.Material.StaticFriction = BodySetup.Friction;
        ShapeDesc.Material.DynamicFriction = BodySetup.Friction;
        ShapeDesc.Material.Restitution = BodySetup.Restitution;
        ShapeDesc.Material.Density = 1.0f;
    }

    float GetPhysicsAssetUniformScale(const FVector& WorldScale)
    {
        return (std::max)({ std::fabs(WorldScale.X), std::fabs(WorldScale.Y), std::fabs(WorldScale.Z), 0.001f });
    }

    bool IsNearlyUniformScale(const FVector& WorldScale, float Tolerance = 0.001f)
    {
        const float Scale = GetPhysicsAssetUniformScale(WorldScale);
        return std::fabs(std::fabs(WorldScale.X) - Scale) <= Tolerance
            && std::fabs(std::fabs(WorldScale.Y) - Scale) <= Tolerance
            && std::fabs(std::fabs(WorldScale.Z) - Scale) <= Tolerance;
    }

    void AppendPhysicsShapes(const UBodySetup& BodySetup, FPhysicsBodyDesc& BodyDesc, float PhysicsAssetScale)
    {
        for (const FKSphereElem& Sphere : BodySetup.AggregateGeom.SphereElems)
        {
            FPhysicsShapeDesc ShapeDesc;
            ShapeDesc.Name = BodySetup.BoneName + "_Sphere";
            ShapeDesc.ShapeType = EPhysicsShapeType::Sphere;
            ShapeDesc.LocalTransform = MakeUnitScaleTransform(Sphere.Center * PhysicsAssetScale);
            ShapeDesc.Radius = Sphere.Radius * PhysicsAssetScale;
            ApplyBodyMaterial(BodySetup, ShapeDesc);
            BodyDesc.Shapes.push_back(ShapeDesc);
        }

        for (const FKBoxElem& Box : BodySetup.AggregateGeom.BoxElems)
        {
            FPhysicsShapeDesc ShapeDesc;
            ShapeDesc.Name = BodySetup.BoneName + "_Box";
            ShapeDesc.ShapeType = EPhysicsShapeType::Box;
            ShapeDesc.LocalTransform = MakeUnitScaleTransform(Box.Center * PhysicsAssetScale, Box.Rotation);
            ShapeDesc.HalfExtent = FVector(Box.HalfX, Box.HalfY, Box.HalfZ) * PhysicsAssetScale;
            ApplyBodyMaterial(BodySetup, ShapeDesc);
            BodyDesc.Shapes.push_back(ShapeDesc);
        }

        for (const FKCapsuleElem& Capsule : BodySetup.AggregateGeom.CapsuleElems)
        {
            FPhysicsShapeDesc ShapeDesc;
            ShapeDesc.Name = BodySetup.BoneName + "_Capsule";
            ShapeDesc.ShapeType = EPhysicsShapeType::Capsule;
            ShapeDesc.LocalTransform = MakeUnitScaleTransform(Capsule.Center * PhysicsAssetScale, Capsule.Rotation);
            ShapeDesc.Radius = Capsule.Radius * PhysicsAssetScale;
            // PhysX PxCapsuleGeometry expects the half distance between the two sphere centers.
            ShapeDesc.HalfHeight = std::max(0.0f, Capsule.HalfHeight - Capsule.Radius) * PhysicsAssetScale;
            ApplyBodyMaterial(BodySetup, ShapeDesc);
            BodyDesc.Shapes.push_back(ShapeDesc);
        }
    }

    FVector TransformPoint(const FMatrix& Matrix, const FVector& Point)
    {
        return Matrix.TransformPositionWithW(Point);
    }

    float QuatAbsDot(const FQuat& A, const FQuat& B)
    {
        return std::fabs(A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W);
    }
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    ClearSkeletalClothBinding();
    DestroyPhysicsAssetBodies();                       // PhysX 바디 먼저 파기(자산 포인터 미보존 → 순서 안전)
    if (PhysicsAssetOverride) { delete PhysicsAssetOverride; PhysicsAssetOverride = nullptr; }
    ClearAnimInstance();
}

FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
    return new FSkeletalMeshSceneProxy(this);
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
    ClearSkeletalClothBinding();
    DestroyPhysicsAssetBodies();
    Super::SetSkeletalMesh(InMesh);
    // Mesh 가 바뀌면 이전 AnimInstance 가 가리키던 본 인덱스/카운트가 무의미해진다.
    // 새 SkeletalMesh 기준으로 AnimInstance 를 재인스턴스화한다.
    InitializeAnimation();

}

void USkeletalMeshComponent::PlayAnimation(UAnimSequenceBase* NewAnimToPlay, bool bLooping)
{
    SetAnimationMode(EAnimationMode::AnimationSingleNode);
    SetAnimation(NewAnimToPlay);
    SetLooping(bLooping);
    SetPlaying(NewAnimToPlay != nullptr);
}

void USkeletalMeshComponent::StopAnimation()
{
    SetAnimation(nullptr);
    SetPlaying(false);

    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetCurrentTime(0.0f);
    }
}

// ──────────────────────────────────────────────
// Animation API
// ──────────────────────────────────────────────
void USkeletalMeshComponent::SetAnimationMode(EAnimationMode InMode)
{
    if (AnimationMode == InMode) return;
    AnimationMode = InMode;
    InitializeAnimation();
}

bool USkeletalMeshComponent::CanUseAnimation(UAnimSequenceBase* InAsset) const
{
    if (!InAsset)
    {
        return true;
    }

    const USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh)
    {
        return false;
    }

    if (const UAnimSequence* Sequence = Cast<UAnimSequence>(InAsset))
    {
        FSkeletonCompatibilityReport Report;
        const bool bCompatible = FAssetRegistry::CheckAnimationForMesh(Sequence, Mesh, &Report);
        if (!bCompatible)
        {
            UE_LOG("SetAnimation rejected: skeleton mismatch. Anim=%s Mesh=%s Reason=%s",
                Sequence->GetName().c_str(),
                Mesh->GetName().c_str(),
                Report.Reason.c_str());
        }
        return bCompatible;
    }

    return true;
}

void USkeletalMeshComponent::SetAnimation(UAnimSequenceBase* InAsset)
{
    if (!CanUseAnimation(InAsset))
    {
        return;
    }

    AnimationData.AnimToPlay = InAsset;

    if (UAnimSequence* Sequence = Cast<UAnimSequence>(InAsset))
    {
        AnimationData.AnimToPlayPath = Sequence->GetAssetPathFileName();
    }
    else if (!InAsset)
    {
        AnimationData.AnimToPlayPath = "None";
    }

    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetAnimationAsset(InAsset);
    }
}

void USkeletalMeshComponent::SetPlayRate(float InRate)
{
    AnimationData.PlayRate = InRate;
    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetPlayRate(InRate);
    }
}

void USkeletalMeshComponent::SetLooping(bool bInLoop)
{
    AnimationData.bLooping = bInLoop;
    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetLooping(bInLoop);
    }
}

void USkeletalMeshComponent::SetPlaying(bool bInPlay)
{
    AnimationData.bPlaying = bInPlay;
    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetPlaying(bInPlay);
    }
}

void USkeletalMeshComponent::SetAnimInstanceClass(UClass* InClass)
{
    if (AnimInstanceClass.Get() == InClass) return;
    AnimInstanceClass = InClass;   // TSubclassOf 가 IsA 가드로 검증 (잘못된 클래스 → nullptr).
    if (AnimationMode == EAnimationMode::AnimationCustom)
    {
        InitializeAnimation();
    }
}

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
    if (AnimInstance == InInstance) return;
    ClearAnimInstance();
    AnimInstance = InInstance;
    if (AnimInstance)
    {
        AnimInstance->SetOuter(this);
        AnimInstance->SetOwningComponent(this);
        AnimInstance->NativeInitializeAnimation();
    }
}

UAnimSingleNodeInstance* USkeletalMeshComponent::GetAnimNodeInstance(FName NodeName) const
{
    (void)NodeName;
    return Cast<UAnimSingleNodeInstance>(AnimInstance);
}

void USkeletalMeshComponent::LoadAnimationFromPath()
{
    AnimationData.AnimToPlay = nullptr;

    if (AnimationData.AnimToPlayPath.empty() || AnimationData.AnimToPlayPath == "None")
    {
        return;
    }

    UAnimSequence* LoadedAnimation = FAnimationManager::Get().LoadAnimation(AnimationData.AnimToPlayPath.ToString());
    if (LoadedAnimation && CanUseAnimation(LoadedAnimation))
    {
        AnimationData.AnimToPlay = LoadedAnimation;
    }
    else
    {
        AnimationData.AnimToPlay = nullptr;
    }
}

void USkeletalMeshComponent::InitializeAnimation()
{
    if (!GetSkeletalMesh())
    {
        ClearAnimInstance();
        return;
    }
    if (AnimationMode == EAnimationMode::None)
    {
        ClearAnimInstance();
        return;
    }

    if (AnimationMode == EAnimationMode::AnimationSingleNode &&
        !AnimationData.AnimToPlay &&
        !AnimationData.AnimToPlayPath.empty() &&
        AnimationData.AnimToPlayPath != "None")
    {
        LoadAnimationFromPath();
    }

    if (AnimationMode == EAnimationMode::AnimationSingleNode && !CanUseAnimation(AnimationData.AnimToPlay))
    {
        AnimationData.AnimToPlay = nullptr;
        AnimationData.AnimToPlayPath = "None";
    }

    switch (AnimationMode)
    {
    case EAnimationMode::AnimationSingleNode:
    {
        ClearAnimInstance();

        UAnimSingleNodeInstance* Single =
            UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>(this);
        AnimInstance = Single;
        Single->SetOwningComponent(this);
        Single->SetAnimationAsset(AnimationData.AnimToPlay);
        Single->SetPlayRate(AnimationData.PlayRate);
        Single->SetLooping(AnimationData.bLooping);
        Single->SetPlaying(AnimationData.bPlaying && AnimationData.AnimToPlay != nullptr);
        Single->NativeInitializeAnimation();
        break;
    }
    case EAnimationMode::AnimationCustom:
    {
        UClass* DesiredClass = AnimInstanceClass.Get();
        if (!DesiredClass)
        {
            ClearAnimInstance();
            return;
        }

        if (AnimInstance && AnimInstance->GetClass() == DesiredClass)
        {
            AnimInstance->SetOuter(this);
            AnimInstance->SetOwningComponent(this);
            AnimInstance->NativeInitializeAnimation();
            break;
        }

        ClearAnimInstance();

        UObject* Obj = FObjectFactory::Get().Create(DesiredClass->GetName(), this);
        AnimInstance = Cast<UAnimInstance>(Obj);
		if (!AnimInstance)
        {
            // 클래스가 등록 안됐거나 캐스트 실패 — 무관한 객체가 생성됐을 수 있으니 정리.
            if (Obj) UObjectManager::Get().DestroyObject(Obj);
            return;
        }
        AnimInstance->SetOwningComponent(this);

        AnimInstance->NativeInitializeAnimation();
        break;
    }
    default:
        break;
    }
}

void USkeletalMeshComponent::ClearAnimInstance()
{
    if (AnimInstance)
    {
        UObjectManager::Get().DestroyObject(AnimInstance);
        AnimInstance = nullptr;
    }
}

bool USkeletalMeshComponent::ResolveSkeletalClothAttachment(FSkeletalClothParticleAttachment& Attachment) const
{
    if (Attachment.BoneIndex < 0 && !Attachment.BoneName.empty())
    {
        Attachment.BoneIndex = FindBoneIndex(Attachment.BoneName);
    }

    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    return Asset && Attachment.BoneIndex >= 0 && Attachment.BoneIndex < static_cast<int32>(Asset->Bones.size());
}

FMatrix USkeletalMeshComponent::GetSkeletalClothWorldMatrix() const
{
    return SkeletalClothBinding.ClothLocalTransform.ToMatrix() * GetWorldMatrix();
}

FMatrix USkeletalMeshComponent::GetSkeletalClothLocalMatrix() const
{
    return SkeletalClothBinding.ClothLocalTransform.ToMatrix();
}

bool USkeletalMeshComponent::BindSkeletalCloth(
    IClothScene& ClothScene,
    FClothInstance* ClothInstance,
    const FSkeletalClothBindingDesc& Desc)
{
    ClearSkeletalClothBinding();

    if (!ClothInstance || !ClothInstance->bValid || ClothInstance->NumParticles == 0)
    {
        return false;
    }

    SkeletalClothBinding = Desc;
    for (FSkeletalClothParticleAttachment& Attachment : SkeletalClothBinding.Attachments)
    {
        if (Attachment.ParticleIndex >= ClothInstance->NumParticles)
        {
            ClearSkeletalClothBinding();
            return false;
        }

        if (!ResolveSkeletalClothAttachment(Attachment))
        {
            ClearSkeletalClothBinding();
            return false;
        }
    }

    SkeletalClothSceneOwner = &ClothScene;
    SkeletalClothInstance = ClothInstance;
    bSkeletalClothBound = true;
    bResetSkeletalClothPinsNextTick = true;

    TickSkeletalCloth(0.0f);
    return true;
}

void USkeletalMeshComponent::ClearSkeletalClothBinding()
{
    SkeletalClothBinding = FSkeletalClothBindingDesc();
    SkeletalClothSceneOwner = nullptr;
    SkeletalClothInstance = nullptr;
    CachedSkeletalClothRenderData.Reset();
    CachedSkeletalClothParticlePositions.clear();
    bSkeletalClothBound = false;
    bResetSkeletalClothPinsNextTick = false;
}

bool USkeletalMeshComponent::BuildSkeletalClothPinnedParticles(
    TArray<FClothPinnedParticle>& OutPins,
    FClothConstraintDesc* OutConstraints) const
{
    OutPins.clear();
    if (OutConstraints)
    {
        *OutConstraints = FClothConstraintDesc();
    }

    if (!bSkeletalClothBound || !SkeletalClothInstance || !SkeletalClothInstance->bValid)
    {
        return false;
    }

    const uint32 NumParticles = SkeletalClothInstance->NumParticles;
    if (OutConstraints && SkeletalClothBinding.bUpdateMotionConstraints)
    {
        OutConstraints->MotionConstraints.resize(NumParticles);
        for (FClothMotionConstraint& Constraint : OutConstraints->MotionConstraints)
        {
            Constraint.Center = FVector::ZeroVector;
            Constraint.Radius = 1000000.0f;
        }

        OutConstraints->MotionConstraintScale = 1.0f;
        OutConstraints->MotionConstraintBias = 0.0f;
        OutConstraints->MotionConstraintStiffness = SkeletalClothBinding.MotionConstraintStiffness;
    }

    const FMatrix ClothWorldInv = GetSkeletalClothWorldMatrix().GetInverse();
    for (const FSkeletalClothParticleAttachment& Attachment : SkeletalClothBinding.Attachments)
    {
        if (Attachment.ParticleIndex >= NumParticles)
        {
            continue;
        }

        FTransform BoneWorld;
        if (!GetBoneWorldTransformByIndex(Attachment.BoneIndex, BoneWorld))
        {
            continue;
        }

        const FVector TargetWorld = TransformPoint(BoneWorld.ToMatrix(), Attachment.LocalPosition);
        const FVector TargetClothLocal = TransformPoint(ClothWorldInv, TargetWorld);

        if (Attachment.bPinned)
        {
            FClothPinnedParticle Pin;
            Pin.ParticleIndex = Attachment.ParticleIndex;
            Pin.Position = TargetClothLocal;
            OutPins.push_back(Pin);
        }

        if (OutConstraints &&
            SkeletalClothBinding.bUpdateMotionConstraints &&
            Attachment.bMotionConstrained &&
            Attachment.ParticleIndex < OutConstraints->MotionConstraints.size())
        {
            FClothMotionConstraint& Constraint = OutConstraints->MotionConstraints[Attachment.ParticleIndex];
            Constraint.Center = TargetClothLocal;
            Constraint.Radius = (std::max)(0.0f, Attachment.MaxDistance);
        }
    }

    return true;
}

bool USkeletalMeshComponent::BuildSkeletalClothCollision(FClothCollisionDesc& OutCollision) const
{
    OutCollision = FClothCollisionDesc();
    if (!bSkeletalClothBound || !SkeletalClothBinding.bUsePhysicsAssetCollision)
    {
        return false;
    }

    UPhysicsAsset* PhysicsAsset = SkeletalClothBinding.CollisionPhysicsAsset;
    if (!PhysicsAsset)
    {
        USkeletalMesh* Mesh = GetSkeletalMesh();
        PhysicsAsset = Mesh ? Mesh->PhysicsAsset : nullptr;
    }

    if (!PhysicsAsset)
    {
        return false;
    }

    const uint32 MaxSpheres = SkeletalClothBinding.MaxCollisionSpheres > 0
        ? SkeletalClothBinding.MaxCollisionSpheres
        : 32;
    const FMatrix ClothWorldInv = GetSkeletalClothWorldMatrix().GetInverse();

    auto HasSphereCapacity = [&OutCollision, MaxSpheres](uint32 Count)
    {
        return static_cast<uint32>(OutCollision.Spheres.size()) + Count <= MaxSpheres;
    };

    for (const UBodySetup* BodySetup : PhysicsAsset->GetBodySetups())
    {
        if (!BodySetup)
        {
            continue;
        }

        const int32 BoneIndex = FindBoneIndex(BodySetup->BoneName);
        FTransform BoneWorld;
        if (BoneIndex < 0 || !GetBoneWorldTransformByIndex(BoneIndex, BoneWorld))
        {
            continue;
        }

        const FQuat BoneWorldRot = BoneWorld.Rotation;
        const FVector BoneWorldPos = BoneWorld.Location;

        for (const FKSphereElem& Sphere : BodySetup->AggregateGeom.SphereElems)
        {
            if (!HasSphereCapacity(1) || Sphere.Radius <= 0.0f)
            {
                continue;
            }

            FClothCollisionSphere ClothSphere;
            ClothSphere.Center = TransformPoint(ClothWorldInv, BoneWorldPos + BoneWorldRot.RotateVector(Sphere.Center));
            ClothSphere.Radius = Sphere.Radius * SkeletalClothBinding.CollisionRadiusScale;
            OutCollision.Spheres.push_back(ClothSphere);
        }

        for (const FKCapsuleElem& Capsule : BodySetup->AggregateGeom.CapsuleElems)
        {
            if (!HasSphereCapacity(2) || Capsule.Radius <= 0.0f || Capsule.HalfHeight <= 0.0f)
            {
                continue;
            }

            const FQuat CapsuleWorldRot = BoneWorldRot * Capsule.Rotation;
            const FVector CapsuleWorldCenter = BoneWorldPos + BoneWorldRot.RotateVector(Capsule.Center);
            const FVector CapsuleAxis = CapsuleWorldRot.RotateVector(FVector::UpVector);
            const float HalfHeight = (std::max)(0.0f, Capsule.HalfHeight);
            const float Radius = Capsule.Radius * SkeletalClothBinding.CollisionRadiusScale;

            const uint32 SphereBase = static_cast<uint32>(OutCollision.Spheres.size());

            FClothCollisionSphere SphereA;
            SphereA.Center = TransformPoint(ClothWorldInv, CapsuleWorldCenter + CapsuleAxis * HalfHeight);
            SphereA.Radius = Radius;
            OutCollision.Spheres.push_back(SphereA);

            FClothCollisionSphere SphereB;
            SphereB.Center = TransformPoint(ClothWorldInv, CapsuleWorldCenter - CapsuleAxis * HalfHeight);
            SphereB.Radius = Radius;
            OutCollision.Spheres.push_back(SphereB);

            FClothCollisionCapsule ClothCapsule;
            ClothCapsule.SphereA = SphereBase;
            ClothCapsule.SphereB = SphereBase + 1;
            OutCollision.Capsules.push_back(ClothCapsule);
        }
    }

    return !OutCollision.Spheres.empty();
}

bool USkeletalMeshComponent::TickSkeletalCloth(float DeltaTime)
{
    if (!bSkeletalClothBound || !SkeletalClothSceneOwner || !SkeletalClothInstance || !SkeletalClothInstance->bValid)
    {
        return false;
    }

    TArray<FClothPinnedParticle> Pins;
    FClothConstraintDesc Constraints;
    if (BuildSkeletalClothPinnedParticles(Pins, &Constraints))
    {
        if (SkeletalClothBinding.bUpdatePinnedParticles && !Pins.empty())
        {
            const bool bResetPrevious = bResetSkeletalClothPinsNextTick ||
                SkeletalClothBinding.bResetPreviousPinnedParticlesEveryFrame;
            if (SkeletalClothSceneOwner->SetPinnedParticlePositions(SkeletalClothInstance, Pins, bResetPrevious))
            {
                bResetSkeletalClothPinsNextTick = false;
            }
        }

        if (SkeletalClothBinding.bUpdateMotionConstraints && !Constraints.MotionConstraints.empty())
        {
            SkeletalClothSceneOwner->SetClothConstraints(SkeletalClothInstance, Constraints);
        }
    }

    FClothCollisionDesc Collision;
    if (BuildSkeletalClothCollision(Collision))
    {
        SkeletalClothSceneOwner->SetClothCollision(SkeletalClothInstance, Collision);
    }

    if (SkeletalClothBinding.bAutoSimulate && DeltaTime > 0.0f)
    {
        SkeletalClothSceneOwner->SimulateCloth(DeltaTime);
    }

    SkeletalClothSceneOwner->GetClothParticlePositions(SkeletalClothInstance, CachedSkeletalClothParticlePositions);
    SkeletalClothSceneOwner->GetClothRenderData(SkeletalClothInstance, CachedSkeletalClothRenderData);
    return true;
}

bool USkeletalMeshComponent::GetSkeletalClothRenderData(FClothRenderData& OutRenderData) const
{
    if (!bSkeletalClothBound || !SkeletalClothSceneOwner || !SkeletalClothInstance || !SkeletalClothInstance->bValid)
    {
        OutRenderData.Reset();
        return false;
    }

    SkeletalClothSceneOwner->GetClothRenderData(SkeletalClothInstance, CachedSkeletalClothRenderData);

    if (CachedSkeletalClothRenderData.Vertices.empty())
    {
        OutRenderData.Reset();
        return false;
    }

    OutRenderData = CachedSkeletalClothRenderData;
    return true;
}

bool USkeletalMeshComponent::GetSkeletalClothStats(FClothStats& OutStats) const
{
    OutStats = FClothStats();
    if (!bSkeletalClothBound || !SkeletalClothSceneOwner || !SkeletalClothInstance || !SkeletalClothInstance->bValid)
    {
        return false;
    }

    SkeletalClothSceneOwner->GetClothStats(SkeletalClothInstance, OutStats);
    return OutStats.NumCloths > 0;
}

bool USkeletalMeshComponent::ExtractSkeletalClothDebugLines(
    TArray<FPhysicsDebugLine>& OutLines,
    const FClothDebugDrawOptions& Options) const
{
    if (!bSkeletalClothBound || !SkeletalClothSceneOwner || !SkeletalClothInstance || !SkeletalClothInstance->bValid)
    {
        return false;
    }

    TArray<FPhysicsDebugLine> LocalLines;
    SkeletalClothSceneOwner->ExtractClothDebugLines(SkeletalClothInstance, LocalLines, Options);
    if (LocalLines.empty())
    {
        return false;
    }

    const FMatrix ClothWorld = GetSkeletalClothWorldMatrix();
    OutLines.reserve(OutLines.size() + LocalLines.size());
    for (const FPhysicsDebugLine& LocalLine : LocalLines)
    {
        FPhysicsDebugLine WorldLine;
        WorldLine.Start = TransformPoint(ClothWorld, LocalLine.Start);
        WorldLine.End = TransformPoint(ClothWorld, LocalLine.End);
        WorldLine.Color = LocalLine.Color;
        OutLines.push_back(WorldLine);
    }

    return true;
}

bool USkeletalMeshComponent::InstantiatePhysicsAssetBodies(IPhysicsScene& Scene)
{
    return InstantiatePhysicsAssetBodies(Scene, GetPhysicsAsset());
}

bool USkeletalMeshComponent::InstantiatePhysicsAssetBodies(IPhysicsScene& Scene, UPhysicsAsset* PhysicsAsset)
{
    DestroyPhysicsAssetBodies();

    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!PhysicsAsset || !Asset || Asset->Bones.empty())
    {
        return false;
    }

    PhysicsSceneOwner = &Scene;
    Bodies.assign(Asset->Bones.size(), nullptr);
    BodyToBoneOffsets.assign(Asset->Bones.size(), FMatrix::Identity);
    Constraints.clear();

    const FVector PhysicsAssetWorldScale = GetWorldScale();
    const float PhysicsAssetScale = GetPhysicsAssetUniformScale(PhysicsAssetWorldScale);
    if (!IsNearlyUniformScale(PhysicsAssetWorldScale))
    {
        UE_LOG(
            "PhysicsAsset non-uniform component scale is approximated as uniform. Scale=(%.4f, %.4f, %.4f) Applied=%.4f",
            PhysicsAssetWorldScale.X,
            PhysicsAssetWorldScale.Y,
            PhysicsAssetWorldScale.Z,
            PhysicsAssetScale);
    }

    bool bCreatedAnyBody = false;

    // per-pair 자기충돌(DisabledCollisionPairs) 필터용. 생성 순서대로 0..31 인덱스를 부여한다
    // (본 인덱스는 32 를 넘길 수 있어 그대로 못 씀). 32 초과분은 필터 제외 + 1회 경고.
    TArray<int32> BodyFilterIndex;
    BodyFilterIndex.assign(Asset->Bones.size(), -1);
    int32 NextFilterIndex = 0;
    bool bRagdollFilterOverflow = false;

    // ── 자기충돌 제어: 같은 메시의 모든 바디를 하나의 PxAggregate 로 묶는다 ──
    //   enableSelfCollision = PhysicsAsset->bEnableSelfCollision.
    //     false → aggregate 내부(같은 메시) 바디끼리 충돌하지 않음(월드와는 충돌) — 래그돌 폭발 방지.
    //     true  → 종전처럼 모두 충돌(조인트 직결 쌍만 PhysX 기본 제외).
    //   PhysX 4.1 aggregate 상한(128) 초과 시 aggregate 없이 진행(자기충돌 끄기 불가 → 경고).
    {
        const int32 MaxActors = (int32)PhysicsAsset->GetBodySetups().size();
        if (MaxActors > 0 && MaxActors <= 128)
        {
            PhysicsAggregate = Scene.CreateAggregate(MaxActors, PhysicsAsset->bEnableSelfCollision);
        }
        else if (MaxActors > 128 && !PhysicsAsset->bEnableSelfCollision)
        {
            UE_LOG("PhysicsAsset self-collision disable skipped: body count %d exceeds PxAggregate max (128).", MaxActors);
        }
    }

    for (UBodySetup* BodySetup : PhysicsAsset->GetBodySetups())
    {
        if (!BodySetup || BodySetup->AggregateGeom.IsEmpty())
        {
            continue;
        }

        const int32 BoneIndex = FindBoneIndex(BodySetup->BoneName);
        if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bodies.size()))
        {
            UE_LOG("PhysicsAsset body skipped: bone not found. Bone=%s", BodySetup->BoneName.c_str());
            continue;
        }

        FTransform BoneWorldTransform;
        if (!GetBoneWorldTransformByIndex(BoneIndex, BoneWorldTransform))
        {
            UE_LOG("PhysicsAsset body skipped: could not resolve bone transform. Bone=%s", BodySetup->BoneName.c_str());
            continue;
        }

        FPhysicsBodyDesc BodyDesc;
        BodyDesc.OwnerComponent = this;
        BodyDesc.BodyName = BodySetup->BoneName;
        BodyDesc.BoneName = BodySetup->BoneName;
        BodyDesc.BoneIndex = BoneIndex;
        BodyDesc.BodyType = BodySetup->bSimulatePhysics ? EPhysicsBodyType::Dynamic : EPhysicsBodyType::Kinematic;
        BodyDesc.WorldTransform = BoneWorldTransform;
        BodyDesc.Mass = std::max(0.001f, BodySetup->Mass);
        BodyDesc.LinearDamping = BodySetup->LinearDamping;
        BodyDesc.AngularDamping = BodySetup->AngularDamping;
        BodyDesc.bUseGravity = true;
        BodyDesc.bEnableCCD = true;
        BodyDesc.bStartAwake = true;
        BodyDesc.Aggregate = PhysicsAggregate;   // 무효 핸들이면 CreateRigidBody 가 씬 직접 추가로 폴백

        AppendPhysicsShapes(*BodySetup, BodyDesc, PhysicsAssetScale);
        if (BodyDesc.Shapes.empty())
        {
            continue;
        }

        FBodyInstance* Body = Scene.CreateRigidBody(BodyDesc);
        if (!Body)
        {
            UE_LOG("PhysicsAsset body creation failed. Bone=%s", BodySetup->BoneName.c_str());
            continue;
        }

        Bodies[BoneIndex] = Body;
        {
            FTransform BodyWorldTransform;
            if (!Scene.GetBodyTransform(Body, BodyWorldTransform))
            {
                BodyWorldTransform = BodyDesc.WorldTransform;
            }
            BodyToBoneOffsets[BoneIndex] = BoneWorldTransform.ToMatrix() * BodyWorldTransform.ToMatrix().GetInverse();
        }
        bCreatedAnyBody = true;

        // per-pair 필터 인덱스 부여 (bEnableSelfCollision 일 때만 의미 있음).
        if (PhysicsAsset->bEnableSelfCollision)
        {
            if (NextFilterIndex <= 31)
            {
                BodyFilterIndex[BoneIndex] = NextFilterIndex++;
            }
            else if (!bRagdollFilterOverflow)
            {
                bRagdollFilterOverflow = true;
                UE_LOG("PhysicsAsset per-pair self-collision filter: ragdoll body count exceeds 32; extra bodies keep colliding.");
            }
        }
    }

    if (!bCreatedAnyBody)
    {
        // 바디가 하나도 안 생겼으면 위에서 만든 빈 aggregate 도 정리(누수/씬 잔류 방지).
        if (PhysicsAggregate.IsValid())
        {
            Scene.DestroyAggregate(PhysicsAggregate);
            PhysicsAggregate = {};
        }
        Bodies.clear();
        BodyToBoneOffsets.clear();
        PhysicsSceneOwner = nullptr;
        return false;
    }

    for (UPhysicsConstraintSetup* Setup : PhysicsAsset->GetConstraints())
    {
        if (!Setup)
        {
            continue;
        }

        const int32 ParentBoneIndex = FindBoneIndex(Setup->ParentBoneName);
        const int32 ChildBoneIndex = FindBoneIndex(Setup->ChildBoneName);
        FBodyInstance* ParentBody = GetBodyInstanceByBoneIndex(ParentBoneIndex);
        FBodyInstance* ChildBody = GetBodyInstanceByBoneIndex(ChildBoneIndex);
        if (!ParentBody || !ChildBody)
        {
            UE_LOG("PhysicsAsset constraint skipped: body not found. Parent=%s Child=%s",
                Setup->ParentBoneName.c_str(),
                Setup->ChildBoneName.c_str());
            continue;
        }

        FTransform ParentBodyWorld;
        FTransform ChildBodyWorld;
        if (!Scene.GetBodyTransform(ParentBody, ParentBodyWorld) ||
            !Scene.GetBodyTransform(ChildBody, ChildBodyWorld))
        {
            UE_LOG("PhysicsAsset constraint skipped: body transform unavailable. Parent=%s Child=%s",
                Setup->ParentBoneName.c_str(),
                Setup->ChildBoneName.c_str());
            continue;
        }

        const FTransform ParentLocalFrame = MakeUnitScaleTransform(Setup->ParentAnchorPos * PhysicsAssetScale, Setup->ParentAnchorRot);
        const FTransform JointWorldFrame = MakeWorldTransform(ParentLocalFrame, ParentBodyWorld);

        FPhysicsConstraintDesc ConstraintDesc;
        ConstraintDesc.ConstraintName = Setup->ParentBoneName + "_" + Setup->ChildBoneName;
        ConstraintDesc.ParentBody = ParentBody;
        ConstraintDesc.ChildBody = ChildBody;
        ConstraintDesc.ParentLocalFrame = ParentLocalFrame;
        ConstraintDesc.ChildLocalFrame = MakeRelativeTransform(JointWorldFrame, ChildBodyWorld);

        const EPhysicsMotionType LinearMotion = Setup->bLockLinearMotion
            ? EPhysicsMotionType::Locked
            : EPhysicsMotionType::Free;
        ConstraintDesc.LinearX = LinearMotion;
        ConstraintDesc.LinearY = LinearMotion;
        ConstraintDesc.LinearZ = LinearMotion;

        ConstraintDesc.Twist = ToPhysicsMotion(Setup->TwistMotion);
        ConstraintDesc.Swing1 = ToPhysicsMotion(Setup->Swing1Motion);
        ConstraintDesc.Swing2 = ToPhysicsMotion(Setup->Swing2Motion);
        ConstraintDesc.TwistLimitRadiansMin = -DegreesToRadians(Setup->TwistLimitAngle);
        ConstraintDesc.TwistLimitRadiansMax = DegreesToRadians(Setup->TwistLimitAngle);
        ConstraintDesc.Swing1LimitRadians = DegreesToRadians(Setup->Swing1LimitAngle);
        ConstraintDesc.Swing2LimitRadians = DegreesToRadians(Setup->Swing2LimitAngle);

        FConstraintInstance* Constraint = Scene.CreateD6Joint(ConstraintDesc);
        if (!Constraint)
        {
            UE_LOG("PhysicsAsset constraint creation failed. Parent=%s Child=%s",
                Setup->ParentBoneName.c_str(),
                Setup->ChildBoneName.c_str());
            continue;
        }

        Constraints.push_back(Constraint);
    }

    // ── DisabledCollisionPairs → PhysX 시뮬 필터 반영 ─────────────────
    // bEnableSelfCollision=true 일 때만 의미. false 면 aggregate 가 이미 전 쌍을 끄므로 불필요.
    if (PhysicsAsset->bEnableSelfCollision)
    {
        static uint32 GNextRagdollFilterGroupId = 1;
        RagdollFilterGroupId = GNextRagdollFilterGroupId++;
        if (GNextRagdollFilterGroupId == 0) GNextRagdollFilterGroupId = 1;   // 0(=비-래그돌) 회피

        for (int32 i = 0; i < static_cast<int32>(Bodies.size()); ++i)
        {
            if (!Bodies[i] || BodyFilterIndex[i] < 0) continue;

            uint32 IgnoreMask = 0;
            for (int32 j = 0; j < static_cast<int32>(Bodies.size()); ++j)
            {
                if (i == j || !Bodies[j] || BodyFilterIndex[j] < 0) continue;
                if (PhysicsAsset->IsCollisionDisabled(Bodies[i]->BoneName, Bodies[j]->BoneName))
                {
                    IgnoreMask |= (1u << BodyFilterIndex[j]);
                }
            }
            Scene.SetRagdollBodyFilter(Bodies[i], RagdollFilterGroupId, static_cast<uint32>(BodyFilterIndex[i]), IgnoreMask);
        }
    }

    return true;
}

void USkeletalMeshComponent::DestroyPhysicsAssetBodies()
{
    if (PhysicsSceneOwner)
    {
        for (FConstraintInstance* Constraint : Constraints)
        {
            if (Constraint)
            {
                PhysicsSceneOwner->DestroyJoint(Constraint);
            }
        }

        for (FBodyInstance* Body : Bodies)
        {
            if (Body)
            {
                PhysicsSceneOwner->DestroyRigidBody(Body);
            }
        }

        // 바디(actor) release 후 빈 aggregate 해제 (actor release 가 aggregate 에서 자동 제거되므로 이 순서).
        if (PhysicsAggregate.IsValid())
        {
            PhysicsSceneOwner->DestroyAggregate(PhysicsAggregate);
        }
    }

    PhysicsAggregate = {};
    RagdollFilterGroupId = 0;
    Constraints.clear();
    Bodies.clear();
    BodyToBoneOffsets.clear();
    PhysicsSceneOwner = nullptr;
}

FBodyInstance* USkeletalMeshComponent::GetBodyInstanceByBoneIndex(int32 BoneIndex) const
{
    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bodies.size()))
    {
        return nullptr;
    }

    return Bodies[BoneIndex];
}

FBodyInstance* USkeletalMeshComponent::GetBodyInstanceByBoneName(const FString& BoneName) const
{
    return GetBodyInstanceByBoneIndex(FindBoneIndex(BoneName));
}

bool USkeletalMeshComponent::EnterRagdollState()
{
    // 사전조건: 이미 인스턴스화돼 있어야 한다 (Bodies / PhysicsSceneOwner).
    if (!PhysicsSceneOwner || Bodies.empty())
    {
        UE_LOG("EnterRagdollState skipped: physics asset bodies are not instantiated.");
        return false;
    }

    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!Asset || Asset->Bones.empty())
    {
        return false;
    }

    // 1) 현재 본 월드 트랜스폼을 body 에 강제로 동기화한다 — kinematic 동안 누적된 anim pose 와
    //    InstantiatePhysicsAssetBodies 직후 ref pose 사이의 격차로 인한 튐을 막는다.
    //    teleport=true 경로는 setGlobalPose 직접 호출이라 kinematic flag 영향 없이 actor 위치만 갱신된다.
    // 2) 이어서 SetBodyType(Dynamic) — kinematic flag 해제 + wakeUp.
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bodies.size()); ++BoneIndex)
    {
        FBodyInstance* Body = Bodies[BoneIndex];
        if (!Body || !Body->bValid)
        {
            continue;
        }

        FTransform BoneWorldTransform;
        if (GetBoneWorldTransformByIndex(BoneIndex, BoneWorldTransform))
        {
            FTransform TargetBodyWorld = BoneWorldTransform;
            if (BoneIndex < static_cast<int32>(BodyToBoneOffsets.size()))
            {
                TargetBodyWorld = FTransform(BodyToBoneOffsets[BoneIndex].GetInverse() * BoneWorldTransform.ToMatrix());
            }

            PhysicsSceneOwner->SetBodyTransform(Body, TargetBodyWorld, /*bTeleport*/ true);

            // [로그 스팸 방지] 래그돌 진입 시 바디마다 찍던 pose-sync 진단 로그 — 기본 off.
            // 대량 바디 PhysicsAsset 이면 진입 한 번에 수십 줄이 콘솔/파일로 쏟아져, 무한 콘솔 버퍼와
            // 줄당 fflush 와 맞물려 메모리 폭증/프리즈를 키웠다. 디버깅 필요 시 true 로.
            static constexpr bool bLogRagdollSyncPose = false;
            if (bLogRagdollSyncPose)
            {
                FTransform BodyWorldAfterSync;
                if (PhysicsSceneOwner->GetBodyTransform(Body, BodyWorldAfterSync))
                {
                    FTransform BoneWorldAfterSync = BodyWorldAfterSync;
                    if (BoneIndex < static_cast<int32>(BodyToBoneOffsets.size()))
                    {
                        BoneWorldAfterSync = FTransform(BodyToBoneOffsets[BoneIndex] * BodyWorldAfterSync.ToMatrix());
                    }

                    const FVector DeltaLocation = BoneWorldAfterSync.Location - BoneWorldTransform.Location;
                    const float RotationAbsDot = QuatAbsDot(BoneWorldAfterSync.Rotation.GetNormalized(), BoneWorldTransform.Rotation.GetNormalized());
                    UE_LOG(
                        "Ragdoll sync pose: Bone=%s Index=%d BoneLoc=(%.4f, %.4f, %.4f) BodyLoc=(%.4f, %.4f, %.4f) OffsetBoneLoc=(%.4f, %.4f, %.4f) DeltaLoc=(%.6f, %.6f, %.6f) DeltaLen=%.6f RotAbsDot=%.6f",
                        Body->BoneName.c_str(),
                        BoneIndex,
                        BoneWorldTransform.Location.X,
                        BoneWorldTransform.Location.Y,
                        BoneWorldTransform.Location.Z,
                        BodyWorldAfterSync.Location.X,
                        BodyWorldAfterSync.Location.Y,
                        BodyWorldAfterSync.Location.Z,
                        BoneWorldAfterSync.Location.X,
                        BoneWorldAfterSync.Location.Y,
                        BoneWorldAfterSync.Location.Z,
                        DeltaLocation.X,
                        DeltaLocation.Y,
                        DeltaLocation.Z,
                        DeltaLocation.Length(),
                        RotationAbsDot);
                }
                else
                {
                    UE_LOG("Ragdoll sync pose: GetBodyTransform failed after SetBodyTransform. Bone=%s Index=%d", Body->BoneName.c_str(), BoneIndex);
                }
            }
        }

        PhysicsSceneOwner->SetBodyType(Body, EPhysicsBodyType::Dynamic);

        // 진입 시점의 이동/낙하 관성을 모든 바디에 동일 선형 속도로 부여 → 가속/관성을 유지하며
        // 날아간다. teleport(setGlobalPose) 는 속도에 영향이 없고, Dynamic 전환 직후라야 적용된다.
        // 속도가 0(정지 중 진입)이면 그대로 제자리에서 쓰러진다 — 무해.
        PhysicsSceneOwner->SetBodyLinearVelocity(Body, RagdollEntryLinearVelocity);
    }

    return true;
}

void USkeletalMeshComponent::CreateRagdoll()
{
    // 애니메이션 평가 차단 — 진입 성공 시 다음 TickComponent 부터 ApplyPhysicsToBones 경로로 분기된다.
    if (EnterRagdollState())
    {
        bSimulatingPhysics = true;
    }
}

void USkeletalMeshComponent::DeactivateOwnerMovementForRagdoll()
{
    RagdollDeactivatedMovement.clear();
    RagdollEntryLinearVelocity = FVector(0.0f, 0.0f, 0.0f);

    AActor* OwnerActor = GetOwner();
    if (!OwnerActor)
    {
        return;
    }

    // Movement 컴포넌트만 정지한다(active 였던 것만) — 캡슐을 계속 구동해 래그돌을 "조작" 하지
    // 못하게 막는다. 형제 충돌 컴포넌트(캡슐 등)의 collision 은 끄지 않는다: 09_ragdoll_session_fixes.md
    // §5-3 의 same-actor 필터 셰이더(word3 = owner UUID 일치 시 SUPPRESS)가 캡슐 ↔ 래그돌 바디
    // "중복 충돌" 을 이미 막으므로 콜라이더를 켜둬도 튕기지 않는다. 켜둬야 캡슐이 래그돌 중에도
    // 월드와 충돌하며 물리로 "움직" 인다 → 결과적으로 "움직이되 조작 불가".
    for (UActorComponent* Comp : OwnerActor->GetComponents())
    {
        if (UMovementComponent* Move = Cast<UMovementComponent>(Comp))
        {
            // 정지 "직전" 의 속도를 캡처해 래그돌 진입 관성으로 넘긴다(걷기=XY, 낙하=XY+중력 Z).
            // 속도 벡터는 CharacterMovement 만 보유 — 정지(SetActive(false)) 는 이 값을 건드리지 않지만
            // 의미상 정지 전에 읽는다.
            if (UCharacterMovementComponent* CharacterMove = Cast<UCharacterMovementComponent>(Move))
            {
                RagdollEntryLinearVelocity = CharacterMove->GetVelocity();
            }

            if (Move->IsActive())
            {
                Move->SetActive(false);
                RagdollDeactivatedMovement.push_back(Move);
            }
        }
    }
}

void USkeletalMeshComponent::RestoreOwnerMovementAfterRagdoll()
{
    // dangling 방지: 저장 포인터를 곧장 deref 하지 않고, Owner 의 "현재" 컴포넌트 목록에
    // 아직 존재하는(주소 일치) 것만 복원한다. 래그돌 중 파괴된 컴포넌트는 목록에서 빠지므로 건너뛴다.
    if (AActor* OwnerActor = GetOwner())
    {
        const TArray<UActorComponent*>& Live = OwnerActor->GetComponents();
        auto IsLive = [&Live](const UActorComponent* C) -> bool
        {
            for (const UActorComponent* L : Live)
            {
                if (L == C) return true;
            }
            return false;
        };

        for (UActorComponent* Move : RagdollDeactivatedMovement)
        {
            if (Move && IsLive(Move))
            {
                Move->SetActive(true);
            }
        }
    }

    RagdollDeactivatedMovement.clear();
}

void USkeletalMeshComponent::SetSimulatingPhysics(bool bSimulate)
{
    if (bSimulate == bSimulatingPhysics)
    {
        return;
    }

    if (bSimulate)
    {
        // movement 를 정지해 캡슐을 더는 구동하지 않게 한다(래그돌 "조작" 차단). 캡슐 등 형제
        // 콜라이더의 collision 은 끄지 않는다 — same-actor 필터 셰이더(§5-3)가 캡슐 ↔ 래그돌 바디
        // 중복 충돌을 막으므로, 콜라이더를 켜둬야 캡슐이 래그돌 중에도 물리로 움직인다.
        // 진입(인스턴스화/EnterRagdollState) 실패 시 정지한 movement 를 원복한다.
        DeactivateOwnerMovementForRagdoll();

        // 진입 시점에 바디가 없으면 소유 월드의 물리 씬에서 즉석 인스턴스화한다.
        // InstantiatePhysicsAssetBodies 가 현재 애님 본 월드 포즈를 읽으므로 진입 순간 포즈가 그대로 포착된다.
        if (!PhysicsSceneOwner || Bodies.empty())
        {
            UWorld* World = GetWorld();
            IPhysicsScene* Scene = World ? World->GetPhysicsScene() : nullptr;
            UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
            if (!Scene || !PhysicsAsset)
            {
                UE_LOG("SetSimulatingPhysics(true) skipped: no physics scene or physics asset.");
                RestoreOwnerMovementAfterRagdoll();
                return;
            }
            if (!InstantiatePhysicsAssetBodies(*Scene, PhysicsAsset))
            {
                UE_LOG("SetSimulatingPhysics(true) skipped: physics asset instantiation failed.");
                RestoreOwnerMovementAfterRagdoll();
                return;
            }
        }

        if (EnterRagdollState())
        {
            bSimulatingPhysics = true;

            // 루트 캡슐(형제 콜라이더)의 충돌/시뮬을 끈다 — same-actor 필터에 의존하지 않고, 캡슐이
            // 래그돌 바디와 충돌해 정렬을 망가뜨리는 것을 원천 차단(캡슐과 메시는 같은 액터). 진입 전
            // 상태를 저장해 종료 시 복원. (SetCollisionEnabled(NoCollision) 이 캡슐 물리 바디를 등록 해제.)
            if (AActor* OwnerActor = GetOwner())
            {
                if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent()))
                {
                    if (RootPrim != this && !bRagdollRootCollisionDisabled)
                    {
                        RagdollSavedRootCollision = RootPrim->GetCollisionEnabled();
                        RagdollSavedRootBodyMode  = RootPrim->GetPhysicsBodyMode();
                        RootPrim->SetPhysicsBodyMode(EPhysicsBodyMode::Static);
                        RootPrim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                        bRagdollRootCollisionDisabled = true;
                    }
                }
            }
        }
        else
        {
            RestoreOwnerMovementAfterRagdoll();
        }
    }
    else
    {
        // 시뮬 종료: 바디를 kinematic 으로 되돌리고 플래그 해제 → 다음 TickComponent 가 애님 경로로 복귀.
        // 바디는 파기하지 않으므로 이후 재진입은 EnterRagdollState 한 번이면 된다.
        if (PhysicsSceneOwner)
        {
            for (FBodyInstance* Body : Bodies)
            {
                if (Body && Body->bValid)
                {
                    PhysicsSceneOwner->SetBodyType(Body, EPhysicsBodyType::Kinematic);
                }
            }
        }
        bSimulatingPhysics = false;
        // 진입 시 정지한 movement 복원 (dangling 안전).
        RestoreOwnerMovementAfterRagdoll();

        // 진입 시 끈 루트 캡슐 충돌/시뮬 복원 (owner 에서 재해석 — dangling 안전).
        if (bRagdollRootCollisionDisabled)
        {
            if (AActor* OwnerActor = GetOwner())
            {
                if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent()))
                {
                    if (RootPrim != this)
                    {
                        RootPrim->SetCollisionEnabled(RagdollSavedRootCollision);
                        RootPrim->SetPhysicsBodyMode(RagdollSavedRootBodyMode);
                    }
                }
            }
            bRagdollRootCollisionDisabled = false;
        }
    }
}

bool USkeletalMeshComponent::HasPhysicsAsset() const
{
    return GetPhysicsAsset() != nullptr;
}

UPhysicsAsset* USkeletalMeshComponent::GetPhysicsAsset() const
{
    // per-instance override 우선, 없으면 메시(에디터 세션이 꽂은 것)로 폴백.
    if (PhysicsAssetOverride)
    {
        return PhysicsAssetOverride;
    }
    USkeletalMesh* Mesh = GetSkeletalMesh();
    return Mesh ? Mesh->PhysicsAsset : nullptr;
}

void USkeletalMeshComponent::LoadPhysicsAssetFromPath()
{
    if (PhysicsAssetOverride)
    {
        delete PhysicsAssetOverride;
        PhysicsAssetOverride = nullptr;
    }

    FString Path;
    if (!PhysicsAssetPath.empty() && PhysicsAssetPath != "None")
    {
        Path = PhysicsAssetPath.ToString();
    }
    else if (USkeletalMesh* Mesh = GetSkeletalMesh())
    {
        // 규약 자동 기본값: <Mesh>_Physics.uasset (에디터 SavePhysicsAsset 저장 위치와 동일 규약).
        const FString MeshPath = Mesh->GetAssetPathFileName();
        if (!MeshPath.empty() && MeshPath != "None")
        {
            Path = UPhysicsAsset::MakeSiblingPath(MeshPath);
        }
    }

    if (!Path.empty() && Path != "None")
    {
        PhysicsAssetOverride = UPhysicsAsset::LoadFromFile(Path);
    }
}

void USkeletalMeshComponent::PostDuplicate()
{
    Super::PostDuplicate();        // USkinnedMeshComponent: SkeletalMeshPath → SetSkeletalMesh
    LoadPhysicsAssetFromPath();    // 이어서 PhysicsAssetPath(또는 규약 기본값) 해석
}

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
    // 런타임 디버그 표시(컴포넌트 단위 bShowPhysicsBodies). 분기와 무관하게 매 틱 1회 큐에 push.
    DrawRuntimePhysicsBodies();

    if (bSimulatingPhysics)
    {
        // Passive ragdoll: AnimInstance 평가 skip, PhysX body → 본 local pose write-back.
        ApplyPhysicsToBones();
        TickSkeletalCloth(DeltaTime);
        UMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
        return;
    }

    if (EvaluateAnimInstance(DeltaTime))
    {
        TickSkeletalCloth(DeltaTime);
        UMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
        return;
    }

    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    TickSkeletalCloth(DeltaTime);
}

void USkeletalMeshComponent::ApplyPhysicsToBones()
{
    if (!PhysicsSceneOwner)
    {
        return;
    }

    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!Asset || Asset->Bones.empty())
    {
        return;
    }

    const int32 BoneCount = static_cast<int32>(Asset->Bones.size());

    // ComponentLocalGlobals[i] 는 component-local 본 글로벌 행렬을 누적한다.
    // 1) body 가 있는 본은 PhysX 결과로 채운다.
    // 2) body 가 없는 중간 본은 유효한 자식 본에서 ref local 을 역산해 따라오게 한다.
    // 3) 남은 본은 parent-first ref pose 로 채운 뒤, 마지막에 local pose 로 변환한다.
    TArray<FMatrix> ComponentLocalGlobals;
    ComponentLocalGlobals.resize(BoneCount, FMatrix::Identity);

    TArray<uint8> bHasSolvedGlobal;
    bHasSolvedGlobal.assign(BoneCount, 0);

    TArray<FTransform> LocalPose;
    LocalPose.resize(BoneCount);

    // body 트랜스폼은 world 기준이고 본 local pose 는 component-local 누적이므로 world↔component 변환이 필요하다.
    // BodyToBoneOffsets 는 현재 authoring path 에선 보통 identity 이지만, write-back 은 항상 명시적으로
    // BoneWorld = BodyToBone * BodyWorld 관계를 통해 계산한다.
    const FMatrix ComponentWorldInv = GetWorldInverseMatrix();

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        FBodyInstance* Body = (BoneIndex < static_cast<int32>(Bodies.size())) ? Bodies[BoneIndex] : nullptr;

        if (Body && Body->bValid)
        {
            FTransform BodyWorld;
            if (PhysicsSceneOwner->GetBodyTransform(Body, BodyWorld))
            {
                FMatrix BoneWorldMatrix = BodyWorld.ToMatrix();
                if (BoneIndex < static_cast<int32>(BodyToBoneOffsets.size()))
                {
                    BoneWorldMatrix = BodyToBoneOffsets[BoneIndex] * BoneWorldMatrix;
                }

                // body world -> bone world -> component-local global.
                FMatrix ComponentGlobal = BoneWorldMatrix * ComponentWorldInv;

                // 컴포넌트 월드 스케일(예: 씬 2x)이 ComponentWorldInv 를 통해 본 글로벌 선형부에 1/S 로
                // 새어들어가 스키닝 행렬(InverseBind * Global)을 왜곡한다 → 메시가 body 에 쪼그라들어 끼는 현상.
                // (에디터 프리뷰는 컴포넌트 스케일이 1 이라 이 누수가 없다. 런타임 씬 스케일!=1 에서만 발생.)
                // 반드시 글로벌 단계에서 스케일을 제거해 회전+이동만 남긴다 — 이래야 자식의 상대 로컬
                // (translation 포함)이 scale-1 부모 기준으로 일관되게 계산된다. local 단계에서 지우면
                // 자식 translation 이 어긋난다. 스케일은 애님 경로처럼 렌더러의 컴포넌트 월드행렬이 일괄
                // 적용한다. (균등 스케일 가정 — 비균등은 미지원)
                FTransform GlobalNoScale(ComponentGlobal);
                GlobalNoScale.Scale = FVector::OneVector;
                ComponentGlobal = GlobalNoScale.ToMatrix();

                ComponentLocalGlobals[BoneIndex] = ComponentGlobal;
                bHasSolvedGlobal[BoneIndex] = 1;
            }
        }
    }

    for (int32 BoneIndex = BoneCount - 1; BoneIndex >= 0; --BoneIndex)
    {
        if (bHasSolvedGlobal[BoneIndex])
        {
            continue;
        }

        int32 SolvedChildIndex = -1;
        int32 SolvedChildCount = 0;
        for (int32 ChildIndex = 0; ChildIndex < BoneCount; ++ChildIndex)
        {
            if (Asset->Bones[ChildIndex].ParentIndex != BoneIndex || !bHasSolvedGlobal[ChildIndex])
            {
                continue;
            }

            SolvedChildIndex = ChildIndex;
            ++SolvedChildCount;
            if (SolvedChildCount > 1)
            {
                break;
            }
        }

        if (SolvedChildCount == 1)
        {
            const FMatrix ChildRefLocal = Asset->Bones[SolvedChildIndex].GetReferenceLocalPose();
            ComponentLocalGlobals[BoneIndex] = ChildRefLocal.GetInverse() * ComponentLocalGlobals[SolvedChildIndex];
            bHasSolvedGlobal[BoneIndex] = 1;
        }
    }

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        if (!bHasSolvedGlobal[BoneIndex])
        {
            const int32 ParentIndex = Asset->Bones[BoneIndex].ParentIndex;
            const FMatrix ParentGlobal = (ParentIndex >= 0)
                ? ComponentLocalGlobals[ParentIndex]
                : FMatrix::Identity;
            ComponentLocalGlobals[BoneIndex] = Asset->Bones[BoneIndex].GetReferenceLocalPose() * ParentGlobal;
        }
    }

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const int32 ParentIndex = Asset->Bones[BoneIndex].ParentIndex;
        const FMatrix ParentGlobal = (ParentIndex >= 0)
            ? ComponentLocalGlobals[ParentIndex]
            : FMatrix::Identity;
        const FMatrix LocalMatrix = (ParentIndex >= 0)
            ? ComponentLocalGlobals[BoneIndex] * ParentGlobal.GetInverse()
            : ComponentLocalGlobals[BoneIndex];
        LocalPose[BoneIndex] = FTransform(LocalMatrix);
    }

    // CPU skinning / bounds dirty 는 SetBoneLocalTransforms 안의 RefreshSkinningAfterPoseChanged 에서 처리된다.
    SetBoneLocalTransforms(LocalPose);
}

// 디버그: 선택 시 PhysicsAsset 바디 셰이프를 와이어로 표시 (레벨 뷰포트).
//   레벨에선 런타임 Bodies 가 비어 있으므로 PhysicsAsset 셰이프를 현재 본 월드 포즈로 그린다.
//   FScene::AddDebugLine 채널(선택 액터에 매 프레임 호출되는 ContributeSelectedVisuals)을 사용.
namespace
{
	constexpr float kDbgPi  = 3.14159265f;
	constexpr float kDbgPi2 = kDbgPi * 2.0f;

	// 와이어 라인 sink. 색/제출처(FScene vs DebugDrawQueue)는 호출측 lambda 가 결정한다.
	using FDbgLineSink = std::function<void(const FVector&, const FVector&)>;

	void DbgWireSphere(const FDbgLineSink& Emit, const FVector& C, float R)
	{
		constexpr int32 Seg = 16;
		const FVector Ax[3] = { FVector(1,0,0), FVector(0,1,0), FVector(0,0,1) };
		for (int32 p = 0; p < 3; ++p)
		{
			const FVector U = Ax[p], V = Ax[(p + 1) % 3];
			FVector Prev = C + U * R;
			for (int32 i = 1; i <= Seg; ++i)
			{
				const float a = kDbgPi2 * i / Seg;
				const FVector Cur = C + (U * cosf(a) + V * sinf(a)) * R;
				Emit(Prev, Cur);
				Prev = Cur;
			}
		}
	}

	void DbgWireBox(const FDbgLineSink& Emit, const FVector& C, const FQuat& Rot, float HX, float HY, float HZ)
	{
		const FVector L[8] = {
			{-HX,-HY,-HZ},{HX,-HY,-HZ},{HX,HY,-HZ},{-HX,HY,-HZ},
			{-HX,-HY, HZ},{HX,-HY, HZ},{HX,HY, HZ},{-HX,HY, HZ},
		};
		FVector P[8];
		for (int32 k = 0; k < 8; ++k) P[k] = C + Rot.RotateVector(L[k]);
		static const int32 E[12][2] = {
			{0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
		};
		for (int32 e = 0; e < 12; ++e) Emit(P[E[e][0]], P[E[e][1]]);
	}

	void DbgWireCapsule(const FDbgLineSink& Emit, const FVector& C, const FQuat& Rot, float Radius, float HalfH)
	{
		constexpr int32 Seg = 16;
		const FVector Axis = Rot.RotateVector(FVector(1,0,0)); // 긴축 = X (PhysX/AlignXToDir 규약)
		const FVector U    = Rot.RotateVector(FVector(0,1,0)); // 반경 평면
		const FVector V    = Rot.RotateVector(FVector(0,0,1)); // 반경 평면
		const FVector TopC = C + Axis * HalfH, BotC = C - Axis * HalfH;
		auto Radial = [&](float a) -> FVector { return U * cosf(a) + V * sinf(a); };

		FVector PrevT = TopC + Radial(0.0f) * Radius;
		FVector PrevB = BotC + Radial(0.0f) * Radius;
		for (int32 i = 1; i <= Seg; ++i)
		{
			const float a = kDbgPi2 * i / Seg;
			const FVector d = Radial(a);
			const FVector CurT = TopC + d * Radius, CurB = BotC + d * Radius;
			Emit(PrevT, CurT);
			Emit(PrevB, CurB);
			PrevT = CurT; PrevB = CurB;
		}
		const FVector Dirs[4] = { U, U * -1.0f, V, V * -1.0f };
		for (int32 di = 0; di < 4; ++di)
			Emit(TopC + Dirs[di] * Radius, BotC + Dirs[di] * Radius);
		const int32 HSeg = Seg / 2;
		const FVector Planes[2] = { U, V };
		for (int32 hemi = 0; hemi < 2; ++hemi)
		{
			const FVector O = hemi ? BotC : TopC;
			const float Sign = hemi ? -1.0f : 1.0f;
			for (int32 pl = 0; pl < 2; ++pl)
			{
				const FVector H = Planes[pl];
				FVector Prev = O + H * Radius;
				for (int32 i = 1; i <= HSeg; ++i)
				{
					const float a = kDbgPi * i / HSeg;
					const FVector Cur = O + (H * cosf(a) + Axis * (Sign * sinf(a))) * Radius;
					Emit(Prev, Cur);
					Prev = Cur;
				}
			}
		}
	}
}

void USkeletalMeshComponent::BuildPhysicsBodyWireframe(const std::function<void(const FVector&, const FVector&)>& EmitLine) const
{
	if (!EmitLine) return;

	// 시뮬과 동일하게 GetPhysicsAsset() 로 해석한다(override=PhysicsAssetOverride 우선).
	// Mesh->PhysicsAsset 직접 접근은 런타임/PIE 에서 null 이라(에디터 세션만 메시에 꽂아줌)
	// 와이어가 통째로 사라졌다. 시뮬과 같은 소스를 써서 두 경로(선택/런타임)를 함께 복구.
	UPhysicsAsset* PA = GetPhysicsAsset();
	if (!PA) return;

	// 레벨/런타임에선 런타임 Bodies 가 비어 있을 수 있으므로 PhysicsAsset 셰이프를 현재 본 월드 포즈로 그린다.
	for (UBodySetup* BS : PA->GetBodySetups())
	{
		if (!BS) continue;
		const int32 BoneIndex = FindBoneIndex(BS->BoneName);
		if (BoneIndex < 0) continue;

		FTransform BoneWorld;
		if (!GetBoneWorldTransformByIndex(BoneIndex, BoneWorld)) continue;
		const FVector BonePos = BoneWorld.Location;
		const FQuat   BoneRot = BoneWorld.Rotation;

		for (const FKSphereElem& E : BS->AggregateGeom.SphereElems)
			DbgWireSphere(EmitLine, BonePos + BoneRot.RotateVector(E.Center), E.Radius);
		for (const FKBoxElem& E : BS->AggregateGeom.BoxElems)
			DbgWireBox(EmitLine, BonePos + BoneRot.RotateVector(E.Center), BoneRot * E.Rotation,
				E.HalfX, E.HalfY, E.HalfZ);
		for (const FKCapsuleElem& E : BS->AggregateGeom.CapsuleElems)
			DbgWireCapsule(EmitLine, BonePos + BoneRot.RotateVector(E.Center), BoneRot * E.Rotation,
				E.Radius, E.HalfHeight);
	}
}

void USkeletalMeshComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	if (!bShowPhysicsBodies) return;

	// 에디터 선택 경로: FScene::AddDebugLine 채널로 즉시 제출(초록).
	BuildPhysicsBodyWireframe([&Scene](const FVector& A, const FVector& B)
	{
		Scene.AddDebugLine(A, B, FColor::Green());
	});
}

void USkeletalMeshComponent::DrawRuntimePhysicsBodies()
{
	if (!bShowPhysicsBodies) return;

	// 런타임(play) 경로: 선택과 무관하게 디버그 드로우 큐로 제출. ShowFlags.bDebugDraw 가 켜져 있어야 보인다.
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay()) return;

	FDebugDrawQueue& Queue = World->GetScene().GetDebugDrawQueue();
	BuildPhysicsBodyWireframe([&Queue](const FVector& A, const FVector& B)
	{
		Queue.AddLine(A, B, FColor::Green(), 0.0f);   // Duration 0 = 1프레임(매 틱 재푸시)
	});
}

// ──────────────────────────────────────────────
// Editor / 직렬화 통합
// ──────────────────────────────────────────────
void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyValue>& OutProps)
{
    Super::GetEditableProperties(OutProps);

    // AnimInstance 자체 properties (Speed 등) 도 패널에 같이 노출 — 컴포넌트가 forward.
    // 자식이 자기 카테고리(예: "Animation|Character") 로 그룹화.
    if (AnimInstance) AnimInstance->GetEditableProperties(OutProps);

    // 연결된 PhysicsAsset 의 편집 속성(Enable Self Collision 등)도 노출 — AnimInstance 와 동일한 forward 패턴.
    // 같은 에셋을 쓰는 모든 인스턴스가 공유하는 값(에셋에 저장)이다.
    // BuildPhysicsBodyWireframe 와 동일 이유: override 우선 게터로 통일해야 런타임/레벨
    // 디테일 패널에도 PhysicsAsset 속성(Enable Self Collision 등)이 노출된다.
    if (UPhysicsAsset* PA = GetPhysicsAsset())
    {
        PA->GetEditableProperties(OutProps);
    }
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
    Super::PostEditProperty(PropertyName);
    if (!PropertyName) return;

    if (std::strcmp(PropertyName, "AnimationMode") == 0)
    {
        InitializeAnimation();
    }
    else if (std::strcmp(PropertyName, "AnimInstanceClass") == 0)
    {
        // 클래스 슬롯이 바뀌면 Custom 모드에서 인스턴스 재생성 필요. (ours — Phase 6)
        if (AnimationMode == EAnimationMode::AnimationCustom) InitializeAnimation();
    }
    else if (std::strcmp(PropertyName, "AnimationData") == 0)
    {
        LoadAnimationFromPath();

        if (AnimInstance)
        {
            InitializeAnimation();
        }
    }
    else if (std::strcmp(PropertyName, "AnimToPlayPath") == 0)
    {
        // theirs (main): FAnimationManager 가 path 로 실제 UAnimSequence 로딩 — Phase 4 의 TODO 해소.
        // Mode 가 None 이면 SingleNode 로 자동 전환, AnimInstance 없으면 Initialize, 있으면 SingleNode setter 들 갱신.
        LoadAnimationFromPath();

        if (AnimationMode == EAnimationMode::None)
        {
            AnimationMode = EAnimationMode::AnimationSingleNode;
        }

        if (!AnimInstance)
        {
            InitializeAnimation();
        }
        else if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
        {
            if (!CanUseAnimation(AnimationData.AnimToPlay))
            {
                AnimationData.AnimToPlay = nullptr;
                AnimationData.AnimToPlayPath = "None";
            }
            SingleNode->SetAnimationAsset(AnimationData.AnimToPlay);
            SingleNode->SetPlayRate(AnimationData.PlayRate);
            SingleNode->SetLooping(AnimationData.bLooping);
            SingleNode->SetPlaying(AnimationData.bPlaying && AnimationData.AnimToPlay != nullptr);
        }
    }
    else if (std::strcmp(PropertyName, "PlayRate") == 0)
    {
        SetPlayRate(AnimationData.PlayRate);
    }
    else if (std::strcmp(PropertyName, "bLooping") == 0)
    {
        SetLooping(AnimationData.bLooping);
    }
    else if (std::strcmp(PropertyName, "bPlaying") == 0)
    {
        SetPlaying(AnimationData.bPlaying);
    }
    else if (std::strcmp(PropertyName, "PhysicsAssetPath") == 0 ||
             std::strcmp(PropertyName, "Physics Asset") == 0)
    {
        // 시뮬 중 자산 교체 → 안전 정리 후 재로드(같은 클래스라 직접 호출, 가상 훅 불필요).
        if (bSimulatingPhysics) SetSimulatingPhysics(false);
        DestroyPhysicsAssetBodies();
        LoadPhysicsAssetFromPath();
    }

    // AnimInstance 자체 properties 는 자식이 자체 PostEdit 처리. 컴포넌트는 dispatch 만.
    // 컴포넌트가 인식한 이름과 겹치지 않는 한 무해 (자식이 모르는 이름은 no-op).
    if (AnimInstance) AnimInstance->PostEditProperty(PropertyName);
}

void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
    Super::Serialize(Ar);

    uint8 ModeRaw = static_cast<uint8>(AnimationMode);
    Ar << ModeRaw;
    AnimationMode = static_cast<EAnimationMode>(ModeRaw);

    // AnimToPlay 의 path 만 라운드트립. 실제 포인터 복원은 InitializeAnimation() → LoadAnimationFromPath() 가 처리.
    FString AnimToPlayPath = Ar.IsSaving() ? AnimationData.AnimToPlayPath.ToString() : FString();
    Ar << AnimToPlayPath;
    if (Ar.IsLoading())
    {
        AnimationData.AnimToPlayPath.SetPath(AnimToPlayPath);
    }
    Ar << AnimationData.PlayRate;
    Ar << AnimationData.bLooping;
    Ar << AnimationData.bPlaying;

}

bool USkeletalMeshComponent::EvaluateAnimInstance(float DeltaTime)
{
    if (!AnimInstance) return false;

    USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh) return false;
    FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
    if (!Asset || Asset->Bones.empty()) return false;

    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        if (!CanUseAnimation(SingleNode->GetAnimationAsset()))
        {
            SingleNode->SetAnimationAsset(nullptr);
            return false;
        }
    }

    AnimInstance->UpdateAnimation(DeltaTime);

    // Root motion 적용은 UCharacterMovementComponent 가 책임.
    // CMC::TickComponent (TG_DuringPhysics) 가 매 frame 이 AnimInstance->ConsumeRootMotion 으로
    // 누적값을 가져가 capsule 이동 / 회전에 반영한다 (sweep / floor stick 통과).
    // Mesh 는 actor transform 을 직접 만지지 않는다 — UE 본가 패턴.
    //
    // 주의: CMC 가 없는 actor 에 root motion 켠 anim 을 붙이면 누적값이 anywhere 도
    // 소비되지 않아 in-place 로 보인다. ACharacter 외 케이스에서 root motion 이 필요해지면
    // 별도 소비 경로가 추가되어야 한다.

    FPoseContext Out;
    Out.SkeletalMesh = Mesh;
    Out.Pose.resize(Asset->Bones.size());
    Out.ResetToRefPose();
    AnimInstance->EvaluatePose(Out);

    SetAnimationPose(Out.Pose, Out.MorphWeights);
    return true;
}
