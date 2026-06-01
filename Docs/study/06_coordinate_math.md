# 06. 좌표·수학 레퍼런스

> 라인 번호는 **확인 시점(2026-06-01) 스냅샷**이며, 항상 심볼명을 함께 적는다. 실제 위치는 심볼로 재확인할 것.
> 이 문서는 나머지 문서(01~05)가 의존하는 수학 규약을 한곳에 모은다.

---

## 1. 이론

### 1.1 행벡터(row-vector) vs 열벡터(column-vector)
4×4 동차행렬로 점/회전/이동을 표현할 때 두 규약이 있다.

- **열벡터**: `v' = M · v`. 변환 합성은 `M_total = M_parent · M_local` (왼쪽이 나중에 적용).
- **행벡터**: `v' = v · M`. 변환 합성은 `M_total = M_local · M_parent` (왼쪽이 먼저 적용). 이동성분은 행렬의 **4번째 행**.

두 규약은 서로 전치(transpose) 관계다. **곱의 순서를 규약과 어긋나게 쓰면 회전·이동이 뒤집히는 버그**가 난다.

### 1.2 쿼터니언
단위 쿼터니언 `q = (x, y, z, w)`는 회전을 표현한다(`w = cos(θ/2)`, `(x,y,z) = axis·sin(θ/2)`).
- 합성: 쿼터니언 곱(Hamilton 곱). 비가환.
- 벡터 회전(능동): `v' = q · v · q⁻¹` (단위 q에서 `q⁻¹ = 켤레`).

### 1.3 엔진 좌표 ↔ PhysX 좌표
물리 엔진을 래핑할 때는 두 좌표계(축 방향, 핸드니스, 단위)를 맞춰야 한다. 변환 지점이 누락되면 "물리는 맞는데 렌더가 90° 틀어짐" 류의 버그가 난다.

---

## 2. 코드 대조 (이 엔진의 실제 규약)

### 2.1 행렬은 row-major + 행벡터 규약
`FMatrix` (`Source/Engine/Math/Matrix.h`): `union { float M[4][4]; float Data[16]; ... }` — row-major 저장.

행렬×행렬 (`FMatrix::operator*`, `Matrix.cpp:74`, 비-SIMD 경로):
```cpp
ret.M[i][j] += M[i][k] * Other.M[k][j];   // 표준 row-major 곱 (this의 행 · Other의 열)
```
벡터×행렬 (`operator*(const FVector&, const FMatrix&)`, `Matrix.cpp:384`) — **벡터가 왼쪽 = 행벡터**:
```cpp
ret.X = v.X*M[0][0] + v.Y*M[1][0] + v.Z*M[2][0] + M[3][0];   // 이동성분이 4번째 행(M[3][*])
... (Y, Z 동일)
```
- `FMatrix::GetLocation()` (`Matrix.cpp:500`): `return FVector(M[3][0], M[3][1], M[3][2]);` → 이동은 4번째 행. row-vector 규약 확정.
- 결론: **`v' = v · M`, 합성은 `M_total = M_local · M_parent`** (local 먼저).

> 주의: `FMatrix::SetAxes`(`Matrix.cpp:515`)와 뷰행렬(`MakeViewMatrix`)은 카메라용으로 `M[2]=Forward`를 쓰지만, 이는 view-space 매핑이다. **본/물리의 월드 회전 행렬에서는 행 0/1/2가 각각 로컬 X/Y/Z 축**이며 이 문서의 관심사다.

### 2.2 쿼터니언 (`FQuat`, `Source/Engine/Math/Quat.h`)
- 멤버: `float X, Y, Z, W;` 기본 생성자 `(0,0,0,1)` = identity.
- `FromAxisAngle(Axis, AngleRad)` (`Quat.h:17`): `FQuat(Axis.X*S, Axis.Y*S, Axis.Z*S, cosf(Half))`, `S=sinf(AngleRad*0.5)`. 표준 반각 공식.
- `operator*` (`Quat.h:25`): Hamilton 곱. (검산: `W*Q.X + X*Q.W + Y*Q.Z - Z*Q.Y` 등 = 표준 `q_this ⊗ q_Q`.)
- `Inverse()` (`Quat.h:61`): `FQuat(-X,-Y,-Z,W)` = 켤레(단위 쿼터니언 가정).
- `RotateVector(V)` (`Quat.h:68`): `*this * VQ * Inverse()` = **`q·v·q⁻¹`** (능동 회전).
- `GetForwardVector()` = `RotateVector(1,0,0)` → **엔진의 "정면"은 +X**.

### 2.3 엔진 ↔ PhysX 변환은 1:1 (축 스왑/핸드니스 변환 없음)
`Source/Engine/Physics/PhysXHelpers.h`:
```cpp
inline PxVec3 ToPxVec3(const FVector& V){ return PxVec3(V.X, V.Y, V.Z); }        // :8
inline PxQuat ToPxQuat(const FQuat& Q){ return PxQuat(Q.X, Q.Y, Q.Z, Q.W); }     // :13
inline PxTransform ToPxTransform(const FTransform& T){ return PxTransform(ToPxVec3(T.Location), ToPxQuat(T.Rotation)); } // :28
```
- **성분 그대로 복사**한다. 전역 축 remap·핸드니스 플립이 **없다** → 엔진 월드 좌표 == PhysX 월드 좌표.
- 스케일은 버려진다(PhysX `PxTransform`은 위치+회전만). 래그돌 경로는 항상 unit scale로 만든다(`MakeUnitScaleTransform`, `SkeletalMeshComponent.cpp:54`).
- 월드 규약: **+X 정면, +Y 우, +Z 상**. 중력은 `(0, 0, -9.81)` (`PhysXRuntime.cpp:361`) → -Z. (`PhysicsTypes.h:220` 주석도 "+X forward, +Y right, +Z up" 명시.)

### 2.4 본 포즈의 row-vector 누적
`FSkeletalMesh::NormalizeBonePoseData` (`Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h:208`):
```cpp
Bone.ReferenceGlobalPose = (Bone.ParentIndex >= 0 && Bone.ParentIndex < BoneIndex)
    ? Bone.ReferenceLocalPose * Bones[Bone.ParentIndex].ReferenceGlobalPose   // local · parentGlobal
    : Bone.ReferenceLocalPose;
```
→ **`Global = Local · ParentGlobal`** (행벡터 규약, local 먼저). 본 인덱스는 parent-first 정렬이라 단일 전방 순회로 부모 글로벌이 항상 먼저 채워진다.

---

## 부록 A. "좌표 규약을 놓치면 생기는 버그" — 캡슐 장축 X vs Z

같은 캡슐인데 **물리 장축과 렌더 장축이 다르면** 디버그 캡슐이 90° 틀어져 보인다. 이 엔진엔 두 경로가 공존한다.

| 경로 | 캡슐 장축 규약 | 보정 |
|---|---|---|
| PhysX `PxCapsuleGeometry(Radius, HalfHeight)` | **+X** (PhysX 고정) | — |
| 래그돌 바디 (`GeneratePhysicsBodies`) | shape `Rotation = AutoGen_AlignXToDir(본방향)` → X를 본방향에 정렬 | 추가 보정 불필요 |
| 엔진 `UCapsuleComponent` (래그돌 아님) | **로컬 Z** 장축 | `BuildBodyDescFromComponent`에서 Z→X 보정 |

컴포넌트 경로의 보정 (`Source/Engine/Physics/PhysXRuntime.cpp:493`):
```cpp
// PhysX capsules are X-axis aligned; engine capsules use local Z as their long axis.
ShapeDesc.LocalTransform.Rotation = FQuat::FromAxisAngle(FVector(0.0f, 1.0f, 0.0f), -PhysicsPi * 0.5f); // Y축 -90° → Z를 X로
```
디버그 렌더는 **X 장축**으로 그린다 (`DbgWireCapsule`, `SkeletalMeshComponent.cpp:1108`):
```cpp
const FVector Axis = Rot.RotateVector(FVector(1,0,0)); // 긴축 = X (PhysX/AlignXToDir 규약)
```
- **교훈**: 래그돌 캡슐은 X 장축(AlignXToDir)으로 만들고 렌더도 X 장축으로 그린다 → 일치. 만약 렌더가 Z 장축을 가정하면 래그돌 캡슐이 옆으로 누워 보인다. (최근 커밋 `9d0d3f4e "debug capsule fix"`가 이 정렬을 맞춘 것으로 보인다 — 현재 코드는 X 장축으로 일치.)
- `AutoGen_AlignXToDir` 주석도 동일 근거를 단다: `// PhysX 캡슐 로컬축 = X` (`MeshEditorWidget.Physics.cpp:109`).

## 부록 B. 자주 쓰는 변환 헬퍼 (런타임)
`Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp` 익명 네임스페이스:
- `MakeWorldTransform(Local, ParentWorld)` (`:64`) = `FTransform(Local.ToMatrix() * ParentWorld.ToMatrix())` — local→world (행벡터).
- `MakeRelativeTransform(World, ParentWorld)` (`:59`) = `FTransform(World.ToMatrix() * ParentWorld.ToMatrix().GetInverse())` — world→parent-local.
- 이 둘이 03 문서의 앵커 3단 변환을 구성한다.

> `[미확인]` `FTransform::ToMatrix()` 내부(스케일·회전·이동 합성 순서)는 본 학습에서 직접 읽지 않았다. 단, 위 두 헬퍼의 사용 결과(03 문서에서 초기 오차 0)가 행벡터 TRS와 일관됨을 확인했다.
