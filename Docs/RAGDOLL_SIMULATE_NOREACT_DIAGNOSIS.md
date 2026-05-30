# Ragdoll "Simulate 무반응" 단계형 진단 (Phase: diagnose/verify only)

> **끊긴 지점 = STAGE 3, 원인 = 에디터 PreviewWorld가 `BeginPlay()`를 호출하지 않아, write-back을 나르는 `PreviewWorld->Tick(LEVELTICK_All)` → `PreviewMeshComponent::TickComponent` → `ApplyPhysicsToBones` 경로가 `ShouldDispatchActorTick`의 `HasActorBegunPlay()` 가드에서 전부 차단됨. 즉 PhysX body는 떨어지지만 그 결과가 본/메시로 전혀 반영되지 않는다.**

- 작성일: 2026-05-31
- 브랜치: `feature/joint`
- 증상(확정): 에디터 "Simulate" 클릭 시 완전 무반응. 본 제자리, 낙하조차 안 함.
- 선행 사실(확정): 로그상 body 생성됨 = `InstantiatePhysicsAssetBodies` 통과.
- 정식 추상화: `IPhysicsRuntime`/`FPhysXRuntime` (legacy scene 미사용).
- 검증 방식: 코드 직접 인용만. 서브에이전트 결과도 코드 재확인 후 채택.

> ⚠️ 주의: "낙하조차 안 함"이라는 표현은 **중력 미적용**처럼 들리지만, 진단 결과 **body는 PhysX 내부에서 실제로 낙하 중일 가능성이 높다**. 화면에 안 보이는 이유는 중력이 아니라 **write-back 경로 차단**이다. (STAGE 1·2는 통과 — 아래 근거.)

---

## STAGE 1 — Body가 Dynamic으로 전환되는가 → **[통과]**

- [통과] `CreateRagdoll`이 kinematic → Dynamic 전환을 실제 수행. body당 `SetBodyType(Body, EPhysicsBodyType::Dynamic)` 호출 ([SkeletalMeshComponent.cpp:610](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L610)).
- [통과] `eKINEMATIC` 플래그 해제됨 — `SetBodyType` 내부에서 `Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, bWantKinematic)` (bWantKinematic=false) + `wakeUp()` ([PhysXRuntime.cpp:1497,1504](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L1497)).
- [통과] `bSimulatingPhysics = true`가 `CreateRagdoll` 끝에서 set ([SkeletalMeshComponent.cpp:614](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L614)). `StartPhysicsSimulation`이 그 직후 `IsSimulatingPhysics()`로 확인까지 함 ([MeshEditorWidget.Physics.cpp:1123-1128](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp#L1123)).
- [통과] 생성 시 타입: BodySetup의 `bSimulatePhysics`에 따라 Dynamic/Kinematic ([SkeletalMeshComponent.cpp:432](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L432)). 어느 쪽이든 `CreateRagdoll`이 Dynamic으로 재전환하므로 무관.
- [통과] mass: `CreateRigidBody`에서 `PxRigidBodyExt::updateMassAndInertia(*Dynamic, Desc.Mass)` 호출, Mass는 `max(0.001f, BodySetup->Mass)`로 0 방지 ([PhysXRuntime.cpp:704](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L704), [SkeletalMeshComponent.cpp:438](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L438)).

→ Dynamic 전환·중력 수용 조건은 코드상 모두 충족. **여기서 안 끊김.**

## STAGE 2 — Body가 Scene에 들어가고 simulation 대상인가 → **[통과]**

- [통과] `Scene->addActor(*Actor)` 호출됨 ([PhysXRuntime.cpp:711](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L711)).
- [통과] `eDISABLE_SIMULATION` 설정하는 코드 없음(grep). 기본 활성.
- [통과] shape `eSIMULATION_SHAPE` 설정됨 — `Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, Desc.bSimulationShape && !Desc.bTriggerShape)` ([PhysXRuntime.cpp:759](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L759)). ragdoll 경로 `FPhysicsShapeDesc.bSimulationShape` 기본 true·bTriggerShape 기본 false ([PhysicsTypes.h:96-97](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h#L96)).
- [통과] 순서: `CreateRigidBody`가 shape 생성([697-700](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L697)) → addActor([711]) 후 반환. `CreateRagdoll`의 Dynamic 전환은 그 **다음** 단계라 scene add가 항상 선행. 타이밍 누락 없음.

→ scene 등록·sim 대상 조건 충족. **여기서 안 끊김.**

## STAGE 3 — Simulate()가 실제로 호출·진행되는가 → **★ [여기서 끊김] ★**

체인을 따라가면 **두 갈래**가 있고, 그중 write-back 갈래가 끊긴다.

**호출 체인(확정):**
```
FMeshEditorWidget::Tick(dt)                          MeshEditorWidget.cpp:313
  └ if (bSimulating && ActiveTab==Physics)           MeshEditorWidget.cpp:321  ← 가드 (bSimulating 기본 false, Start가 true로)
      └ TickPhysicsSimulation(dt)                     MeshEditorWidget.cpp:323
          ├ Runtime->Simulate(SimDt)                  MeshEditorWidget.Physics.cpp:1198  ← [A] PhysX 스텝
          └ PreviewWorld->Tick(SimDt, LEVELTICK_All)  MeshEditorWidget.Physics.cpp:1199  ← [B] write-back 트리거
```

- [통과·A] `TickPhysicsSimulation`은 매 프레임 호출됨. `FMeshEditorWidget::Tick`이 에디터 루프에서 돌고, `bSimulating`(Start에서 set)·`ActiveTab==Physics` 가드 통과 시 매 틱 진입. `SimDt = dt * max(0, SimSpeed)`, SimSpeed 기본 1.0이라 dt>0 ([MeshEditorWidget.h:216](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.h#L216), [MeshEditorWidget.Physics.cpp:1185-1190](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp#L1185)).
- [통과·A] `Runtime->Simulate`는 내부에서 `Scene->simulate(dt)` + `Scene->fetchResults(true)` 수행 ([PhysXRuntime.cpp:621-622](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L621)). **→ PhysX body는 매 프레임 실제로 적분되어 떨어지고 있을 것.**
- [**여기서 끊김·B**] `PreviewWorld->Tick(SimDt, LEVELTICK_All)`이 write-back을 나르려 하지만, **`PreviewMeshComponent::TickComponent`까지 도달하지 못한다.**

  근본 원인 — 3개 사실의 결합:
  1. `UWorld::Tick`은 `TickManager.Tick(this, dt, type)` 호출, 그 안 `GatherTickFunctions`가 tick 대상을 모음 ([World.cpp:361](KraftonEngine/Source/Engine/GameFramework/World.cpp#L361), [TickFunction.cpp:45,77](KraftonEngine/Source/Engine/Core/TickFunction.cpp#L77)).
  2. `GatherTickFunctions` → 액터별 `ShouldDispatchActorTick` 게이트. **`LEVELTICK_All` 분기는 `Actor->bNeedsTick && Actor->HasActorBegunPlay()`를 요구** ([TickFunction.cpp:20-23](KraftonEngine/Source/Engine/Core/TickFunction.cpp#L20)).
  3. 에디터 `MeshEditorWidget::Open`은 PreviewWorld에 `InitWorld()`만 부르고 **`BeginPlay()`를 호출하지 않는다** ([MeshEditorWidget.cpp:226-235](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.cpp#L226)). → preview actor의 `bActorHasBegunPlay`는 false 기본값 그대로 ([AActor.h:173](KraftonEngine/Source/Engine/GameFramework/AActor.h#L173), `HasActorBegunPlay()` [AActor.h:32](KraftonEngine/Source/Engine/GameFramework/AActor.h#L32)).

  결과: `HasActorBegunPlay()==false` → `ShouldDispatchActorTick`이 false → preview actor와 그 컴포넌트의 tick이 **수집조차 안 됨** → `USkeletalMeshComponent::TickComponent` 미실행 → `ApplyPhysicsToBones` 미실행. **body는 떨어지나 본/메시는 영원히 제자리.**

- [관찰 지점] 코드 수정은 다음 사이클이지만, 확인 로그를 찍는다면:
  - `TickPhysicsSimulation` 안에서 `Runtime->Simulate` 직후 body 하나의 `GetBodyTransform().Location.Z`를 출력 → **프레임마다 Z가 내려가면 STAGE 1~3[A] 정상, 화면만 정지 = write-back 문제 확정.**
  - `USkeletalMeshComponent::TickComponent` 진입부에 로그 → **한 번도 안 찍히면** 본 진단(tick 미수집) 확정.

### 반증 테스트 — "그럼 애니메이션 프리뷰는 왜 도는가?" → 오히려 가설을 **확정**

애니 프리뷰는 `TickComponent` 경로를 **쓰지 않는다.** `FMeshEditorWidget::Tick`이 Animation 탭에서 위젯이 직접 `NodeInst->UpdateAnimation` → `EvaluatePose` → `Comp->SetAnimationPose`를 호출한다 ([MeshEditorWidget.cpp:360-383](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.cpp#L360)). 즉 애니는 BeginPlay/TickComponent와 무관한 별도 경로라 정상 동작.

반면 ragdoll write-back은 **오직** `PreviewWorld->Tick → TickComponent → ApplyPhysicsToBones`에만 의존한다(위젯이 직접 `ApplyPhysicsToBones`를 부르는 경로 없음 — grep 확인). → **이 비대칭이 "애니는 되는데 ragdoll만 무반응"의 정확한 이유.**

## STAGE 4 — write-back 미실행 + 단위 → **[STAGE 3에 가려 부분 무의미]**

- [STAGE 3에 가려짐] `ApplyPhysicsToBones`가 sim 이후 매 프레임 불려야 하나, STAGE 3[B]에서 `TickComponent`가 차단되므로 **애초에 호출 자체가 안 됨.** write-back 로직([SkeletalMeshComponent.cpp:636-697](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L636))의 정합성은 STAGE 3 해소 후에야 검증 가능.
- [참고·독립 확인] write-back 함수 자체의 early-return 가드는 `if (!PhysicsRuntimeOwner) return;`뿐 ([SkeletalMeshComponent.cpp:638](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L638)). `PhysicsRuntimeOwner`는 instantiate에서 set되므로, **STAGE 3만 풀리면 이 함수는 진입 가능**.
- [미검증·단위 Risk A] STAGE 3에 가려 현재 증상의 원인은 아님. 단, 해소 후 "느리게 떨어짐"으로 재현될 수 있는 잠복 리스크로 유지: PhysX는 `PxTolerancesScale()` 기본값(1=1m 가정, [PhysXCore.cpp:128](KraftonEngine/Source/Engine/Physics/PhysXCore.cpp#L128)), 변환 헬퍼 무스케일 ([PhysXHelpers.h:8-36](KraftonEngine/Source/Engine/Physics/PhysXHelpers.h#L8)). 본 좌표가 cm면 100배 어긋남. **단, 현 증상은 "안 움직임"이지 "느림"이 아니므로 후순위.**
  - [관찰 지점] STAGE 3 해소 후, body Z의 프레임당 Δ를 본다. 정상 중력이면 약 9.81·dt²(m계). Δ가 100배 작으면 단위 문제.

## STAGE 5 — Joint 기본값이 본을 잠그는가 → **[STAGE 3에 가려 무의미]**

- [STAGE 3에 가려짐] joint가 모두 정상이어도 write-back이 안 되면 화면은 정지. 현 증상의 원인 아님.
- [참고] 본 1개 + joint 없는 asset이면 STAGE 5는 애초에 해당 없음. body 1개로 검증 시 무관.
- [참고·잠복] D6 motion 매핑 기본은 `EConstraintMotion::Locked → PxD6Motion::eLOCKED` ([SkeletalMeshComponent.cpp:38-49](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L38), [PhysXHelpers.h:38-50](KraftonEngine/Source/Engine/Physics/PhysXHelpers.h#L38)). 본 2개 이상 연결 시 모든 축 Locked면 강체처럼 굳을 수 있으나, 이는 STAGE 3 해소 후 별도 검증.

---

## 결론

- **끊긴 STAGE: STAGE 3 (Simulate 경로 중 write-back 트리거 [B]).**
  - PhysX 스텝[A]은 돈다. 끊긴 건 그 결과를 본으로 되돌리는 tick 디스패치.
  - **근본원인 1줄:** 에디터 PreviewWorld가 `BeginPlay()` 미호출 → preview actor `HasActorBegunPlay()==false` → `ShouldDispatchActorTick`의 `LEVELTICK_All` 가드([TickFunction.cpp:23](KraftonEngine/Source/Engine/Core/TickFunction.cpp#L23))가 컴포넌트 tick을 수집 거부 → `ApplyPhysicsToBones` 미호출.
- **단위/joint 아님.** 그 앞단(tick 디스패치)이 원인. 단위(STAGE 4 Risk A)·joint(STAGE 5)는 STAGE 3 해소 후에야 의미 있는 잠복 리스크.

### 다음 사이클이 손댈 단일 지점 후보 (택1, 구현은 다음 사이클)

| 후보 | 내용 | 위치 | 난이도 | 비고 |
|---|---|---|:---:|---|
| **A (권장)** | 에디터가 `PreviewWorld->Tick` 대신 ragdoll write-back을 **직접 트리거** — 애니 프리뷰가 쓰는 패턴과 동일하게 위젯에서 `ApplyPhysicsToBones` 상당의 공개 진입점 호출 | `MeshEditorWidget.Physics.cpp` TickPhysicsSimulation + `USkeletalMeshComponent` 공개 메서드 | 중 | 애니 경로([MeshEditorWidget.cpp:360-383])와 대칭. BeginPlay 부작용 회피 |
| B | preview actor만 `bTickInEditor=true`로 두고, 에디터 tick을 `LEVELTICK_ViewportsOnly`로 호출 (이 분기는 BeginPlay 불요, [TickFunction.cpp:17-18](KraftonEngine/Source/Engine/Core/TickFunction.cpp#L17)) | `MeshEditorWidget.Physics.cpp:1199` + preview actor 플래그 | 중 | TickComponent 경로 재사용. 단 anim/other tick 동시 발화 부작용 점검 필요 |
| C | PreviewWorld에 `BeginPlay()` 호출 | `MeshEditorWidget.cpp:226` 부근 | 하 | 가장 적은 변경이나, 에디터 프리뷰에 게임 BeginPlay 전체 부작용(GameMode/타이머/물리 자동스텝 등) 유입 위험 — 신중 |

### 코드 수정 없이 우회 가능한가

- **불가(현 증상 한정).** 끊긴 지점이 tick 디스패치라 데이터/조작만으로는 write-back을 못 살린다. body 유무·shape 종류와 무관하게 화면 정지는 동일하게 재현된다.
- 단, **"PhysX는 실제로 도는가"는 코드 수정 없이 간접 확인 가능**: 시뮬 중 `GetPhysicsStats`의 active body 수나 PVD(개발 빌드, [PhysXCore.cpp:110-122](KraftonEngine/Source/Engine/Physics/PhysXCore.cpp#L110))로 body가 떨어지는지 관찰 → STAGE 1~3[A] 통과를 외부에서 확증하면 본 진단이 굳어진다.
