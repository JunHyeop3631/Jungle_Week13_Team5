# 03. 앵커 / 조인트 프레임 (이 엔진의 핵심 설계)

> 라인 번호는 확인 시점(2026-06-01) 스냅샷. 심볼명으로 재확인. 행벡터 곱 규약은 [06](06_coordinate_math.md) 필수 선행.

---

## 1. 이론

### 1.1 joint frame과 anchor
D6 조인트는 **두 actor 각각에 로컬 프레임**을 둔다(`ParentLocalFrame`, `ChildLocalFrame`). solver는 "이 두 프레임을 일치시키되 허용된 자유도만 풀어준다"로 동작한다. 따라서:
- 두 프레임의 **원점(pivot)** = 관절이 회전·병진하는 중심.
- 두 프레임의 **X축** = twist 축(= [02](02_d6_joint_theory.md)).
- 생성 시점에 두 프레임이 **월드에서 정확히 겹치면 초기 오차 0** → 래그돌이 튀지 않는다.

### 1.2 pivot 위치가 거동에 미치는 영향
관절 pivot을 어디에 두느냐가 동작을 결정한다.
- pivot을 **자식 본의 뿌리(원점)**에 두면, 자식이 부모에 매달려 그 점을 중심으로 흔들린다(생물학적 관절과 일치).
- pivot을 본 중간/끝에 두면 지렛대가 어긋나 부자연스럽게 보인다.

### 1.3 frame 표현의 일관성 문제
부모·자식은 서로 다른 좌표계(각자 body local)를 갖는다. **같은 월드 점/자세**를 부모 local과 자식 local **양쪽 좌표로** 표현해 넘겨야 한다. 한쪽만 맞으면 조인트가 비틀린 채 시작한다.

---

## 2. 코드 대조

### 2.1 자산은 "부모 앵커" 하나만 저장
`UPhysicsConstraintSetup` (`PhysicsConstraintSetup.h`)에는 **`ParentAnchorPos` + `ParentAnchorRot`만** 있다(자식 앵커 필드 없음). 자식 프레임은 인스턴스화에서 유도한다(2.3).
```cpp
FVector ParentAnchorPos = (0,0,0);
FQuat   ParentAnchorRot = FQuat();   // identity
```

### 2.2 앵커 산정: `AutoGen_ComputeConstraintAnchorLocal`
`MeshEditorWidget.Physics.cpp:128` (자동생성·수동 우클릭 경로 공유):
```cpp
const FMatrix Rel = Child.GetReferenceGlobalPose() * Parent.GetReferenceGlobalPose().GetInverse();
OutPos = Rel.GetLocation();
OutRot = (OutPos.Length() > eps) ? AutoGen_AlignXToDir(OutPos) : FQuat::Identity;
```
**유도 (행벡터 규약, [06](06_coordinate_math.md))**:
- 어떤 점 `p`를 자식 로컬→월드: `p · ChildGlobal`. 월드→부모 로컬: `· ParentGlobal⁻¹`.
- 따라서 자식 로컬→부모 로컬 변환 = `ChildGlobal · ParentGlobal⁻¹` = `Rel`.
- `Rel.GetLocation()` = **자식 본 원점을 부모 본 로컬 좌표로 표현한 값** → pivot을 자식 본 뿌리에 둔다(이론 1.2와 일치).
- `OutRot = AlignXToDir(OutPos)`: 프레임 **X축을 부모→자식 방향**(=뼈 축)에 정렬 → twist가 뼈 축 둘레 회전이 되게 한다([02](02_d6_joint_theory.md)).
- 길이 0(겹친 본) → identity 회전(degenerate 방어).

> 자동생성에서의 호출 (`MeshEditorWidget.Physics.cpp:1324`)은 `Bones[b]`=자식, `Bones[anc]`=바디 보유 최근접 조상=부모. 수동 경로(`CreateConstraintWith`, `:498`)도 동일 헬퍼 사용 → 앵커 산정이 한 곳으로 통일된다.

### 2.3 인스턴스화의 3단 프레임 변환 (핵심)
`InstantiatePhysicsAssetBodies` (`SkeletalMeshComponent.cpp:854`):
```cpp
const FTransform ParentLocalFrame = MakeUnitScaleTransform(Setup->ParentAnchorPos, Setup->ParentAnchorRot);   // ①
const FTransform JointWorldFrame  = MakeWorldTransform(ParentLocalFrame, ParentBodyWorld);                    // ②
...
ConstraintDesc.ParentLocalFrame = ParentLocalFrame;                                                          // 부모에 그대로
ConstraintDesc.ChildLocalFrame  = MakeRelativeTransform(JointWorldFrame, ChildBodyWorld);                    // ③
```
헬퍼(`SkeletalMeshComponent.cpp:59,64`):
- `MakeWorldTransform(L, PW)` = `L.ToMatrix() · PW.ToMatrix()`  → local→world
- `MakeRelativeTransform(W, PW)` = `W.ToMatrix() · PW.ToMatrix().GetInverse()` → world→local

**3단 흐름**
| 단계 | 식 | 좌표계 |
|---|---|---|
| ① 부모 앵커 | `(ParentAnchorPos, ParentAnchorRot)` | **부모 본/바디 로컬** |
| ② 월드 조인트 프레임 | `① · ParentBodyWorld` | **월드** |
| ③ 자식 프레임 | `② · ChildBodyWorld⁻¹` | **자식 바디 로컬** |

- 부모 프레임은 ①을 그대로(`ParentLocalFrame`), 자식 프레임은 같은 월드 프레임 ②를 자식 로컬로 되돌린 ③ → **두 프레임이 월드에서 정확히 동일** → 생성 시 조인트 오차 0(이론 1.3 충족).
- **왜 ①을 부모 바디 로컬로 바로 써도 되나**: 바디는 본 월드에 오프셋 0으로 생성되므로(부모 바디 frame == 부모 본 frame, [01](01_rigid_body_setup.md) 2.4) ParentAnchor(부모 본 로컬)가 곧 부모 바디 로컬이다.

### 2.4 좌표 일관성 (앵커 산정 ↔ 인스턴스화)
- 앵커 산정은 **ref pose**(`GetReferenceGlobalPose`)로 `Rel`을 만든다 → 자산에 고정 데이터로 저장.
- 인스턴스화는 **현재 본 월드 포즈**(`GetBoneWorldTransformByIndex` → `ParentBodyWorld`/`ChildBodyWorld`)를 쓴다.
- 그래도 ②·③이 **동일한 `JointWorldFrame`**에서 파생되므로, 인스턴스 시점 포즈가 ref pose와 달라도 두 프레임은 항상 겹친다. ref pose가 영향을 주는 건 "부모 로컬에서 pivot의 상대 위치/축"이라는 **고정 설계값**뿐이다.
- 두 변환 모두 같은 행벡터 곱 순서(`A · B⁻¹` = "A를 B 로컬로")를 쓰므로 규약이 일관된다.

### 2.5 바디 미존재 시 skip
조인트는 **양쪽 바디가 다 있어야** 만든다 (`SkeletalMeshComponent.cpp:831`):
```cpp
FBodyInstance* ParentBody = GetBodyInstanceByBoneIndex(FindBoneIndex(Setup->ParentBoneName));
FBodyInstance* ChildBody  = GetBodyInstanceByBoneIndex(FindBoneIndex(Setup->ChildBoneName));
if (!ParentBody || !ChildBody) { UE_LOG("... constraint skipped: body not found ..."); continue; }
```
- 작은 본이 바디 생성에서 걸러지면, 그 본을 끼는 조인트는 조용히 skip되고 로그만 남는다. (자동생성이 "바디 보유 최근접 조상"을 부모로 고르는 이유 — 중간의 바디 없는 본을 건너뛰어 끊김 없는 사슬을 만든다, `MeshEditorWidget.Physics.cpp:1310`.)
