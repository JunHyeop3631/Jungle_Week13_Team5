# 목표 4 진단명세 — SkeletalMesh 본 기준 임의 depth 자동 Body 생성

> 사이클: **diagnose/verify** (코드 미수정). 산출물: 이 문서 1개.
> 삽입 지점: `FMeshEditorWidget::GeneratePhysicsBodies()` (현재 빈 stub).
> 단위 전제: 엔진 = **m(미터)**. cm/m 모호성은 범위 밖, "m 가정 일관성"만 점검.

---

## STEP 1 — 재검증 diff 표 (physics_temp.md ↔ 코드 직접확인)

| 점검 지점 | physics_temp.md 기술 | 코드 재검증 (파일:행) | 결과 |
|---|---|---|---|
| `GeneratePhysicsBodies()` stub | 빈 stub, UE_LOG 한 줄 | 본문 = `UE_LOG("… not implemented yet")` [MeshEditorWidget.Physics.cpp:1060-1068](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1060), 호출=`RenderPhysicsToolsPanel` "Re-generate Bodies" [1054-1057](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1054) | **일치** |
| `FBodyCreationSettings` 필드 | 9필드, MaxBoneDepth 부재 | 9필드 확인, depth/root 필드 없음 [MeshEditorWidget.h:49-61](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.h:49) | **일치** |
| `FBone` parent-first + 접근자 | ParentIndex, GetReferenceLocal/GlobalPose | `int32 ParentIndex=-1` [SkeletalMeshAsset.h:22](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h:22), 접근자 [46-49](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h:46), parent<child 가정 [219](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h:219) | **일치** |
| 캡슐 HalfHeight 변환 | `HalfHeight - Radius` | [SkeletalMeshComponent.cpp:107](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:107) | **일치** |
| 앵커/조인트 프레임식 | Parent/JointWorld/Child 3단 | [SkeletalMeshComponent.cpp:495-503](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:495) | **일치** |
| (보강) 캡슐 축 보정 | 문서: "래그돌 경로 보정 없음" | 컴포넌트 경로 = `FromAxisAngle(Y,-90°)` [PhysXRuntime.cpp:487](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:487) **vs** 래그돌 경로 보정 없음 [SkeletalMeshComponent.cpp:104](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:104) → **PhysX 캡슐축=X 입증, `FQuat::FromAxisAngle` 존재 입증** | **일치+근거보강** |
| (신규) 빈 AggregateGeom skip | 미강조 | `if (!BodySetup \|\| BodySetup->AggregateGeom.IsEmpty()) continue;` [SkeletalMeshComponent.cpp:408](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:408) | **신규 명시** (아래 D-치명 의존성) |

---

## STEP 2 — 진단

### 2.1 시스템 흐름 (자동생성 → 래그돌, 컴포넌트 횡단)
- [x] 흐름 1줄: `GeneratePhysicsBodies` → `UPhysicsAsset`(BodySetup/Constraint/DisabledPair 채움) → `InstantiatePhysicsAssetBodies` → `CreateRagdoll` → `IPhysicsScene::Simulate` → `ApplyPhysicsToBones`. 근거: [SkeletalMeshComponent.cpp:389](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:389),[576](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:576),[636](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:636).
- [x] 자산 API 시그니처 확인:
  - `UBodySetup* GetOrCreateBodySetup(const FString& BoneName)` — **빈 AggregateGeom 으로 생성** [PhysicsAsset.cpp:24-31](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.cpp:24)
  - `UPhysicsConstraintSetup* GetOrCreateConstraintSetup(const FString& Parent, const FString& Child)` — 기본 limit(Twist/Swing Limited 45°, bLockLinearMotion=true) [PhysicsAsset.cpp:33-41](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.cpp:33)
  - `void SetCollisionDisabled(const FString& A, const FString& B, bool)` — 순서무관·중복방지·A==B 무시 [PhysicsAsset.cpp:70-89](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.cpp:70)
- [x] 자산/본 접근 경로(stub 내부에서 사용 가능): `Comp=ViewportClient.GetPreviewMeshComponent()` → `Comp->GetSkeletalMesh()->GetSkeletalMeshAsset()` → `FSkeletalMesh::Bones`; 대상=`PhysicsTabState.PhysicsAsset`. 근거: 동일 패턴 [MeshEditorWidget.Physics.cpp:804-806](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:804),[132](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:132).
- [x] **셰이프 3종 한계**: `PrimitiveType ∈ {Sphere,Box,Capsule}` 만, FKAggregateGeom 도 3종만. Sphyl/Convex 없음 → 생성 분기 3종으로 제약. 근거: [MeshEditorWidget.h:34](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.h:34),[PhysicsGeometry.h:40-50](KraftonEngine/Source/Engine/Physics/Asset/PhysicsGeometry.h:40).
- [x] **치명 의존성**: `GetOrCreateBodySetup` 만 부르고 AggregateGeom 을 비워두면 `Instantiate` 가 건너뜀([SkeletalMeshComponent.cpp:408](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:408)) → **생성기는 반드시 AggregateGeom 을 채워야** 래그돌이 생긴다.

### 2.2 미결 A — 순회 + 본길이 산정
- [x] 자식 탐색: parent-first 보장이므로 `for i in (b+1 .. N): Bones[i].ParentIndex==b`. 기존 동일 패턴 [MeshEditorWidget.Physics.cpp:337-339](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:337), BFS 참조 `BuildBoneMaskFromRoot` [AnimNode_LayeredBlendPerBone.cpp:91-122](KraftonEngine/Source/Engine/Animation/Nodes/AnimNode_LayeredBlendPerBone.cpp:91).
- [x] 루트: 스켈레톤은 **다중 루트 가능**(ParentIndex==-1 복수) → 무지정 시 모든 루트 큐잉. 근거: 루트 순회 [MeshEditorWidget.Physics.cpp:164-168](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:164).
- [x] depth 정의: 루트=0, 자식=부모+1 누적.
- [x] 본 길이 = 자식 본 원점까지 거리 = `|Bones[child].GetReferenceLocalPose().GetTranslation()|` (자식의 로컬포즈가 곧 부모기준 위치). 근거: ReferenceLocalPose=부모상대 [SkeletalMeshAsset.h:24-26](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h:24).
- [x] **결정필요-① 다중 자식**: 대표 자식 규칙. → 권고: **최장거리 자식**(`argmax |childLocal|`) 으로 길이·방향 산정(분기 본=골반/쇄골은 근사 허용).
- [x] **결정필요-② leaf(자식 없음)**: 길이 측정 불가. → 권고: 옵션 (a) **leaf 바디 생략**(언리얼 기본 유사), (b) 폴백길이=`max(MinBoneSize, 부모길이*leafRatio)`. 기본은 (a) 생략 권고.
- [x] MinBoneSize/bWalkPastSmallBones × depth 컷 상호작용:
  - depth 컷 = `MaxBoneDepth` 초과 시 **가지치기(자식 큐잉 중단)**.
  - `MinBoneSize`: 본길이 < MinBoneSize → 해당 본 **바디 생략**.
  - `bWalkPastSmallBones=true`: 소본은 바디 생략하되 **자식 순회는 계속**, 더 깊은 바디의 컨스트레인트는 "최근접 조상 바디"에 연결(기존 패턴 [MeshEditorWidget.Physics.cpp:469-471](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:469)).
  - `bWalkPastSmallBones=false`: 소본에서 **서브트리 가지치기**.

### 2.3 미결 B — MaxBoneDepth 필드 설계 (G2)
- [x] 추가 시그니처(제안): `int32 MaxBoneDepth = -1;  // -1 = 무제한` → `FBodyCreationSettings` ([MeshEditorWidget.h:49-61](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.h:49)) 에 삽입. (-1 관례로 기존 "무제한" 거동 보존).
- [x] 시작 루트 지정 동반: **필요**. → 제안 `int32 RootBoneIndex = -1;  // -1 = 모든 스켈레톤 루트`. UI 는 `SelectedBoneIndex` 재사용 옵션도 가능(선택 본부터 생성). depth 는 RootBoneIndex(또는 각 루트)=0 기준.
- [x] 기존 필드 의미 충돌 정리(권고 우선순위):
  1. `MaxBoneDepth` = **하드 컷**(모든 옵션에 우선, 초과 본은 무조건 미생성).
  2. `bCreateBodyForAllBones=true` = depth 내에서 **MinBoneSize/소본 스킵 무시**(작은 본도 바디 생성). depth 컷은 여전히 적용.
  3. `bWalkPastSmallBones` = (bCreateBodyForAllBones=false 일 때만 의미) 소본 처리 정책.
- [x] 직렬화 영향: `FBodyCreationSettings` 는 **에디터 UI 상태 전용**(UPhysicsAsset 에 미포함) → SavePhysicsAsset/Serialize 영향 **없음**. 근거: 직렬화는 UPhysicsAsset 만 [PhysicsAsset.cpp:91](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.cpp:91).

### 2.4 미결 C — 캡슐 축 보정 (G4)
- [x] 사실: PhysX 캡슐 로컬축 = **X**(컴포넌트 경로가 Z→X 보정 `FromAxisAngle((0,1,0), -π/2)` [PhysXRuntime.cpp:487](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:487) 로 입증). 래그돌 경로는 `FKCapsuleElem.Rotation` 을 LocalPose 에 **그대로** 사용·보정 없음 [SkeletalMeshComponent.cpp:104](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:104).
- [x] 보정 회전식: 본방향 `d`(본-로컬 자식방향, 정규화) 를 X축에 정렬하는 쿼터니언.
  ```
  AlignXToDir(d):                       // 확인된 FQuat::FromAxisAngle 사용
    c = dot(UnitX, d)
    if c >  1-eps: return Identity
    if c < -1+eps: return FromAxisAngle(UnitZ, Pi)     // 180° degenerate
    axis = normalize(cross(UnitX, d)); ang = acos(c)
    return FromAxisAngle(axis, ang)
  ```
  → `FKCapsuleElem.Rotation = AlignXToDir(d)`. (`FQuat::FromAxisAngle` 존재 확인됨; `cross/dot/acos` 는 표준.)
- [x] degenerate 처리: 본방향≈+X → Identity, ≈-X → Z축 180°, **영길이(L≈0)** → 캡슐 생성 생략하고 Sphere 폴백 또는 본 스킵.

### 2.5 미결 D — 단위(m) 일관성 (G9 축소판)
- [x] **MinBoneSize 기본값 20.0 = m 단위와 불일치 플래그**: 20.0 m 는 비현실적(20미터). 언리얼 기본(20uu=20cm) 잔재로 추정. → **m 기준 재해석 필요**(예: 0.2 m). 근거: [MeshEditorWidget.h:51](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.h:51).
- [x] 크기 산정 경로 cm 상수 스캔(1회):
  - 자산 지오메트리 기본값 `0.1`(=10cm, m 일관) — [PhysicsGeometry.h:13,22-24,33-34](KraftonEngine/Source/Engine/Physics/Asset/PhysicsGeometry.h:13).
  - `AppendPhysicsShapes` 는 순수 패스스루(스케일 상수 없음) [SkeletalMeshComponent.cpp:75-111](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:75).
  - ⚠️ `FPhysicsShapeDesc` 기본 `HalfExtent=50/Radius=50/HalfHeight=100` [PhysicsTypes.h:91-93](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h:91) 은 **컴포넌트 placeholder**(래그돌 경로 미사용, override 됨) → 생성기는 이 기본값에 의존 금지.
  - 결론: 산정 경로에 cm 하드코딩 **없음**. 단일 outlier = `MinBoneSize=20.0`(위 플래그).

---

## STEP 3 — 설계

### 3.1 `GeneratePhysicsBodies()` 의사코드
```cpp
void GeneratePhysicsBodies():
  Comp  = ViewportClient.GetPreviewMeshComponent()
  Asset = Comp ? Comp->GetSkeletalMesh()->GetSkeletalMeshAsset() : null   // FSkeletalMesh
  PA    = PhysicsTabState.PhysicsAsset
  if (!Asset || !PA || Asset->Bones.empty()) return
  S = PhysicsTabState.BodyCreation

  roots = (S.RootBoneIndex >= 0) ? {S.RootBoneIndex}
                                 : {i : Bones[i].ParentIndex == -1}
  depth[N] = -1; queue Q
  for r in roots: depth[r]=0; Q.push(r)

  while !Q.empty():
    b = Q.pop()
    children = {i>b : Bones[i].ParentIndex == b}              // parent-first
    L = BoneLength(b, children)                               // 2.2
    withinDepth = (S.MaxBoneDepth < 0 || depth[b] <= S.MaxBoneDepth)
    tooSmall    = (L < S.MinBoneSize)
    make = withinDepth && (S.bCreateBodyForAllBones || !tooSmall) && (children.empty()==false || !skipLeaf)

    if make:
      BS = PA->GetOrCreateBodySetup(Bones[b].Name)
      FillAggregateGeom(BS, S, b, children, L)                // 3.2 (필수: geom 채움)
      if S.bCreateConstraints && depth[b] > 0:
        anc = NearestAncestorWithBody(PA, Asset, b)            // ParentIndex 거슬러 FindBodySetup
        if anc >= 0:
          CS = PA->GetOrCreateConstraintSetup(Bones[anc].Name, Bones[b].Name)
          SetAnchorFrame(CS, Asset, anc, b)                    // 3.3
          SetAngularMode(CS, S.AngularConstraintMode)          // 0/1/2 → Locked/Limited/Free
          if S.bDisableCollisionByDefault:
            PA->SetCollisionDisabled(Bones[anc].Name, Bones[b].Name, true)

    // 자식 큐잉 (depth 컷/소본 정책)
    if !withinDepth: continue
    if tooSmall && !S.bWalkPastSmallBones && !S.bCreateBodyForAllBones: continue   // 서브트리 가지치기
    for c in children: depth[c]=depth[b]+1; Q.push(c)
  MarkPhysicsAssetDirty()    // 기존 dirty 플래그 토글(저장 유도)
```
보조:
```cpp
BoneLength(b, children):
  if children.empty(): return skipLeaf ? 0 : max(MinBoneSize, ...)   // 결정필요-②
  c* = argmax_{c in children} |Bones[c].GetReferenceLocalPose().GetTranslation()|
  return |Bones[c*].GetReferenceLocalPose().GetTranslation()|

FillAggregateGeom(BS, S, b, children, L):                 // Sphere/Box/Capsule
  if L <= eps && S.PrimitiveType==Capsule: → Sphere 폴백
  d = normalize(Bones[c*].ReferenceLocalPose.GetTranslation())
  switch S.PrimitiveType:
   Sphere : push FKSphereElem{ Center=d*(L*0.5), Radius=clamp(L*rr, minR, ...) }
   Box    : push FKBoxElem{ Center=d*(L*0.5),
                            Rotation = S.bOrientAlongBone ? AlignXToDir(d) : Identity,
                            HalfX=L*0.5, HalfY=HalfZ=L*widthRatio }
   Capsule: push FKCapsuleElem{ Center=d*(L*0.5),
                            Rotation = S.bOrientAlongBone ? AlignXToDir(d) : Identity,  // G4
                            Radius=clamp(L*rr, minR, L*0.5-eps), HalfHeight=L*0.5 }      // 변환부 -Radius

SetAnchorFrame(CS, Asset, anc, b):                        // ancestor-bone-local
  Rel = Bones[b].GetReferenceGlobalPose() * Bones[anc].GetReferenceGlobalPose().GetInverse()
        // 엔진 row-major 규약, cf. MakeRelativeTransform [SkeletalMeshComponent.cpp:57-60]
  CS.ParentAnchorPos = Rel.GetTranslation()
  CS.ParentAnchorRot = QuatFromMatrix(Rel)
```

### 3.2 추가/변경 자료구조 — `FBodyCreationSettings`
| 이름 | 타입 | 기본값 | 의미 |
|---|---|---|---|
| `MaxBoneDepth` | `int32` | `-1` | 루트=0 기준 최대 깊이. -1=무제한 |
| `RootBoneIndex` | `int32` | `-1` | 시작 루트. -1=모든 스켈레톤 루트 |
| (선택) `CapsuleRadiusRatio` | `float` | `0.25` | Radius = L×ratio |
| (선택) `bSkipLeafBones` | `bool` | `true` | leaf 본 바디 생략 |

> 기존 9필드는 유지. UI: `RenderPhysicsToolsPanel` [MeshEditorWidget.Physics.cpp:1012](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1012) 에 `DragInt("Max Bone Depth")` 등 추가.

### 3.3 저장 연계
- [x] **직렬화 영향 없음**: 신규 필드는 UI 상태(`FBodyCreationSettings`)뿐, `UPhysicsAsset::Serialize` 무변경. 생성 결과(BodySetup.AggregateGeom / ConstraintSetup / DisabledPair)는 **기존 직렬화 경로 그대로** 저장됨 [PhysicsAsset.cpp:91-135](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.cpp:91). UBodySetup 신규 필드 **불필요**.

### 3.4 다음 구현 사이클 분할 (각 1개 = 단일관심사)
- [x] **C1**: `FBodyCreationSettings` 에 `MaxBoneDepth`(+`RootBoneIndex`) 필드 + Tools UI 컨트롤 추가. (검증: 값 변경/표시만)
- [x] **C2**: `GeneratePhysicsBodies` 순회 + **Sphere/Box 바디 생성**(AggregateGeom 채움, 축보정 불필요/단순). MinBoneSize=20.0→m 재해석 동반. (검증: 바디 개수·Instantiate 통과·Simulate 낙하)
- [x] **C3**: **Capsule + 축보정**(`AlignXToDir`, degenerate 처리). (검증: 캡슐이 본 따라 정렬)
- [x] **C4**: **컨스트레인트 자동생성**(최근접 조상, 앵커프레임, AngularConstraintMode). (검증: 조인트 한계 거동)
- [x] **C5**: **인접쌍 충돌 비활성화**(`SetCollisionDisabled`). (검증: 인접 바디 떨림 제거)

---

## 부록 — 미결정 사항(구현 전 확정 필요)
1. 다중 자식 대표 규칙: **최장거리 자식**(권고) vs 평균 vs 첫 자식.
2. leaf 본: **생략**(권고) vs 폴백길이.
3. `MaxBoneDepth` 기본값: `-1`(무제한, 거동보존) vs 유한기본.
4. `MinBoneSize` m 재해석값(예 0.2) 확정.
5. 루트 지정: `RootBoneIndex` 신규필드 vs `SelectedBoneIndex` 재사용.
