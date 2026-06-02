# 수정 명세서 — 전투 히트 & 중력의 PhysX 전환 (이동 적분은 유지)

> 상태: **명세 only. 코드 미수정.** 브랜치 `feature/joint`, 현재 코드 직접 검증 기준.
> 목표: (A) 전투 히트 판정을 커스텀 sphere↔AABB 브루트포스 → **PhysX overlap 쿼리**로, (B) 캐릭터 중력을 하드코딩 상수 → **PhysX 씬 중력값(단일 소스)** 으로 전환. **이동의 위치 적분/floor stick/입력·루트모션은 그대로 유지.**

---

## 0. 범위 / 비목표

**목표**
1. `UAnimNotifyState_AttackHitWindow` 의 타격 판정을 PhysX overlap 쿼리로 교체.
2. `UCharacterMovementComponent` 의 중력 가속도를 PhysX 씬(`PxScene::getGravity`)에서 가져오도록 교체.

**비목표(명시적으로 유지)**
- 이동의 **위치 적분**(`Velocity*dt → SetWorldLocation`), **floor stick**(`TraceFloor` PhysX raycast), 입력→속도, 루트모션, yaw orient — **무변경**.
- 캐릭터 캡슐을 PhysX 강체로 만들지 않음. 여전히 **query-only**. PhysX가 캡슐을 직접 이동시키지 않음.
- 비-PhysX 충돌 레이어(`Engine/Collision/` 피킹·컬링)는 본 작업과 무관.

---

## 1. 현재 상태 (근거)

### 1.1 전투 히트 — `AnimNotifyState_AttackHitWindow.cpp`
- `NotifyBegin` [:173](KraftonEngine/Source/Engine/Animation/Notify/AnimNotifyState_AttackHitWindow.cpp:173): 히트셋(`HitActorsByMesh`) 초기화 + 트레일 파티클 on.
- `NotifyTick` [:199](KraftonEngine/Source/Engine/Animation/Notify/AnimNotifyState_AttackHitWindow.cpp:199): `Center = GetHitCenter(본 위치 + actor-local Offset)`, `Radius`(기본 60).
  - **`World->GetActors()` 전수 순회** [:222] → 각 액터의 `GetPrimitiveComponents()` → `Primitive->GetWorldBoundingBox()` (루즈 AABB) → `DistanceSquaredPointAABB(Center,Bounds) <= Radius²` [:286].
  - 필터: `bRequireTargetActorTag`/`TargetActorTag`(기본 "HitTarget"), `bRequireQueryCollision`, `bHitWorldStatic`.
  - 히트 시: `HitActors` dedup(윈도우당 1회) → `LuaScript->DispatchOverlap(Owner)` [:329] → `ApplyHitStop`(공격자+피격자) → `ApplyKnockback` → 디버그/로그.
- `NotifyEnd` [:366]: 맵 정리.
- ⚠️ broad-phase 없음 → **O(전체 액터 × 컴포넌트)**. PhysX 완전 무관(순수 수학).

### 1.2 중력 — `CharacterMovementComponent.cpp`
- `TickFalling` [:286](KraftonEngine/Source/Engine/Component/Movement/CharacterMovementComponent.cpp:286): `Velocity.Z -= Gravity * DeltaTime;` (`Gravity` = `float` 멤버, 기본 9.8, [.h:76](KraftonEngine/Source/Engine/Component/Movement/CharacterMovementComponent.h:76)). 이어 `SetWorldLocation(loc + Velocity*dt)` [:293].
- 씬 중력은 **별도 하드코딩**: `SceneDesc.gravity = PxVec3(0,0,-9.81)` [PhysXRuntime.cpp:703](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:703).
- → 두 값이 독립(9.8 vs 9.81). 단일 소스 아님.

### 1.3 PhysX 쪽 가용 자산 (재사용/신설 판단)
| 항목 | 상태 | 위치 |
|---|---|---|
| `GetComponentFromActor(PxRigidActor*)` | ✅ 있음 | PhysXRuntime.cpp:205 |
| `FillHitResult(PxLocationHit&, FHitResult&)` | ✅ 있음 | PhysXRuntime.cpp:216 |
| 쿼리 필터 패턴(Channel/ObjectType/Sweep, `preFilter`+`word0/word1`) | ✅ 있음 | PhysXRuntime.cpp:1818~1986 |
| `FOverlapResult{OverlapActor, OverlapComponent}` | ✅ **이미 존재** | CollisionTypes.h:132 |
| `Scene->getGravity()` (PxScene) | ✅ 내부 사용 중 | PhysXRuntime.cpp:995 |
| **overlap 다중쿼리** (IPhysicsScene) | ❌ **없음 → 신설** | — |
| **`GetGravity()`** (IPhysicsScene) | ❌ **없음 → 신설** | — |
| UWorld 물리쿼리 래퍼 | `PhysicsRaycast*`/`PhysicsSphereSweep*` 만 | World.cpp:221~243 |

---

## 2. 변경 A — 전투 히트 → PhysX overlap

### A-1. [신설] `IPhysicsScene::OverlapSphere` (다중 touch)
[IPhysicsScene.h:64](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:64) `SphereSweepShapeComponents` 선언 뒤에 추가:
```cpp
// 구(sphere) 영역과 겹치는 모든 query shape 의 소유 컴포넌트를 수집(블로킹 없음, 다중).
// ObjectTypeMask: (1u<<ECollisionChannel) 비트 OR. IgnoreActor 소유 컴포넌트는 제외.
// 반환: 수집된 overlap 개수. (버퍼 상한 초과 시 잘림 + 경고 로그)
virtual int32 OverlapSphere(const FVector& Center, float Radius, uint32 ObjectTypeMask,
    TArray<FOverlapResult>& OutOverlaps, const AActor* IgnoreActor = nullptr) const = 0;
```
→ 순수가상이므로 `FPhysXRuntime`(및 향후 모든 IPhysicsScene 구현)에 override 의무.

### A-2. [신설] `FPhysXRuntime::OverlapSphere` 구현 스케치
`RaycastByObjectTypes`(:1864) 패턴을 그대로 복제하되 **`eBLOCK` → `eTOUCH`**, raycast → `Scene->overlap`, `PxOverlapBuffer` 다중 수집:
```cpp
int32 FPhysXRuntime::OverlapSphere(const FVector& Center, float Radius, uint32 ObjectTypeMask,
    TArray<FOverlapResult>& OutOverlaps, const AActor* IgnoreActor) const
{
    OutOverlaps.clear();
    if (!Scene || Radius <= 0.0f || ObjectTypeMask == 0) return 0;

    struct FOverlapFilter : PxQueryFilterCallback {
        const AActor* IgnoreActor = nullptr; PxU32 ObjectTypeMask = 0;
        PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape,
            const PxRigidActor* Actor, PxHitFlags&) override {
            if (IsTriggerShape(Shape)) return PxQueryHitType::eNONE;
            UPrimitiveComponent* Comp = GetComponentFromActor(Actor);
            if (!Comp || (IgnoreActor && Comp->GetOwner() == IgnoreActor)) return PxQueryHitType::eNONE;
            const PxFilterData F = Shape ? Shape->getQueryFilterData() : PxFilterData();
            return (ObjectTypeMask & (1u << F.word0)) ? PxQueryHitType::eTOUCH : PxQueryHitType::eNONE; // ★eTOUCH
        }
        PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override {
            return PxQueryHitType::eTOUCH;
        }
    } Filter;
    Filter.IgnoreActor = IgnoreActor; Filter.ObjectTypeMask = ObjectTypeMask;

    constexpr PxU32 kMaxTouch = 64;
    PxOverlapHit HitBuf[kMaxTouch];
    PxOverlapBuffer Buf(HitBuf, kMaxTouch);
    {
        PHYSX_SCENE_READ_LOCK(Scene);
        Scene->overlap(PxSphereGeometry(Radius), PxTransform(ToPxVec3(Center)), Buf,
            PxQueryFilterData(PxQueryFlag::eDYNAMIC | PxQueryFlag::eSTATIC | PxQueryFlag::ePREFILTER),
            &Filter);
    }
    if (Buf.getNbTouches() >= kMaxTouch)
        UE_LOG("[OverlapSphere] touch buffer saturated (%u) — results truncated", kMaxTouch); // silent cap 금지

    // 컴포넌트 단위 수집 (액터 dedup 은 호출부에서)
    for (PxU32 i = 0; i < Buf.getNbTouches(); ++i) {
        if (UPrimitiveComponent* Comp = GetComponentFromActor(Buf.getTouch(i).actor)) {
            FOverlapResult R; R.OverlapComponent = Comp; R.OverlapActor = Comp->GetOwner();
            OutOverlaps.push_back(R);
        }
    }
    return static_cast<int32>(OutOverlaps.size());
}
```
- `ToPxVec3`/`IsTriggerShape`/`GetComponentFromActor`/`PHYSX_SCENE_READ_LOCK` 전부 기존 자산.
- 버퍼 상한(64)은 silent cap 금지 원칙에 따라 초과 시 로그.

### A-3. [신설] UWorld 래퍼
[World.cpp:243](KraftonEngine/Source/Engine/GameFramework/World.cpp:243) `PhysicsSphereSweepShapeComponents` 뒤:
```cpp
int32 UWorld::PhysicsOverlapSphere(const FVector& Center, float Radius, uint32 ObjectTypeMask,
    TArray<FOverlapResult>& Out, const AActor* IgnoreActor) const
{
    return PhysicsScene ? PhysicsScene->OverlapSphere(Center, Radius, ObjectTypeMask, Out, IgnoreActor) : 0;
}
```
(선언은 World.h 의 `PhysicsSphereSweepShapeComponents` 옆.)

### A-4. `AnimNotifyState_AttackHitWindow::NotifyTick` 재작성
교체 대상: [:222~354](KraftonEngine/Source/Engine/Animation/Notify/AnimNotifyState_AttackHitWindow.cpp:222) 의 `for (AActor* Candidate : World->GetActors())` **후보 수집 루프 전체**. 그 외(Center/Radius 산출, 디버그 sphere, tag/dedup/효과/로그)는 보존.

신규 흐름:
```
Center, Radius 동일 산출 (GetHitCenter)
mask = MakeObjectTypeMask()   // 아래 결정 §6.2. 기본: WorldDynamic|Pawn, bHitWorldStatic 이면 WorldStatic 추가
TArray<FOverlapResult> Overlaps;
World->PhysicsOverlapSphere(Center, Radius, mask, Overlaps, Owner);   // Owner 자동 제외
// 액터 단위 환원
for (const FOverlapResult& O : Overlaps) {
    AActor* Candidate = O.OverlapActor;
    if (!Candidate || Candidate == Owner) continue;
    // ─ 이하 전부 기존 로직 그대로 ─
    tag 필터(bRequireTargetActorTag/TargetActorTag)
    if (HitActors.contains(Candidate)) continue;     // 윈도우당 dedup
    HitActors.insert(Candidate);
    LuaScript->DispatchOverlap(Owner);
    ApplyHitStop(Owner) / ApplyHitStop(Candidate)
    if (bApplyKnockback) ApplyKnockback(...)
    로그/디버그
}
```

**기존 → 신규 매핑**
| 기존 동작 | 신규 |
|---|---|
| `GetActors()` 전수 + `DistanceSquaredPointAABB` | `PhysicsOverlapSphere` (PhysX broad+narrow) |
| `GetWorldBoundingBox()` (루즈 AABB) | **실제 query shape** 형상 기준 |
| `bHitWorldStatic` 분기 | ObjectTypeMask 에서 WorldStatic 비트 on/off |
| `bRequireQueryCollision` + `IsQueryCollisionEnabled()` | overlap 이 query shape 만 보므로 사실상 항상 충족 → 플래그 **deprecate 후보** |
| `Candidate == Owner` skip | `IgnoreActor = Owner` |
| tag / HitActors / DispatchOverlap / HitStop / Knockback / 디버그 sphere | **그대로 유지** |

### A-5. 동작 변화 / 리스크 (반드시 검토)
1. **루즈 AABB → 실제 query shape**: 지금은 PhysX 셰이프가 없어도 bounding box 로 맞았음. 전환 후엔 **대상이 PhysX query shape 를 보유**해야 히트됨 → 대상의 `CollisionEnabled ∈ {QueryOnly, QueryAndPhysics}` + BeginPlay 시 `RegisterComponent` 등록 필요. **마이그레이션**: "HitTarget" 액터들의 쿼리 충돌 설정 점검.
2. **BeginPlay 의존**: PhysX 바디는 BeginPlay 에서 등록됨. anim notify 는 게임 재생 중 동작하므로 정상이나, 에디터 PreviewWorld(BeginPlay 미호출)에서는 overlap 0.
3. **버퍼 상한 64**: 동시 타격 대상이 매우 많으면 잘림 → 로그 노출(위).
4. **단위 일관성(G9)**: 본 위치(Center)와 PhysX shape 가 동일 월드 단위여야 함 — 같은 월드 트랜스폼에서 바디 생성되므로 일관 가정(§6.5 확인).

---

## 3. 변경 B — 중력 → PhysX 소스 (적분 유지)

### B-1. [신설] `IPhysicsScene::GetGravity`
[IPhysicsScene.h](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h) 에 추가:
```cpp
virtual FVector GetGravity() const = 0;   // 씬 중력 가속도(m/s^2). 기본 (0,0,-9.81).
```
`FPhysXRuntime`:
```cpp
FVector FPhysXRuntime::GetGravity() const {
    if (!Scene) return FVector(0.0f, 0.0f, -9.81f);
    PHYSX_SCENE_READ_LOCK(Scene);
    return ToFVector(Scene->getGravity());   // PhysXHelpers 역변환
}
```
UWorld 래퍼:
```cpp
FVector UWorld::GetGravity() const {
    return PhysicsScene ? PhysicsScene->GetGravity() : FVector(0.0f, 0.0f, -9.81f);
}
```

### B-2. `CharacterMovementComponent::TickFalling` 변경
[:286](KraftonEngine/Source/Engine/Component/Movement/CharacterMovementComponent.cpp:286) 한 줄 교체:
```cpp
// 기존: Velocity.Z -= Gravity * DeltaTime;
const FVector G = Owner->GetWorld()->GetGravity();          // 기본 (0,0,-9.81)
Velocity.Z += G.Z * GravityScale * DeltaTime;               // G.Z 가 음수 → += 로 하강(부호 일치)
```
- 현재 설계가 **Z 성분만 중력**(XY 는 입력/floor 책임)이므로 **Z 성분만** 반영 → 이동 적분 구조 보존.
- 위치 적분(`SetWorldLocation`, :293), floor stick, `TickWalking`, `TraceFloor` : **무변경.**

### B-3. `Gravity` 멤버 처리 (결정 §6.4)
- **(권장)** `float Gravity=9.8` 멤버를 `float GravityScale=1.0` 로 교체 → 씬 중력 × 스케일. `Serialize`(:355)의 `Ar << Gravity` → `Ar << GravityScale`. (기존 자산 역직렬화 호환: 버전 가드 또는 일회 마이그레이션.)
- **(대안)** `Gravity` 유지하되 미사용 처리(deprecate), 씬 값만 사용.
- 폴백: `World`/`PhysicsScene` null → `(0,0,-9.81)` 상수(위 래퍼가 처리).

### B-4. 부호/단위 점검
- 멤버 9.8 vs 씬 9.81 → 거의 동일, 체감 거동 변화 미미.
- 이동/본 공간이 m 단위라는 가정 확인 필요(§6.5). m 이면 그대로, cm 면 스케일 보정.

---

## 4. 이동 — 변경 없음 (명시)

`TickComponent` 전체 골격, `ApplyInputToVelocity`, `TickWalking`(floor stick), `PhysOrientToMovement`, 루트모션 합성/소비, `TraceFloor`(PhysX raycast 바닥 감지) — **전부 유지**. 즉 "이동 = 커스텀 위치 적분" 보존, **중력의 소스만** PhysX 로 단일화.

---

## 5. 영향 파일 & 작업 순서

| # | 파일 | 작업 |
|---|---|---|
| 1 | `Core/Types/CollisionTypes.h` | `FOverlapResult` 이미 존재 — **확인만** |
| 2 | `Physics/IPhysicsScene.h` | 순수가상 `OverlapSphere`, `GetGravity` 2개 선언 |
| 3 | `Physics/PhysXRuntime.h/.cpp` | 두 override 구현(§A-2, §B-1) |
| 4 | `GameFramework/World.h/.cpp` | `PhysicsOverlapSphere`, `GetGravity` 래퍼 |
| 5 | `Component/Movement/CharacterMovementComponent.h/.cpp` | 중력 소스 교체(+`GravityScale` 결정), `Serialize` |
| 6 | `Animation/Notify/AnimNotifyState_AttackHitWindow.cpp` (+`.h`) | `NotifyTick` 후보수집부 교체, `bRequireQueryCollision`/`bHitWorldStatic` 의미 갱신 |

**순서**: 2 → 3 → 4 (쿼리/중력 인프라 먼저, 각 단계 컴파일) → 6(전투) · 5(중력) 병렬. 인프라가 순수가상 추가라 3을 안 하면 빌드가 깨지므로 2·3·4 를 한 묶음으로.

---

## 6. 결정/확인 필요 (착수 전 컨펌)

1. **전투 쿼리 종류**: `overlap`(순간·다중) 채택 — 현재 per-tick 순간판정과 일치. `sweep`(궤적) 불필요. ✅ 맞는지?
2. **ObjectTypeMask 기본값**: `WorldDynamic | Pawn`(적), `bHitWorldStatic`=true 면 `WorldStatic` 추가. → **`ECollisionChannel` 에 Pawn 채널 존재 여부 확정** 필요(현재 확인된 값: WorldStatic=0, WorldDynamic=1, …).
3. **AABB 폴백 유지?**: PhysX 미등록 대상도 맞히던 기존 동작 일부 보존(하이브리드) vs 완전 PhysX 전환. → **완전 전환 권장**(단일 경로·명확).
4. **중력 파라미터**: `GravityScale` 신설(권장) vs `Gravity` 멤버 유지. 자산 직렬화 호환 정책 동반.
5. **단위(G9)**: 본/이동 월드 공간이 m 인지 최종 확인(전수조사의 lurking risk). 전투 Radius=60 / Offset=25 가 cm 처럼 보이는 점과 교차검증.

---

## 7. 검증 계획

- **전투**: 쿼리콜리전 ON 더미("HitTarget") 단일/다중 배치 → 공격 → (a) 다중 동시 히트, (b) 윈도우당 dedup, (c) `DispatchOverlap`(Lua `OnHit`)·HitStop·Knockback 발화, (d) 디버그 sphere ↔ 실제 히트 일치. PhysX 셰이프 없는 대상이 더 이상 안 맞는지(의도된 변화) 확인.
- **중력**: 낙하 가속도 ≈ 기존(9.8) 유지, 씬 중력 변경 시 캐릭터도 추종(단일 소스), 점프 arc/착지 정상.
- **회귀(무영향 확인)**: floor stick, 벽 통과(미구현 유지), 루트모션, yaw orient, 에디터 래그돌 Simulate.

---

## 부록. 한눈에

```
[전투]  AnimNotify NotifyTick
  기존: World.GetActors() 전수 + 점↔AABB 거리         (비-PhysX, O(N·M))
  신규: World.PhysicsOverlapSphere(Center,R,mask,Owner) (PhysX overlap, broad+narrow)
        → 액터 dedup → tag → DispatchOverlap/HitStop/Knockback (그대로)

[중력]  CharacterMovement TickFalling
  기존: Velocity.Z -= Gravity(9.8) * dt                (하드코딩)
  신규: Velocity.Z += World.GetGravity().Z * GravityScale * dt  (PhysX 씬 = 단일 소스)
        → SetWorldLocation 위치 적분은 그대로

[이동]  무변경 (위치 적분 / floor stick raycast / 입력 / 루트모션 보존)
```
