#pragma once

#include "Gizmo/GizmoTransformTarget.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Math/Quat.h"

class UWorld;

// ============================================================
// FPhysicsShapeGizmoTarget
//
// 콜리전 셰이프의 Center 포인터를 IGizmoTransformTarget 으로 감싸
// UGizmoComponent 가 셰이프를 직접 이동할 수 있게 한다.
//
// Bind() 로 FVector* 를 연결하고, Unbind() 로 해제.
// 셰이프가 제거되기 전에 반드시 Unbind() 할 것 (댕글링 포인터 방지).
// ============================================================
class FPhysicsShapeGizmoTarget : public IGizmoTransformTarget
{
public:
    // InBoneWorldPos / InBoneWorldQuat: 본의 월드 트랜스폼
    // Center는 본 로컬 공간 기준 오프셋 포인터
    void Bind(FVector* InCenter, UWorld* InWorld, FVector InBoneWorldPos, FQuat InBoneWorldQuat)
    {
        Center        = InCenter;
        World         = InWorld;
        BoneWorldPos  = InBoneWorldPos;
        BoneWorldQuat = InBoneWorldQuat;
    }
	
    void Unbind()
    {
        Center = nullptr;
        World  = nullptr;
    }

    // ── IGizmoTransformTarget ─────────────────────────────────
    bool    IsValid()          const override { return Center != nullptr; }
    UWorld* GetWorld()         const override { return World; }

    // 셰이프 월드 위치 = 본 월드 위치 + 본 로테이션 * 로컬 센터
    FVector  GetWorldLocation() const override
    {
        return Center ? BoneWorldPos + BoneWorldQuat.RotateVector(*Center) : FVector(0,0,0);
    }
    FRotator GetWorldRotation() const override { return FRotator(); }
    FQuat    GetWorldQuat()     const override { return FQuat(); }
    FVector  GetWorldScale()    const override { return FVector(1,1,1); }

    // 월드 위치 → 본 로컬 센터로 역변환
    void SetWorldLocation(const FVector& NewWorldPos) override
    {
        if (Center) *Center = BoneWorldQuat.Inverse().RotateVector(NewWorldPos - BoneWorldPos);
    }
    void SetWorldRotation(const FRotator&) override {}
    void SetWorldRotation(const FQuat&)    override {}
    void SetWorldScale   (const FVector&)  override {}

    // 월드 델타 → 본 로컬 델타로 역변환
    void AddWorldOffset   (const FVector& Delta) override
    {
        if (Center) *Center += BoneWorldQuat.Inverse().RotateVector(Delta);
    }
    void AddWorldRotation (const FQuat&, bool) override {}
    void AddScaleDelta    (const FVector&)     override {}

private:
    FVector* Center        = nullptr;
    UWorld*  World         = nullptr;
    FVector  BoneWorldPos  = FVector(0,0,0);
    FQuat    BoneWorldQuat = FQuat::Identity;
};
