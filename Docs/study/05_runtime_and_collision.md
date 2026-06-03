# 05. 런타임 루프와 충돌

> 라인 번호는 확인 시점(2026-06-03) 스냅샷. 심볼명으로 재확인. write-back 수학은 [06](06_coordinate_math.md) 행벡터 규약 사용.
> 에디터 물리 코드는 `MeshEditorWidget.Physics.cpp` → `PhysicsEditorWidget.cpp`로 분리됐다([00](00_overview.md) 규칙4).

---

## 1. 이론

### 1.1 시뮬레이션 step
매 프레임: `simulate(dt)`로 적분+constraint solve를 수행하고, `fetchResults()`로 결과(actor 새 트랜스폼)를 회수한다. `fetchResults(true)`는 시뮬이 끝날 때까지 **블로킹**한다.

### 1.2 본 적용 순서 (애님 → 물리 → 스키닝 → 렌더)
래그돌은 "애니메이션이 본을 모는" 평소와 반대로 "**물리가 본을 몬다**". 한 프레임에서:
1. (래그돌 off) 애님 평가로 본 로컬 포즈 결정, **또는** (래그돌 on) 물리 step 후 actor 포즈를 본 로컬로 역산(write-back).
2. 본 로컬 → 스키닝 행렬 → 정점 스키닝 → 렌더.

물리 결과를 본에 되먹이려면 `fetchResults`가 끝난 **뒤에** write-back해야 한다(순서 의존).

### 1.3 인접 바디 self-collision 문제
래그돌은 인접 본 캡슐이 살짝 겹치게 배치되기 쉽다. 자기들끼리 충돌 처리하면 서로 밀어내며 **떨리거나 폭발**한다. 그래서 보통 (a) 같은 캐릭터 내부 자기충돌을 끄거나 (b) 조인트로 직결된 쌍만 충돌 제외한다.

---

## 2. 코드 대조

### 2.1 scene 구성
`FPhysXRuntime::Initialize` (`PhysXRuntime.cpp:939`) + `ConfigurePhysXSceneDesc` (`PhysXCore.cpp:195~204`):
```cpp
Dispatcher = PxDefaultCpuDispatcherCreate(4);          // :960
SceneDesc.gravity = PxVec3(0, 0, -9.81f);              // :968  -Z 중력
SceneDesc.filterShader = KraftonRagdollFilterShader;   // :970  ★ 커스텀 필터셰이더 (2.7 참조 — 과거 PxDefault에서 변경됨)
SceneDesc.flags |= eENABLE_ACTIVE_ACTORS | eENABLE_CCD | eENABLE_PCM;   // :197~199
#if KRAFTON_PHYSX_REQUIRE_RW_LOCK
SceneDesc.flags |= eREQUIRE_RW_LOCK;                   // :202  컴파일 가드 안에 있음
#endif
DefaultMaterial = Physics->createMaterial(0.5f, 0.5f, 0.3f);   // :983
```
- 4-thread dispatcher지만 step은 `fetchResults(true)`로 동기 완료(2.2).
- ★ **변경**: 필터셰이더가 더 이상 `PxDefaultSimulationFilterShader`가 아니라 **커스텀 `KraftonRagdollFilterShader`**(`:327~398`)다. same-actor 제외·래그돌 그룹 eKILL·채널 응답을 직접 판정한다(2.7).
- `eREQUIRE_RW_LOCK`은 이제 `#if KRAFTON_PHYSX_REQUIRE_RW_LOCK` 가드 안에 있다. 켜지면 모든 접근에 `PHYSX_SCENE_WRITE_LOCK/READ_LOCK` 필요. `eENABLE_STABILIZATION`은 여전히 없음.

### 2.2 step: `FPhysXRuntime::Simulate`
(`PhysXRuntime.cpp:1217`)
```cpp
// (1) 본 바디가 아닌 kinematic 바디만 컴포넌트 월드로 setGlobalPose (BoneIndex>=0은 제외)   :1234~1260
for (Body : Bodies) if (Body->BoneIndex < 0 && Body->BodyType != Dynamic) { ... Actor->setGlobalPose(...); }   // 가드 :1236
// (2) 차량 업데이트 (범위 외)   :1262~1308
Scene->simulate(DeltaTime);     // :1310
Scene->fetchResults(true);      // :1311  ← 블로킹
// (3) READ_LOCK 잡고 actor 새 트랜스폼 → Body->CachedWorldTransform 갱신 (:1322~1366)
```
- 래그돌 본 바디(`BoneIndex >= 0`)는 (1)의 kinematic 동기 대상에서 **빠진다** → 시뮬 중엔 순수 dynamic.
- step 후 `CachedWorldTransform`에 결과를 캐시. write-back은 이 캐시를 읽는다.

### 2.3 두 가지 틱 경로
**(A) 런타임 컴포넌트** `USkeletalMeshComponent::TickComponent` (`SkeletalMeshComponent.cpp:1361~1384`):
```cpp
DrawRuntimePhysicsBodies();                                                  // :1364 (신규)
if (bSimulatingPhysics) { ApplyPhysicsToBones(); TickSkeletalCloth(...); ... return; }   // :1366~1373 래그돌: 애님 skip
if (EvaluateAnimInstance(DeltaTime)) { ... return; }                         // :1375~1380 일반: 애님 평가
```
→ **상호배타 분기**. "애님 평가 → 물리"가 한 틱에 순차로 도는 게 아니라, 래그돌이면 애님을 건너뛰고 물리 write-back만 한다. (cloth 틱이 두 경로에 끼어든다.) (※ scene step `Simulate`는 `UWorld` 틱이 별도로 돌린다.)

**(B) 에디터 미리보기** `FMeshEditorWidget::TickPhysicsSimulation` (`PhysicsEditorWidget.cpp:1536`):
```cpp
Scene->Simulate(SimDt);          // :1567 step (fetchResults 포함)
MeshComp->ApplyPhysicsToBones(); // :1568 같은 틱에서 즉시 write-back
```
- 주석(`:1562~1566`)이 이유를 명시: PreviewWorld는 `BeginPlay` 미호출이라 `UWorld::Tick`의 `bHasBegunPlay && PhysicsScene` 가드에 막혀 scene step도, `TickComponent` 디스패치도 안 된다. 그래서 **에디터는 step과 write-back을 여기서 직접** 돈다. (메모리의 "Simulate 무반응" 원인과 정확히 일치 — `[[project_ragdoll_simulate_noreact]]`.)
- 시작은 `StartPhysicsSimulation`(`:1385`): `InstantiatePhysicsAssetBodies`(`:1418`) → `CreateRagdoll`(`:1427`) → `bSimulating=true`(`:1477`). **신규**: 같이 정적 바닥 박스(`FloorPhysicsBody`, 10×10×0.5, top=Z0)를 생성(`:1440~1461`)해 래그돌이 착지하게 하고, `StopPhysicsSimulation`(`:1484`)에서 파괴한다. `StopPhysicsSimulation`은 `MeshComp->SetSimulatingPhysics(false)`(`:1506`)도 호출한다(과거엔 "끄는 API 없음"이라 했으나 — [07](07_glossary_and_gaps.md) 4장 — 지금은 존재. `:1512~1518`에 그 stale 주석이 남아 모순).

### 2.4 래그돌 진입: `CreateRagdoll` → `EnterRagdollState`
`CreateRagdoll`(`SkeletalMeshComponent.cpp:1148~1155`)은 `EnterRagdollState`(`:1062~1146`)를 호출하고 성공 시 `bSimulatingPhysics=true`만 세팅한다. 실제 로직은 `EnterRagdollState`:
```cpp
for (Body : Bodies) {
    GetBoneWorldTransformByIndex(BoneIndex, BoneWorld);
    // ★ 바디↔본 오프셋을 되돌려 타깃 바디 월드 계산 (오프셋0/scale1이면 BoneWorld 그대로)
    TargetBodyWorld = FTransform(BodyToBoneOffsets[i].GetInverse() * BoneWorld.ToMatrix());   // :1096
    PhysicsSceneOwner->SetBodyTransform(Body, TargetBodyWorld, /*bTeleport*/ true);           // :1099
    PhysicsSceneOwner->SetBodyType(Body, EPhysicsBodyType::Dynamic);                          // :1137 kinematic 해제 + wake
    PhysicsSceneOwner->SetBodyLinearVelocity(Body, RagdollEntryLinearVelocity);               // :1142 ★ 진입 관성(신규)
}
```
- 인스턴스 직후 ref pose와 현재 애님 포즈의 격차로 인한 튐을 막으려 **teleport로 본 월드에 맞춘 뒤** dynamic 전환.
- ★ **변경**(커밋 `d14280e3` 계열): teleport가 `BodyToBoneOffsets`를 적용하도록 재작성됐고, 진입 시 `UCharacterMovementComponent`의 속도를 `RagdollEntryLinearVelocity`로 받아 전 바디에 부여(`:1142`)한다. 진단 로그(`:1112~1134`)도 추가됨.
- 상위 진입점 `SetSimulatingPhysics(bool)`(`:1222~1308`)은 바디 없으면 lazy 인스턴스화 + 진입, false면 바디를 Kinematic으로 되돌리고 플래그 off(바디 유지 → 재진입 저렴). 진입 시 owner 루트 캡슐 teleport + 관성 부여(`:1269~1283`)도 한다.

### 2.5 write-back: `ApplyPhysicsToBones`
(`SkeletalMeshComponent.cpp:1386~1513`)
```cpp
const FMatrix ComponentWorldInv = GetWorldInverseMatrix();                          // :1418
for (BoneIndex 오름차순 /*parent-first*/) {                                          // :1420
    if (Body valid && GetBodyTransform(Body, BodyWorld)) {
        BoneWorldMatrix = BodyToBoneOffsets[i] * BodyWorld.ToMatrix();              // :1430~1433 ★ 오프셋 적용
        ComponentGlobal = BoneWorldMatrix * ComponentWorldInv;                     // :1436 월드 → 컴포넌트 글로벌
        // ★ 글로벌 단계에서 컴포넌트 스케일(1/S) 누수 제거 — 균등 스케일 가정
        GlobalNoScale = FTransform(ComponentGlobal); GlobalNoScale.Scale = OneVector;   // :1445~1446
        ComponentGlobal = GlobalNoScale.ToMatrix();                                // :1447
    }
}
// 바디 없는 본: (a) 역방향 루프 — 풀린 자식이 하나면 그 자식 ref-local로 부모 글로벌 복원 (:1455~1485)
//              (b) 정방향 루프 — 나머지는 RefLocal * ParentGlobal (:1487~1497)
for (BoneIndex) LocalMatrix = (ParentIndex>=0) ? ComponentGlobal * ParentGlobal.GetInverse() : ComponentGlobal;   // :1505~1507
SetBoneLocalTransforms(LocalPose);   // :1512 스키닝/bounds 갱신 트리거
```
- 행벡터 규약: `Global = Local · ParentGlobal` 의 역 → `Local = Global · ParentGlobal⁻¹` ([06](06_coordinate_math.md) 2.4와 정확히 대응).
- ★ **변경**: body world == 본 world라 오프셋은 보통 identity이지만 코드는 항상 `BodyToBoneOffsets`를 곱한다(`:1430~1433`). 그리고 컴포넌트 월드 스케일이 본 글로벌에 1/S로 새어 스키닝을 왜곡하는 것을 막으려 **글로벌 단계에서 scale-1 정규화**(`:1445~1447`)를 한다(원리는 [08](08_ragdoll_skinning_physics.md) 5장). 과거의 "오프셋 0, 보정 불필요(`:1030` 주석)"는 이 인프라로 대체됨.
- 바디 없는 본은 **2단계**로 채운다: 풀린 자식이 정확히 하나면 그 자식의 ref-local로 부모 글로벌을 역산(`:1455~1485`), 나머지는 `RefLocal * ParentGlobal`(`:1487~1497`) — 사슬이 자연스럽게 이어짐(커밋 `f6462e61`/`016166fe` 계열).

### 2.6 race 부재 근거
- step은 `fetchResults(true)`로 **완료까지 블로킹**(`:1311`) → 결과가 확정된 뒤에만 다음 줄로 진행.
- write-back(`ApplyPhysicsToBones`)은 같은 스레드에서 step **이후** 호출((A) TickComponent, (B) TickPhysicsSimulation 모두). 멀티스레드 본 쓰기 없음.
- PhysX 접근은 `eREQUIRE_RW_LOCK` + `PHYSX_SCENE_WRITE/READ_LOCK`로 보호.
→ "본 transform fetch → 물리 결과 적용"이 단일스레드 동기 순서로 직렬화되어 race가 없다.

### 2.7 자기충돌 제어 (두 경로 — gap 해소됨)
자기충돌은 이제 **bEnableSelfCollision 값에 따라 두 경로**로 갈린다.

**경로 (a) self-collision OFF (기본)** — 같은 메시의 모든 바디를 하나의 `PxAggregate`로 묶고 `enableSelfCollision=false`로 전역 차단.
`InstantiatePhysicsAssetBodies` (`SkeletalMeshComponent.cpp:816~826`):
```cpp
const int32 MaxActors = (int32)PhysicsAsset->GetBodySetups().size();
if (MaxActors > 0 && MaxActors <= 128)
    PhysicsAggregate = Scene.CreateAggregate(MaxActors, PhysicsAsset->bEnableSelfCollision);
else if (MaxActors > 128 && !bEnableSelfCollision)
    UE_LOG("... exceeds PxAggregate max (128).");   // 128 초과 시 자기충돌 끄기 불가
```
`CreateAggregate` (`PhysXRuntime.cpp:1494`): `Physics->createAggregate(MaxActors, bEnableSelfCollision)`(`:1504`, 2-인자 — PhysX 4.1 시그니처); `Scene->addAggregate(*Aggregate)`(`:1510`).
- `bEnableSelfCollision=false` → broadphase가 intra-aggregate 쌍을 막아 **같은 메시 바디끼리 충돌 안 함**(월드와는 충돌). 래그돌 폭발 방지.
- 조인트로 직결된 두 바디는 PhysX D6 joint 기본(연결 actor 충돌 비활성)으로 어차피 제외된다(코드가 `eCOLLISION_ENABLED`를 켜지 않음).

**경로 (b) self-collision ON — per-pair `DisabledCollisionPairs`가 이제 런타임에 배선됨 ✅ (과거 gap 해소)**:
- `UPhysicsAsset`은 `DisabledCollisionPairs`(본 쌍별 비활성)와 `IsCollisionDisabled/SetCollisionDisabled`를 갖고, 에디터 툴바·자동생성이 이를 채운다(`PhysicsEditorWidget.cpp:831,836,844,1353`).
- **변경**: `bEnableSelfCollision=true`일 때 `InstantiatePhysicsAssetBodies`(`:984~1007`)가 각 바디 쌍에 대해 `PhysicsAsset->IsCollisionDisabled(BoneName_i, BoneName_j)`(`:1000`)를 읽어 `IgnoreMask`를 만들고, `RagdollFilterGroupId`+`BodyFilterIndex`와 함께 `Scene.SetRagdollBodyFilter(...)`(`:1005`)로 PhysX 시뮬 필터에 반영한다.
- 커스텀 필터셰이더 `KraftonRagdollFilterShader`(`PhysXRuntime.cpp:327~398`)가 이를 해석한다: 같은 래그돌 그룹(`word0`)이고 서로의 ignore-mask(`word2 & word1`, 양방향)에 들어 있으면 `eKILL`(`:361~366`)로 충돌 제거. self-collision OFF 경로에선 이 루프를 건너뛴다(aggregate가 이미 전 쌍 차단).
- 같은 액터의 다른 컴포넌트(이동 캡슐 ↔ 래그돌 바디 등)는 `word3`=owner UUID 일치로 `eSUPPRESS`(`:341~345`) — same-actor 중복충돌 튕김 방지([09](09_ragdoll_session_fixes.md) 5장).

> 즉 **쌍별 비활성이 더 이상 "저장/편집만"이 아니라 self-collision ON 경로에서 실제 시뮬에 반영된다.** `IsCollisionDisabled` 런타임 호출처: `SkeletalMeshComponent.cpp:1000`. (관련 커밋 `44d67167 "self collision fix"` — 본 문서는 현재 코드 상태 기준.)
