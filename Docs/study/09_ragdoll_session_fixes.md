# 09. Ragdoll 수정 로그 (세션 정리)

> 이 문서는 한 작업 세션에서 ragdoll(패시브 래그돌)을 안정화하기 위해 적용한 **모든 수정 로직**을 정리한 것이다.
> 아키텍처 배경은 [08_ragdoll_skinning_physics.md](08_ragdoll_skinning_physics.md)(본·바디·스키닝·피직스 관계)를 함께 참고.
> 경로는 `KraftonEngine/` 접두어 생략. 라인 번호는 변동되므로 **심볼명 기준**으로 찾을 것.

---

## 0. 한눈에 보기

| # | 증상 | 근본 원인 | 수정 | 상태 |
|---|---|---|---|---|
| 1 | 진입 시 액터가 솟아오름/폭발 | 진입 순간 겹침 → depenetration 무제한 분리속도 | `setMaxDepenetrationVelocity(1.0f)` | 커밋 `ef46f3ed` |
| 2 | PIE/레벨에서 body 디버그 와이어 안 보임 | `Mesh->PhysicsAsset` 직접 접근(런타임 null) | `GetPhysicsAsset()`(override 우선)로 통일 | 커밋 `03105410` |
| 3 | 본 사이 늘어남/찢어짐 | D6 Locked 조인트에 projection 없음 | projection 시도 → flying과 무관 판명 → 비활성 | 제거됨(비활성) |
| 4 | R 연타 토글 시 contact 콜백 crash | release된 actor를 deref(UAF) | `onContact`에 removed-actor/shape 가드 | 커밋 `fcf6ee17` |
| 5 | 캡슐 ↔ 래그돌 바디 중복 충돌 → flying | 같은 액터 컴포넌트 간 충돌 미차단 | filter shader **same-actor 제외** | 미커밋 |
| 6 | ragdoll에서 원래 상태로 복귀 불필요 | — | Lua 단방향 진입(`enter_ragdoll`) | 커밋 `fcf6ee17` |
| 7 | 시작 시 캡슐-메시 정렬 어긋남(밖→안) | 캡슐 `bSimulatePhysics=true` + 공중 배치 → 낙하·movement 충돌 | 캡슐 kinematic 시작 → 착지(Walking) 후 dynamic | 미커밋 |

> 보조 수단(진입 시 형제 콜라이더 disable + movement 정지)도 만들었으나, #5의 same-actor 필터가 근본 해결이라 이제 **중복(belt-and-suspenders)** 이다.

---

## 1. 진입 폭발 방지 — depenetration 속도 클램프

**증상:** `R`로 ragdoll 진입 시 액터가 위로 솟거나 폭발.

**원인:** [InstantiatePhysicsAssetBodies](Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp)가 바디를 Dynamic·awake·중력 ON으로 **현재 본 월드 포즈**에 한꺼번에 생성한다. 인접 캡슐이 서로/바닥과 겹친 채 첫 스텝을 맞으면 PhysX가 depenetration으로 분리하는데, [`CreateRigidBody`](Source/Engine/Physics/PhysXRuntime.cpp)가 `setMaxDepenetrationVelocity`를 설정하지 않아 **PhysX 기본값(사실상 무제한)** 으로 폭발적으로 튕긴다. 월드 단위는 미터(`PxTolerancesScale()` 기본 length=1, `SceneDesc.gravity = -9.81`).

**수정** — `FPhysXRuntime::CreateRigidBody`의 dynamic 바디 설정부:
```cpp
Dynamic->setMaxDepenetrationVelocity(1.0f);  // 1 m/s 클램프(5cm 겹침 ≈ 3프레임 해소, 폭발 차단)
```

**상태:** 커밋 `ef46f3ed collision clamp`.

---

## 2. body 디버그 와이어 미표시 — override-aware 게터 통일

**증상:** "Show Physics Bodies"가 Physics Asset 에디터 프리뷰에선 보이는데 **PIE / 메인 에디터 레벨 월드**에선 안 보임.

**원인:** 와이어를 만드는 [`BuildPhysicsBodyWireframe`](Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp)와 패널 속성 [`GetEditableProperties`](Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp)가 `Mesh->PhysicsAsset`를 **직접** 읽었다. 런타임은 PhysicsAsset을 컴포넌트의 `PhysicsAssetOverride`(=`<Mesh>_Physics.uasset` 규약)로 로드하고 `Mesh->PhysicsAsset`은 **null**(에디터 세션만 메시에 꽂아줌) → 조기 리턴.

**수정:** 두 함수 모두 시뮬과 동일한 override 우선 게터로 통일.
```cpp
UPhysicsAsset* PA = GetPhysicsAsset();   // was: Mesh ? Mesh->PhysicsAsset : nullptr
```
두 그리기 경로(선택=ContributeSelectedVisuals / 런타임=DrawRuntimePhysicsBodies)가 같은 빌더를 거치므로 한 번에 복구.

**상태:** 커밋 `03105410 body debug line fix`.

---

## 3. 찢어짐 — projection (시도 후 비활성)

**점검:** 조인트 선형 DOF는 [InstantiatePhysicsAssetBodies](Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp)에서 **Lock/Free 이분법**으로만 들어가고(유한 거리 limit 미지원), [`CreateD6Joint`](Source/Engine/Physics/PhysXRuntime.cpp)는 `setLinearLimit`을 호출하지 않는다. 기본 `bLockLinearMotion=true`라 Locked이지만 **projection이 없어** 솔버 미수렴 시 벌어진다.

**시도:** `CreateD6Joint`에 projection 추가(`ePROJECTION`, linear 0.01 / angular 0.35).
- 결과: **찢어짐은 줄었으나**, flying(튕김)은 projection 도입 전에도 존재 → **근본 원인 아님**.
- 격리 테스트 위해 주석 처리했고, 이후 병합 과정에서 코드에서 제거됨.

**상태:** 현재 코드에 없음. (필요 시 재평가 가능 — 단, flying과는 무관.)

---

## 4. R 연타 crash — contact 콜백 removed-actor 가드

**증상:** `R`을 연타해 ragdoll을 빠르게 토글하면 [`onContact`](Source/Engine/Physics/PhysXRuntime.cpp)에서 `CompB`가 nullptr/garbage → crash.

**원인 사슬:**
1. 보조 로직의 `SetCollisionEnabled(NoCollision)` → `UnregisterComponent` → `DestroyRigidBody`가 **PxActor를 즉시 release**.
2. 접촉 중이던 actor가 release되면 PhysX가 다음 `fetchResults`에서 그 쌍을 `PxContactPairHeaderFlag::eREMOVED_ACTOR_*` 플래그와 **dangling actor 포인터**로 보고.
3. `onContact`가 플래그 확인 없이 `GetComponentFromActor(PairHeader.actors[i])` → `Actor->userData` deref → **UAF**.
4. 바로 아래 `onTrigger`는 `eREMOVED_SHAPE` 가드가 있는데 `onContact`엔 없던 **비대칭**.

**수정** — `onContact` 선두(deref 전) + per-pair 루프:
```cpp
if (PairHeader.flags & (PxContactPairHeaderFlag::eREMOVED_ACTOR_0 | PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
    return;
...
if (Pair.flags & (PxContactPairFlag::eREMOVED_SHAPE_0 | PxContactPairFlag::eREMOVED_SHAPE_1))
    continue;
```

**상태:** 커밋됨(`fcf6ee17`). 현재 코드 유지.

---

## 5. 캡슐 중복 충돌 / flying — same-actor 충돌 제외 (핵심)

### 5-1. 1차 접근: 진입 시 형제 콜라이더 disable + movement 정지
`SkeletalMeshComponent`에 추가:
- `DisableOwnerCollisionForRagdoll()` — 소유 액터의 형제 `UPrimitiveComponent` collision off + `UMovementComponent` 정지.
- `RestoreOwnerCollisionAfterRagdoll()` — 종료 시 복원. **dangling 방지:** 저장 포인터를 직접 deref하지 않고 Owner의 현재 `GetComponents()`에 살아있는 것만 복원.
- `SetSimulatingPhysics(true)` 훅 + **reorder**: 콜라이더 disable을 `InstantiatePhysicsAssetBodies` **이전**으로 이동(진입 실패 시 원복). (World::Tick이 Simulate→Tick 순서라 이 시점에 끄면 다음 Simulate부터 빠진 상태 보장.)

→ flying은 일시적으로 잡혔으나, **main 병합 후 재발**.

### 5-2. bisection: 코드 vs 데이터
`SkeletalMeshComponent.cpp/.h`를 pre-merge(`fcf6ee17`)로 되돌려도 flying 지속 → **병합된 코드는 무죄, 데이터(씬의 캡슐)가 원인** 확정.

진단 보조 사실:
- self-collision은 aggregate(`enableSelfCollision=false`)로 OFF, 자산 마지막 바이트=0으로 재확인.
- DeltaLen 로그(EnterRagdollState)가 모두 0 → 바디는 본에 정확히 배치(배치/스케일 문제 아님).
- "Show Physics Bodies" 와이어는 **본 위치(write-back 결과)** 에 그려지지만, body 본은 물리 위치로 복원되므로 **와이어 튕김 = 물리 바디 튕김**.

### 5-3. 근본 수정: filter shader same-actor 제외
엔진은 이미 컴포넌트 셰이프에 **`word3 = owner actor UUID`** 를 넣는다([`BuildComponentFilterData`](Source/Engine/Physics/PhysXRuntime.cpp)). 래그돌 바디 셰이프도 `bOverrideFilterData=false`라 같은 경로(`ApplyComponentFilterData`)를 타므로 **이미 word3를 보유**. 캡슐도 동일. 빠진 건 **접촉 필터 셰이더의 same-actor 비교**였다.

**수정** — [`KraftonRagdollFilterShader`](Source/Engine/Physics/PhysXRuntime.cpp)에 추가:
```cpp
// 같은 액터(word3 일치) 쌍은 충돌 제외. 단 래그돌-래그돌 자기충돌(aggregate/ragdoll 경로 전담)과
// 트리거(오버랩 보존)는 제외.
if (FilterData0.word3 != 0 && FilterData0.word3 == FilterData1.word3 && !(bRagdoll0 && bRagdoll1)
    && !PxFilterObjectIsTrigger(Attributes0) && !PxFilterObjectIsTrigger(Attributes1))
    return PxFilterFlag::eSUPPRESS;
```
override 경로(`bOverrideFilterData=true`)의 `word3`도 owner UUID로 통일(정책 일관성, owner 없는 플로어는 0).

**개념 정리:** 한 액터의 컴포넌트들(이동용 캡슐 + 시각/래그돌 메시)은 **하나의 개체**이고 서로 **겹치도록 설계**된다(캡슐이 몸을 감쌈). 따라서 그들 간 물리 충돌은 항상 아티팩트 → **same-actor 제외가 유일하게 옳은 해법**(위치 보정 불가/무의미). 위치·스케일과 무관하게 동작.

**상태:** **미커밋** (PhysXRuntime.cpp). self-collision **ON** 모드에선 `SetRagdollBodyFilter`가 word3를 0으로 덮으므로 그 경로에도 owner UUID를 넣어야 완전 robust(후속). 현재 OFF 케이스는 해결.

---

## 6. ragdoll 단방향 진입 (복귀 모드 제거)

**점검:** `SetSimulatingPhysics(false)`(복귀 경로)는 게임플레이뿐 아니라 **에디터 Simulate 정지 + 자산 교체 정리**도 공유 → C++에서 제거하면 에디터가 깨짐. 따라서 **유지**.

**수정:** 게임플레이 복귀만 Lua에서 제거. [`ULevel_3_ASkeletalMeshActor_3.lua`](Content/Script/ULevel_3_ASkeletalMeshActor_3.lua): `toggle_ragdoll`(양방향) → `enter_ragdoll`(단방향, 이미 시뮬 중이면 `return`). R 핸들러도 갱신.

**상태:** 커밋됨(`fcf6ee17`).

---

## 7. 시작 시 캡슐-메시 정렬 — kinematic 시작 → 착지 후 dynamic

**증상:** PIE 시작 직후 스켈레탈 메시가 캡슐 바깥에 있다가 시간이 지나며 안으로 들어옴. 시작 직후 ragdoll이면 컴포넌트끼리 겹쳐 보임.

**셋업(씬 데이터):** 루트=`UCapsuleComponent`(HalfHeight 2.01, **`bSimulatePhysics=true`=dynamic**, Z=3.23 공중), 자식=`USkeletalMeshComponent`(상대위치 [0,0,-2.02], `CollisionEnabled=0`), movement=`UCharacterMovementComponent`.

**원인:** 캡슐이 **dynamic + 공중 배치** → 시작 시 물리로 낙하하고, 동시에 movement가 구동하려 해 **물리 vs movement 충돌**로 첫 프레임들이 흔들림(메시가 한 박자 늦게 따라옴). 정렬 어긋남은 충돌 버그가 아니라 **착지/정착 과도기**.

**수정:** 캡슐을 **초기화 동안 kinematic** 으로 두고, movement가 **착지(Walking)** 한 뒤 dynamic으로 전환.

C++ — lua 바인딩 추가 ([`LuaScriptManager.cpp`](Source/Engine/Lua/LuaScriptManager.cpp)):
- `UCharacterMovementComponent` usertype: `IsWalking()` / `IsFalling()` / `GetSpeed()`
- `Actor:GetCharacterMovement()` 접근자

Lua ([`ULevel_3_ASkeletalMeshActor_3.lua`](Content/Script/ULevel_3_ASkeletalMeshActor_3.lua)):
```lua
-- BeginPlay: 캡슐 kinematic 고정
capsule = obj:GetRootPrimitiveComponent()
if capsule ~= nil then capsule:SetSimulatePhysics(false) end
capsulePendingDynamic = true

-- Tick: 착지(Walking)하면 dynamic 전환
if capsulePendingDynamic then
    local move = obj:GetCharacterMovement()
    if move ~= nil and move:IsWalking() then
        if capsule ~= nil then capsule:SetSimulatePhysics(true) end
        capsulePendingDynamic = false
    end
end
```
→ kinematic 동안 movement가 캡슐을 floor로 내려놓고, 착지 시점에 물리로 넘기므로 **공중 낙하/흔들림 없음**.

**상태:** **미커밋** (LuaScriptManager.cpp + lua). C++ 바인딩 추가라 **재빌드 필요**.

---

## 8. 병합으로 들어온 타인 변경 (참고)

flying 재발 진단 중, main 병합으로 들어온 ragdoll 관련 변경(우리 것 아님):
- `d14280e3 fix: ragdoll scale issue` — 컴포넌트 스케일을 물리 셰이프/앵커에 **베이크**(`AppendPhysicsShapes(*, PhysicsAssetScale)`, `BodyToBoneOffsets`), `EnterRagdollState` 텔레포트 재작성. **현재 씬 scale=1에선 기하학적 no-op**.
- `f6462e61` / `016166fe` — body 없는 본의 글로벌을 자식 ref-local로 역산하는 write-back(`ApplyPhysicsToBones`) 재작성. (write-back은 물리에 피드백하지 않으므로 물리 flying의 직접 원인은 아님.)

---

## 9. 진단에서 얻은 일반 사실 (재사용)

- **World::Tick 순서:** `PhysicsScene->Simulate()` → `TickManager.Tick()`([World.cpp](Source/Engine/GameFramework/World.cpp)). 컴포넌트 틱(Lua 포함)에서의 상태 변경은 **다음 프레임 Simulate부터** 반영.
- **"Show Physics Bodies" 와이어**는 raw 물리 바디가 아니라 `GetBoneWorldTransformByIndex`(=write-back 결과)에 그려진다. 단 body 본은 물리 위치로 복원되므로 와이어가 날면 물리도 난다.
- **self-collision OFF**는 `PxAggregate(enableSelfCollision=false)`가 broadphase에서 intra-aggregate 쌍을 막아 구현(filter shader 도달 전).
- **같은 액터 self-filter id** = sim filter `word3` = owner actor UUID. 차량 레이캐스트 prefilter가 이미 사용 중이었고, 이번에 접촉 필터 셰이더에도 도입.

---

## 10. 최종 미커밋 변경 목록

| 파일 | 변경 | 재빌드 |
|---|---|---|
| `Source/Engine/Physics/PhysXRuntime.cpp` | filter shader same-actor 제외 (+ override word3) | 필요 |
| `Source/Engine/Lua/LuaScriptManager.cpp` | `UCharacterMovementComponent` 바인딩 + `GetCharacterMovement` | 필요 |
| `Content/Script/ULevel_3_ASkeletalMeshActor_3.lua` | 캡슐 kinematic 시작 → 착지 후 dynamic | 불필요(스크립트) |

**후속 후보**
- self-collision ON 모드: `SetRagdollBodyFilter`에도 owner UUID(word3) 채우기.
- 진입 시 형제 콜라이더 disable 로직: same-actor 필터로 대체됐으므로 중복 — 정리 여부 결정.
- 캡슐 dynamic 필요성 재검토(영구 kinematic이면 movement-physics 충돌 자체가 사라짐).
