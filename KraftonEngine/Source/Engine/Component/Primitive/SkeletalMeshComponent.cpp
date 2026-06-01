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
#include "GameFramework/AActor.h"
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

    void AppendPhysicsShapes(const UBodySetup& BodySetup, FPhysicsBodyDesc& BodyDesc)
    {
        for (const FKSphereElem& Sphere : BodySetup.AggregateGeom.SphereElems)
        {
            FPhysicsShapeDesc ShapeDesc;
            ShapeDesc.Name = BodySetup.BoneName + "_Sphere";
            ShapeDesc.ShapeType = EPhysicsShapeType::Sphere;
            ShapeDesc.LocalTransform = MakeUnitScaleTransform(Sphere.Center);
            ShapeDesc.Radius = Sphere.Radius;
            ApplyBodyMaterial(BodySetup, ShapeDesc);
            BodyDesc.Shapes.push_back(ShapeDesc);
        }

        for (const FKBoxElem& Box : BodySetup.AggregateGeom.BoxElems)
        {
            FPhysicsShapeDesc ShapeDesc;
            ShapeDesc.Name = BodySetup.BoneName + "_Box";
            ShapeDesc.ShapeType = EPhysicsShapeType::Box;
            ShapeDesc.LocalTransform = MakeUnitScaleTransform(Box.Center, Box.Rotation);
            ShapeDesc.HalfExtent = FVector(Box.HalfX, Box.HalfY, Box.HalfZ);
            ApplyBodyMaterial(BodySetup, ShapeDesc);
            BodyDesc.Shapes.push_back(ShapeDesc);
        }

        for (const FKCapsuleElem& Capsule : BodySetup.AggregateGeom.CapsuleElems)
        {
            FPhysicsShapeDesc ShapeDesc;
            ShapeDesc.Name = BodySetup.BoneName + "_Capsule";
            ShapeDesc.ShapeType = EPhysicsShapeType::Capsule;
            ShapeDesc.LocalTransform = MakeUnitScaleTransform(Capsule.Center, Capsule.Rotation);
            ShapeDesc.Radius = Capsule.Radius;
            // PhysX PxCapsuleGeometry expects the half distance between the two sphere centers.
            ShapeDesc.HalfHeight = std::max(0.0f, Capsule.HalfHeight - Capsule.Radius);
            ApplyBodyMaterial(BodySetup, ShapeDesc);
            BodyDesc.Shapes.push_back(ShapeDesc);
        }
    }

    FVector TransformPoint(const FMatrix& Matrix, const FVector& Point)
    {
        return Matrix.TransformPositionWithW(Point);
    }
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    ClearSkeletalClothBinding();
    DestroyPhysicsAssetBodies();
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
    if (!bSkeletalClothBound || CachedSkeletalClothRenderData.Vertices.empty())
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
    USkeletalMesh* Mesh = GetSkeletalMesh();
    return Mesh ? InstantiatePhysicsAssetBodies(Scene, Mesh->PhysicsAsset) : false;
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
    Constraints.clear();

    bool bCreatedAnyBody = false;

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

        AppendPhysicsShapes(*BodySetup, BodyDesc);
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
        bCreatedAnyBody = true;
    }

    if (!bCreatedAnyBody)
    {
        Bodies.clear();
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

        const FTransform ParentLocalFrame = MakeUnitScaleTransform(Setup->ParentAnchorPos, Setup->ParentAnchorRot);
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
    }

    Constraints.clear();
    Bodies.clear();
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

void USkeletalMeshComponent::CreateRagdoll()
{
    // 패시브 ragdoll 진입: PhysicsAsset 이 이미 인스턴스화돼 있어야 한다 (Bodies / PhysicsSceneOwner).
    if (!PhysicsSceneOwner || Bodies.empty())
    {
        UE_LOG("CreateRagdoll skipped: physics asset bodies are not instantiated.");
        return;
    }

    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!Asset || Asset->Bones.empty())
    {
        return;
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
            PhysicsSceneOwner->SetBodyTransform(Body, BoneWorldTransform, /*bTeleport*/ true);
        }

        PhysicsSceneOwner->SetBodyType(Body, EPhysicsBodyType::Dynamic);
    }

    // 3) 애니메이션 평가 차단 — 다음 TickComponent 부터 ApplyPhysicsToBones 경로로 분기된다.
    bSimulatingPhysics = true;
}

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
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

    // 본 인덱스 오름차순 = parent-first 가 엔진 규약이라 단순 순회로 부모 글로벌이 항상 채워진 뒤
    // 자식이 사용한다. ComponentLocalGlobals[i] 는 component-local 본 글로벌 행렬을 누적한다.
    TArray<FMatrix> ComponentLocalGlobals;
    ComponentLocalGlobals.resize(BoneCount, FMatrix::Identity);

    TArray<FTransform> LocalPose;
    LocalPose.resize(BoneCount);

    // body 트랜스폼은 world 기준이고 본 local pose 는 component-local 누적이므로 world↔component 변환이 필요하다.
    // InstantiatePhysicsAssetBodies 가 본 world == body actor pose 로 생성했기 때문에 body→bone 별도 오프셋 보정은 없다.
    const FMatrix ComponentWorldInv = GetWorldInverseMatrix();

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const int32 ParentIndex = Asset->Bones[BoneIndex].ParentIndex;
        const FMatrix ParentGlobal = (ParentIndex >= 0)
            ? ComponentLocalGlobals[ParentIndex]
            : FMatrix::Identity;
        const FMatrix RefLocal = Asset->Bones[BoneIndex].GetReferenceLocalPose();

        FBodyInstance* Body = (BoneIndex < static_cast<int32>(Bodies.size())) ? Bodies[BoneIndex] : nullptr;

        FMatrix LocalMatrix = RefLocal;
        FMatrix ComponentGlobal = RefLocal * ParentGlobal;

        if (Body && Body->bValid)
        {
            FTransform BodyWorld;
            if (PhysicsSceneOwner->GetBodyTransform(Body, BodyWorld))
            {
                // body world → component-local global → parent global inverse 곱으로 local pose 산출.
                // 루트(ParentIndex < 0)는 ParentGlobal == Identity 이므로 component global 이 곧 local 이 된다.
                ComponentGlobal = BodyWorld.ToMatrix() * ComponentWorldInv;
                LocalMatrix = (ParentIndex >= 0)
                    ? ComponentGlobal * ParentGlobal.GetInverse()
                    : ComponentGlobal;
            }
        }

        ComponentLocalGlobals[BoneIndex] = ComponentGlobal;
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

	void DbgWireSphere(FScene& Scene, const FVector& C, float R, const FColor& Col)
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
				Scene.AddDebugLine(Prev, Cur, Col);
				Prev = Cur;
			}
		}
	}

	void DbgWireBox(FScene& Scene, const FVector& C, const FQuat& Rot, float HX, float HY, float HZ, const FColor& Col)
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
		for (int32 e = 0; e < 12; ++e) Scene.AddDebugLine(P[E[e][0]], P[E[e][1]], Col);
	}

	void DbgWireCapsule(FScene& Scene, const FVector& C, const FQuat& Rot, float Radius, float HalfH, const FColor& Col)
	{
		constexpr int32 Seg = 16;
		const FVector Up      = Rot.RotateVector(FVector(0,0,1));
		const FVector Right   = Rot.RotateVector(FVector(1,0,0));
		const FVector Forward = Rot.RotateVector(FVector(0,1,0));
		const FVector TopC = C + Up * HalfH, BotC = C - Up * HalfH;
		auto Radial = [&](float a) -> FVector { return Right * cosf(a) + Forward * sinf(a); };

		FVector PrevT = TopC + Radial(0.0f) * Radius;
		FVector PrevB = BotC + Radial(0.0f) * Radius;
		for (int32 i = 1; i <= Seg; ++i)
		{
			const float a = kDbgPi2 * i / Seg;
			const FVector d = Radial(a);
			const FVector CurT = TopC + d * Radius, CurB = BotC + d * Radius;
			Scene.AddDebugLine(PrevT, CurT, Col);
			Scene.AddDebugLine(PrevB, CurB, Col);
			PrevT = CurT; PrevB = CurB;
		}
		const FVector Dirs[4] = { Right, Right * -1.0f, Forward, Forward * -1.0f };
		for (int32 di = 0; di < 4; ++di)
			Scene.AddDebugLine(TopC + Dirs[di] * Radius, BotC + Dirs[di] * Radius, Col);
		const int32 HSeg = Seg / 2;
		const FVector Planes[2] = { Right, Forward };
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
					const FVector Cur = O + (H * cosf(a) + Up * (Sign * sinf(a))) * Radius;
					Scene.AddDebugLine(Prev, Cur, Col);
					Prev = Cur;
				}
			}
		}
	}
}

void USkeletalMeshComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	if (!bShowPhysicsBodies) return;

	USkeletalMesh* Mesh = GetSkeletalMesh();
	UPhysicsAsset* PA = Mesh ? Mesh->PhysicsAsset : nullptr;
	if (!PA) return;

	const FColor Col = FColor::Green();
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
			DbgWireSphere(Scene, BonePos + BoneRot.RotateVector(E.Center), E.Radius, Col);
		for (const FKBoxElem& E : BS->AggregateGeom.BoxElems)
			DbgWireBox(Scene, BonePos + BoneRot.RotateVector(E.Center), BoneRot * E.Rotation,
				E.HalfX, E.HalfY, E.HalfZ, Col);
		for (const FKCapsuleElem& E : BS->AggregateGeom.CapsuleElems)
			DbgWireCapsule(Scene, BonePos + BoneRot.RotateVector(E.Center), BoneRot * E.Rotation,
				E.Radius, E.HalfHeight, Col);
	}
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
