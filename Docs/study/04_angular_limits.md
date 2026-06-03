# 04. 각 한계와 모션 모드

> 라인 번호는 확인 시점(2026-06-03) 스냅샷. 심볼명으로 재확인. 축 정의·enum 매핑은 [02](02_d6_joint_theory.md) 참조.
> 에디터 물리 코드는 `MeshEditorWidget.Physics.cpp` → `PhysicsEditorWidget.cpp`로 분리됐다([00](00_overview.md) 규칙4).

---

## 1. 이론

### 1.1 swing cone 한계
조인트 프레임 X축(뼈 축)이 꺾일 수 있는 범위를 **원뿔(cone)**로 제한한다. swing1(Y 둘레)·swing2(Z 둘레) 두 한계각이 원뿔의 두 반각이며, 같으면 정원뿔, 다르면 **타원뿔**이다. X축이 이 원뿔 밖으로 못 나간다.

### 1.2 twist 한계
뼈 축 둘레 비틀기. 보통 **하한~상한**(min~max)으로 준다. 대칭(±θ)일 수도, 비대칭(예: 0~90°)일 수도 있다.

### 1.3 deg ↔ rad, ± 대칭
사람이 편집하는 값은 도(degree), PhysX API는 라디안. 변환 지점에서 `rad = deg · π/180`. twist를 `±θ`로 대칭화하려면 `min = -θ_rad`, `max = +θ_rad`.

### 1.4 hard vs soft limit
- **hard limit**: 한계에서 즉시 막음(벽).
- **soft limit**: 스프링(stiffness/damping)으로 한계 부근에서 부드럽게 되민다.
PhysX의 `PxJointLimitCone`/`PxJointAngularLimitPair`는 스프링 파라미터를 주면 soft, 안 주면 hard다.

---

## 2. 코드 대조

### 2.1 자산의 한계 필드
`UPhysicsConstraintSetup` (`PhysicsConstraintSetup.h:26~41`):
```cpp
EConstraintMotion TwistMotion  = Limited;  float TwistLimitAngle  = 45.f;   // :32,:33 degree
EConstraintMotion Swing1Motion = Limited;  float Swing1LimitAngle = 45.f;   // :35,:36
EConstraintMotion Swing2Motion = Limited;  float Swing2LimitAngle = 45.f;   // :38,:39
bool bLockLinearMotion = true;                                              // :41
```
- 한계각은 **도(degree)** 단위, 모드가 `Limited`일 때만 의미. UI 슬라이더 범위 0~180° (`PhysicsEditorWidget.cpp:773`, `RenderAxis` 람다 — 슬라이더는 `Limited`일 때만 노출), 라벨 `Twist (X) / Swing1 (Y) / Swing2 (Z)`(`:776~778`).

### 2.2 deg→rad 변환과 twist ± 대칭화 (인스턴스화)
`InstantiatePhysicsAssetBodies` (`SkeletalMeshComponent.cpp:964~970`):
```cpp
ConstraintDesc.Twist  = ToPhysicsMotion(Setup->TwistMotion);
ConstraintDesc.Swing1 = ToPhysicsMotion(Setup->Swing1Motion);
ConstraintDesc.Swing2 = ToPhysicsMotion(Setup->Swing2Motion);
ConstraintDesc.TwistLimitRadiansMin = -DegreesToRadians(Setup->TwistLimitAngle);   // ★ -θ
ConstraintDesc.TwistLimitRadiansMax =  DegreesToRadians(Setup->TwistLimitAngle);   // ★ +θ  → 대칭
ConstraintDesc.Swing1LimitRadians   =  DegreesToRadians(Setup->Swing1LimitAngle);
ConstraintDesc.Swing2LimitRadians   =  DegreesToRadians(Setup->Swing2LimitAngle);
```
- `DegreesToRadians` = `Degrees * PhysicsPi / 180` (`SkeletalMeshComponent.cpp:40`).
- **twist는 항상 ±대칭**(단일 슬라이더 → min=-θ, max=+θ). 비대칭 twist는 자산 스키마상 표현 불가(min/max를 따로 못 줌) → [07](07_glossary_and_gaps.md).
- swing은 단일 한계각 → 원뿔 반각.

### 2.3 PhysX limit set
`FPhysXRuntime::CreateD6Joint` (`PhysXRuntime.cpp:1684~1685`):
```cpp
Joint->setTwistLimit(PxJointAngularLimitPair(Desc.TwistLimitRadiansMin, Desc.TwistLimitRadiansMax));
Joint->setSwingLimit(PxJointLimitCone(Desc.Swing1LimitRadians, Desc.Swing2LimitRadians));
```
- `PxJointAngularLimitPair(lower, upper)` ← twist min/max.
- `PxJointLimitCone(yAngle, zAngle)` ← (Swing1, Swing2) = 타원뿔 반각.
- **스프링 파라미터를 주지 않음 → 모두 hard limit**. soft(스프링) 한계 미사용. → [07](07_glossary_and_gaps.md).
- 한계는 모드와 무관하게 항상 set되지만, **PhysX는 해당 축이 `eLIMITED`일 때만 한계를 적용**한다(`eFREE`/`eLOCKED`면 무시). 즉 모드가 실제 on/off 스위치.

### 2.4 Ball / Hinge / Prismatic 프리셋의 의미
편집 툴바 `SetMotions(Twist, Swing1, Swing2, bLockLinear)` 람다 (`PhysicsEditorWidget.cpp:807`):

| 프리셋 | Twist | Swing1 | Swing2 | bLockLinear | 물리적 의미 |
|---|---|---|---|---|---|
| **Ball & Socket** (`:818`) | Free | Free | Free | true | 모든 회전 자유, 병진 고정 = 어깨/엉덩이 관절 |
| **Hinge** (`:821`) | Free | Locked | Locked | true | 한 축(twist)만 회전 = 팔꿈치/무릎 경첩 |
| **Prismatic** (`:824`) | Locked | Locked | Locked | **false** | 회전 전부 잠금 + **병진 자유** = 직선 슬라이더 |

- 회전 자유도(twist/swing)와 선형 잠금(`bLockLinearMotion`)의 조합으로 관절 종류가 결정된다([02](02_d6_joint_theory.md) 2.3).
- 한계각을 쓰려면 해당 축을 `Limited`로(프리셋은 Free/Locked만 설정하므로 한계각은 별도 슬라이더에서).

### 2.5 자동생성의 기본 한계
`GeneratePhysicsBodies` (`PhysicsEditorWidget.cpp:1338~1343`):
```cpp
const EConstraintMotion M = (EConstraintMotion)S.AngularConstraintMode;   // :1338 일괄 같은 모드
CS->TwistMotion = M; CS->Swing1Motion = M; CS->Swing2Motion = M;          // :1339
CS->TwistLimitAngle = CS->Swing1LimitAngle = CS->Swing2LimitAngle = S.DefaultAngularLimitDeg;   // :1341~1343
// bLockLinearMotion = 기본값(true) 유지
```
- 자동생성은 **세 회전축에 같은 모드·같은 한계각**을 부여하고 선형은 기본 잠금. 세부 튜닝은 이후 에디터에서 축별로 조정.

### 2.6 (참고) 한계 디버그 시각화
에디터는 swing 원뿔/twist 호를 와이어로 그린다. 두 함수로 나뉘어 있다: 히트테스트 `PickPhysicsAtScreen`(`PhysicsEditorWidget.cpp:1778`; `S1`/`S2` 계산 `:1855~1856`)와 실제 드로우 `DrawConstraintLimitsOverlay`(`:2053`; `S1`/`S2` `:2120~2121`). 예: `S1 = (Swing1Motion==Free ? 90 : Swing1LimitAngle) * D2R`처럼 **Free면 90°로 그려** 자유를 표현. 시뮬레이션엔 영향 없는 표시용.
