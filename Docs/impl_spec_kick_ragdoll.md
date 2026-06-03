# 구현 명세서 — 히트 시 피충돌 Skeletal 자동 래그돌 (범용)

> 상태: **§2 C++ 구현 완료(빌드 통과).** 남은 것은 콘텐츠(§3 노티파이 배치) + 대상 셋업(§4). 브랜치 `feature/joint`.
> 구현값: 수평 5.0 m/s + 수직 2.5 m/s(≈3m 비행), 방향 `AwayFromAttacker`(위치기반), owner 기준, 본 미지정. PhysicsAsset 은 사용자가 직접 부여.
> 목표: 공격 active window 동안 **scene overlap 쿼리로 잡힌 피충돌 component 의 owner 가 skeletal 이면** 그 메시를 `SetSimulatingPhysics(true)` + 발사 임펄스로 래그돌 전환. **발차기 전용 함수·본 이름 지정·대상별 Lua·전용 태그 전부 없음.**
> 방식 결정: **쿼리 방식(엔진 변경 0)** — 기존 overlap 노티파이의 매 프레임 쿼리 결과를 "충돌 판정"으로 사용.
> 선행: [impl_spec_physx_combat_gravity.md](impl_spec_physx_combat_gravity.md), [survey_collision_target_components.md](survey_collision_target_components.md).

---

## 0. 범위 / 비목표

**목표**
1. 기존 `UAnimNotifyState_AttackHitWindow` 에 **범용 래그돌 분기 1개** 추가 — 히트 처리에서 "피충돌 owner 가 skeletal 이면 `SetSimulatingPhysics(true)` + impulse".
2. 공격 montage(발차기 포함 아무 공격)의 타격 구간에 이 노티파이를 얹기. **본 지정/태그 강제 없음**(필요 시 옵션).

**비목표(명시)**
- 발차기 전용 C++ 함수, 발 본 이름 강제, 대상별 Lua `OnOverlap` 작성, `"HitTarget"` 태그 강제 — **전부 안 함**.
- 새 충돌/이벤트 시스템 안 만듦. 진짜 물리 overlap 이벤트(`OnComponentBeginOverlap`) 경로는 **이번 방식 아님**(static-static 미보고 제약 → scene 플래그 변경 필요해서 제외).
- 공격자 캡슐을 PhysX 강체로 만들지 않음(이동 커스텀 적분 유지). 래그돌→애니 복귀(get-up)는 비목표(단방향).

---

## 1. 현재 상태 (근거) — 이미 있는 것

### 1.1 히트 쿼리 파이프라인 (그대로 재사용)
[AnimNotifyState_AttackHitWindow.cpp](../KraftonEngine/Source/Engine/Animation/Notify/AnimNotifyState_AttackHitWindow.cpp) — `NotifyTick` [:178]:
- `Center` = 본 위치 + offset, **`BoneName` 비면 액터 중심으로 자동 폴백** [:56](../KraftonEngine/Source/Engine/Animation/Notify/AnimNotifyState_AttackHitWindow.cpp:56) (`FindBoneIndex` 가 빈 이름 → -1 [:21]). → **본 지정은 선택**.
- `World->PhysicsOverlapSphere(Center, Radius, mask, Overlaps, Owner)` [:210] — Owner(공격자) 자동 제외, query shape 보유 대상만 수집.
- 액터 dedup + 태그 필터(`bRequireTargetActorTag`, **false 면 태그 무관 전체 통과** [:229]) + 윈도우당 1회 dedup [:242].
- 히트 확정 블록 [:247-258]: `DispatchOverlap`(대상 Lua `OnOverlap`) / `ApplyHitStop` / `bApplyKnockback` 시 `ApplyKnockback`. **← 여기에 분기 1개만 추가.**

### 1.2 래그돌 API (그대로 호출)
- `USkeletalMeshComponent::SetSimulatingPhysics(true)` [SkeletalMeshComponent.cpp:1222](../KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:1222): 바디 자동 인스턴스화 + Dynamic 전환, **idempotent**. 전제: `GetWorld()->GetPhysicsScene()` + `HasPhysicsAsset()`.
- `GetPhysicsBodies()` [SkeletalMeshComponent.h:118](../KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h:118), `IPhysicsScene::SetBodyLinearVelocity(FBodyInstance*, FVector)` [IPhysicsScene.h:81](../KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:81)(Dynamic 만 적용).
- 방향 헬퍼 `ResolveKnockbackDirection(Attacker, Target, Mode)` [.cpp:79] 재사용.

### 1.3 왜 쿼리 결과를 "충돌"로 쓰는가
PhysX 는 **static-static 쌍의 contact/trigger 를 생성하지 않음** + 씬에 kinematic-pair 플래그 없음 → 애니메이션 중 캐릭터(Static 캡슐)끼리는 **자동 충돌 이벤트가 발생하지 않는다.** overlap 쿼리는 static/dynamic 무관하게 동작하므로, 쿼리 결과가 곧 "충돌 판정". (전투 히트가 이미 이 경로.)

---

## 2. 변경 — `AttackHitWindow` 에 범용 래그돌 분기 (핵심, 거의 전부)

### 2.1 [신설] 필드 — `AnimNotifyState_AttackHitWindow.h`
```cpp
UPROPERTY(Edit, Save, Category="AttackHitWindow|Ragdoll", DisplayName="Enable Ragdoll On Hit")
bool bEnableRagdollOnHit = false;                 // 기본 off → 기존 공격 무변경

UPROPERTY(Edit, Save, Category="AttackHitWindow|Ragdoll", DisplayName="Ragdoll Launch Mode", Enum=EAttackKnockbackMode)
EAttackKnockbackMode RagdollLaunchMode = EAttackKnockbackMode::AwayFromAttacker;

UPROPERTY(Edit, Save, Category="AttackHitWindow|Ragdoll", DisplayName="Ragdoll Launch Speed", Min=0.0f, Max=2000.0f, Speed=1.0f)
float RagdollLaunchSpeed = 300.0f;                // 단위 §5 확인

UPROPERTY(Edit, Save, Category="AttackHitWindow|Ragdoll", DisplayName="Ragdoll Up Bias", Min=0.0f, Max=1000.0f, Speed=1.0f)
float RagdollUpBias = 120.0f;
```

### 2.2 [신설] 범용 헬퍼 + 히트블록 1줄 — `AnimNotifyState_AttackHitWindow.cpp`
익명 namespace(ApplyKnockback 근처)에 추가 — **"피충돌이 skeletal 이면" 판정이 핵심**:
```cpp
void ApplyRagdollLaunch(AActor* Attacker, AActor* Target,
    EAttackKnockbackMode Mode, float Speed, float UpBias)
{
    if (!Target) return;

    // 피충돌 owner 가 skeletal 인가? (아니면 그냥 무시 → 범용)
    USkeletalMeshComponent* Mesh = Target->GetComponentByClass<USkeletalMeshComponent>();
    if (!Mesh || !Mesh->HasPhysicsAsset() || Mesh->IsSimulatingPhysics()) return;

    UWorld* World = Mesh->GetWorld();
    IPhysicsScene* Scene = World ? World->GetPhysicsScene() : nullptr;
    if (!Scene) return;

    Mesh->SetSimulatingPhysics(true);                       // ragdoll 진입
    const FVector Launch = ResolveKnockbackDirection(Attacker, Target, Mode) * Speed
                         + FVector::UpVector * UpBias;       // impulse
    for (FBodyInstance* Body : Mesh->GetPhysicsBodies())
        if (Body && Body->bValid) Scene->SetBodyLinearVelocity(Body, Launch);
}
```
히트 확정 블록 [:247](../KraftonEngine/Source/Engine/Animation/Notify/AnimNotifyState_AttackHitWindow.cpp:247) 끝(`ApplyKnockback` 호출 뒤)에 1줄:
```cpp
if (bEnableRagdollOnHit)
    ApplyRagdollLaunch(Owner, Candidate, RagdollLaunchMode, RagdollLaunchSpeed, RagdollUpBias);
```
include: `Component/Primitive/SkeletalMeshComponent.h`, `Physics/IPhysicsScene.h`(World.h·SkeletalMeshComponent.h 는 기존 포함).

> 피충돌 component 를 더 정확히 쓰려면 `Overlap.OverlapComponent` 를 헬퍼에 넘겨 `Cast<USkeletalMeshComponent>` 시도 후, 실패 시 owner 의 `GetComponentByClass` 폴백도 가능. 위는 owner 기준(가장 단순).

### 2.3 동작 주의
- **넉백 ↔ 래그돌**: 래그돌 진입이 대상 movement 를 끄므로 넉백(캡슐 이동)이 상쇄됨 → 노티파이에서 `bApplyKnockback=false` + 래그돌 impulse 로 대체 권장.
- **idempotent**: 윈도우당 dedup + `SetSimulatingPhysics`/`IsSimulatingPhysics` 가드 → 1회만 진입.
- 임펄스는 전 바디 동일 속도(균일 발사). `SetBodyLinearVelocity` 는 절대값 설정이라 진입 관성 위에 덮어씀(발사 우선, 의도된 동작).

---

## 3. 노티파이 배치 (콘텐츠, 최소 설정)

공격 montage(발차기든 무엇이든) 타격 구간에 `AnimNotifyState_AttackHitWindow` 추가:

| 필드 | 값 | 비고 |
|---|---|---|
| `bEnableRagdollOnHit` | **true** | ★ 유일한 필수 |
| `BoneName` | **빈 값** | 비우면 액터 중심 기준(폴백). 발 정밀도 필요 시에만 본 입력 |
| `Radius` | 리치에 맞춰(40~80) | |
| `bRequireTargetActorTag` | **false** | 태그 무관 — 모든 skeletal 대상에 발동 |
| `bApplyKnockback` | **false** | §2.3 |
| `RagdollLaunchMode/Speed/UpBias` | §5 튜닝 | |

→ **발차기 전용 코드/입력 추가 불필요.** 발차기 모션에 이 노티파이를 얹는 것으로 끝(원하면 별도 발차기 입력은 기존 `Anim.play_montage` 패턴으로 추가 가능하나 본 명세 밖).

---

## 4. 불가피한 전제 — 대상 query 콜라이더 (방식 무관 공통)

[survey 문서](survey_collision_target_components.md) 결론: **skeletal 단독 액터는 PhysX 쿼리 씬에 안 보임** → overlap 에 안 잡힘. 대상이 맞으려면:
1. **query shape**: 대상에 `UCapsuleComponent`(또는 Box/Sphere) 부착 → `SetCollisionEnabled(QueryOnly)` + `SetCollisionObjectType(Pawn)`(또는 `WorldDynamic`). 기본 `NoCollision`/`WorldStatic` 이라 **명시 설정 필수**.
2. **PhysicsAsset**: `<Mesh>_Physics.uasset` 없으면 `HasPhysicsAsset()` false → 래그돌 skip(HitStop 만).

(이 두 가지는 진짜 물리 이벤트 방식으로 갔어도 똑같이 필요 — 줄일 수 없음.)

---

## 5. 영향 파일 & 결정 필요

| # | 파일 | 작업 |
|---|---|---|
| 1 | `Animation/Notify/AnimNotifyState_AttackHitWindow.h` | 래그돌 4필드(§2.1) |
| 2 | `Animation/Notify/AnimNotifyState_AttackHitWindow.cpp` | `ApplyRagdollLaunch` + 히트블록 1줄(§2.2) |
| 3 | (콘텐츠) 공격 montage | 노티파이 1개 배치(§3) |
| 4 | (셋업) 대상 액터 | QueryOnly 콜라이더(Pawn) + PhysicsAsset(§4) |

**순서**: 1→2(컴파일·기존 공격 회귀 무영향 확인) → 4(대상 셋업, 기존 검 공격으로 overlap 히트 검증) → 3(노티파이 얹기) → 통합 검증.

**결정/확인**
1. **단위/세기**: `RagdollLaunchSpeed`·`Radius`·`UpBias` 월드 단위(m vs cm) — 한 대상에 적용 후 튜닝([combat spec §6.5](impl_spec_physx_combat_gravity.md) 동일 리스크).
2. **방향 모드**: `AwayFromAttacker`(위치 기반) vs `Forward`(정면) + `UpBias`.
3. **피충돌 정밀도**: owner 기준(단순) vs `OverlapComponent` 기준(§2.2 주석). 보통 owner 로 충분.
4. (선택) 발 정밀 타격이 필요하면 그때만 `BoneName` 입력.

---

## 6. 검증 계획

- **회귀(무영향)**: `bEnableRagdollOnHit=false` 인 기존 검/총 공격 거동 무변경.
- **대상 셋업**: QueryOnly(Pawn) 콜라이더 + PhysicsAsset 더미 → 기존 공격으로 overlap 히트되는지 단독 확인.
- **자동 래그돌**: 공격 active window 에 대상이 sphere 안에 들면 **즉시 래그돌 + 발사**. 윈도우당 1회, idempotent. 디버그 sphere ↔ 실제 히트 일치. 진입 후 바닥(WorldStatic)과 충돌해 자연 낙하.
- **엣지**: PhysicsAsset 없는 대상 → skip(크래시 없음). PreviewWorld(BeginPlay 미호출)에선 overlap 0.

---

## 부록. 한눈에

```
[공격 active window]  AttackHitWindow.NotifyTick
   World.PhysicsOverlapSphere(Center[본 or 액터중심], R, Pawn|WorldDynamic, Owner제외)
      → 피충돌 후보
   ├─ 기존: DispatchOverlap / HitStop / (Knockback)
   └─ 신규(bEnableRagdollOnHit): "owner 가 skeletal?" →
         Target.SetSimulatingPhysics(true)
         for body: Scene.SetBodyLinearVelocity(body, Dir*Speed + Up*Bias)

빠진 것: 발차기 함수 ✗  본 이름 강제 ✗  대상 Lua ✗  전용 태그 ✗  새 충돌시스템 ✗
남은 것: 범용 분기 1개 + 노티파이 1개 + 대상 query 콜라이더(불가피).
```
