# 07. 용어집 + 미구현/미확인 목록

> 라인 번호는 확인 시점(2026-06-01) 스냅샷. "미사용"은 **코드에서 호출처를 찾지 못한 것**만 적었다(grep 근거 병기). 단정 못 할 것은 `[미확인]`/`추정:`으로 표시.

---

## 1. 용어집 (PhysX/물리)

| 용어 | 뜻 | 이 엔진에서 |
|---|---|---|
| **rigid body** | 변형 없는 강체 | `PxRigidDynamic` ← `FBodyInstance`/`UBodySetup` |
| **D6 joint** | 6자유도 일반 조인트(3선형+3각) | `PxD6Joint` ← `CreateD6Joint` |
| **twist** | 조인트 프레임 **X축 둘레** 회전 | `PxD6Axis::eTWIST`, 한계 ±대칭 |
| **swing1 / swing2** | **Y / Z축 둘레** 회전 | `eSWING1/eSWING2`, `PxJointLimitCone`(타원뿔) |
| **cone limit** | swing이 이루는 원뿔 한계 | `setSwingLimit` |
| **Locked / Limited / Free** | 축 자유도: 고정 / 범위제한 / 무제한 | `EConstraintMotion`→`PxD6Motion` |
| **anchor / joint frame** | 두 actor에 박는 로컬 프레임(pivot+축) | `ParentLocalFrame`/`ChildLocalFrame` |
| **hard / soft limit** | 즉시 차단 / 스프링 완충 | **hard만 사용**(스프링 파라미터 미설정) |
| **drive** | 조인트 모터/스프링(능동 구동) | **미사용** (3.1) |
| **breakable joint** | 일정 힘/토크 초과 시 파괴 | 구현은 있으나 래그돌 경로 미설정 (3.1) |
| **density vs mass** | 밀도(질량=밀도×부피) vs 질량 직접 | **density 경로 사용**(이름은 Mass) (3.2) |
| **inertia tensor** | 회전 관성 | `updateMassAndInertia`가 형상에서 계산 |
| **aggregate** | 여러 actor 묶음 + 자기충돌 일괄 제어 | `PxAggregate`, 자기충돌 전역 토글 |
| **CCD** | 연속 충돌 검출(터널링 방지) | scene `eENABLE_CCD` + 바디 `eENABLE_CCD`(강제 on) |
| **PCM** | persistent contact manifold | scene `eENABLE_PCM` |
| **kinematic vs dynamic** | 애님이 모는 바디 vs 물리가 모는 바디 | `EPhysicsBodyType`; 래그돌 진입 시 Dynamic 전환 |
| **fetchResults(true)** | 시뮬 완료까지 블로킹 후 결과 회수 | `PhysXRuntime.cpp:650` |
| **write-back** | 물리 actor 포즈 → 본 로컬 포즈 역산 | `ApplyPhysicsToBones` |

---

## 2. row-vector 빠른 참조 (자주 헷갈림)
- 점 변환: `v' = v · M` (M[3][*]가 이동).
- 합성: `Global = Local · ParentGlobal` (local 먼저).
- A를 B 로컬로: `A · B⁻¹`.
- 자식을 부모 로컬로(앵커): `ChildGlobal · ParentGlobal⁻¹`.
- 본 로컬 역산(write-back): `Local = ComponentGlobal · ParentGlobal⁻¹`.
- 상세: [06](06_coordinate_math.md).

---

## 3. PhysX엔 있으나 이 엔진이 안 쓰는 기능 (코드로 확인)

### 3.1 조인트 관련
- **drive (모터/스프링 구동)**: `setDrive`/`PxD6JointDrive` 호출 **0건**(grep). → 능동 토크·소프트 복원 없음. 순수 수동(passive) 래그돌.
- **soft limit**: `setTwistLimit`/`setSwingLimit`에 스프링(stiffness/damping) 파라미터를 주지 않음(`PhysXRuntime.cpp:938`) → 전부 hard limit.
- **breakable**: `setBreakForce`는 구현(`PhysXRuntime.cpp:941`)되어 있으나 인스턴스화가 `bBreakable`을 켜지 않음(`FPhysicsConstraintDesc` 기본 false) → 래그돌 조인트 안 부러짐.
- **선형 한계거리 / 축별 선형 모드**: 자산은 `bLockLinearMotion`(bool) 하나뿐 → 선형은 전부 Lock 또는 전부 Free. 부분/거리제한 불가([02](02_d6_joint_theory.md) 2.3).
- **비대칭 twist**: twist 한계가 단일 슬라이더 → 항상 `±θ`. min≠|max| 표현 불가([04](04_angular_limits.md) 2.2).

### 3.2 바디/재질 관련 (관찰된 동작 — 추후 수정 후보)
- **"Mass"가 사실상 density**: `PxRigidBodyExt::updateMassAndInertia(*, Desc.Mass)`(`PhysXRuntime.cpp:761`)와 `(*, Material.Density)`(`:898`) 모두 **density 오버로드**. `setMassAndUpdateInertia`(질량 직접)는 미사용 → 실효 질량 = density×부피. **필드 이름(Mass)과 의미(density) 불일치 → 잠재 버그.**
- **BodySetup Friction/Restitution 미적용**: 셰이프는 `createExclusiveShape(..., *DefaultMaterial)`(`:880`)로 고정 `0.5/0.5/0.3`(`:374`)만 사용. `ApplyBodyMaterial`이 채운 `Desc.Material`에서 실제 소비되는 값은 `Density`뿐.
- **bEnableGravity 무시**: 인스턴스화가 `BodyDesc.bUseGravity = true`로 강제(`SkeletalMeshComponent.cpp:789`). `bEnableCCD`도 강제 true.
- **SetCenterOfMass 미호출(래그돌)**: 컴포넌트 등록 경로(`RegisterComponent:534`)엔 있으나 래그돌 인스턴스화엔 없음 → COM은 `updateMassAndInertia` 계산값 사용.

### 3.3 충돌 관련
- **per-pair `DisabledCollisionPairs` 미배선**: 자산/에디터는 채우지만 런타임이 `IsCollisionDisabled`를 읽어 PhysX 필터에 반영하지 않음(grep: 호출처가 에디터·자산뿐). 필터셰이더는 `PxDefaultSimulationFilterShader`. → 실제 자기충돌 제어는 **aggregate 전역 on/off**가 전부([05](05_runtime_and_collision.md) 2.7).
- **커스텀 simulation filter shader 없음**: 그룹/마스크 기반 세밀 충돌 규칙 미사용(쿼리용 prefilter는 별도로 있음).

### 3.4 시뮬레이션 관련
- **solver iteration 커스텀 없음**: `setSolverIterationCounts` 호출 0건 → PhysX 기본 iteration. 무거운 사슬/큰 질량비에서 늘어짐 가능.
- **`eENABLE_STABILIZATION` 미설정**: scene 플래그에 stabilization 없음(`PhysXCore.cpp:197~202`엔 ACTIVE_ACTORS/CCD/PCM/RW_LOCK만).

---

## 4. 상태 전이 관련 관찰
- **bSimulatingPhysics를 끄는 공개 API 없음**: `StopPhysicsSimulation`이 바디는 파괴하지만 `USkeletalMeshComponent::bSimulatingPhysics`를 false로 되돌리는 setter가 없다고 코드 주석이 명시(`MeshEditorWidget.Physics.cpp:1442~1445`). → 같은 컴포넌트로 재시작하면 write-back 경로에 머무를 수 있음.

---

## 5. `[미확인]` 목록 (직접 확정하지 못함)
- **`RagdoleBone` 구조체**(`PhysicsTypes.h:251`, 철자 "Ragdole"): 정의만 존재, 사용처 grep **0건**. → 레거시/미사용 추정. 실제 래그돌 데이터는 `Bodies[]`/`Constraints[]`/`UPhysicsAsset`을 쓴다.
- **`FTransform::ToMatrix()` 내부 합성 순서**: 직접 읽지 않음. 단, `MakeWorldTransform`/`MakeRelativeTransform` 사용 결과가 행벡터 TRS와 일관됨을 확인([03](03_anchor_frames.md)).
- **`USkinnedMeshComponent::GetBoneWorldTransformByIndex` 내부**(`SkinnedMeshComponent.cpp:291`): 본 월드 트랜스폼을 돌려준다는 계약만 사용. 내부 수식은 직접 읽지 않음(이름·사용처로 역할 확정).
- **PhysX 4.1 정확한 D6 limit 내부 동작(soft/contactDistance 기본값)**: 일반론으로만 기술. 이 엔진이 기본값 외 별도 설정을 하지 않음은 확인.
- **`updateMassAndInertia` 중복 호출 순서 효과**: `CreateShape`(`:898`)와 `CreateRigidBody`(`:761`) 둘 다 호출 — 셰이프 루프 뒤 `:761`이 최종 권위로 보이나(추정: 마지막 호출이 우선), 두 density 값이 같아(기본 1.0) 가시적 차이는 없음.

---

## 6. 관련 메모(엔진 진단 맥락)
- 본 학습은 코드 현재 상태 기준. 과거 진단 문서(`Docs/RAGDOLL_*`, `Docs/physics_temp.md` 등)는 일부 stale일 수 있음(메모리 `[[project_physics_diag_docs_stale]]`).
- 에디터 "Simulate" 무반응의 과거 원인(PreviewWorld BeginPlay 미호출 → write-back 차단)은 현재 `TickPhysicsSimulation`이 step+write-back을 직접 돌려 우회([05](05_runtime_and_collision.md) 2.3, 메모리 `[[project_ragdoll_simulate_noreact]]`).
