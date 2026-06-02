#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Quat.h"

// ============================================================
// 콜리전 셰이프 디버그 렌더링용 공유 타입 + geometry 빌더
// PhysicsShapeDebugSceneProxy / StaticMeshCollisionDebugSceneProxy 양쪽에서 사용
// ============================================================

struct FColoredVertex
{
	FVector  Pos;
	FVector4 Color;
};

struct FColoredLine
{
	FVector  Start;
	FVector  End;
	FVector4 Color;
};

namespace ShapeDebugUtils
{
	constexpr float kPi  = 3.14159265f;
	constexpr float kPi2 = kPi * 2.0f;

	// ── 색상 헬퍼 ────────────────────────────────────────────────
	inline FVector4 ShadeColor(const FVector& WorldN, const FVector4& Base)
	{
		static const FVector L = FVector(0.35f, 0.45f, 0.82f).Normalized();
		float d = WorldN.Dot(L);
		if (d < 0.f) d = 0.f;
		const float s = 0.40f + 0.60f * d;
		return FVector4(Base.X * s, Base.Y * s, Base.Z * s, Base.W);
	}

	// ── 기본 emit 헬퍼 ───────────────────────────────────────────
	inline void PushTri(TArray<FColoredVertex>& Out,
	                    const FVector& A, const FVector& B, const FVector& C,
	                    const FVector4& cA, const FVector4& cB, const FVector4& cC)
	{
		Out.push_back({ A, cA }); Out.push_back({ B, cB }); Out.push_back({ C, cC });
		Out.push_back({ A, cA }); Out.push_back({ C, cC }); Out.push_back({ B, cB });
	}

	inline void PushLine(TArray<FColoredLine>& Out,
	                     const FVector& A, const FVector& B, const FVector4& Col)
	{
		Out.push_back({ A, B, Col });
	}

	// ── Solid: Sphere ─────────────────────────────────────────────
	inline void AppendSolidSphere(TArray<FColoredVertex>& Out,
	                              const FVector& C, float R, const FVector4& Base)
	{
		constexpr int32 Rings = 8, Sectors = 12;
		auto N = [&](int32 ri, int32 si) -> FVector
		{
			const float v = kPi  * ri / Rings;
			const float u = kPi2 * si / Sectors;
			return FVector(sinf(v)*cosf(u), sinf(v)*sinf(u), cosf(v));
		};
		for (int32 ri = 0; ri < Rings; ++ri)
			for (int32 si = 0; si < Sectors; ++si)
			{
				const FVector n00 = N(ri,si),   n01 = N(ri,si+1);
				const FVector n10 = N(ri+1,si),  n11 = N(ri+1,si+1);
				const FVector4 c00 = ShadeColor(n00,Base), c01 = ShadeColor(n01,Base);
				const FVector4 c10 = ShadeColor(n10,Base), c11 = ShadeColor(n11,Base);
				PushTri(Out, C+n00*R, C+n10*R, C+n11*R, c00,c10,c11);
				PushTri(Out, C+n00*R, C+n11*R, C+n01*R, c00,c11,c01);
			}
	}

	// ── Solid: Box ────────────────────────────────────────────────
	inline void AppendSolidBox(TArray<FColoredVertex>& Out,
	                           const FVector& C, const FQuat& Rot,
	                           float HX, float HY, float HZ, const FVector4& Base)
	{
		const FVector V[8] = {
			{-HX,-HY,-HZ},{HX,-HY,-HZ},{HX,HY,-HZ},{-HX,HY,-HZ},
			{-HX,-HY, HZ},{HX,-HY, HZ},{HX,HY, HZ},{-HX,HY, HZ},
		};
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
			PushTri(Out, p0,p1,p2, col,col,col);
			PushTri(Out, p0,p2,p3, col,col,col);
		}
	}

	// ── Solid: Capsule ────────────────────────────────────────────
	inline void AppendSolidCapsule(TArray<FColoredVertex>& Out,
	                               const FVector& C, const FQuat& Rot,
	                               float Radius, float HalfH, const FVector4& Base)
	{
		const FVector Axis = Rot.RotateVector(FVector(1,0,0));
		const FVector U    = Rot.RotateVector(FVector(0,1,0));
		const FVector V    = Rot.RotateVector(FVector(0,0,1));
		const FVector TopC = C + Axis*HalfH, BotC = C - Axis*HalfH;
		constexpr int32 Sectors = 12, HemiRings = 4;
		const float Step = kPi2 / Sectors;
		auto Radial = [&](float ang) -> FVector { return U*cosf(ang) + V*sinf(ang); };

		for (int32 si = 0; si < Sectors; ++si)
		{
			const FVector d0 = Radial(si*Step), d1 = Radial((si+1)*Step);
			const FVector4 c0 = ShadeColor(d0,Base), c1 = ShadeColor(d1,Base);
			const FVector t0=TopC+d0*Radius, t1=TopC+d1*Radius;
			const FVector b0=BotC+d0*Radius, b1=BotC+d1*Radius;
			PushTri(Out, t0,b0,b1, c0,c0,c1);
			PushTri(Out, t0,b1,t1, c0,c1,c1);
		}
		for (int32 Hemi = 0; Hemi < 2; ++Hemi)
		{
			const FVector O = Hemi ? BotC : TopC;
			const float Sign = Hemi ? -1.f : 1.f;
			auto HN = [&](int32 ri, int32 si) -> FVector
			{
				const float phi = (kPi/2.f) * ri / HemiRings;
				return Radial(si*Step)*cosf(phi) + Axis*(Sign*sinf(phi));
			};
			for (int32 ri = 0; ri < HemiRings; ++ri)
				for (int32 si = 0; si < Sectors; ++si)
				{
					const FVector n00=HN(ri,si), n01=HN(ri,si+1);
					const FVector n10=HN(ri+1,si), n11=HN(ri+1,si+1);
					const FVector4 c00=ShadeColor(n00,Base), c01=ShadeColor(n01,Base);
					const FVector4 c10=ShadeColor(n10,Base), c11=ShadeColor(n11,Base);
					PushTri(Out, O+n00*Radius,O+n10*Radius,O+n11*Radius, c00,c10,c11);
					PushTri(Out, O+n00*Radius,O+n11*Radius,O+n01*Radius, c00,c11,c01);
				}
		}
	}

	// ── Wire: Sphere ──────────────────────────────────────────────
	inline void AppendWireSphere(TArray<FColoredLine>& Out,
	                             const FVector& C, float R, const FVector4& Col)
	{
		constexpr int32 Seg = 16;
		const FVector Ax[3] = { FVector(1,0,0), FVector(0,1,0), FVector(0,0,1) };
		for (int32 p = 0; p < 3; ++p)
		{
			const FVector U=Ax[p], V=Ax[(p+1)%3];
			FVector Prev = C + U*R;
			for (int32 i = 1; i <= Seg; ++i)
			{
				const float a = kPi2 * i / Seg;
				const FVector Cur = C + (U*cosf(a) + V*sinf(a))*R;
				PushLine(Out, Prev, Cur, Col);
				Prev = Cur;
			}
		}
	}

	// ── Wire: Box ─────────────────────────────────────────────────
	inline void AppendWireBox(TArray<FColoredLine>& Out,
	                          const FVector& C, const FQuat& Rot,
	                          float HX, float HY, float HZ, const FVector4& Col)
	{
		const FVector L[8] = {
			{-HX,-HY,-HZ},{HX,-HY,-HZ},{HX,HY,-HZ},{-HX,HY,-HZ},
			{-HX,-HY, HZ},{HX,-HY, HZ},{HX,HY, HZ},{-HX,HY, HZ},
		};
		FVector P[8];
		for (int32 k = 0; k < 8; ++k) P[k] = C + Rot.RotateVector(L[k]);
		static const int32 E[12][2] = {
			{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
		};
		for (int32 e = 0; e < 12; ++e) PushLine(Out, P[E[e][0]], P[E[e][1]], Col);
	}

	// ── Wire: Capsule ─────────────────────────────────────────────
	inline void AppendWireCapsule(TArray<FColoredLine>& Out,
	                              const FVector& C, const FQuat& Rot,
	                              float Radius, float HalfH, const FVector4& Col)
	{
		constexpr int32 Seg = 16;
		const FVector Axis = Rot.RotateVector(FVector(1,0,0));
		const FVector U    = Rot.RotateVector(FVector(0,1,0));
		const FVector V    = Rot.RotateVector(FVector(0,0,1));
		const FVector TopC = C+Axis*HalfH, BotC = C-Axis*HalfH;
		auto Radial = [&](float a) -> FVector { return U*cosf(a) + V*sinf(a); };

		FVector PrevT = TopC+Radial(0.0f)*Radius, PrevB = BotC+Radial(0.0f)*Radius;
		for (int32 i = 1; i <= Seg; ++i)
		{
			const float a = kPi2*i/Seg;
			const FVector d = Radial(a);
			const FVector CurT=TopC+d*Radius, CurB=BotC+d*Radius;
			PushLine(Out, PrevT, CurT, Col);
			PushLine(Out, PrevB, CurB, Col);
			PrevT=CurT; PrevB=CurB;
		}
		const FVector Dirs[4] = { U, U*-1.0f, V, V*-1.0f };
		for (int32 di = 0; di < 4; ++di)
			PushLine(Out, TopC+Dirs[di]*Radius, BotC+Dirs[di]*Radius, Col);

		const int32 HSeg = Seg/2;
		const FVector Planes[2] = { U, V };
		for (int32 hemi = 0; hemi < 2; ++hemi)
		{
			const FVector O = hemi ? BotC : TopC;
			const float Sign = hemi ? -1.0f : 1.0f;
			for (int32 pl = 0; pl < 2; ++pl)
			{
				const FVector H = Planes[pl];
				FVector Prev = O + H*Radius;
				for (int32 i = 1; i <= HSeg; ++i)
				{
					const float a = kPi*i/HSeg;
					const FVector Cur = O + (H*cosf(a) + Axis*(Sign*sinf(a)))*Radius;
					PushLine(Out, Prev, Cur, Col);
					Prev = Cur;
				}
			}
		}
	}

	// ── Wire: Convex (버텍스 AABB) ────────────────────────────────
	inline void AppendWireConvex(TArray<FColoredLine>& Out,
	                             const TArray<FVector>& Verts, const FVector4& Col)
	{
		if (Verts.empty()) return;
		FVector Min = Verts[0], Max = Verts[0];
		for (const FVector& Vt : Verts)
		{
			if (Vt.X < Min.X) Min.X = Vt.X; if (Vt.X > Max.X) Max.X = Vt.X;
			if (Vt.Y < Min.Y) Min.Y = Vt.Y; if (Vt.Y > Max.Y) Max.Y = Vt.Y;
			if (Vt.Z < Min.Z) Min.Z = Vt.Z; if (Vt.Z > Max.Z) Max.Z = Vt.Z;
		}
		const FVector Center  = (Min + Max) * 0.5f;
		const FVector HalfExt = (Max - Min) * 0.5f;
		AppendWireBox(Out, Center, FQuat::Identity,
		              HalfExt.X, HalfExt.Y, HalfExt.Z, Col);
	}
} // namespace ShapeDebugUtils
