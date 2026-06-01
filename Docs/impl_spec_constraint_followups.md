# 구현 명세서 — C4 후속 4건 (④ 한계각 / ⑤ 수동앵커 / C5 충돌비활성 / DisabledPairs clear API)

> 사이클: **spec → implement**. 본 문서 = 구현 명세(코드 미수정). 4개 항목 일괄 구현 대상.
> 참조: `Docs/diagnose_auto_constraint.md`(C4 진단), `Docs/GOAL4_AUTOGEN_DIAGNOSIS.md`, 인프라 맵 `Docs/physics_temp.md`.
> 원칙: 기존 사이클 규약 유지 — 단일 파일 영향 최소화, 직렬화 포맷 무변경, 각 변경에 근거 주석.

---

## 0. 변경 파일 요약

| # | 항목 | 파일 | 변경 종류 |
|---|---|---|---|
| T1 | DisabledPairs clear API (신규) | `Physics/Asset/PhysicsAsset.h` | 공개 인라인 메서드 1개 추가 |
| T2 | ④ 한계각 노출 | `Editor/.../MeshEditorWidget.h` | `FBodyCreationSettings` 필드 1개 |
| T2 | ④ 한계각 UI + 적용 | `Editor/.../MeshEditorWidget.Physics.cpp` | Tools UI 1줄 + C4 패스 한계각 set |
| T3 | C5 충돌 비활성화 | `Editor/.../MeshEditorWidget.Physics.cpp` | C4 패스에 SetCollisionDisabled + 진입 clear 에 ClearDisabledCollisionPairs |
| T4 | ⑤ 수동경로 앵커 + 헬퍼 공유 | `Editor/.../MeshEditorWidget.Physics.cpp` | AutoGen 헬퍼 네임스페이스 상단 이동 + 공유 헬퍼 추가 + 수동경로 앵커 산정 |

> 직렬화: `UPhysicsAsset::Serialize`/`UPhysicsConstraintSetup::Serialize` **무변경**. 신규 데이터 필드 없음(에디터 UI 상태 `DefaultAngularLimitDeg` 만 추가, 비직렬화).

---

## 1. STEP 0 재검증 (touch point 확정)

| 점검 | 코드 (파일:행) | 결과 |
|---|---|---|
| 한계각 필드명 | `UPhysicsConstraintSetup::TwistLimitAngle/Swing1LimitAngle/Swing2LimitAngle`(기본 45f) [PhysicsConstraintSetup.h:33-39](KraftonEngine/Source/Engine/Physics/Asset/PhysicsConstraintSetup.h:33) | 확인 |
| Constraint UI 위치 | Combo "Angular Constraint Mode" [MeshEditorWidget.Physics.cpp:1048](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1048), `if(bCreateConstraints)` 블록 내 | 확인 |
| C5 API | `SetCollisionDisabled(A,B,bool)` 순서무관·중복방지 [PhysicsAsset.cpp:70-89](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.cpp:70) | 확인 |
| DisabledPairs clear | `GetNumDisabledCollisionPairs()` 가 private 멤버 인라인 접근 [PhysicsAsset.h:39](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.h:39) → **동일 패턴 인라인 clear 가능(.cpp 불필요)** | 확인 |
| 수동 컨스트레인트 생성 | `CreateConstraintWith` 람다 = GetOrCreate 만, **앵커 미설정** [MeshEditorWidget.Physics.cpp:451-463](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:451) | 확인(⑤ 대상) |
| AlignXToDir 위치 | anon namespace [MeshEditorWidget.Physics.cpp:1069-1093](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1069) — **수동경로(451)보다 뒤** → 그대로는 451 에서 미가시 | ⚠️ T4 에서 상단 이동 필요 |
| FBone 가시성 | `Mesh/Skeletal/SkeletalMeshAsset.h` 포함됨 [MeshEditorWidget.Physics.cpp:17](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:17) → 상단 네임스페이스에서 `FBone` 사용 가능 | 확인 |

---

## 2. 항목별 명세

### 2.A — T1: `UPhysicsAsset::ClearDisabledCollisionPairs()` (기반)
**파일**: [PhysicsAsset.h:39](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.h:39) 직후.
```cpp
int32 GetNumDisabledCollisionPairs() const { return (int32)DisabledCollisionPairs.size(); }
void  ClearDisabledCollisionPairs() { DisabledCollisionPairs.clear(); }   // ← 신규(인라인, private 멤버 접근)
```
- 값 타입 배열이라 `delete` 불필요, `.clear()` 로 충분. **.cpp 변경 없음.**
- 목적: 진입 clear(②)에서 DisabledPairs 까지 비워 **C5 재생성 멱등성** 완성.

### 2.B — T2(④): 한계각 노출
**B-1 필드** — [MeshEditorWidget.h FBodyCreationSettings](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.h:50) 에 추가:
```cpp
int32 AngularConstraintMode  = 1;          // (기존)
float DefaultAngularLimitDeg = 45.0f;      // ← 신규: Limited 시 Twist/Swing 공통 한계각(deg)
```
**B-2 UI** — Constraint Creation, Combo([MeshEditorWidget.Physics.cpp:1048](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1048)) 직후:
```cpp
ImGui::Combo("Angular Constraint Mode", &S.AngularConstraintMode, Modes, 3);
if (S.AngularConstraintMode == 1)  // Limited 일 때만 의미
    ImGui::DragFloat("Angular Limit (deg)", &S.DefaultAngularLimitDeg, 0.5f, 0.f, 180.f);
```
**B-3 적용** — C4 컨스트레인트 패스(현 `CS->TwistMotion=M; ...` 직후)에 추가:
```cpp
CS->TwistLimitAngle  = S.DefaultAngularLimitDeg;
CS->Swing1LimitAngle = S.DefaultAngularLimitDeg;
CS->Swing2LimitAngle = S.DefaultAngularLimitDeg;
```
- 인스턴스화가 deg→rad·twist ±대칭 변환([SkeletalMeshComponent.cpp:515-518](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:515)). 한계는 Limited 모드에서만 효과(Free/Locked 무관·무해).

### 2.C — T3(C5): 인접쌍 충돌 비활성화
**C-1 생성** — C4 컨스트레인트 패스, `++CreatedConstraints;` 직전/후:
```cpp
if (S.bDisableCollisionByDefault)
{
    PA->SetCollisionDisabled(Asset->Bones[anc].Name, Asset->Bones[b].Name, true);
    ++DisabledPairsCreated;   // 로그용 카운터(함수 로컬)
}
```
**C-2 진입 clear 보강(②완성)** — 진입 clear 블록의 컨테이너 clear 뒤에:
```cpp
PA->GetConstraintsMutable().clear();
PA->ClearDisabledCollisionPairs();          // ← T1 사용: DisabledPairs 까지 완전 clear
```
- 이로써 진입 clear 가 **바디+컨스트레인트+DisabledPairs** 전부 비움 → 매 Re-gen 동일 결과(C5 멱등). (이전 C4 에서 부득이 제외했던 ② 일부를 T1 으로 해소.)
- 로그에 `disabled_pairs=%d` 추가.

### 2.D — T4(⑤): 수동경로 앵커 + 헬퍼 공유
**D-1 헬퍼 네임스페이스 상단 이동 + 공유 헬퍼 추가**
- 현 anon namespace([MeshEditorWidget.Physics.cpp:1069-1093](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1069), 상수 + `AutoGen_AlignXToDir` + `AutoGen_Clamp`)를 **파일 상단(includes 직후, 첫 사용 함수 RenderPhysicsBoneTree[321] 보다 앞)** 으로 이동.
- 공유 앵커 헬퍼 추가(C4·수동 공통):
```cpp
// 컨스트레인트 앵커(부모/조상 본 로컬): 위치=자식 본 원점, 회전=부모→자식 본방향 X정렬.
void AutoGen_ComputeConstraintAnchorLocal(const FBone& Child, const FBone& Parent,
                                          FVector& OutPos, FQuat& OutRot)
{
    const FMatrix Rel = Child.GetReferenceGlobalPose() * Parent.GetReferenceGlobalPose().GetInverse();
    OutPos = Rel.GetLocation();
    OutRot = (OutPos.Length() > kAutoGenEps) ? AutoGen_AlignXToDir(OutPos) : FQuat::Identity;
}
```
**D-2 C4 패스 리팩터(중복 제거, 동작 동일)** — 인라인 Rel 계산을 공유 헬퍼 호출로 치환:
```cpp
AutoGen_ComputeConstraintAnchorLocal(Asset->Bones[b], Asset->Bones[anc],
                                     CS->ParentAnchorPos, CS->ParentAnchorRot);
```
**D-3 수동경로 앵커 적용** — `CreateConstraintWith`([MeshEditorWidget.Physics.cpp:451-463](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:451)) 를 parent/child **인덱스**로 정리 후 앵커 산정:
```cpp
auto CreateConstraintWith = [&](int32 OtherIdx)
{
    int32 ParentIdx, ChildIdx;
    if      (IsAncestorOf(OtherIdx, BoneIndex)) { ParentIdx = OtherIdx;  ChildIdx = BoneIndex; }
    else if (IsAncestorOf(BoneIndex, OtherIdx)) { ParentIdx = BoneIndex; ChildIdx = OtherIdx;  }
    else                                        { ParentIdx = OtherIdx;  ChildIdx = BoneIndex; }
    const FString& ParentName = Asset->Bones[ParentIdx].Name;
    const FString& ChildName  = Asset->Bones[ChildIdx].Name;

    UPhysicsConstraintSetup* CS = PhysicsAsset->GetOrCreateConstraintSetup(ParentName, ChildName);
    AutoGen_ComputeConstraintAnchorLocal(Asset->Bones[ChildIdx], Asset->Bones[ParentIdx],
                                         CS->ParentAnchorPos, CS->ParentAnchorRot);   // ⑤ 앵커 산정
    PhysicsTabState.SelectedConstraintIndex = (int32)PhysicsAsset->GetConstraints().size() - 1;
    PhysicsTabState.SelectedBodySetupIndex  = -1;
    MarkDirty();
};
```
- 동작: 자동/수동 둘 다 동일 앵커(자식 본 원점 + 본방향 X정렬). 모션/한계각은 **수동은 기본값 유지**(⑤는 앵커만, 모션은 기존 프리셋 툴바로 조정 — 범위 한정).

---

## 3. 구현 순서 / 의존성
1. **T1**(ClearDisabledCollisionPairs) — 다른 항목의 기반(C5 멱등). 단독·무위험.
2. **T4-D1**(헬퍼 상단 이동 + 공유 헬퍼) — C4·수동 공통 인프라. 컴파일 확인 후.
3. **T2**(④ 한계각) — 필드/UI/적용.
4. **T3**(C5) — C4 패스 + 진입 clear(T1 사용).
5. **T4-D2/D3**(C4 리팩터 + 수동 앵커) — 공유 헬퍼 사용.
- 각 단계 후 컴파일. T2~T4 는 모두 `GeneratePhysicsBodies` 또는 인접 UI/람다라 충돌 없음.

---

## 4. 검증 방법
- [ ] 컴파일(Debug|x64) — 각 단계.
- [ ] **C5 멱등(②완성)**: Re-gen 2회 → 바디·컨스트레인트·**disabled_pairs 개수 불변**(로그). 수동 충돌토글 추가 후 Re-gen → 자동분만 남음.
- [ ] **④ 한계각**: Angular Limit 슬라이더 변경 → Re-gen → Simulate 시 스윙 콘 각도 변화(오버레이/거동). 0°≈잠김, 큰 값≈헐렁.
- [ ] **C5 효과**: bDisableCollisionByDefault on → 인접 바디 떨림/끼임 감소. `GetNumDisabledCollisionPairs` 로그로 쌍 수 확인.
- [ ] **⑤ 수동 앵커**: 우클릭 "Add Constraint to" 로 수동 생성 → 컨스트레인트 기즈모/오버레이가 **자식 관절 위치 + 본방향 트위스트축**에 표시(이전엔 부모 원점/identity).
- (런타임/시각 항목은 에디터 필요 — 이 환경에선 컴파일까지.)

---

## 5. 미결정 / 주의
1. **한계각 단일값**: Twist/Swing1/Swing2 공통 `DefaultAngularLimitDeg` 1개. 축별 분리는 범위 밖(필요 시 후속).
2. **⑤ 모션 미변경**: 수동 생성 시 앵커만 산정, 모션은 `UPhysicsConstraintSetup` 기본값(Twist/Swing Limited 45°). 모션 프리셋은 기존 툴바([MeshEditorWidget.Physics.cpp:746-763](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:746)) 유지.
3. **헬퍼 이동 위험**: anon namespace 통째 이동 시 다른 anon namespace(피킹 등)와 중복 정의 없는지 컴파일로 확인(이름 `AutoGen_*` 고유).
4. **DisabledPairs 직렬화**: 기존 포맷 그대로(개수+쌍). T1 은 메모리만, 직렬화 무변경.
5. **수동경로 IsAncestorOf 동률**: 둘 다 조상 아님이면 OtherIdx=Parent 로 폴백(기존 동작 유지).
