# Ragdoll Infra 진단 (Phase 1: diagnose/verify only)

> **한 줄 요약: 현재 infra로 "본 1개 PhysX 구동"까지 코드 파이프라인은 end-to-end 이미 구현됨. 실질 차단 gap은 1개 — 유효한 BodySetup이 든 PhysicsAsset 데이터(자동 생성 `GeneratePhysicsBodies()`가 stub)이며, 이는 코드 수정 없이 에디터 수동 조작으로 우회 가능. 나머지는 좌표 단위(cm↔m 100배)·handedness(LH↔RH)·capsule 축 보정 3개의 lurking risk.**

- 작성일: 2026-05-31
- 대상 브랜치: `feature/joint`
- 검증 방식: 코드 직접 인용만. 기존 진단 문서(stale 가능)는 근거로 쓰지 않음.
- 정식 추상화: **`IPhysicsRuntime` / `FPhysXRuntime`** (legacy `IPhysicsScene`/`PhysXPhysicsScene`/`NativePhysicsScene`는 live tick에서 미사용 — 확인됨).

> ⚠️ 정찰 중 자동 탐색이 "skeleton↔body 매핑·ragdoll 인스턴스화 전무"라고 1차 보고했으나 **코드 직접 확인 결과 오답**. 해당 코드는 모두 존재한다. 아래는 코드 인용 기반 정정본.

---

## 0. 코드베이스 정찰

- [있음] `engine/physics` 트리 — `KraftonEngine/Source/Engine/Physics/`
  - 런타임: [IPhysicsRuntime.h](KraftonEngine/Source/Engine/Physics/IPhysicsRuntime.h), [PhysXRuntime.h](KraftonEngine/Source/Engine/Physics/PhysXRuntime.h)/[.cpp](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp), [PhysXCore.cpp](KraftonEngine/Source/Engine/Physics/PhysXCore.cpp), [PhysXHelpers.h](KraftonEngine/Source/Engine/Physics/PhysXHelpers.h)
  - 자료형: [PhysicsTypes.h](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h), [BodyInstance.h](KraftonEngine/Source/Engine/Physics/BodyInstance.h), [ConstraintInstance.h](KraftonEngine/Source/Engine/Physics/ConstraintInstance.h)
  - 자산 설계도: [Asset/PhysicsAsset.h](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.h), [Asset/BodySetup.h](KraftonEngine/Source/Engine/Physics/Asset/BodySetup.h), [Asset/PhysicsConstraintSetup.h](KraftonEngine/Source/Engine/Physics/Asset/PhysicsConstraintSetup.h), [Asset/PhysicsGeometry.h](KraftonEngine/Source/Engine/Physics/Asset/PhysicsGeometry.h)
  - legacy(미사용): `PhysXPhysicsScene.*`, `NativePhysicsScene.*`, `IPhysicsScene.h`

- [있음] PhysX SDK 초기화 — 공유 코어 단일 경로
  - `PxCreateFoundation`: [PhysXCore.cpp:103](KraftonEngine/Source/Engine/Physics/PhysXCore.cpp#L103); `PxCreatePhysics(... PxTolerancesScale() ...)`: [PhysXCore.cpp:125-130](KraftonEngine/Source/Engine/Physics/PhysXCore.cpp#L125) — refcount 공유(`AcquireSharedPhysXCore`), foundation 단일 호출(이전 "이중 foundation"은 커밋 01ba85ac에서 해소).
  - `PxScene` 생성 + 중력: [PhysXRuntime.cpp:359-372](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L359) — `gravity = PxVec3(0,0,-9.81)`, +Z up 일치.
  - [없음] `PxCreateCooking` — **엔진 코드(PhysXCore/PhysXRuntime)에 호출 없음**. convex/trimesh 굽기 경로 미연결과 일관(§2). (PhysX SDK 헤더에만 존재.)

- [있음] 매 프레임 `simulate()/fetchResults()` — [PhysXRuntime.cpp:621-622](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L621) (`FPhysXRuntime::Simulate`)

- [있음] skeletal mesh / bone 자료구조 — `struct FBone`([SkeletalMeshAsset.h:19-70](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h#L19)): `Name`, `ParentIndex`, `ReferenceLocalPose`, `ReferenceGlobalPose`, `InverseBindPoseMatrix`. 런타임 보유는 `FSkeletalMesh::Bones`([181](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h#L181)). **bone order = parent-first 규약** (인덱스 오름차순, [SkeletalMeshAsset.h:219](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h#L219) 누적식이 전제).
  - 참고: `USkeleton`/`FReferenceSkeleton`(SkeletonTypes.h)도 존재하나 ragdoll 경로는 `FSkeletalMesh::Bones`(`FBone`)를 사용한다.

- [있음] 프레임 루프 순서 — [World.cpp:355-365](KraftonEngine/Source/Engine/GameFramework/World.cpp#L355)
  ```
  (1) PhysicsRuntime->Simulate(dt)   // 물리 먼저
  (2) TickManager.Tick(...)          // 그 다음 컴포넌트 tick (여기서 SkeletalMesh write-back)
  (3) TickPlayerCamera()
  ```
  ⚠️ 가드 `if (bHasBegunPlay && PhysicsRuntime)` — **에디터 PreviewWorld는 BeginPlay 미호출**이라 World::Tick 안에서 Simulate가 안 돈다. 그래서 에디터는 [MeshEditorWidget.Physics.cpp:1198-1199](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp#L1198)에서 `Runtime->Simulate()` + `PreviewWorld->Tick()`을 **직접** 돌린다.

- [없음] 고정 timestep / substep — `Scene->simulate(DeltaTime)`에 raw dt 직결([PhysXRuntime.cpp:621](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L621)). accumulator/clamp 없음. (1차 비차단)

---

## 1. Physics ↔ Skeleton 데이터 매핑 (bone↔body)

- [있음] bone → body 컨테이너 — `TArray<FBodyInstance*> Bodies` **bone index로 직접 인덱싱** ([SkeletalMeshComponent.h:115](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h#L115)). `Bodies.assign(Asset->Bones.size(), nullptr)` 후 `Bodies[BoneIndex] = Body` ([SkeletalMeshComponent.cpp:401,455](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L401)). 조인트는 `TArray<FConstraintInstance*> Constraints`([116](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h#L116)). 조회: `GetBodyInstanceByBoneIndex/Name`([561,571](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L561)).
  - ※ `PhysicsTypes.h:240`의 `struct RagdoleBone`은 **미사용 死코드** — 실제 매핑은 위 `Bodies` 배열.

- [부분] 좌표계/단위 변환 레이어 — **성분 직통, 무보정** (§4 Risk).
  - 엔진: **Row-Major, Left-Handed** ([Matrix.h:97](KraftonEngine/Source/Engine/Math/Matrix.h#L97) 주석 "Reversed-Z, Row-Major LH"), +X fwd/+Y right/+Z up ([PhysicsTypes.h:209](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h#L209)). **길이 단위는 코드상 미명시**(cm/m 모호).
  - PhysX: **Right-Handed**, 기본 TolerancesScale length=1.0(권장 "1=1m").
  - 변환 헬퍼 `ToPxVec3/ToPxQuat/ToFVector/ToFQuat`는 **성분 그대로 복사** (`V.X→x` …), **handedness 반전·단위 스케일 둘 다 없음** ([PhysXHelpers.h:8-36](KraftonEngine/Source/Engine/Physics/PhysXHelpers.h#L8)).

- [있음] bone world → `PxTransform` — `BodyDesc.WorldTransform = GetBoneWorldTransformByIndex()`([SkeletalMeshComponent.cpp:434](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L434)) → `ToPxTransform`([PhysXRuntime.cpp:656](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L656)). bone world 계산: `BuildBoneEditGlobalMatrices() * GetWorldMatrix()` ([SkinnedMeshComponent.cpp:299](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.cpp#L299)).

- [있음] 역방향 `PxTransform → bone global → bone local` — `ApplyPhysicsToBones()`([SkeletalMeshComponent.cpp:636-697](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L636)):
  ```
  ComponentGlobal = BodyWorld.ToMatrix() * ComponentWorldInv;          // world → component-local
  LocalMatrix     = ComponentGlobal * ParentGlobal.GetInverse();       // → parent-relative local
  ... SetBoneLocalTransforms(LocalPose);                                // → skinning 갱신
  ```
  parent-first 순회로 부모 global이 항상 먼저 채워짐. 라운드트립(world→PhysX→world)은 **자기일관**이라 위치는 보존됨.

- [부분/Risk] handedness 뒤집힘 처리 — **없음**. 위 라운드트립이 자기일관이라 단일 바디 자유낙하의 *위치*는 안 깨지지만, PhysX solver는 RH 가정이라 **회전 동역학/joint가 거울 반전**될 risk (§4 Risk B).

---

## 2. Per-bone Collision Shape 생성 파이프라인

- [있음] `PxShape` 생성 — `PxRigidActorExt::createExclusiveShape` ([PhysXRuntime.cpp:752](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L752), `FPhysXRuntime::CreateShape`).
- [있음/부분] 지원 geometry — **Box/Sphere/Capsule만** 구현, **Convex/TriangleMesh는 `return false`** ([PhysXHelpers.h:88-106](KraftonEngine/Source/Engine/Physics/PhysXHelpers.h#L88)). ragdoll엔 충분.
- [있음] `PxMaterial` — 전역 default 1개 `createMaterial(0.5,0.5,0.3)` ([PhysXRuntime.cpp:375](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L375)). per-shape material override는 없음(BodySetup의 Friction/Restitution은 `FPhysicsShapeDesc.Material`에 담기나 CreateShape는 default를 씀 — 경미).
- [있음] 본 1개에 box/capsule 붙이는 최소 API — 존재. 설계도 `FKAggregateGeom`(Sphere/Box/Capsule elems, [PhysicsGeometry.h:40](KraftonEngine/Source/Engine/Physics/Asset/PhysicsGeometry.h#L40)) → `AppendPhysicsShapes()`([SkeletalMeshComponent.cpp:75-115](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L75))가 `FPhysicsShapeDesc`로 변환.
- [있음] shape local pose(본 기준 오프셋) — `Shape->setLocalPose(ToPxTransform(Desc.LocalTransform))` ([PhysXRuntime.cpp:758](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L758)). 설계도의 `Elem.Center`/`Elem.Rotation`이 그대로 전달됨.
- [부분/Risk] capsule 축 정렬 — **두 경로 불일치**:
  - 컴포넌트 경로(`BuildBodyDescFromComponent`)는 Z→X 90° 보정 있음 ([PhysXRuntime.cpp:485-486](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L485)).
  - **ragdoll 경로(`AppendPhysicsShapes`)는 보정 없음** — `Capsule.Rotation`을 그대로 사용 ([SkeletalMeshComponent.cpp:101-108](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L101)). PhysX capsule은 X축 정렬이라, 에디터에서 본 long-axis로 세운 capsule이 90° 틀어질 risk (§4 Risk C).
- [없음] 본 길이/바운드 → shape 크기 자동 산출 — `GeneratePhysicsBodies()`가 stub ([MeshEditorWidget.Physics.cpp:1060-1068](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp#L1060)). 크기는 에디터에서 수동 입력.

---

## 3. Joint / Constraint 셋업

- [있음] `PxD6Joint` 생성 — `PxD6JointCreate` ([PhysXRuntime.cpp:790](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L790), `FPhysXRuntime::CreateD6Joint`). motion/limit/breakforce 래핑 완비 ([802-814](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L802)). 그 외 joint 타입·articulation은 없음(ragdoll 불필요).
- [있음] joint local frame 계산 헬퍼 — ragdoll 난점이 **이미 구현됨** ([SkeletalMeshComponent.cpp:496-519](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L496)):
  ```
  ParentLocalFrame = (Setup->ParentAnchorPos, ParentAnchorRot);
  JointWorldFrame  = MakeWorldTransform(ParentLocalFrame, ParentBodyWorld);
  ChildLocalFrame  = MakeRelativeTransform(JointWorldFrame, ChildBodyWorld);
  ```
- [있음] D6 motion/limit/drive 래퍼 — `EConstraintMotion`→`EPhysicsMotionType`→`PxD6Motion` ([SkeletalMeshComponent.cpp:38](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L38), [PhysXHelpers.h:38](KraftonEngine/Source/Engine/Physics/PhysXHelpers.h#L38)). 각도는 deg→rad 변환됨([507-510](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L507)). **drive(active ragdoll)는 없음** — passive 전용.
- [있음] 본 1개 목표엔 joint 불필요. 본 2개 연결 시 선결 조건: 두 본 모두 BodySetup 보유 + `UPhysicsConstraintSetup`(Parent/Child BoneName) 존재. 에디터 UI는 이미 구비([MeshEditorWidget.Physics.cpp:465-502](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp#L465)).

---

## 4. Vertical Slice Gap 분석 (결론)

### 구현 완료된 end-to-end 체인 (코드 인용 확인)
```
World가 FPhysXRuntime 생성/Init           World.cpp:306-307
  → 에디터 "Simulate" 버튼                  MeshEditorWidget.Physics.cpp:1080 StartPhysicsSimulation
  → InstantiatePhysicsAssetBodies          SkeletalMeshComponent.cpp:389  (CreateRigidBody/CreateShape/CreateD6Joint)
  → CreateRagdoll                          SkeletalMeshComponent.cpp:576  (kinematic→Dynamic, bSimulatingPhysics=true)
  → TickPhysicsSimulation                  MeshEditorWidget.Physics.cpp:1166 (Runtime->Simulate + PreviewWorld->Tick)
  → ApplyPhysicsToBones                    SkeletalMeshComponent.cpp:636  (body world→bone local→skinning)
```
**즉 "코드가 없어서" 막히는 곳은 사실상 없다.** 막는 것은 데이터와 3개 risk다.

### 우선순위 표 — "본 1개 PhysX 구동"에 지금 막는 것

| # | Gap | 차단도 | 위치(신설/수정) | 난이도 | 의존 |
|---|-----|:---:|------|:---:|------|
| 1 | **유효 BodySetup이 든 PhysicsAsset 부재** — `GeneratePhysicsBodies()` stub이라 자동 바디 생성 불가. 본에 BodySetup+AggregateGeom이 1개도 없으면 `InstantiatePhysicsAssetBodies`가 false 반환([SkeletalMeshComponent.cpp:457-462](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L457)). 기존 `*_Physics.uasset`은 122/1035 bytes로 바디 거의/전무 추정. | **실질 유일 차단** | 데이터: 에디터 Physics 탭에서 본 우클릭→Add Body→Box/Sphere 1개. 또는 코드: `GeneratePhysicsBodies()` 구현([MeshEditorWidget.Physics.cpp:1060](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp#L1060)) | 하(수동)/중(자동화) | 없음 |
| 2 | 게임플레이(런타임 월드) ragdoll 자동 진입 경로 없음 — `CreateRagdoll` 호출자가 에디터 프리뷰뿐. 게임 중 사망→ragdoll 전환 트리거 미존재. | 1차 비차단 | 신설: 게임측에서 `InstantiatePhysicsAssetBodies`+`CreateRagdoll` 호출 지점 | 중 | #1 |
| 3 | `bSimulatingPhysics`를 끄는 공개 API 없음 — `StopPhysicsSimulation`이 로그만 남김([MeshEditorWidget.Physics.cpp:1161](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp#L1161)). 재시뮬레이션 시 잔류 상태. | 비차단(사소) | 수정: `USkeletalMeshComponent`에 `StopSimulatingPhysics()` setter | 하 | 없음 |

### 최소 구현 순서 제안 (다음 사이클)
1. **데이터부터**: yui 계열 메시 1개에 본 1~2개만 Box/Sphere BodySetup 부여 후 Save → 에디터 "Simulate"로 **코드 수정 0으로 본 구동 가능성 확인**. (1차 목표는 여기서 충족될 공산이 큼.)
2. 그 검증에서 §4 risk(낙하 속도/뒤틀림/capsule 방향)가 실제로 나오는지 관찰 → risk를 우선순위화.
3. 이후 `GeneratePhysicsBodies()` 구현(자동 바디) → 게임 런타임 진입 경로(#2) → stop setter(#3).

### ⚠️ Ragdoll을 깨뜨릴 수 있는 lurking risk (좌표/단위/handedness)

- **Risk A — 단위 스케일 cm vs m (미검증 — 코드에 단위 명시 없음, 런타임 관찰 필요)**
  PhysX는 `PxCreatePhysics(... PxTolerancesScale() ...)` **기본값** 사용 = length 1.0 (PhysX 권장 "1.0 = 1m") ([PhysXCore.cpp:128](KraftonEngine/Source/Engine/Physics/PhysXCore.cpp#L128)). 중력은 SI값 `-9.81` ([PhysXRuntime.cpp:360](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L360)). 변환 헬퍼는 ×0.01/×100 없이 성분 직통 ([PhysXHelpers.h:8-36](KraftonEngine/Source/Engine/Physics/PhysXHelpers.h#L8)).
  ※ **엔진 좌표가 cm임을 명시한 주석·상수는 코드에서 찾지 못함**(메모리에도 "단위 cm/m 모호"로 기록). 따라서 "100배 오차 확정"이 아니다. **만약 스켈레톤 본 좌표가 cm라면** PhysX(1=1m 가정)와 100배 불일치 → 낙하가 비현실적으로 느리거나 solver tolerance 어긋남으로 불안정/떨림 발생 가능. → **1차 검증의 핵심 관찰 포인트**(낙하 속도가 정상인가).

- **Risk B — handedness LH vs RH (확정)**
  엔진 LH([Matrix.h:9](KraftonEngine/Source/Engine/Math/Matrix.h#L9)) ↔ PhysX RH, 변환은 성분 직접 복사로 **카이랄성 미처리**([PhysXHelpers.h:13-26](KraftonEngine/Source/Engine/Physics/PhysXHelpers.h#L13)). world→PhysX→world 라운드트립은 자기일관이라 단일 바디 *위치*는 보존되나, solver의 회전 적분/joint swing·twist가 **거울 반전**되어 본이 반대로 꺾이는 risk.

- **Risk C — capsule 축 보정 누락 (확정)**
  ragdoll 경로(`AppendPhysicsShapes`)는 capsule을 X축 정렬 보정 없이 생성([SkeletalMeshComponent.cpp:101](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L101)) — 컴포넌트 경로([PhysXRuntime.cpp:485](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L485))와 불일치. → **1차에선 Box/Sphere를 쓰면 회피**, capsule은 회전 어긋남 가능.

> 권장: 1차 검증은 **Box/Sphere bodies + 본 1개**로 시작해 Risk C를 배제하고, 낙하 속도(Risk A)·뒤틀림(Risk B)을 육안 관찰해 다음 사이클 우선순위를 정한다.
