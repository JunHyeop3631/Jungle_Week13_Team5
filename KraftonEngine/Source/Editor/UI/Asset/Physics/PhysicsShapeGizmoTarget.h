#pragma once

#include "Gizmo/GizmoTransformTarget.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Math/Quat.h"
#include "Physics/Asset/PhysicsGeometry.h"

class UWorld;

// ============================================================
// FPhysicsShapeGizmoTarget
//
// 콜리전 셰이프(Sphere/Box/Capsule)를 IGizmoTransformTarget 으로 감싸
// UGizmoComponent 가 셰이프를 직접 이동/회전/스케일 할 수 있게 한다.
//
// 셰이프 데이터는 본 로컬 공간 기준으로 저장된다:
//   - Center   : 본 로컬 오프셋
//   - Rotation : 본 로컬 회전 (Sphere 는 회전 없음)
//   - 치수      : Box=HalfX/Y/Z, Capsule=Radius/HalfHeight, Sphere=Radius
//
// 스케일 기즈모는 단일 스케일 벡터가 아니라 각 셰이프의 치수에 매핑된다
// (언리얼 PhAT 와 동일):
//   - Box     : X→HalfX, Y→HalfY, Z→HalfZ
//   - Capsule : X/Y→Radius, Z→HalfHeight
//   - Sphere  : 균등 → Radius
//
// BindXxx() 로 셰이프를 연결하고, Unbind() 로 해제.
// 셰이프가 제거되기 전에 반드시 Unbind() 할 것 (댕글링 포인터 방지).
// ============================================================
class FPhysicsShapeGizmoTarget : public IGizmoTransformTarget
{
public:
    enum class EKind { None, Sphere, Box, Capsule };

    // InBoneWorldPos / InBoneWorldQuat: 본의 월드 트랜스폼
    void BindSphere(FKSphereElem* E, UWorld* InWorld, FVector InBoneWorldPos, FQuat InBoneWorldQuat)
    {
        Reset();
        Kind          = EKind::Sphere;
        World         = InWorld;
        BoneWorldPos  = InBoneWorldPos;
        BoneWorldQuat = InBoneWorldQuat;
        if (E)
        {
            Center = &E->Center;
            Radius = &E->Radius;
        }
    }

    void BindBox(FKBoxElem* E, UWorld* InWorld, FVector InBoneWorldPos, FQuat InBoneWorldQuat)
    {
        Reset();
        Kind          = EKind::Box;
        World         = InWorld;
        BoneWorldPos  = InBoneWorldPos;
        BoneWorldQuat = InBoneWorldQuat;
        if (E)
        {
            Center   = &E->Center;
            Rotation = &E->Rotation;
            HalfX    = &E->HalfX;
            HalfY    = &E->HalfY;
            HalfZ    = &E->HalfZ;
        }
    }

    void BindCapsule(FKCapsuleElem* E, UWorld* InWorld, FVector InBoneWorldPos, FQuat InBoneWorldQuat)
    {
        Reset();
        Kind          = EKind::Capsule;
        World         = InWorld;
        BoneWorldPos  = InBoneWorldPos;
        BoneWorldQuat = InBoneWorldQuat;
        if (E)
        {
            Center     = &E->Center;
            Rotation   = &E->Rotation;
            Radius     = &E->Radius;
            HalfHeight = &E->HalfHeight;
        }
    }

    void Unbind() { Reset(); }

    // ── IGizmoTransformTarget ─────────────────────────────────
    bool    IsValid()  const override { return Center != nullptr; }
    UWorld* GetWorld() const override { return World; }

    // 셰이프 월드 위치 = 본 월드 위치 + 본 로테이션 * 로컬 센터
    FVector GetWorldLocation() const override
    {
        return Center ? BoneWorldPos + BoneWorldQuat.RotateVector(*Center) : FVector(0, 0, 0);
    }

    // 셰이프 월드 회전 = 본 월드 회전 * 셰이프 로컬 회전
    FQuat GetWorldQuat() const override
    {
        return Rotation ? (BoneWorldQuat * (*Rotation)) : BoneWorldQuat;
    }
    FRotator GetWorldRotation() const override { return GetWorldQuat().ToRotator(); }
    FVector  GetWorldScale()    const override { return FVector(1, 1, 1); }

    // 월드 위치 → 본 로컬 센터로 역변환
    void SetWorldLocation(const FVector& NewWorldPos) override
    {
        if (Center) *Center = BoneWorldQuat.Inverse().RotateVector(NewWorldPos - BoneWorldPos);
    }

    // 월드 회전 → 본 로컬 회전으로 역변환
    void SetWorldRotation(const FQuat& NewWorldQuat) override
    {
        if (Rotation)
        {
            *Rotation = BoneWorldQuat.Inverse() * NewWorldQuat;
            Rotation->Normalize();
        }
    }
    void SetWorldRotation(const FRotator& NewRotation) override
    {
        SetWorldRotation(FQuat::FromRotator(NewRotation));
    }
    void SetWorldScale(const FVector&) override {}

    // 월드 델타 → 본 로컬 델타로 역변환
    void AddWorldOffset(const FVector& Delta) override
    {
        if (Center) *Center += BoneWorldQuat.Inverse().RotateVector(Delta);
    }

    // 기즈모 회전 델타를 셰이프 로컬 회전에 누적.
    // 표시되는 월드 회전 = BoneWorldQuat * (*Rotation) 이므로,
    //   - 월드 공간: NewWorld = Delta * World  ⇒  Local = Bq^-1 * Delta * Bq * Local
    //   - 로컬 공간: NewWorld = World * Delta  ⇒  Local = Local * Delta
    void AddWorldRotation(const FQuat& Delta, bool bWorldSpace) override
    {
        if (!Rotation) return;
        if (bWorldSpace)
            *Rotation = BoneWorldQuat.Inverse() * Delta * BoneWorldQuat * (*Rotation);
        else
            *Rotation = (*Rotation) * Delta;
        Rotation->Normalize();
    }

    // 스케일 델타를 셰이프 치수에 매핑 (상대 배율, 한 번에 한 축).
    void AddScaleDelta(const FVector& Delta) override
    {
        auto Apply = [](float* V, float D)
        {
            if (!V) return;
            *V *= (1.0f + D);
            if (*V < MinDimension) *V = MinDimension;
        };

        switch (Kind)
        {
        case EKind::Sphere:
            Apply(Radius, Delta.X + Delta.Y + Delta.Z); // 균등
            break;
        case EKind::Box:
            Apply(HalfX, Delta.X);
            Apply(HalfY, Delta.Y);
            Apply(HalfZ, Delta.Z);
            break;
        case EKind::Capsule:
            Apply(Radius, Delta.X + Delta.Y); // 반경(XY 평면)
            Apply(HalfHeight, Delta.Z);       // 높이(Z 축)
            break;
        default:
            break;
        }
    }

private:
    static constexpr float MinDimension = 0.1f;

    void Reset()
    {
        Kind          = EKind::None;
        World         = nullptr;
        Center        = nullptr;
        Rotation      = nullptr;
        Radius        = nullptr;
        HalfX = HalfY = HalfZ = nullptr;
        HalfHeight    = nullptr;
    }

    EKind    Kind          = EKind::None;
    UWorld*  World         = nullptr;
    FVector  BoneWorldPos  = FVector(0, 0, 0);
    FQuat    BoneWorldQuat = FQuat::Identity;

    // 바인딩된 셰이프 필드 포인터
    FVector* Center        = nullptr;
    FQuat*   Rotation      = nullptr; // Sphere 는 nullptr
    float*   Radius        = nullptr; // Sphere / Capsule
    float*   HalfX         = nullptr; // Box
    float*   HalfY         = nullptr; // Box
    float*   HalfZ         = nullptr; // Box
    float*   HalfHeight    = nullptr; // Capsule
};
