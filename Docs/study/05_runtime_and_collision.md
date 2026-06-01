# 05. 런타임 루프와 충돌

> 라인 번호는 확인 시점(2026-06-01) 스냅샷. 심볼명으로 재확인. write-back 수학은 [06](06_coordinate_math.md) 행벡터 규약 사용.

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
`FPhysXRuntime::Initialize` (`PhysXRuntime.cpp:353`) + `ConfigurePhysXSceneDesc` (`PhysXCore.cpp:195`):
```cpp
Dispatcher = PxDefaultCpuDispatcherCreate(4);
SceneDesc.gravity = PxVec3(0, 0, -9.81f);             // -Z 중력
SceneDesc.filterShader = PxDefaultSimulationFilterShader;   // ★ 기본 필터셰이더 (커스텀 아님)
SceneDesc.flags |= eENABLE_ACTIVE_ACTORS | eENABLE_CCD | eENABLE_PCM | eREQUIRE_RW_LOCK;
DefaultMaterial = Physics->createMaterial(0.5f, 0.5f, 0.3f);
```
- 4-thread dispatcher지만 step은 `fetchResults(true)`로 동기 완료(2.2).
- `eREQUIRE_RW_LOCK` → 모든 접근에 `PHYSX_SCENE_WRITE_LOCK/READ_LOCK` 필요.

### 2.2 step: `FPhysXRuntime::Simulate`
(`PhysXRuntime.cpp:556`)
```cpp
// (1) 본 바디가 아닌 kinematic 바디만 컴포넌트 월드로 setGlobalPose (BoneIndex>=0은 제외)
for (Body : Bodies) if (Body->BoneIndex < 0 && Body->BodyType != Dynamic) { ... Actor->setGlobalPose(...); }
// (2) 차량 업데이트 (범위 외)
Scene->simulate(DeltaTime);     // :649
Scene->fetchResults(true);      // :650  ← 블로킹
// (3) READ_LOCK 잡고 actor 새 트랜스폼 → Body->CachedWorldTransform 갱신 (:653~)
```
- 래그돌 본 바디(`BoneIndex >= 0`)는 (1)의 kinematic 동기 대상에서 **빠진다** → 시뮬 중엔 순수 dynamic.
- step 후 `CachedWorldTransform`에 결과를 캐시. write-back은 이 캐시를 읽는다.

### 2.3 두 가지 틱 경로
**(A) 런타임 컴포넌트** `USkeletalMeshComponent::TickComponent` (`SkeletalMeshComponent.cpp:983`):
```cpp
if (bSimulatingPhysics) { ApplyPhysicsToBones(); ... return; }   // 래그돌: 애님 skip
if (EvaluateAnimInstance(DeltaTime)) { ... return; }             // 일반: 애님 평가
```
→ **상호배타 분기**. "애님 평가 → 물리"가 한 틱에 순차로 도는 게 아니라, 래그돌이면 애님을 건너뛰고 물리 write-back만 한다. (※ scene step `Simulate`는 `UWorld` 틱이 별도로 돌린다.)

**(B) 에디터 미리보기** `FMeshEditorWidget::TickPhysicsSimulation` (`MeshEditorWidget.Physics.cpp:1452`):
```cpp
Scene->Simulate(SimDt);          // step (fetchResults 포함)
MeshComp->ApplyPhysicsToBones(); // 같은 틱에서 즉시 write-back
```
- 주석(`:1478~`)이 이유를 명시: PreviewWorld는 `BeginPlay` 미호출이라 `UWorld::Tick`의 `bHasBegunPlay && PhysicsScene` 가드에 막혀 scene step도, `TickComponent` 디스패치도 안 된다. 그래서 **에디터는 step과 write-back을 여기서 직접** 돈다. (메모리의 "Simulate 무반응" 원인과 정확히 일치 — `[[project_ragdoll_simulate_noreact]]`.)
- 시작은 `StartPhysicsSimulation`(`:1364`): `InstantiatePhysicsAssetBodies` → `CreateRagdoll` → `bSimulating=true`.

### 2.4 래그돌 진입: `CreateRagdoll`
(`SkeletalMeshComponent.cpp` ~`:940`)
```cpp
for (Body : Bodies) {
    GetBoneWorldTransformByIndex(BoneIndex, BoneWorldTransform);
    PhysicsSceneOwner->SetBodyTransform(Body, BoneWorldTransform, /*bTeleport*/ true);  // 현재 본 포즈로 순간이동
    PhysicsSceneOwner->SetBodyType(Body, EPhysicsBodyType::Dynamic);                    // kinematic 해제 + wake
}
bSimulatingPhysics = true;   // 다음 틱부터 ApplyPhysicsToBones 경로
```
- 인스턴스 직후 ref pose와 현재 애님 포즈의 격차로 인한 튐을 막으려 **teleport로 본 월드에 맞춘 뒤** dynamic 전환.

### 2.5 write-back: `ApplyPhysicsToBones`
(`SkeletalMeshComponent.cpp:1005`)
```cpp
const FMatrix ComponentWorldInv = GetWorldInverseMatrix();
for (BoneIndex 오름차순 /*parent-first*/) {
    ParentGlobal = (ParentIndex>=0) ? ComponentLocalGlobals[ParentIndex] : Identity;
    if (Body valid && GetBodyTransform(Body, BodyWorld)) {
        ComponentGlobal = BodyWorld.ToMatrix() * ComponentWorldInv;                 // 월드 → 컴포넌트 로컬 글로벌
        LocalMatrix     = (ParentIndex>=0) ? ComponentGlobal * ParentGlobal.GetInverse() : ComponentGlobal;
    } else { /* body 없으면 ref local 유지 */ }
    ComponentLocalGlobals[BoneIndex] = ComponentGlobal;
    LocalPose[BoneIndex] = FTransform(LocalMatrix);
}
SetBoneLocalTransforms(LocalPose);   // 스키닝/bounds 갱신 트리거
```
- 행벡터 규약: `Global = Local · ParentGlobal` 의 역 → `Local = Global · ParentGlobal⁻¹` ([06](06_coordinate_math.md) 2.4와 정확히 대응).
- body world == 본 world(오프셋 0, [01](01_rigid_body_setup.md) 2.4)라 **body→bone 별도 오프셋 보정 불필요**(주석 `:1030`).
- 바디 없는 본은 ref local 유지 → 사슬이 자연스럽게 이어짐.

### 2.6 race 부재 근거
- step은 `fetchResults(true)`로 **완료까지 블로킹**(`:650`) → 결과가 확정된 뒤에만 다음 줄로 진행.
- write-back(`ApplyPhysicsToBones`)은 같은 스레드에서 step **이후** 호출((A) TickComponent, (B) TickPhysicsSimulation 모두). 멀티스레드 본 쓰기 없음.
- PhysX 접근은 `eREQUIRE_RW_LOCK` + `PHYSX_SCENE_WRITE/READ_LOCK`로 보호.
→ "본 transform fetch → 물리 결과 적용"이 단일스레드 동기 순서로 직렬화되어 race가 없다.

### 2.7 자기충돌 제어 (실제 동작 + gap)
**구현된 것**: 같은 메시의 모든 바디를 하나의 `PxAggregate`로 묶고 `enableSelfCollision`을 전역 토글.
`InstantiatePhysicsAssetBodies` (`SkeletalMeshComponent.cpp:746`):
```cpp
const int32 MaxActors = (int32)PhysicsAsset->GetBodySetups().size();
if (MaxActors > 0 && MaxActors <= 128)
    PhysicsAggregate = Scene.CreateAggregate(MaxActors, PhysicsAsset->bEnableSelfCollision);
else if (MaxActors > 128 && !bEnableSelfCollision)
    UE_LOG("... exceeds PxAggregate max (128).");   // 128 초과 시 자기충돌 끄기 불가
```
`CreateAggregate` (`PhysXRuntime.cpp:806`): `Physics->createAggregate(MaxActors, bEnableSelfCollision); Scene->addAggregate(*Aggregate);`
- `bEnableSelfCollision=false`(기본) → **같은 메시 바디끼리 충돌 안 함**(월드와는 충돌). 래그돌 폭발 방지.
- 조인트로 직결된 두 바디는 PhysX D6 joint 기본(연결 actor 충돌 비활성)으로 어차피 제외된다(코드가 `eCOLLISION_ENABLED`를 켜지 않음).

**⚠ gap — per-pair `DisabledCollisionPairs`는 런타임 미배선**:
- `UPhysicsAsset`은 `DisabledCollisionPairs`(본 쌍별 비활성)와 `IsCollisionDisabled/SetCollisionDisabled`를 갖고, 에디터 툴바·자동생성이 이를 채운다(`MeshEditorWidget.Physics.cpp:810,815,823,1332`).
- 그러나 **런타임(InstantiatePhysicsAssetBodies / PhysXRuntime)에서 `IsCollisionDisabled`를 읽어 PhysX 충돌 필터에 반영하는 코드가 없다**(grep 확인: 호출처는 에디터·자산 정의뿐). 필터셰이더도 `PxDefaultSimulationFilterShader`(커스텀 아님).
- 즉 현재 자기충돌 제어는 **aggregate 전역 on/off**가 전부고, **쌍별 비활성은 저장/편집만 되고 시뮬에 반영되지 않는다**. → [07](07_glossary_and_gaps.md)의 gap 목록. (최근 커밋 `44d67167 "self collision fix"` 관련 영역 — 본 문서는 현재 코드 상태 기준.)
