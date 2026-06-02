#pragma once

#include "Gizmo/GizmoTransformTarget.h"
#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Mesh/Static/StaticMeshAsset.h"

class UWorld;

// FStaticMeshCollisionShape를 IGizmoTransformTarget으로 감싸
// PhysicsShapeGizmoTarget과 동일 구조지만 본 트랜스폼 없이 메시 로컬 공간 직접 사용
class FStaticMeshShapeGizmoTarget : public IGizmoTransformTarget
{
public:
    void Bind(FStaticMeshCollisionShape* InShape, UWorld* InWorld)
    {
        Shape = InShape;
        World = InWorld;
    }

    void Unbind() { Shape = nullptr; World = nullptr; }

    bool    IsValid()  const override { return Shape != nullptr; }
    UWorld* GetWorld() const override { return World; }

    FVector  GetWorldLocation() const override { return Shape ? Shape->Center : FVector(0, 0, 0); }
    FQuat    GetWorldQuat()     const override { return Shape ? Shape->Rotation : FQuat::Identity; }
    FRotator GetWorldRotation() const override { return GetWorldQuat().ToRotator(); }
    FVector  GetWorldScale()    const override { return FVector(1, 1, 1); }

    void SetWorldLocation(const FVector& NewPos) override
    {
        if (Shape) Shape->Center = NewPos;
    }

    void SetWorldRotation(const FQuat& NewQuat) override
    {
        if (Shape && Shape->ShapeType != EStaticMeshCollisionShapeType::Sphere)
        {
            Shape->Rotation = NewQuat;
            Shape->Rotation.Normalize();
        }
    }
    void SetWorldRotation(const FRotator& R) override { SetWorldRotation(FQuat::FromRotator(R)); }
    void SetWorldScale(const FVector&) override {}

    void AddWorldOffset(const FVector& Delta) override
    {
        if (Shape) Shape->Center += Delta;
    }

    void AddWorldRotation(const FQuat& Delta, bool bWorldSpace) override
    {
        if (!Shape || Shape->ShapeType == EStaticMeshCollisionShapeType::Sphere) return;
        if (bWorldSpace)
            Shape->Rotation = Delta * Shape->Rotation;
        else
            Shape->Rotation = Shape->Rotation * Delta;
        Shape->Rotation.Normalize();
    }

    void AddScaleDelta(const FVector& Delta) override
    {
        if (!Shape) return;
        auto Apply = [](float& V, float D)
        {
            V *= (1.0f + D);
            if (V < 0.01f) V = 0.01f;
        };
        switch (Shape->ShapeType)
        {
        case EStaticMeshCollisionShapeType::Box:
            Apply(Shape->HalfExtent.X, Delta.X);
            Apply(Shape->HalfExtent.Y, Delta.Y);
            Apply(Shape->HalfExtent.Z, Delta.Z);
            break;
        case EStaticMeshCollisionShapeType::Sphere:
        {
            float Uniform = (Delta.X + Delta.Y + Delta.Z) / 3.0f;
            Apply(Shape->Radius, Uniform);
            break;
        }
        case EStaticMeshCollisionShapeType::Capsule:
        {
            float XY = (Delta.X + Delta.Y) * 0.5f;
            Apply(Shape->Radius, XY);
            Apply(Shape->HalfHeight, Delta.Z);
            break;
        }
        default: break;
        }
    }

private:
    FStaticMeshCollisionShape* Shape = nullptr;
    UWorld* World = nullptr;
};
