# InstantiatePhysicsAssetBodies "no valid bodies" 원인 진단

## 0. 메타

- **모드**: 진단 전용. 코드 수정/추가/생성 없음.
- **관측된 증상**:
  - 에디터 Physics 탭에서 본에 body 추가 → Simulate 클릭.
  - 로그 시퀀스: `PhysicsAsset body creation failed. Bone=…` × N → `[Physics] StartPhysicsSimulation: InstantiatePhysicsAssetBodies returned false (no valid bodies created).`
  - 즉 BodySetup(데이터)은 만들어졌으나 `FBodyInstance`(런타임 actor) 생성은 0건.
- **목표 사슬** (PHYSICS_INFRA_INVENTORY.md 참조):
  `PhysicsAsset 확보 → InstantiatePhysicsAssetBodies → CreateRagdoll → Simulate(dt) → ApplyPhysicsToBones write-back`. 이번 진단은 두 번째 단계 안에서 끊기는 지점.

상태 태그: `[확인됨]` / `[없음]` / `[부분]` / `[불명확/검증필요]`

---

## 1. PhysicsAsset 인스턴스 동일성 — [확인됨/통과]

- [확인됨] `FMeshEditorWidget::StartPhysicsSimulation`은 `PhysicsTabState.PhysicsAsset`을 명시적으로 2-인자 오버로드에 전달 — [MeshEditorWidget.Physics.cpp Phase2 구현부](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp).
- [확인됨] 같은 PA를 에디터 Add Body 핸들러가 `GetOrCreateBodySetup` + `Sphere/Box/CapsuleElems.push_back`로 채움 — [MeshEditorWidget.Physics.cpp:391-432](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:391).
- 결론: 에디터가 편집한 PA와 Simulate가 보는 PA가 동일 인스턴스. **불일치 아님**.

---

## 2. BoneName → BoneIndex 해석 — [확인됨/통과 (관측 로그로 추론)]

- [확인됨] 관측된 로그는 `"PhysicsAsset body creation failed. Bone=…"` — [SkeletalMeshComponent.cpp:450](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:450). 이 로그는 `Runtime.CreateRigidBody` 결과가 null일 때만 발생.
- [확인됨] BoneName이 없거나 Bones에서 못 찾았다면 `"PhysicsAsset body skipped: bone not found. Bone=…"`로 빠짐 — [SkeletalMeshComponent.cpp:413-418](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:413). 이 로그는 관측되지 않음 → 통과한 것.
- [확인됨] `FindBoneIndex`(BoneName empty면 -1, 아니면 `Asset->Bones[i].Name == BoneName` 비교) — [SkinnedMeshComponent.cpp:276-289](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.cpp:276).
- [확인됨] 에디터 Add Body가 `GetOrCreateBodySetup(Bone.Name)`로 BoneName 채움 — [MeshEditorWidget.Physics.cpp:393](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:393).
- 결론: **통과**.

---

## 3. AggregateGeom / Shapes 비어있지 않음 — [확인됨/통과 (관측 로그로 추론)]

- [확인됨] `if (!BodySetup || BodySetup->AggregateGeom.IsEmpty()) continue;` 가드 — [SkeletalMeshComponent.cpp:408-411](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:408). 여기서 막혔다면 "body creation failed" 로그도 발생하지 않음 → 통과.
- [확인됨] `if (BodyDesc.Shapes.empty()) continue;` 가드 — [SkeletalMeshComponent.cpp:442-445](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:442). 동일한 이유로 통과.
- [확인됨] 에디터 `AddBodyWithShape`는 `GetOrCreateBodySetup` + `AddShapeFn`(SphereElems/BoxElems/CapsuleElems 중 하나에 push)를 한 번에 실행 — [MeshEditorWidget.Physics.cpp:391-432](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:391). 즉 body 추가 시 shape 1개도 같이 들어감.
- 결론: **통과**.

---

## 4. `CreateRigidBody`가 `nullptr` 반환 — [확인됨/실패 지점]

- [확인됨] 로그 발생 위치: `Runtime.CreateRigidBody(BodyDesc)` 결과가 null — [SkeletalMeshComponent.cpp:447-452](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:447).
- `CreateRigidBody`의 null 반환 경로 후보:
  - (a) 첫 가드 `if (!Initialize() || !Physics || !Scene) return nullptr;` — [PhysXRuntime.cpp:539-542](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:539).
  - (b) Actor 생성 실패 `if (!Actor) return nullptr;` — [PhysXRuntime.cpp:568-571](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:568).

### 4-A. 현재 Physics 백엔드 설정 — [확인됨] PhysX
- [확인됨] [ProjectSettings.ini:10](KraftonEngine/Settings/ProjectSettings.ini:10) — `"Backend": 1`.
- [확인됨] [ProjectSettings.cpp:83-87](KraftonEngine/Source/Engine/Core/ProjectSettings.cpp:83) — `if (v == 1) Physics.Backend = EPhysicsBackend::PhysX; else … Native;`. → **현 설정 = PhysX backend.**

### 4-B. World가 만드는 두 객체의 `PxCreateFoundation` 호출 순서 — [확인됨] 같은 프로세스에서 2회 호출
- [확인됨] `UWorld::InitWorld()` 본문 — [World.cpp:308-316](KraftonEngine/Source/Engine/GameFramework/World.cpp:308):
  1. Backend==PhysX → `PhysicsScene = std::make_unique<FPhysXPhysicsScene>();`
  2. `PhysicsScene->Initialize(this);`
     - `FPhysXPhysicsScene::Initialize` → `AcquireSharedPhysXCore();` — [PhysXPhysicsScene.cpp:349](KraftonEngine/Source/Engine/Physics/PhysXPhysicsScene.cpp:349).
     - `AcquireSharedPhysXCore` → `CreateSharedPhysXCore` → **`GSharedFoundation = PxCreateFoundation(...)`** — [PhysXCore.cpp:96](KraftonEngine/Source/Engine/Physics/PhysXCore.cpp:96). **(첫 호출, 성공)**
  3. `PhysicsRuntime = std::make_unique<FPhysXRuntime>(); PhysicsRuntime->Initialize();`
  4. `FPhysXRuntime::Initialize()` → **`Foundation = PxCreateFoundation(...)`** — [PhysXRuntime.cpp:333](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:333). **(두 번째 호출)**

### 4-C. PhysX 4.1 사양상 두 번째 호출 = `NULL` — [확인됨]
- [확인됨] [PxFoundation.h:110-111](KraftonEngine/ThirdParty/PhysX41/include/physx/PxFoundation.h:110) (헤더 docstring 그대로 인용):
  > _"Calling this method after an instance has been created already will result in an error message and NULL will be returned."_

### 4-D. NULL Foundation → Initialize 실패 → 영구 null 상태 — [확인됨]
- [확인됨] [PhysXRuntime.cpp:328-337](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:328):
  ```
  if (Foundation || Physics || Scene) return true;            // ← 모두 null이라 통과 못함
  Foundation = PxCreateFoundation(...);                       // ← 두 번째 호출, NULL 반환
  if (!Foundation) return false;
  ```
- [확인됨] Initialize 실패 후에도 `Foundation/Physics/Scene` 모두 nullptr인 채 `FPhysXRuntime` 인스턴스는 생존(unique_ptr가 해제하지 않음).
- [확인됨] 다음 호출에서 `Initialize()` 다시 진입 → 가드 통과 못해 `PxCreateFoundation` 재시도 → 또 NULL → **영구히 false**.
- [확인됨] [CreateRigidBody:539-542](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:539): `if (!Initialize() || !Physics || !Scene) return nullptr;` → **모든 BodySetup이 동일 경로로 nullptr 받음**.
- [확인됨] 결과: SkeletalMeshComponent.cpp:450 `UE_LOG("PhysicsAsset body creation failed. ...")` × N → `bCreatedAnyBody = false` → `return false`(SkeletalMeshComponent.cpp:458-463) → 에디터 측 `[Physics] StartPhysicsSimulation: InstantiatePhysicsAssetBodies returned false (no valid bodies created).`

---

## 결론 — false로 빠지는 단일 지점

**4-D, [PhysXRuntime.cpp:333-337](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:333) — `Foundation = PxCreateFoundation(...); if (!Foundation) return false;`**

### 근본 원인 한 문장

PhysX 4.1의 `PxCreateFoundation`은 **프로세스당 1회**만 허용([PxFoundation.h:110-111](KraftonEngine/ThirdParty/PhysX41/include/physx/PxFoundation.h:110))인데, 현 설정([ProjectSettings.ini:10](KraftonEngine/Settings/ProjectSettings.ini:10) `Backend=1` = PhysX)에서 `UWorld::InitWorld`가 (1) [PhysXCore.cpp:96](KraftonEngine/Source/Engine/Physics/PhysXCore.cpp:96)의 공유 Foundation 생성과 (2) [PhysXRuntime.cpp:333](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:333)의 자체 Foundation 생성을 같은 프로세스에서 [World.cpp:312, 316](KraftonEngine/Source/Engine/GameFramework/World.cpp:312) 순서로 연달아 호출한다. 두 번째 호출이 `NULL`을 받아 `FPhysXRuntime::Initialize()`가 영구 실패 → `FPhysXRuntime::CreateRigidBody`의 첫 가드 `!Initialize()`에서 매번 `nullptr` 반환 → `InstantiatePhysicsAssetBodies` 안 모든 BodySetup이 동일하게 [SkeletalMeshComponent.cpp:447-451](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:447)의 "body creation failed" 로그로 빠지고 `bCreatedAnyBody=false` → `return false`.

### 부수 단서

- 동일 갭이 [Docs/PHYSICS_INFRA_INVENTORY.md A.1](Docs/PHYSICS_INFRA_INVENTORY.md) — "PhysXCore의 '공유 코어'(`AcquireSharedPhysXCore`)는 **사용하지 않고** 자체 `PxCreateFoundation/PxCreatePhysics`로 별도 인스턴스 생성" — 으로 사전 진단된 항목과 정확히 일치.
- Backend가 `Native`(ProjectSettings.ini의 값이 1이 아닌 경우)였다면 `FNativePhysicsScene`만 만들어져 `PhysXCore::AcquireSharedPhysXCore`가 호출되지 않으므로 `FPhysXRuntime::Initialize`의 `PxCreateFoundation`이 첫 호출 → 성공 → 이 시나리오는 재현되지 않음. 즉 본 증상은 **PhysX backend + Runtime 동시 활성** 상태에서만 발생.

### 영향 범위 (해결책은 다음 사이클에서 별도 처리)

- 직접 영향: `InstantiatePhysicsAssetBodies` 전부 실패 → `CreateRagdoll` 가드(`Bodies.empty()`)에 막혀 진입 불가([SkeletalMeshComponent.cpp:579-583](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:579)) → 에디터 ragdoll 미리보기 실패.
- 잠재 부수 효과: `FPhysXRuntime::Simulate`/`SetBodyTransform`/`SetBodyType` 모두 `Scene == nullptr` 상태에서 no-op([PhysXRuntime.cpp:463-468, 1053-1083](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:463)) — 즉 에디터 Tick에서 `Runtime->Simulate(dt)`도 매 프레임 즉시 return 중.
- 비-PhysX 분기로 두 PxFoundation이 한 번도 동시 생성되지 않은 경로는 본 증상에서 자유.
