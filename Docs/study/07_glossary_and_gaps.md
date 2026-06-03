# 07. 용어집 + 미구현/미확인 목록

> 라인 번호는 확인 시점(2026-06-03) 스냅샷. "미사용"은 **코드에서 호출처를 찾지 못한 것**만 적었다(grep 근거 병기). 단정 못 할 것은 `[미확인]`/`추정:`으로 표시.
> ⚠ 2026-06-03 갱신: 과거 gap이던 **(1) per-pair `DisabledCollisionPairs` 런타임 미배선**과 **(2) 커스텀 필터셰이더 부재**는 둘 다 **해소**됐다(3.3 참조). 신규로 triangle-mesh 충돌·균등 스케일 베이킹 경로가 생겼다.

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
| **aggregate** | 여러 actor 묶음 + 자기충돌 일괄 제어 | `PxAggregate`, self-collision OFF 경로의 전역 토글 |
| **filter shader** | 충돌 쌍 생성 규칙(필터) | ★ 커스텀 `KraftonRagdollFilterShader`(`PhysXRuntime.cpp:327`) — same-actor 제외/래그돌 그룹 eKILL/채널 응답 |
| **CCD** | 연속 충돌 검출(터널링 방지) | scene `eENABLE_CCD` + 바디 `eENABLE_CCD`(강제 on) |
| **PCM** | persistent contact manifold | scene `eENABLE_PCM` |
| **kinematic vs dynamic** | 애님이 모는 바디 vs 물리가 모는 바디 | `EPhysicsBodyType{Static,Dynamic,Kinematic}`; 래그돌 진입 시 Dynamic 전환 |
| **triangle mesh** | 임의 삼각형 콜리전(static 전용) | ★ 신규: `CookTriangleMesh`/`PxTriangleMeshGeometry`(static-mesh 충돌, 3.5) |
| **fetchResults(true)** | 시뮬 완료까지 블로킹 후 결과 회수 | `PhysXRuntime.cpp:1311` |
| **write-back** | 물리 actor 포즈 → 본 로컬 포즈 역산 | `ApplyPhysicsToBones`(+ scale-1 정규화) |

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
- **soft limit**: `setTwistLimit`/`setSwingLimit`에 스프링(stiffness/damping) 파라미터를 주지 않음(`PhysXRuntime.cpp:1684~1685`) → 전부 hard limit.
- **breakable**: `setBreakForce`는 구현(`PhysXRuntime.cpp:1689`, `if(bBreakable)` 가드 `:1687`)되어 있으나 인스턴스화가 `bBreakable`을 켜지 않음(`FPhysicsConstraintDesc` 기본 false, `PhysicsTypes.h:176`) → 래그돌 조인트 안 부러짐.
- **projection**: `setConstraintFlag(ePROJECTION)`/`setProjection*Tolerance` 호출 **0건**(grep). 과거 시도 후 제거([09](09_ragdoll_session_fixes.md) 3장).
- **선형 한계거리 / 축별 선형 모드**: 자산은 `bLockLinearMotion`(bool) 하나뿐, `setLinearLimit` 호출 0건 → 선형은 전부 Lock 또는 전부 Free. 부분/거리제한 불가([02](02_d6_joint_theory.md) 2.3).
- **비대칭 twist**: twist 한계가 단일 슬라이더 → 항상 `±θ`. min≠|max| 표현 불가(자산 스키마 기준). (런타임 `FPhysicsConstraintDesc`는 `TwistLimitRadiansMin/Max`를 따로 갖지만 인스턴스화가 대칭으로만 채움 — [04](04_angular_limits.md) 2.2.)

### 3.2 바디/재질 관련 (관찰된 동작 — 추후 수정 후보)
- **"Mass"가 사실상 density**: `PxRigidBodyExt::updateMassAndInertia(*, Desc.Mass)`(`PhysXRuntime.cpp:1432`)와 `(*, Material.Density)`(`:1644`) 모두 **density 오버로드**. `setMassAndUpdateInertia`(질량 직접)는 미사용 → 실효 질량 = density×부피. **필드 이름(Mass)과 의미(density) 불일치 → 잠재 버그.**
- **BodySetup Friction/Restitution 미적용**: 셰이프는 `createExclusiveShape(..., *DefaultMaterial)`(`:1599`)로 고정 `0.5/0.5/0.3`(`:983`)만 사용. `ApplyBodyMaterial`이 채운 셰이프별 `Material`(`FPhysicsShapeDesc.Material`, `PhysicsTypes.h:125`)에서 실제 소비되는 값은 `Density`뿐.
- **bEnableGravity 무시**: 인스턴스화가 `BodyDesc.bUseGravity = true`로 강제(`SkeletalMeshComponent.cpp:859`). `bEnableCCD`도 강제 true(`:860`), `bStartAwake=true`(`:861`).
- **SetCenterOfMass 미호출(래그돌)**: 래그돌 인스턴스화엔 COM 설정이 없음 → COM은 `updateMassAndInertia` 계산값 사용. (`추정:` 컴포넌트 등록 경로엔 별도 존재 — 라인은 코드 변동으로 재확인 필요.)

### 3.3 충돌 관련 — ✅ 과거 gap 해소됨 (2026-06-03)
- **per-pair `DisabledCollisionPairs` 배선 완료**: 과거엔 자산/에디터만 채우고 런타임이 안 읽었으나, 이제 `SkeletalMeshComponent.cpp:1000`에서 `IsCollisionDisabled`를 읽어 `Scene.SetRagdollBodyFilter`로 PhysX 시뮬 필터에 반영한다(`:984~1007`). **단 `bEnableSelfCollision=true` 경로에서만** 의미(false면 aggregate가 이미 전 쌍 차단). `IsCollisionDisabled` 호출처(grep 전수): `PhysicsAsset.{h:43,cpp:65,cpp:76}`, `PhysicsEditorWidget.cpp:831`, **`SkeletalMeshComponent.cpp:1000`(런타임 — 신규)**. → [05](05_runtime_and_collision.md) 2.7.
- **커스텀 simulation filter shader 존재**: scene에 `KraftonRagdollFilterShader`(`PhysXRuntime.cpp:327~398`, scene 배선 `:970`)가 붙어 있다. (a) same-actor `word3` 일치 쌍 `eSUPPRESS`(`:341~345`), (b) 같은 래그돌 그룹+상호 ignore-mask `eKILL`(`:361~366`), (c) 트리거 보존, (d) 비-래그돌은 채널 기반 block/overlap/ignore. 시뮬 필터만 변경하므로 쿼리(레이캐스트/스윕) 채널엔 무영향. `RAGDOLL_FILTER_TAG=0x80000000`(`:295`).
- **`SetRagdollBodyFilter`의 word3=0 덮어쓰기**: self-collision ON 경로에서 래그돌 바디 시뮬 필터는 `word0=태그|그룹 / word1=인덱스비트 / word2=ignore마스크 / word3=0`(`PhysXRuntime.cpp:2570`)으로 재구성 → same-actor 억제 대신 그룹 로직이 지배. 쿼리 필터는 미변경([09](09_ragdoll_session_fixes.md) 5장 후속).

### 3.4 시뮬레이션 관련
- **solver iteration 커스텀 없음**: `setSolverIterationCounts` 호출 0건 → PhysX 기본 iteration. 무거운 사슬/큰 질량비에서 늘어짐 가능.
- **`eENABLE_STABILIZATION` 미설정**: scene 플래그에 stabilization 없음(`PhysXCore.cpp:195~204`엔 ACTIVE_ACTORS/CCD/PCM, RW_LOCK은 `#if KRAFTON_PHYSX_REQUIRE_RW_LOCK` 가드).

### 3.5 신규 기능 (과거 문서엔 없던 것 — 참고)
- **triangle mesh 충돌(static 전용)**: `CookTriangleMesh`(`PhysXRuntime.cpp:211~251`, `PxCreateCooking`→`cookTriangleMesh`)와 `PxTriangleMeshGeometry`로 임의 삼각형 콜리전 지원. `CreateShape_AssumesLocked`(`:1573~1589`)는 시뮬 셰이프일 때 **static actor로 제한**(non-static이면 로그+거부). `UStaticMeshComponent` 경로(`BuildBodyDescFromComponent:1112~1131`)에서만 쓰이며, 래그돌/스켈레탈 경로와 무관(에디터 버튼도 `StaticMeshEditorWidget.cpp:395`의 static-mesh 충돌용). 쿡된 메시는 `Body->TriangleMeshHandles`로 추적·해제.
- **균등 스케일 베이킹**: `AppendPhysicsShapes(*, *, PhysicsAssetScale)`가 셰이프/앵커 치수에 컴포넌트 균등 스케일을 곱하고, write-back은 글로벌을 scale-1로 정규화 + `BodyToBoneOffsets` 적용([01](01_rigid_body_setup.md) 2.3, [08](08_ragdoll_skinning_physics.md) 5장). 비균등 스케일은 근사+경고로 미지원.
- **신규 타입**: `EPhysicsShapeType{Box,Sphere,Capsule,Convex,TriangleMesh}`(`PhysicsTypes.h:21~28`), `EPhysicsBodyType`에 `Static` 추가(`:14~19`), `UBodySetup.CollisionEnabled`+`EBodyCollisionEnabled`(`BodySetup.h:10~16,:47`).

---

## 4. 상태 전이 관련 관찰 (과거 관찰 — 갱신됨)
- **bSimulatingPhysics를 끄는 공개 API가 이제 존재**: `USkeletalMeshComponent::SetSimulatingPhysics(bool)`(`SkeletalMeshComponent.cpp:1222~1308`)가 있고, false 분기(`:1290~1307`)는 바디를 Kinematic으로 되돌리고 `bSimulatingPhysics=false`로 세팅한다(바디는 유지 → 재진입 저렴). 에디터 `StopPhysicsSimulation`도 `SetSimulatingPhysics(false)`(`PhysicsEditorWidget.cpp:1506`)를 호출한 뒤 바디를 파괴한다. **단 `PhysicsEditorWidget.cpp:1512~1518`에 "끄는 API 없음"이라는 stale 주석/로그가 남아 모순**이므로 주석을 코드 근거로 삼지 말 것([00](00_overview.md) 규칙2).

---

## 5. `[미확인]` 목록 (직접 확정하지 못함)
- **`RagdoleBone` 구조체**(`PhysicsTypes.h:268~277`, 철자 "Ragdole"): 정의만 존재, `KraftonEngine/Source` 전수 grep 사용처 **0건**. → 레거시/미사용 확인. 실제 래그돌 데이터는 `Bodies[]`/`Constraints[]`/`UPhysicsAsset`을 쓴다.
- **`FTransform::ToMatrix()` 내부 합성 순서**: 직접 읽지 않음. 단, `MakeWorldTransform`/`MakeRelativeTransform` 사용 결과가 행벡터 TRS와 일관됨을 확인([03](03_anchor_frames.md)).
- **`USkinnedMeshComponent::GetBoneWorldTransformByIndex` 내부**(`SkinnedMeshComponent.cpp:291`): 본 월드 트랜스폼을 돌려준다는 계약만 사용. 내부 수식은 직접 읽지 않음(이름·사용처로 역할 확정).
- **PhysX 4.1 정확한 D6 limit 내부 동작(soft/contactDistance 기본값)**: 일반론으로만 기술. 이 엔진이 기본값 외 별도 설정을 하지 않음은 확인.
- **`updateMassAndInertia` 중복 호출 순서 효과**: `CreateShape`(`:898`)와 `CreateRigidBody`(`:761`) 둘 다 호출 — 셰이프 루프 뒤 `:761`이 최종 권위로 보이나(추정: 마지막 호출이 우선), 두 density 값이 같아(기본 1.0) 가시적 차이는 없음.

---

## 6. 관련 메모(엔진 진단 맥락)
- 본 학습은 코드 현재 상태 기준. 과거 진단 문서(`Docs/RAGDOLL_*`, `Docs/physics_temp.md` 등)는 일부 stale일 수 있음(메모리 `[[project_physics_diag_docs_stale]]`).
- 에디터 "Simulate" 무반응의 과거 원인(PreviewWorld BeginPlay 미호출 → write-back 차단)은 현재 `TickPhysicsSimulation`이 step+write-back을 직접 돌려 우회([05](05_runtime_and_collision.md) 2.3, 메모리 `[[project_ragdoll_simulate_noreact]]`).
