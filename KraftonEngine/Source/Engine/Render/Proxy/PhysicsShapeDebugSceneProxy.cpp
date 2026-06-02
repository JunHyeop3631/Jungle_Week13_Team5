#include "PhysicsShapeDebugSceneProxy.h"

#include "Component/Debug/PhysicsShapeDebugComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/Asset/PhysicsAsset.h"
#include "Physics/Asset/BodySetup.h"

#include <cmath>

namespace
{
	FVector4 PickSolidColor(bool bSel, bool bBodySel)
	{
		if (bSel)     return FVector4(1.0f, 0.82f, 0.10f, 0.40f);
		if (bBodySel) return FVector4(0.10f, 0.80f, 0.42f, 0.32f);
		return FVector4(0.08f, 0.62f, 0.30f, 0.22f);
	}

	FVector4 PickWireColor(bool bSel, bool bBodySel)
	{
		if (bSel)     return FVector4(1.0f, 0.85f, 0.10f, 1.0f);
		if (bBodySel) return FVector4(0.20f, 1.00f, 0.55f, 1.0f);
		return             FVector4(0.15f, 0.80f, 0.42f, 1.0f);
	}
}

// ── 생성자 ────────────────────────────────────────────────────

FPhysicsShapeDebugSceneProxy::FPhysicsShapeDebugSceneProxy(UPrimitiveComponent* InComponent, bool /*bSubclass*/)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags = EPrimitiveProxyFlags::EditorOnly
		| EPrimitiveProxyFlags::NeverCull
		| EPrimitiveProxyFlags::PhysicsShapeDebug;
	// RebuildGeometry() 미호출 — 서브클래스 생성자에서 vtable 완성 후 직접 호출
}

FPhysicsShapeDebugSceneProxy::FPhysicsShapeDebugSceneProxy(UPhysicsShapeDebugComponent* InComponent)
	: FPhysicsShapeDebugSceneProxy(static_cast<UPrimitiveComponent*>(InComponent), false)
{
	RebuildGeometry();
}

FPhysicsShapeDebugSceneProxy::~FPhysicsShapeDebugSceneProxy()
{
}

void FPhysicsShapeDebugSceneProxy::UpdateTransform()
{
	FPrimitiveSceneProxy::UpdateTransform();
	RebuildGeometry();
}

// ── 본 캐시 ───────────────────────────────────────────────────

void FPhysicsShapeDebugSceneProxy::RebuildBoneCache(UPhysicsShapeDebugComponent* Comp)
{
	CachedBoneIndices.clear();

	UPhysicsAsset* PA = Comp->GetPhysicsAsset();
	USkeletalMeshComponent* MeshComp = Comp->GetTargetMeshComponent();
	if (!PA || !MeshComp) return;

	USkeletalMesh* Mesh = MeshComp->GetSkeletalMesh();
	const FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;

	const auto& Setups = PA->GetBodySetups();
	CachedBoneIndices.resize(Setups.size(), -1);

	if (!MeshAsset) return;

	for (int32 Idx = 0; Idx < (int32)Setups.size(); ++Idx)
	{
		UBodySetup* BS = Setups[Idx];
		if (!BS) continue;
		for (int32 i = 0; i < (int32)MeshAsset->Bones.size(); ++i)
		{
			if (MeshAsset->Bones[i].Name == BS->BoneName)
			{
				CachedBoneIndices[Idx] = i;
				break;
			}
		}
	}

	bBoneCacheDirty = false;
}

// ── RebuildGeometry ───────────────────────────────────────────

void FPhysicsShapeDebugSceneProxy::RebuildGeometry()
{
	UPhysicsShapeDebugComponent* Comp = static_cast<UPhysicsShapeDebugComponent*>(GetOwner());
	if (!Comp) return;

	CachedSolid.clear();
	CachedWire.clear();

	bShowSolid = Comp->GetShowSolid();
	bShowWire  = Comp->GetShowWire();

	const int32 SelBody = Comp->GetSelBodyIndex();
	const int32 SelKind = Comp->GetSelKind();
	const int32 SelElem = Comp->GetSelElemIndex();

	UPhysicsAsset* PA = Comp->GetPhysicsAsset();
	USkeletalMeshComponent* MeshComp = Comp->GetTargetMeshComponent();
	if (!PA || !MeshComp) return;

	if (bBoneCacheDirty)
		RebuildBoneCache(Comp);

	const auto& Setups = PA->GetBodySetups();
	for (int32 Idx = 0; Idx < (int32)Setups.size(); ++Idx)
	{
		UBodySetup* BS = Setups[Idx];
		if (!BS) continue;
		const bool bBodySel = (Idx == SelBody);

		FVector BoneWorldPos  = FVector(0,0,0);
		FQuat   BoneWorldQuat = FQuat::Identity;
		const int32 BoneIdx = (Idx < (int32)CachedBoneIndices.size()) ? CachedBoneIndices[Idx] : -1;
		if (BoneIdx >= 0)
		{
			BoneWorldPos  = MeshComp->GetBoneLocationByIndex(BoneIdx);
			BoneWorldQuat = MeshComp->GetBoneQuatByIndex(BoneIdx);
		}

		for (int32 Si = 0; Si < (int32)BS->AggregateGeom.SphereElems.size(); ++Si)
		{
			const FKSphereElem& E = BS->AggregateGeom.SphereElems[Si];
			const bool bSel = bBodySel && SelKind == 1 && SelElem == Si;
			const FVector WorldCenter = BoneWorldPos + BoneWorldQuat.RotateVector(E.Center);
			ShapeDebugUtils::AppendSolidSphere(CachedSolid, WorldCenter, E.Radius, PickSolidColor(bSel, bBodySel));
			ShapeDebugUtils::AppendWireSphere (CachedWire,  WorldCenter, E.Radius, PickWireColor(bSel, bBodySel));
		}
		for (int32 Bi = 0; Bi < (int32)BS->AggregateGeom.BoxElems.size(); ++Bi)
		{
			const FKBoxElem& E = BS->AggregateGeom.BoxElems[Bi];
			const bool bSel = bBodySel && SelKind == 2 && SelElem == Bi;
			const FVector WorldCenter = BoneWorldPos + BoneWorldQuat.RotateVector(E.Center);
			const FQuat   WorldRot    = BoneWorldQuat * E.Rotation;
			ShapeDebugUtils::AppendSolidBox(CachedSolid, WorldCenter, WorldRot, E.HalfX, E.HalfY, E.HalfZ, PickSolidColor(bSel, bBodySel));
			ShapeDebugUtils::AppendWireBox (CachedWire,  WorldCenter, WorldRot, E.HalfX, E.HalfY, E.HalfZ, PickWireColor(bSel, bBodySel));
		}
		for (int32 Ci = 0; Ci < (int32)BS->AggregateGeom.CapsuleElems.size(); ++Ci)
		{
			const FKCapsuleElem& E = BS->AggregateGeom.CapsuleElems[Ci];
			const bool bSel = bBodySel && SelKind == 3 && SelElem == Ci;
			const FVector WorldCenter = BoneWorldPos + BoneWorldQuat.RotateVector(E.Center);
			const FQuat   WorldRot    = BoneWorldQuat * E.Rotation;
			ShapeDebugUtils::AppendSolidCapsule(CachedSolid, WorldCenter, WorldRot, E.Radius, E.HalfHeight, PickSolidColor(bSel, bBodySel));
			ShapeDebugUtils::AppendWireCapsule (CachedWire,  WorldCenter, WorldRot, E.Radius, E.HalfHeight, PickWireColor(bSel, bBodySel));
		}
	}
}
