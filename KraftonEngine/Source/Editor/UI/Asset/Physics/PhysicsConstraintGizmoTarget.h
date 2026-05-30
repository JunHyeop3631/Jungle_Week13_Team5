#pragma once

#include "Gizmo/GizmoTransformTarget.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Math/Quat.h"

class UWorld;

// ============================================================
// FPhysicsConstraintGizmoTarget
//
// 컨스트레인트(관절)의 anchor 프레임을 IGizmoTransformTarget 으로 감싸
// UGizmoComponent 가 관절 프레임을 직접 이동/회전 할 수 있게 한다.
//
// anchor 는 부모 본(부모 바디) 로컬 공간 기준으로 저장된다
// (런타임 규약과 일치 — SkeletalMeshComponent 의 ParentLocalFrame):
//   - AnchorPos : 부모 본 로컬 오프셋  (ParentAnchorPos)
//   - AnchorRot : 부모 본 로컬 회전    (ParentAnchorRot)
//
// 관절 프레임은 위치+회전만 의미가 있으므로 스케일 기즈모는 무시한다.
//
// Bind() 로 컨스트레인트를 연결하고, Unbind() 로 해제.
// 컨스트레인트가 제거되기 전에 반드시 Unbind() 할 것 (댕글링 포인터 방지).
// ============================================================
class FPhysicsConstraintGizmoTarget : public IGizmoTransformTarget
{
public:
    // InAnchorPos / InAnchorRot : 편집 대상 필드 포인터 (부모 본 로컬)
    // InBoneWorldPos / InBoneWorldQuat : 부모 본의 월드 트랜스폼
    void Bind(FVector* InAnchorPos, FQuat* InAnchorRot, UWorld* InWorld,
              FVector InBoneWorldPos, FQuat InBoneWorldQuat)
    {
        AnchorPos     = InAnchorPos;
        AnchorRot     = InAnchorRot;
        World         = InWorld;
        BoneWorldPos  = InBoneWorldPos;
        BoneWorldQuat = InBoneWorldQuat;
    }

    void Unbind()
    {
        AnchorPos     = nullptr;
        AnchorRot     = nullptr;
        World         = nullptr;
        BoneWorldPos  = FVector(0, 0, 0);
        BoneWorldQuat = FQuat::Identity;
    }

    // ── IGizmoTransformTarget ─────────────────────────────────
    bool    IsValid()  const override { return AnchorPos != nullptr; }
    UWorld* GetWorld() const override { return World; }

    // 관절 월드 위치 = 부모 본 월드 위치 + 부모 본 회전 * 로컬 anchor
    FVector GetWorldLocation() const override
    {
        return AnchorPos ? BoneWorldPos + BoneWorldQuat.RotateVector(*AnchorPos) : FVector(0, 0, 0);
    }

    // 관절 월드 회전 = 부모 본 월드 회전 * 로컬 anchor 회전
    FQuat GetWorldQuat() const override
    {
        return AnchorRot ? (BoneWorldQuat * (*AnchorRot)) : BoneWorldQuat;
    }
    FRotator GetWorldRotation() const override { return GetWorldQuat().ToRotator(); }
    FVector  GetWorldScale()    const override { return FVector(1, 1, 1); }

    // 월드 위치 → 부모 본 로컬 anchor 로 역변환
    void SetWorldLocation(const FVector& NewWorldPos) override
    {
        if (AnchorPos) *AnchorPos = BoneWorldQuat.Inverse().RotateVector(NewWorldPos - BoneWorldPos);
    }

    // 월드 회전 → 부모 본 로컬 anchor 회전으로 역변환
    void SetWorldRotation(const FQuat& NewWorldQuat) override
    {
        if (AnchorRot)
        {
            *AnchorRot = BoneWorldQuat.Inverse() * NewWorldQuat;
            AnchorRot->Normalize();
        }
    }
    void SetWorldRotation(const FRotator& NewRotation) override
    {
        SetWorldRotation(FQuat::FromRotator(NewRotation));
    }
    void SetWorldScale(const FVector&) override {}

    // 월드 델타 → 부모 본 로컬 델타로 역변환
    void AddWorldOffset(const FVector& Delta) override
    {
        if (AnchorPos) *AnchorPos += BoneWorldQuat.Inverse().RotateVector(Delta);
    }

    // 기즈모 회전 델타를 anchor 로컬 회전에 누적 (셰이프 기즈모와 동일 규약).
    //   - 월드 공간: Local = Bq^-1 * Delta * Bq * Local
    //   - 로컬 공간: Local = Local * Delta
    void AddWorldRotation(const FQuat& Delta, bool bWorldSpace) override
    {
        if (!AnchorRot) return;
        if (bWorldSpace)
            *AnchorRot = BoneWorldQuat.Inverse() * Delta * BoneWorldQuat * (*AnchorRot);
        else
            *AnchorRot = (*AnchorRot) * Delta;
        AnchorRot->Normalize();
    }

    void AddScaleDelta(const FVector&) override {}

private:
    UWorld*  World         = nullptr;
    FVector  BoneWorldPos  = FVector(0, 0, 0);
    FQuat    BoneWorldQuat = FQuat::Identity;

    // 바인딩된 컨스트레인트 필드 포인터 (부모 본 로컬)
    FVector* AnchorPos     = nullptr;
    FQuat*   AnchorRot     = nullptr;
};
