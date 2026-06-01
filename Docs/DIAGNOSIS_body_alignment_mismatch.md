# DIAGNOSIS — body 정렬 방향 ≠ 본 방향

> 사이클: **diagnose/verify** (코드 미수정). 산출물: 본 문서 1개.
> **결론: 가설 C 확정** — 물리(PhysX)는 X-긴축으로 **정상**, 디버그 렌더러 3개가 **Z-긴축**으로 그려 화면에서만 90° 어긋남. (시각 버그, 물리 정상)
> 참조: `Docs/GOAL4_AUTOGEN_DIAGNOSIS.md`, `Docs/diagnose_auto_constraint.md`, `Docs/impl_spec_body_debug_lines.md`, 인프라 맵 `Docs/physics_temp.md`.

---

## A~D 판정표

| 가설 | 참/거짓 | 근거 (파일:행 / 값 / 수식) | 한줄 결론 |
|---|---|---|---|
| **A** — `GetReferenceLocalPose()` 기준 공간 | **거짓** | `NormalizeBonePoseData`: `ReferenceGlobalPose = ReferenceLocalPose * Parent.ReferenceGlobalPose` [SkeletalMeshAsset.h:219-221]. `FMatrix::operator*`=A행·B열 내적 + `GetLocation()=M[3][0..2]`(위치 4번째 행) [Matrix.cpp:74, 500-503] ⇒ **row-vector 규약**, Local 은 **부모-상대**. ⇒ `d=Bones[cStar].GetReferenceLocalPose().GetLocation()` [Physics.cpp:1238] 은 **부모(본 b) 로컬의 자식 오프셋 = 본 방향**. GOAL4:59 와 일치. | 생성부 가정(부모-상대) 맞음. 컴포넌트 공간 아님 → **원인 아님** |
| **B** — 부착 본 프레임 vs 방향 산정 프레임 | **거짓** | `d`·`Rot=AlignXToDir(d)` 모두 본 b 로컬 [Physics.cpp:1238-1240]. 런타임은 `BoneRot(b) * E.Rotation` 적용 [SkeletalMeshComponent.cpp:808,811],[PhysicsShapeDebugSceneProxy.cpp:319,328]. A가 부모-상대로 판명 → 프레임 일치(A 종속). | 프레임 일관 → **원인 아님** |
| **C** — 디버그 와이어/솔리드 렌더 축 | **참 (근본 원인)** | 물리=X-긴축: `AlignXToDir`가 X정렬 [Physics.cpp:110-119], `AppendPhysicsShapes`가 `Capsule.Rotation` 보정없이 통과 [SkeletalMeshComponent.cpp:105,108], 컴포넌트 경로만 Z→X 보정 `FromAxisAngle((0,1,0),-π/2)` [GOAL4:58 / PhysXRuntime.cpp:487]. **렌더=Z-긴축**: `Up=Rot*(0,0,1)`, `TopC/BotC=C±Up*HalfH` — `DbgWireCapsule` [SkeletalMeshComponent.cpp:743,746], `AppendWireCapsule` [PhysicsShapeDebugSceneProxy.cpp:194,197], `AppendSolidCapsule` [PhysicsShapeDebugSceneProxy.cpp:98,101]. ⇒ 그려진 긴축=`Rot*Z` ⟂ `Rot*X=d`. | **90° 시각 불일치. 물리 정상, 렌더 3종만 틀림** |
| **D** — 회전 합성 / 행렬 규약 | **거짓** | `BoneRot * E.Rotation` 곱이 박스·캡슐 **동일** 적용인데 박스는 정상 렌더(아래 STEP2). `FQuat::operator*`=표준 Hamilton [Quat.h:25-33], `RotateVector`=`q*v*q⁻¹` [Quat.h:68-72], `FromAxisAngle` 표준 [Quat.h:17-22]. 캡슐 오차는 쿼터니언이 아니라 **렌더러 축 선택**에서 발생. | 쿼터니언/곱순서 정상 → **원인 아님** |

**비고(가정↔코드 차이):**
- 프롬프트는 생성 로직을 `SkeletalMeshComponent.cpp`로 가정했으나, 실제 `GeneratePhysicsBodies`/`AutoGen_AlignXToDir`/`AutoGen_ComputeConstraintAnchorLocal` 는 **`MeshEditorWidget.Physics.cpp`** 에 있음. `SkeletalMeshComponent.cpp` 는 런타임 변환(:105,108)·디버그 렌더(:740)만 담당. (라인 105/108/740/811 참조는 모두 일치 확인.)
- 사전 검증 사실(PhysX X-긴축, 런타임 무보정, AlignXToDir 수학 정상)은 **모두 코드와 일치**.

---

## STEP 2 — 교차 검증 (가설 분리)

- [x] **물리 vs 시각 분리**: 물리는 `Capsule.Rotation`(=`AlignXToDir(d)`)을 PhysX 에 무보정 통과 → PhysX X축 캡슐이 `Rot*X=d`(본 방향)으로 누움 ⇒ **시뮬레이션은 본 따라 정렬(정상)**. 렌더만 Z-긴축으로 90° 어긋나 보임.
- [x] **Sphere 대조(방향 없음)**: 위치=`BonePos+BoneRot.RotateVector(E.Center)` [PhysicsShapeDebugSceneProxy.cpp:310],[SkeletalMeshComponent.cpp:806], 회전 무관. `Center=d*(L*0.5)` [Physics.cpp:1239] ⇒ **위치 산정 정상, 방향만 문제**. (A의 회전부 아님 → C로 좁혀짐)
- [x] **Box vs Capsule 대조(결정적 판별자)**: 둘 다 같은 `Rot`. 박스 긴변=로컬 X (`HalfX=L*0.5`, `HalfY=HalfZ=Rad`) [Physics.cpp:1248]. 박스 렌더는 꼭짓점 `{±HX,±HY,±HZ}` 를 `Rot` 로 직접 회전 [PhysicsShapeDebugSceneProxy.cpp:174-187],[SkeletalMeshComponent.cpp:807-809] ⇒ 긴변=`Rot*X=d` **정상 정렬**. 캡슐만 `Rot*Z` 로 90° 틀어짐. ⇒ **캡슐 전용 축 버그(C) 확정**, A/D(라면 박스도 틀어짐) 배제.
- [ ] **단일 본 로그(구현 사이클 제안)**: 본 하나에 대해 `child.GetReferenceLocalPose().GetLocation()`(=d), `child.GlobalPos-parent.GlobalPos`, 최종 `E.Rotation.RotateVector((1,0,0))` 3벡터 로그 → d가 본 방향과 일치함을 수치 확인(A=거짓 재확인용). **여기선 위치만 명시, 코드 미삽입.**

> **예측(검증 시나리오)**: PrimitiveType=Box 로 재생성 → 본과 정렬됨. Capsule 로 재생성 → 90° 어긋남. 이 차이가 나오면 C 확정.

---

## STEP 3 — 채택 수정 분기: **C (와이어/솔리드 긴축 X정렬 교정)**

> A·B·D 분기 미채택. 물리(생성/런타임)는 **정상이므로 절대 건드리지 않음**.

- [ ] **수정 대상 3개 함수(렌더 전용)** — 긴축을 Z → **X** 로 교정:
  - `DbgWireCapsule` [SkeletalMeshComponent.cpp:740]
  - `AppendWireCapsule` [PhysicsShapeDebugSceneProxy.cpp:190]
  - `AppendSolidCapsule` [PhysicsShapeDebugSceneProxy.cpp:95]
- [ ] **교정 방식(공통)**: 긴축 `Axis=Rot.RotateVector((1,0,0))`, 캡(반구) 중심 `TopC/BotC=C±Axis*HalfH`, 반경 평면을 `Rot*Y`·`Rot*Z` 직교쌍으로 spanning(예: `U=Rot*(0,1,0)`, `V=Rot*(0,0,1)`; `Radial(a)=U*cos+V*sin`), 캡 호는 `(U,Axis)`·`(V,Axis)` 평면. (현 코드의 Up=Z 가정만 X 로 치환, 나머지 구조 동일.)
- [ ] **금지**: `AutoGen_AlignXToDir`/`GeneratePhysicsBodies`/`AppendPhysicsShapes` 등 물리 경로 무수정.
- [ ] **영향 범위**: 렌더 3함수만. **회귀 위험**: 3개를 **반드시 함께** 교정(하나만 고치면 ①솔리드 vs 와이어, ②`bShowPhysicsBodies` 경로 vs `PhysicsShapeDebugComponent` 경로 가 서로 불일치). Sphere/Box 렌더는 무수정(정상).

---

## STEP 4 — 연쇄 영향 점검 (같은 API/가정 공유 코드)

- [x] **컨스트레인트 앵커** `AutoGen_ComputeConstraintAnchorLocal` [Physics.cpp:128-134]: `Rel=ChildGlobal*ParentGlobal⁻¹=ChildLocal`(row-vector) ⇒ `OutPos`=부모→자식 오프셋, `OutRot=AlignXToDir(OutPos)`. A=거짓(부모-상대 정상)이므로 **앵커도 정상**(별건 결함 아님). *주: A가 원인이었다면 앵커도 같은 결함을 공유했을 것 → A 배제가 앵커 무결을 보증.*
- [x] **수동 경로**: `CreateConstraintWith` [Physics.cpp:488-503] 도 동일 `AutoGen_ComputeConstraintAnchorLocal` 사용 → 정상. 수동 Add Body 셰이프 [Physics.cpp:451-471] 는 `E.Rotation` 기본 identity(자동 방향산정 없음) → 공유 결함 없음.
- [x] **디버그 렌더 2경로 + 솔리드 일관성**: `DbgWireCapsule`(와이어, SkeletalMeshComponent) + `AppendWireCapsule`(와이어, Proxy) + `AppendSolidCapsule`(솔리드, Proxy) **3개 모두 Z-긴축** → 전부 물리(X)와 불일치. STEP3 에서 3개 동시 교정 필수(부분 수정 시 새 불일치 발생).

---

## 요약

- **근본 원인 = C**: 캡슐 디버그 렌더러(와이어 2 + 솔리드 1)가 긴축을 **Z**(`Up=Rot*(0,0,1)`)로 가정하나, 자동생성/PhysX 는 **X**(`AlignXToDir`) 규약 → 화면상 90° 불일치.
- **물리 시뮬레이션은 정상**(캡슐이 본 따라 누움). 사용자가 본 "틀어짐"은 **시각 표시만의 문제**.
- **수정**: 렌더 3함수 긴축 X정렬 교정(동시), 물리 경로 무수정. 구현은 별도 사이클.
