# 02. D6 joint 이론 (가장 깊게)

> 라인 번호는 확인 시점(2026-06-01) 스냅샷. 심볼명으로 재확인. 좌표 규약은 [06](06_coordinate_math.md), 프레임 산정은 [03](03_anchor_frames.md) 참조.

---

## 1. 이론

### 1.1 6자유도(6DOF) 구속
두 강체를 잇는 가장 일반적인 조인트가 PhysX의 **D6 joint**다. 두 actor 각각에 **조인트 프레임(joint frame)**을 박고, 그 두 프레임이 서로에 대해 가질 수 있는 6개 자유도를 축별로 제어한다.

- **3 linear**: 프레임 X/Y/Z 방향 **병진**.
- **3 angular**:
  - **twist** = 프레임 **X축 둘레 회전**(비틀기).
  - **swing1** = **Y축** 둘레 회전.
  - **swing2** = **Z축** 둘레 회전.

각 축은 세 모드 중 하나:
- **Locked**: 자유도 0 (완전 고정).
- **Limited**: 일정 범위 내에서만 허용(한계각/한계거리). → [04](04_angular_limits.md).
- **Free**: 무제한.

이 6개를 조합하면 ball-socket, hinge, prismatic, 고정 등 거의 모든 관절을 표현할 수 있다. → [04](04_angular_limits.md)의 프리셋.

### 1.2 twist vs swing의 기하
조인트 프레임 X축을 "뼈 축"이라 하면:
- **twist**: 뼈 축을 중심으로 한 회전(팔뚝을 비트는 것).
- **swing1/swing2**: 뼈 축이 원뿔(cone) 안에서 꺾이는 것. swing1·swing2의 한계각이 원뿔의 Y/Z 반각을 이룬다(타원뿔). → [04](04_angular_limits.md).

### 1.3 constraint solver가 조인트를 푸는 방식 (개념)
강체 시뮬레이션은 매 스텝:
1. **적분(predict)**: 외력(중력 등)으로 속도·위치를 임시 전진.
2. **constraint 위반 측정**: 각 조인트/접촉에서 위치·속도 오차(error)를 계산(예: 두 프레임이 벌어진 정도).
3. **임펄스 반복(iterative solver)**: 오차를 줄이는 **임펄스(impulse)**를 축마다 계산해 속도에 누적, 이를 여러 번(iteration) 반복해 모든 구속을 근사적으로 동시에 만족. PhysX 기본은 PGS(projected Gauss–Seidel) 계열 + 위치 보정(bias).
4. **확정(integrate)**: 보정된 속도로 위치 갱신.

→ "정확해"가 아니라 **반복 근사**이므로, iteration 수·질량비·한계 설정에 따라 떨림/늘어짐이 생긴다. 이 엔진은 solver iteration을 따로 지정하지 않아 **PhysX 기본값**을 쓴다(아래 2.4).

---

## 2. 코드 대조

### 2.1 모션 모드 enum의 2단 매핑
편집 자산은 `EConstraintMotion`, 런타임 디스크립터는 `EPhysicsMotionType`, PhysX는 `PxD6Motion`. 세 enum은 의미가 1:1로 일치한다.

| 자산 `EConstraintMotion` | → 런타임 `EPhysicsMotionType` | → PhysX `PxD6Motion` |
|---|---|---|
| `Locked` (=0) | `Locked` | `eLOCKED` |
| `Limited` (=1) | `Limited` | `eLIMITED` |
| `Free` (=2) | `Free` | `eFREE` |

- 1단계: `ToPhysicsMotion(EConstraintMotion)` (`SkeletalMeshComponent.cpp:40`).
- 2단계: `PhysXHelpers::ToPxD6Motion(EPhysicsMotionType)` (`PhysXHelpers.h:38`).
- 정의: `EConstraintMotion`(`PhysicsConstraintSetup.h:11`), `EPhysicsMotionType`(`PhysicsTypes.h:29`).

### 2.2 축 ↔ PhysX D6 축 매핑
UI 라벨 (`MeshEditorWidget.Physics.cpp:755`): `Twist (X)`, `Swing1 (Y)`, `Swing2 (Z)`. 런타임 set 호출과 정확히 대응한다:

`FPhysXRuntime::CreateD6Joint` (`PhysXRuntime.cpp:904`):
```cpp
PxD6Joint* Joint = PxD6JointCreate(*Physics,
    ParentActor, ToPxTransform(Desc.ParentLocalFrame),   // 부모 프레임 (03 문서)
    ChildActor,  ToPxTransform(Desc.ChildLocalFrame));   // 자식 프레임
Joint->setMotion(PxD6Axis::eX,      ToPxD6Motion(Desc.LinearX));
Joint->setMotion(PxD6Axis::eY,      ToPxD6Motion(Desc.LinearY));
Joint->setMotion(PxD6Axis::eZ,      ToPxD6Motion(Desc.LinearZ));
Joint->setMotion(PxD6Axis::eTWIST,  ToPxD6Motion(Desc.Twist));     // X 둘레
Joint->setMotion(PxD6Axis::eSWING1, ToPxD6Motion(Desc.Swing1));    // Y 둘레
Joint->setMotion(PxD6Axis::eSWING2, ToPxD6Motion(Desc.Swing2));    // Z 둘레
Joint->setTwistLimit(PxJointAngularLimitPair(Desc.TwistLimitRadiansMin, Desc.TwistLimitRadiansMax));
Joint->setSwingLimit(PxJointLimitCone(Desc.Swing1LimitRadians, Desc.Swing2LimitRadians));
```
- 인터페이스 선언: `IPhysicsScene::CreateD6Joint` (`IPhysicsScene.h:32`), 구현은 `FPhysXRuntime`(메모리: 정식 인터페이스는 `IPhysicsScene`).
- 6축 모두 명시적으로 `setMotion` 호출 → 디폴트에 의존하지 않음.

### 2.3 linear 축은 한 덩어리로 제어
자산엔 선형 축별 모드가 없고 `bLockLinearMotion`(bool) 하나뿐. 인스턴스화에서 X/Y/Z에 일괄 적용 (`SkeletalMeshComponent.cpp:864`):
```cpp
const EPhysicsMotionType LinearMotion = Setup->bLockLinearMotion ? EPhysicsMotionType::Locked : EPhysicsMotionType::Free;
ConstraintDesc.LinearX = ConstraintDesc.LinearY = ConstraintDesc.LinearZ = LinearMotion;
```
- 즉 선형은 **전부 Lock**(피벗 고정, 일반 관절) **또는 전부 Free**(슬라이드 허용, prismatic류). 부분 잠금·선형 한계거리는 자산 스키마에 없다 → [07](07_glossary_and_gaps.md).

### 2.4 solver / breakable
- **solver iteration**: `setSolverIterationCounts` 호출이 코드에 없음 → PhysX 기본 iteration 사용. scene 플래그는 `eENABLE_PCM`(접촉 매니폴드), `eENABLE_CCD`(연속충돌), `eENABLE_STABILIZATION`은 없음 (`PhysXCore.cpp:197~202`). → [05](05_runtime_and_collision.md).
- **breakable joint**: 디스크립터에 `bBreakable/BreakForce/BreakTorque` 필드가 있고 `setBreakForce`까지 구현돼 있으나(`PhysXRuntime.cpp:941`), 래그돌 인스턴스화 경로는 이를 설정하지 않는다(`FPhysicsConstraintDesc` 기본 `bBreakable=false`) → 래그돌 조인트는 안 부러진다.
- **drive(모터/스프링)**: `PxD6JointDrive`/`setDrive` 호출 **없음** → 조인트 구동(능동 토크)·소프트 복원력 미사용. → [07](07_glossary_and_gaps.md).

### 2.5 래퍼 객체
조인트 성공 시 `FConstraintInstance`로 감싸 보관 (`PhysXRuntime.cpp:946`): `ConstraintName/ParentBody/ChildBody/JointHandle{PxD6Joint*}/Desc/bValid`. 컴포넌트는 `Constraints` 배열에 push (`SkeletalMeshComponent.cpp:888`).

### 2.6 등장 객체 매핑
| 엔진 | PhysX |
|---|---|
| `UPhysicsConstraintSetup` | (자산) |
| `FPhysicsConstraintDesc` | (디스크립터) |
| `FConstraintInstance` | `PxD6Joint` 래퍼 |
| `Desc.Twist/Swing1/Swing2` | `eTWIST/eSWING1/eSWING2` |
| `TwistLimitRadiansMin/Max` | `PxJointAngularLimitPair` |
| `Swing1/2LimitRadians` | `PxJointLimitCone` |
