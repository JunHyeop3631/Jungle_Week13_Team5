#include "PhysicsShapeDebugSceneProxy.h"

#include "Component/Debug/PhysicsShapeDebugComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/Asset/PhysicsAsset.h"
#include "Physics/Asset/BodySetup.h"

#include <cmath>

#pragma region Solid Build

namespace
{
	constexpr float kPi  = 3.14159265f;
	constexpr float kPi2 = kPi * 2.0f;

	// ── 반투명 솔리드(언리얼 PhAT 스타일) ──────────────────────────────
	// 솔리드 채움 색 (RGB + 알파). 곡면을 옅은 알파로 채운다.
	FVector4 PickSolidColor(bool bSel, bool bBodySel)
	{
		if (bSel)     return FVector4(1.0f, 0.82f, 0.10f, 0.40f);
		if (bBodySel) return FVector4(0.10f, 0.80f, 0.42f, 0.32f);
		return FVector4(0.08f, 0.62f, 0.30f, 0.22f);
	}

	// 가짜 명암 — 월드 노멀과 고정 광원 방향의 내적으로 곡면을 입체적으로 보이게 한다.
	// (Editor.hlsl 은 unlit 정점색 셰이더라, 셰이딩을 정점 색에 직접 구워넣는다.)
	FVector4 ShadeColor(const FVector& WorldN, const FVector4& Base)
	{
		static const FVector L = FVector(0.35f, 0.45f, 0.82f).Normalized();
		float d = WorldN.Dot(L);
		if (d < 0.f) d = 0.f;
		const float s = 0.40f + 0.60f * d;
		return FVector4(Base.X * s, Base.Y * s, Base.Z * s, Base.W);
	}

	// 양면(two-sided) 삼각형 — AlphaBlend 패스의 백페이스 컬링과 무관하게 항상 보이도록.
	void PushTri(TArray<FColoredVertex>& Out,
	             const FVector& A, const FVector& B, const FVector& C,
	             const FVector4& cA, const FVector4& cB, const FVector4& cC)
	{
		Out.push_back({ A, cA }); Out.push_back({ B, cB }); Out.push_back({ C, cC });
		Out.push_back({ A, cA }); Out.push_back({ C, cC }); Out.push_back({ B, cB });
	}

	void AppendSolidSphere(TArray<FColoredVertex>& Out, const FVector& C, float R, const FVector4& Base)
	{
		constexpr int32 Rings = 8, Sectors = 12;
		auto N = [&](int32 ri, int32 si) -> FVector
		{
			const float v = kPi * ri / Rings;       // 0..pi (위→아래)
			const float u = kPi2 * si / Sectors;    // 0..2pi
			return FVector(sinf(v)*cosf(u), sinf(v)*sinf(u), cosf(v));
		};
		for (int32 ri = 0; ri < Rings; ++ri)
			for (int32 si = 0; si < Sectors; ++si)
			{
				const FVector n00 = N(ri, si),   n01 = N(ri, si+1);
				const FVector n10 = N(ri+1, si),  n11 = N(ri+1, si+1);
				const FVector4 c00 = ShadeColor(n00, Base), c01 = ShadeColor(n01, Base);
				const FVector4 c10 = ShadeColor(n10, Base), c11 = ShadeColor(n11, Base);
				PushTri(Out, C+n00*R, C+n10*R, C+n11*R, c00, c10, c11);
				PushTri(Out, C+n00*R, C+n11*R, C+n01*R, c00, c11, c01);
			}
	}

	void AppendSolidBox(TArray<FColoredVertex>& Out, const FVector& C, const FQuat& Rot,
	                    float HX, float HY, float HZ, const FVector4& Base)
	{
		const FVector V[8] = {
			{-HX,-HY,-HZ},{HX,-HY,-HZ},{HX,HY,-HZ},{-HX,HY,-HZ},
			{-HX,-HY, HZ},{HX,-HY, HZ},{HX,HY, HZ},{-HX,HY, HZ},
		};
		// 면별 4꼭짓점(CCW) + 면 노멀
		static const int32 F[6][4] = {
			{0,3,2,1},{4,5,6,7},{0,1,5,4},{2,3,7,6},{1,2,6,5},{0,4,7,3}
		};
		static const FVector Nl[6] = {
			{0,0,-1},{0,0,1},{0,-1,0},{0,1,0},{1,0,0},{-1,0,0}
		};
		for (int32 f = 0; f < 6; ++f)
		{
			const FVector4 col = ShadeColor(Rot.RotateVector(Nl[f]), Base);
			const FVector p0 = C + Rot.RotateVector(V[F[f][0]]);
			const FVector p1 = C + Rot.RotateVector(V[F[f][1]]);
			const FVector p2 = C + Rot.RotateVector(V[F[f][2]]);
			const FVector p3 = C + Rot.RotateVector(V[F[f][3]]);
			PushTri(Out, p0, p1, p2, col, col, col);
			PushTri(Out, p0, p2, p3, col, col, col);
		}
	}

	void AppendSolidCapsule(TArray<FColoredVertex>& Out, const FVector& C, const FQuat& Rot,
	                        float Radius, float HalfH, const FVector4& Base)
	{
		const FVector Axis = Rot.RotateVector(FVector(1,0,0)); // 긴축 = X (PhysX/AlignXToDir 규약)
		const FVector U    = Rot.RotateVector(FVector(0,1,0)); // 반경 평면
		const FVector V    = Rot.RotateVector(FVector(0,0,1)); // 반경 평면
		const FVector TopC = C + Axis * HalfH, BotC = C - Axis * HalfH;
		constexpr int32 Sectors = 12, HemiRings = 4;
		const float Step = kPi2 / Sectors;

		auto Radial = [&](float ang) -> FVector { return U*cosf(ang) + V*sinf(ang); };

		// 측면 실린더
		for (int32 si = 0; si < Sectors; ++si)
		{
			const FVector d0 = Radial(si*Step), d1 = Radial((si+1)*Step);
			const FVector4 c0 = ShadeColor(d0, Base), c1 = ShadeColor(d1, Base);
			const FVector t0 = TopC + d0*Radius, t1 = TopC + d1*Radius;
			const FVector b0 = BotC + d0*Radius, b1 = BotC + d1*Radius;
			PushTri(Out, t0, b0, b1, c0, c0, c1);
			PushTri(Out, t0, b1, t1, c0, c1, c1);
		}
		// 반구 (Hemi 0 = +Axis, Hemi 1 = -Axis)
		for (int32 Hemi = 0; Hemi < 2; ++Hemi)
		{
			const FVector O = Hemi ? BotC : TopC;
			const float Sign = Hemi ? -1.f : 1.f;
			auto HN = [&](int32 ri, int32 si) -> FVector
			{
				const float phi = (kPi/2.f) * ri / HemiRings; // 0(적도)..pi/2(극)
				const FVector r = Radial(si*Step);
				return r*cosf(phi) + Axis*(Sign*sinf(phi));
			};
			for (int32 ri = 0; ri < HemiRings; ++ri)
				for (int32 si = 0; si < Sectors; ++si)
				{
					const FVector n00 = HN(ri, si),   n01 = HN(ri, si+1);
					const FVector n10 = HN(ri+1, si),  n11 = HN(ri+1, si+1);
					const FVector4 c00 = ShadeColor(n00, Base), c01 = ShadeColor(n01, Base);
					const FVector4 c10 = ShadeColor(n10, Base), c11 = ShadeColor(n11, Base);
					PushTri(Out, O+n00*Radius, O+n10*Radius, O+n11*Radius, c00, c10, c11);
					PushTri(Out, O+n00*Radius, O+n11*Radius, O+n01*Radius, c00, c11, c01);
				}
		}
	}

	// ── 와이어프레임(라인) ────────────────────────────────────────────
	FVector4 PickWireColor(bool bSel, bool bBodySel)
	{
		if (bSel)     return FVector4(1.0f, 0.85f, 0.10f, 1.0f);
		if (bBodySel) return FVector4(0.20f, 1.00f, 0.55f, 1.0f);
		return                FVector4(0.15f, 0.80f, 0.42f, 1.0f);
	}

	void PushLine(TArray<FColoredLine>& Out, const FVector& A, const FVector& B, const FVector4& Col)
	{
		Out.push_back({ A, B, Col });
	}

	// 구 = 3 직교 대원(XY, YZ, ZX)
	void AppendWireSphere(TArray<FColoredLine>& Out, const FVector& C, float R, const FVector4& Col)
	{
		constexpr int32 Seg = 16;
		const FVector Ax[3] = { FVector(1,0,0), FVector(0,1,0), FVector(0,0,1) };
		for (int32 p = 0; p < 3; ++p)
		{
			const FVector U = Ax[p], V = Ax[(p + 1) % 3];
			FVector Prev = C + U * R;
			for (int32 i = 1; i <= Seg; ++i)
			{
				const float a = kPi2 * i / Seg;
				const FVector Cur = C + (U * cosf(a) + V * sinf(a)) * R;
				PushLine(Out, Prev, Cur, Col);
				Prev = Cur;
			}
		}
	}

	// 박스 = 8 꼭짓점(회전) → 12 엣지
	void AppendWireBox(TArray<FColoredLine>& Out, const FVector& C, const FQuat& Rot,
	                   float HX, float HY, float HZ, const FVector4& Col)
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
		for (int32 e = 0; e < 12; ++e) PushLine(Out, P[E[e][0]], P[E[e][1]], Col);
	}

	// 캡슐 = 양끝 링 2 + 세로선 4 + 캡 반원 호(반구당 2: U-Axis, V-Axis 평면)
	void AppendWireCapsule(TArray<FColoredLine>& Out, const FVector& C, const FQuat& Rot,
	                       float Radius, float HalfH, const FVector4& Col)
	{
		constexpr int32 Seg = 16;
		const FVector Axis = Rot.RotateVector(FVector(1,0,0)); // 긴축 = X (PhysX/AlignXToDir 규약)
		const FVector U    = Rot.RotateVector(FVector(0,1,0)); // 반경 평면
		const FVector V    = Rot.RotateVector(FVector(0,0,1)); // 반경 평면
		const FVector TopC = C + Axis * HalfH, BotC = C - Axis * HalfH;
		auto Radial = [&](float a) -> FVector { return U * cosf(a) + V * sinf(a); };

		// 1) 양끝 링 (U-V 평면)
		FVector PrevT = TopC + Radial(0.0f) * Radius;
		FVector PrevB = BotC + Radial(0.0f) * Radius;
		for (int32 i = 1; i <= Seg; ++i)
		{
			const float a = kPi2 * i / Seg;
			const FVector d = Radial(a);
			const FVector CurT = TopC + d * Radius, CurB = BotC + d * Radius;
			PushLine(Out, PrevT, CurT, Col);
			PushLine(Out, PrevB, CurB, Col);
			PrevT = CurT; PrevB = CurB;
		}
		// 2) 세로선 4 (±U, ±V)
		const FVector Dirs[4] = { U, U * -1.0f, V, V * -1.0f };
		for (int32 di = 0; di < 4; ++di)
			PushLine(Out, TopC + Dirs[di] * Radius, BotC + Dirs[di] * Radius, Col);
		// 3) 캡 반원 호 — 각 끝점에서 (U-Axis), (V-Axis) 평면의 반원
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
					const float a = kPi * i / HSeg;  // 0..pi 반원
					const FVector Cur = O + (H * cosf(a) + Axis * (Sign * sinf(a))) * Radius;
					PushLine(Out, Prev, Cur, Col);
					Prev = Cur;
				}
			}
		}
	}
}

#pragma endregion


FPhysicsShapeDebugSceneProxy::FPhysicsShapeDebugSceneProxy(UPhysicsShapeDebugComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags = EPrimitiveProxyFlags::EditorOnly
		| EPrimitiveProxyFlags::NeverCull
		| EPrimitiveProxyFlags::PhysicsShapeDebug;
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
			AppendSolidSphere(CachedSolid, WorldCenter, E.Radius, PickSolidColor(bSel, bBodySel));
			AppendWireSphere (CachedWire,  WorldCenter, E.Radius, PickWireColor(bSel, bBodySel));
		}
		for (int32 Bi = 0; Bi < (int32)BS->AggregateGeom.BoxElems.size(); ++Bi)
		{
			const FKBoxElem& E = BS->AggregateGeom.BoxElems[Bi];
			const bool bSel = bBodySel && SelKind == 2 && SelElem == Bi;
			const FVector WorldCenter = BoneWorldPos + BoneWorldQuat.RotateVector(E.Center);
			const FQuat   WorldRot    = BoneWorldQuat * E.Rotation;
			AppendSolidBox(CachedSolid, WorldCenter, WorldRot, E.HalfX, E.HalfY, E.HalfZ, PickSolidColor(bSel, bBodySel));
			AppendWireBox (CachedWire,  WorldCenter, WorldRot, E.HalfX, E.HalfY, E.HalfZ, PickWireColor(bSel, bBodySel));
		}
		for (int32 Ci = 0; Ci < (int32)BS->AggregateGeom.CapsuleElems.size(); ++Ci)
		{
			const FKCapsuleElem& E = BS->AggregateGeom.CapsuleElems[Ci];
			const bool bSel = bBodySel && SelKind == 3 && SelElem == Ci;
			const FVector WorldCenter = BoneWorldPos + BoneWorldQuat.RotateVector(E.Center);
			const FQuat   WorldRot    = BoneWorldQuat * E.Rotation;
			AppendSolidCapsule(CachedSolid, WorldCenter, WorldRot, E.Radius, E.HalfHeight, PickSolidColor(bSel, bBodySel));
			AppendWireCapsule (CachedWire,  WorldCenter, WorldRot, E.Radius, E.HalfHeight, PickWireColor(bSel, bBodySel));
		}
	}
}
