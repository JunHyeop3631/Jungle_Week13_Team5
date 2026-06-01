# C4 진단명세 — 부모-자식 컨스트레인트 자동 생성

> 사이클: **diagnose/verify** (코드 미수정). 산출물: 이 문서 1개.
> 삽입 지점: `FMeshEditorWidget::GeneratePhysicsBodies()` — 바디 생성(C1~C3 완료) 뒤에 **컨스트레인트 생성 패스** 추가.
> 참조 진단 패턴: `Docs/GOAL4_AUTOGEN_DIAGNOSIS.md` (STEP0 재검증 diff표 → STEP2 진단 체크리스트 → STEP3 설계 → 부록 미결정). 본 문서도 동일 구조.
> 단위 전제: m. 본 크기/거리 일관성은 C1~C3 에서 확정됨(여기서는 재점검 안 함).

---

## STEP 0/1 — 재검증 diff 표 (예상 ↔ 코드 직접확인)

| 점검 지점 | 예상/기존 기술 | 코드 재검증 (파일:행) | 결과 |
|---|---|---|---|
| `UPhysicsConstraintSetup` 필드 | Parent/Child 이름, ParentAnchorPos/Rot, Twist/Swing 모션·한계각(deg), bLockLinearMotion | 동일 [PhysicsConstraintSetup.h:24-43](KraftonEngine/Source/Engine/Physics/Asset/PhysicsConstraintSetup.h:24) | **일치** |
| `EConstraintMotion` 값 | Locked=0/Limited=1/Free=2 | 동일 [PhysicsConstraintSetup.h:11-16](KraftonEngine/Source/Engine/Physics/Asset/PhysicsConstraintSetup.h:11) → **AngularConstraintMode(int) 와 값 일치** | **일치(중요)** |
| `GetOrCreateConstraintSetup` 동작 | 이름만 세팅, 나머지 기본값, 멱등 | 확인: 이름만 set, 기존 있으면 반환 [PhysicsAsset.cpp:33-41](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.cpp:33) | **일치** |
| `FindConstraintSetup` 매칭 | 두 이름 정확 일치 | **순서 민감**(Parent==Parent && Child==Child) [PhysicsAsset.cpp:17-22](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.cpp:17) | **일치(주의: 순서민감)** |
| 인스턴스화 컨스트레인트 경로 | 앵커→프레임 3단, deg→rad, CreateD6Joint | 동일 [SkeletalMeshComponent.cpp:465-530](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:465); 바디 없으면 skip+log [476-482](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:476) | **일치** |
| 변환 헬퍼 | DegToRad, ToPhysicsMotion, MakeRelative/World | [SkeletalMeshComponent.cpp:33-65](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:33) | **일치** |
| 최근접 조상(바디 보유) 패턴 | ParentIndex 거슬러 FindBodySetup | 에디터 수동경로에 존재 [MeshEditorWidget.Physics.cpp:468-472](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:468) | **일치(재사용)** |
| (⚠️신규발견) 수동 컨스트레인트의 앵커 | — | 수동 생성은 `GetOrCreateConstraintSetup` 만 호출 → **ParentAnchorPos/Rot 미설정 = 기본값(원점/identity)** [MeshEditorWidget.Physics.cpp:451-463](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:451) | **신규(2.3 핵심)** |
| 프리셋 motion 매핑 | Ball/Hinge/Prismatic | Ball=Free/Free/Free/lock, Hinge=Free/Lock/Lock/lock, Prismatic=Lock/Lock/Lock/unlock [MeshEditorWidget.Physics.cpp:746-763](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:746) | **일치** |
| FMatrix→앵커 API | 회전·위치 추출 | `FMatrix::GetLocation()` [Matrix.h:109](KraftonEngine/Source/Engine/Math/Matrix.h:109), `FMatrix::ToQuat()` [Matrix.h:113](KraftonEngine/Source/Engine/Math/Matrix.h:113), `GetInverse()` [Matrix.h:75](KraftonEngine/Source/Engine/Math/Matrix.h:75) | **일치** |

---

## STEP 2 — 진단

### 2.1 시스템 흐름 (자동생성 → 래그돌)
- [x] 흐름: `GeneratePhysicsBodies`(바디 + **신규 컨스트레인트**) → `UPhysicsAsset.ConstraintSetups` → `InstantiatePhysicsAssetBodies`(쌍별 CreateD6Joint) → 래그돌.
- [x] 자산 API: `GetOrCreateConstraintSetup(Parent, Child)` 만으로 충분(이름 set + 멱등). 모션/앵커는 반환된 `UPhysicsConstraintSetup*` 에 직접 대입.
- [x] 인스턴스화가 컨스트레인트에 요구하는 것: **두 본 모두 바디 보유**(없으면 [SkeletalMeshComponent.cpp:476-482](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:476) 에서 skip). → C4 는 **바디 있는 본끼리만** 쌍 생성.
- [x] 삽입 위치 옵션: 현재 `GeneratePhysicsBodies` 바디 루프는 parent-first 전방 순회 → **조상 바디는 항상 먼저 생성됨** → 같은 루프에서 바디 생성 직후 컨스트레인트 생성 가능(2-pass 불필요). 단 `bCreateConstraints` 가드.

### 2.2 미결 A — 컨스트레인트 쌍 선정 (최근접 조상)
- [x] 규칙: 바디를 가진 본 `b` 마다, 부모를 거슬러 올라가 **바디를 가진 최근접 조상** `anc` 를 찾음 → 쌍 (anc=Parent, b=Child). 패턴 재사용 [MeshEditorWidget.Physics.cpp:468-472](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:468).
- [x] 조상 바디 없음(= 래그돌 루트 바디) → 컨스트레인트 생략(정상).
- [x] 중복 방지: `GetOrCreateConstraintSetup` 가 멱등. 단 `FindConstraintSetup` 은 **순서 민감** → (anc,b) 일관 순서로 항상 생성하므로 문제 없음.
- [x] 바디 보유 판정 수단: (a) `PA->FindBodySetup(name)`(에디터 방식) 또는 (b) 바디 루프에서 `TArray<bool> BoneHasBody` 채움. → **(b) 권고**(문자열 탐색 제거, parent-first 라 조상은 이미 채워져 있음).

### 2.3 미결 B — 앵커 프레임 산정 (⚠️ 핵심)
- [x] **사실**: 기존 수동 컨스트레인트는 앵커 미설정 → ParentAnchorPos=(0,0,0)/Rot=identity. 인스턴스화 식([SkeletalMeshComponent.cpp:495-503](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:495))상 이는 **조인트 피벗을 "부모(조상) 바디 원점"에 둠**. 다본 래그돌에선 피벗이 부모 본의 **근위(proximal) 관절**에 생겨 물리적으로 어긋남.
- [x] **개선(권고)**: 피벗을 **자식 본 원점**(= 부모-자식 실제 관절)에 두도록 앵커를 부모-본-로컬로 산정.
  ```
  Rel = Bones[b].ReferenceGlobalPose * Bones[anc].ReferenceGlobalPose.GetInverse()   // child rel ancestor
        // 엔진 row-major 규약, cf. MakeRelativeTransform [SkeletalMeshComponent.cpp:57-60]
  CS->ParentAnchorPos = Rel.GetLocation()
  CS->ParentAnchorRot = Rel.ToQuat()        // 조인트 프레임 = 자식 본 프레임
  ```
- [x] **전제**: 앵커를 ref(bind) global 로 산정 → **생성/시뮬을 ref pose 에서 수행 가정**(PhAT 표준). 애님 포즈 중 생성 시 바디 월드와 앵커가 어긋날 수 있음 → 생성은 ref pose 권고(플래그).
- [x] 회전 선택지: 기본 = 자식 본 프레임(`Rel.ToQuat()`). 대안(B+) = 트위스트축을 본 방향에 정렬(`AutoGen_AlignXToDir`, 캡슐과 일관) — **결정필요**(부록-①).
- [x] 부수효과: 기존 수동경로도 같은 결함 보유 → C4 가 자동 산정하면 수동보다 정확. (수동경로 보정은 **C4 범위 밖**, 별건으로 기록.)

### 2.4 미결 C — Angular 모드 / 한계 매핑
- [x] `AngularConstraintMode`(int 0/1/2) → `EConstraintMotion` **값 동일**하므로 `(EConstraintMotion)S.AngularConstraintMode` 직접 캐스트. Twist/Swing1/Swing2 **3축 동일 적용**.
- [x] 한계각: `FBodyCreationSettings` 에 **한계각 필드 없음** → `UPhysicsConstraintSetup` 기본값(Twist/Swing 45°) 사용. 인스턴스화가 deg→rad 변환·twist 는 ±대칭 [SkeletalMeshComponent.cpp:515-518](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:515).
- [x] 기본 `AngularConstraintMode=1(Limited)` → 45° 콘/트위스트 한계의 래그돌. (프리셋 Ball&Socket 의 Free 와는 다름 — 의도된 기본값.)
- [x] 선형: `bLockLinearMotion` 기본 true 유지 → ball-socket 형(병진 잠금). 별도 설정 불필요.

### 2.5 미결 D — 멱등성 / 재생성(stale)
- [x] `GetOrCreateConstraintSetup` 멱등 → "Re-generate" 반복해도 중복 없음.
- [x] **stale**: 바디를 줄여 재생성하면 옛 컨스트레인트가 남을 수 있음. 런타임 무해(인스턴스화가 바디 없는 컨스트레인트 skip [SkeletalMeshComponent.cpp:476-482](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:476))하나 자산이 지저분.
- [x] **결정필요**(부록-②): "Re-generate" 시 (a) 기존 컨스트레인트 유지(additive, 현행 바디와 동일) vs (b) 전체 clear 후 재생성. 바디 경로(`GetOrCreateBodySetup`)도 현재 additive → 일관성 위해 (a) 또는 바디·컨스트레인트 동시 clear 정책.

### 2.6 미결 E — bWalkPastSmallBones 의미 확정
- [x] "최근접 조상 바디" 방식은 바디 없는 중간 본을 **자동으로 건너뜀**(직접 조상 바디에 연결) → `bWalkPastSmallBones` 의 본래 의도가 이미 내재.
- [x] **결정필요**(부록-③): 이 플래그를 (a) 무시(항상 최근접 조상 연결) vs (b) false 면 **직속 부모에 바디 없을 때 컨스트레인트 생략**(간극 미허용)으로 해석. → (a) 권고(단순·일반적).

---

## STEP 3 — 설계

### 3.1 의사코드 — 단일 패스 통합 (바디 루프 내 컨스트레인트 생성)
> 기존 `GeneratePhysicsBodies` 바디 생성부 뒤에 삽입. parent-first 보장으로 조상 바디는 이미 존재.
```cpp
// (바디 루프 진입 전) 본별 바디 보유 추적
TArray<bool> BoneHasBody; BoneHasBody.assign(BoneCount, false);
TArray<FString> BodyBoneName; BodyBoneName.assign(BoneCount, FString());   // 선택: 이름 캐시

for (int32 b = 0; b < BoneCount; ++b)
{
    ... // (C1~C3) 크기필터 + 셰이프 생성. 바디 만들면:
    //   UBodySetup* BS = PA->GetOrCreateBodySetup(Bones[b].Name);
    //   BoneHasBody[b] = true;

    // ── (C4) 컨스트레인트: 바디 생성 직후 ──
    if (S.bCreateConstraints && BoneHasBody[b])
    {
        int32 anc = -1;
        for (int32 p = Bones[b].ParentIndex; p >= 0; p = Bones[p].ParentIndex)
            if (BoneHasBody[p]) { anc = p; break; }                 // 최근접 조상 바디
        if (anc >= 0)
        {
            const FString& AncName = Bones[anc].Name;
            UPhysicsConstraintSetup* CS = PA->GetOrCreateConstraintSetup(AncName, Bones[b].Name);
            const EConstraintMotion M = (EConstraintMotion)S.AngularConstraintMode;
            CS->TwistMotion = M; CS->Swing1Motion = M; CS->Swing2Motion = M;
            // 앵커: 자식 본 원점 = 부모(조상) 본 로컬 (2.3)
            const FMatrix Rel = Bones[b].GetReferenceGlobalPose()
                              * Bones[anc].GetReferenceGlobalPose().GetInverse();
            CS->ParentAnchorPos = Rel.GetLocation();
            CS->ParentAnchorRot = Rel.ToQuat();
            // bLockLinearMotion / 한계각 = 기본값(45°) 유지
            ++CreatedConstraints;
        }
    }
}
...
MarkDirty();
// 로그에 created_constraints 추가
```

### 3.2 자료구조 / 시그니처
- [x] **신규 필드 불필요**: `bCreateConstraints`, `AngularConstraintMode` 이미 존재 [MeshEditorWidget.h:64-65](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.h:64). UI 도 이미 있음(Constraint Creation 섹션 [MeshEditorWidget.Physics.cpp:1040-1048](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1040)).
- [x] (선택) 한계각 기본값 설정 필드: 현재 없음 → 필요하면 `float DefaultAngularLimitDeg = 45.f` 추가 가능(부록-④). 미추가 시 UPhysicsConstraintSetup 기본 45° 사용.
- [x] 추가 추적: `TArray<bool> BoneHasBody`(루프 내 채움) + `int32 CreatedConstraints`(로그용). 둘 다 함수 로컬.

### 3.3 저장 / 직렬화 영향
- [x] **영향 없음**: 컨스트레인트는 `UPhysicsAsset::Serialize` 의 기존 ConstraintSetups 경로로 저장됨 [PhysicsAsset.cpp:108-119](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.cpp:108). `UPhysicsConstraintSetup::Serialize` 무변경. 신규 필드 없으면 직렬화 무변경.

### 3.4 검증 방법 (구현 사이클에서)
- [x] 컴파일.
- [x] "Re-generate Bodies"(bCreateConstraints=on) → 컨스트레인트 개수 로그 = (바디 수 − 루트 바디 수) 근사 확인.
- [x] "Simulate" → 본들이 관절 한계 안에서 매달려 흔들리는지(자유낙하 분해 아님) 확인. AngularConstraintMode=Locked → 거의 강체, Limited → 콘 한계, Free → 헐렁.
- [x] 앵커 정합: 조인트 한계 오버레이(`DrawConstraintLimitsOverlay`)가 **부모 본 원점이 아니라 자식 관절 위치**에 그려지는지 육안 확인(2.3 개선 검증).
- [x] (음성) 바디 줄여 재생성 → stale 컨스트레인트가 무해히 skip 되는지 로그 확인(2.5).

### 3.5 다음 사이클(C5) 경계
- [x] C5(인접쌍 충돌 비활성화)는 **C4 가 만든 (anc,b) 쌍을 그대로 재사용** → `bDisableCollisionByDefault` 시 `PA->SetCollisionDisabled(AncName, Bones[b].Name, true)`. **이번 C4 에서는 작성하지 않음**.

---

## 부록 — 미결정 사항 (구현 전 확정)
1. **앵커 회전**: 자식 본 프레임(`Rel.ToQuat()`, 단순) vs 트위스트축 본방향 정렬(`AlignXToDir`, 캡슐과 일관). → 권고: **자식 본 프레임**(단순·결정적), 필요시 후속 개선.
2. **재생성 정책**: additive(현행 바디와 일관) vs clear-후-재생성. → 권고: 현 단계 **additive 유지**, 별도 "Clear" 액션은 후속.
3. **bWalkPastSmallBones 해석**: 항상 최근접 조상 연결(무시) vs 직속부모 바디 없으면 생략. → 권고: **무시(항상 연결)**.
4. **한계각 설정 노출**: 기본 45° 고정 vs `DefaultAngularLimitDeg` 필드+UI 추가. → 권고: 이번엔 **기본 고정**, 필요 시 후속 사이클.
5. **수동경로 앵커 결함**(2.3): 기존 우클릭 생성도 앵커 미설정 → 별건 이슈로 기록(C4 범위 밖, 추후 동일 산정식 적용 검토).
