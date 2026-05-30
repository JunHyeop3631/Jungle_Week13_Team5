# RAGDOLL_DIAGNOSIS

진단 일자: 2026-05-30
브랜치: feature/joint
대상: KraftonEngine 패시브 ragdoll (PhysX 4.1 + IPhysicsRuntime)

> 표기 규약: `[확인됨] / [없음] / [불명확]` + 파일경로:라인.
> 호출자 0건은 grep으로 검증한 결과. PHYSICS_AUDIT.md/Docs/*.md 내 매칭은 호출자가 아니라 문서 인용이므로 제외.

---

## A. 호출 사슬 — 어디까지 연결됐고 어디서 끊겼나

### A.1 World가 IPhysicsRuntime(FPhysXRuntime)을 소유하는가
- [확인됨] 멤버 선언: `std::unique_ptr<IPhysicsRuntime> PhysicsRuntime;` — [KraftonEngine/Source/Engine/GameFramework/World.h:139](KraftonEngine/Source/Engine/GameFramework/World.h:139)
- [확인됨] 인스턴스 생성: `PhysicsRuntime = std::make_unique<FPhysXRuntime>();` — [KraftonEngine/Source/Engine/GameFramework/World.cpp:314](KraftonEngine/Source/Engine/GameFramework/World.cpp:314) (UWorld::InitWorld 내부)
- [확인됨] 백엔드 분기 없음: PhysicsScene은 ProjectSettings의 백엔드(PhysX/Native)에 따라 분기되지만(World.cpp:308-311), PhysicsRuntime은 무조건 FPhysXRuntime으로 생성됨

### A.2 엔진/World Tick에서 Runtime->Simulate(dt)가 실제 호출되는가
- [확인됨] 엔진 진입점 → World::Tick: `World->Tick(DeltaTime, TickType);` — [KraftonEngine/Source/Engine/Runtime/Engine.cpp:200](KraftonEngine/Source/Engine/Runtime/Engine.cpp:200)
- [확인됨] World::Tick → Runtime->Simulate(dt): `PhysicsRuntime->Simulate(DeltaTime);` — [KraftonEngine/Source/Engine/GameFramework/World.cpp:368](KraftonEngine/Source/Engine/GameFramework/World.cpp:368)
- [확인됨] dt 출처: `World::Tick(float DeltaTime, ...)` 인자, Engine.cpp:200에서 전달 — [KraftonEngine/Source/Engine/GameFramework/World.cpp:345](KraftonEngine/Source/Engine/GameFramework/World.cpp:345)
- [확인됨] 가드 조건: `if (bHasBegunPlay && PhysicsScene)` 안에서 호출됨 — World.cpp:363. **주의:** 가드 변수는 `PhysicsScene`이고 `PhysicsRuntime` 자체는 nullptr 체크 안 됨. InitWorld에서 무조건 생성되므로 현 시점에선 안전하나, 의존 변수가 어긋남
- [확인됨] bPaused일 땐 early return — World.cpp:357-361

### A.3 Runtime 초기화(Initialize/Foundation 생성)가 Simulate 전에 보장되는가
- [수정함] World::InitWorld에서 `PhysicsRuntime->Initialize()` 명시 호출 **없음** — World.cpp:299-316 본문 확인, `make_unique<FPhysXRuntime>()`(314) 직후 Initialize 호출 없음
- [확인됨] FPhysXRuntime::Initialize() 정의 자체는 존재 — [KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:107](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:107) (PxFoundation/PxPhysics/PxScene/PxDispatcher/DefaultMaterial 생성)
- [확인됨] Initialize는 **lazy 호출 경로만 존재**: CreateRigidBody 첫 호출 시 진입 — `if (!Initialize() || !Physics || !Scene)` — [KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:251](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:251)
- [확인됨] Simulate는 Initialize를 호출하지 않음. Scene이 nullptr이면 즉시 return — [KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:230](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:230)
- **귀결:** CreateRigidBody 호출이 한 번도 일어나지 않으면 Scene이 영구적으로 nullptr → 매 프레임 Simulate는 no-op. 아래 A.4와 직접 연결되는 문제

### A.4 InstantiatePhysicsAssetBodies(Runtime)를 호출하는 코드가 존재하는가
- [없음] **호출자 0건.** grep `InstantiatePhysicsAssetBodies` — 매칭 위치:
  - 선언/정의: SkeletalMeshComponent.h:76-77, SkeletalMeshComponent.cpp:383, 389 (자기 호출 1건: cpp:386에서 1인자→2인자 오버로드 위임)
  - 그 외 매칭: PHYSICS_AUDIT.md/Docs/* (문서 인용)
- [확인됨] 본문은 완전히 구현됨 — [KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:389-533](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:389) (Bodies 인덱스 할당, BodySetup 순회, FindBoneIndex, CreateRigidBody, CreateD6Joint까지)

### A.5 CreateRagdoll()을 호출하는 코드가 존재하는가
- [없음] **호출자 0건.** grep `CreateRagdoll` — 매칭 위치:
  - 선언/정의: SkeletalMeshComponent.h:84, SkeletalMeshComponent.cpp:576
  - 로그 문자열: cpp:581
  - 그 외 매칭: PHYSICS_AUDIT.md/Docs/* (문서 인용)
- 외부(액터/GameMode/Lua) 어디에서도 USkeletalMeshComponent::CreateRagdoll() 호출 없음

### A.6 CreateRagdoll() 본문이 구현됐는가 vs 빈 스텁인가
- [확인됨] 본문 **구현됨** — [KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:576-615](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:576)
  - PhysicsRuntimeOwner / Bodies.empty 가드 (579)
  - 각 본 world transform을 body에 강제 동기화 — SetBodyTransform(teleport=true) (607)
  - SetBodyType(Dynamic) — kinematic 해제 + wakeUp (610)
  - bSimulatingPhysics = true (614)
- [확인됨] PHYSICS_AUDIT.md:222의 "빈 본문" 기술은 **현 시점 기준 outdated**. 현재 .cpp:576-615 본문은 빈 스텁 아님

### A.7 TickComponent에 물리 write-back 분기가 존재하는가, 어떤 플래그로 갈리는가
- [확인됨] 분기 존재 — [KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:617-634](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:617)
  - 분기 플래그: `bSimulatingPhysics` (cpp:619)
  - true일 때: ApplyPhysicsToBones() → UMeshComponent::TickComponent (cpp:621-624). AnimInstance 평가 skip
  - false일 때: EvaluateAnimInstance(DeltaTime) → 기존 애니메이션 경로 (cpp:627-633)
- [확인됨] ApplyPhysicsToBones 본문도 **구현됨** — [KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:636-697](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:636)
  - Bone parent-first 순회로 ComponentLocalGlobals 누적 (664-693)
  - body world → component-local global → local pose 산출 (677-688)
  - SetBoneLocalTransforms로 푸시 (696)
- [확인됨] bSimulatingPhysics를 true로 set하는 유일한 지점은 CreateRagdoll cpp:614. **호출자가 없으므로 영구히 false** → write-back 경로 영구 미진입

### A.8 호출 그래프 요약
```
[Engine::Tick]
    └─→ World->Tick(dt)                                  [확인됨, Engine.cpp:200]
        ├─→ PhysicsScene->Tick(dt)                       [확인됨, World.cpp:366 — 기존 Nuget 레거시 경로]
        └─→ PhysicsRuntime->Simulate(dt)                 [확인됨, World.cpp:368]
            └─ Scene이 nullptr이면 no-op                  [확인됨, PhysXRuntime.cpp:230-233]
                ↑ Scene 생성은 FPhysXRuntime::Initialize()
                  ├─ World::InitWorld에서 명시 호출 ❌    [없음]
                  └─ CreateRigidBody가 lazy로 호출 ✅    [PhysXRuntime.cpp:251]
                     ↑ CreateRigidBody 호출은
                       └─ USkeletalMeshComponent::InstantiatePhysicsAssetBodies가 함  [SkeletalMeshComponent.cpp:447]
                          ↑ 그 함수의 호출자 = ❌ 없음    [grep 0건]

[USkeletalMeshComponent::TickComponent]                  [확인됨, cpp:617]
    └─ if (bSimulatingPhysics)                           [확인됨, cpp:619]
       └─ ApplyPhysicsToBones()                          [확인됨 구현, cpp:636-697]
       ↑ bSimulatingPhysics = true 설정은
         └─ USkeletalMeshComponent::CreateRagdoll()      [확인됨 구현, cpp:614]
            ↑ 그 함수의 호출자 = ❌ 없음                  [grep 0건]
```
- **결론:** 코드 자체는 인터페이스→런타임→컴포넌트가 다 연결되어 있고, 컴포넌트 내부 함수 본문도 구현되어 있다. 그러나 **외부에서 트리거가 없다.** InstantiatePhysicsAssetBodies / CreateRagdoll 모두 호출자 0건.

---

## B. bone ↔ PhysicsAsset bone 대응

### B.1 PhysicsAsset의 BodySetup이 본을 무엇으로 참조하는가
- [확인됨] **BoneName 문자열** 참조 — `FString BoneName;` — [KraftonEngine/Source/Engine/Physics/Asset/BodySetup.h:16](KraftonEngine/Source/Engine/Physics/Asset/BodySetup.h:16)
- [확인됨] 인덱스 참조 없음. PhysicsAsset의 BodySetup/ConstraintSetup 어디에도 BoneIndex 멤버 없음 (BodySetup.h:12-37, PhysicsConstraintSetup.h:20-57)
- [확인됨] 직렬화도 BoneName 문자열로 round-trip — BodySetup.h:28, PhysicsConstraintSetup.h:45-46

### B.2 InstantiatePhysicsAssetBodies가 BoneName → BoneIndex를 어떻게 푸는가
- [확인됨] BodySetup마다 FindBoneIndex 호출 — `const int32 BoneIndex = FindBoneIndex(BodySetup->BoneName);` — [KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:413](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:413)
- [확인됨] Constraint도 Parent/Child BoneName으로 동일 패턴 — SkeletalMeshComponent.cpp:472-473
- [확인됨] FindBoneIndex 구현은 USkinnedMeshComponent — 선형 검색, 이름 비교 — [KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.cpp:276-289](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.cpp:276) (Asset->Bones[i].Name == BoneName)

### B.3 FindBoneIndex가 -1을 반환할 때(이름 불일치) 어떻게 처리되는가
- [확인됨] BodySetup: 로그 후 continue (해당 BodySetup 통째로 skip) — `UE_LOG("PhysicsAsset body skipped: bone not found. Bone=%s", ...)` — [KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:414-418](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:414)
- [확인됨] Constraint: GetBodyInstanceByBoneIndex(-1) → nullptr → 로그 후 continue — SkeletalMeshComponent.cpp:474-482
- [확인됨] 범위 초과(BoneIndex >= Bodies.size())도 같은 분기로 차단 — cpp:414
- **증상 예측 (실측 아님):** PhysicsAsset의 BoneName과 SkeletalMesh의 본 이름이 다를 경우 → body 생성 0건 → `if (!bCreatedAnyBody) { Bodies.clear(); PhysicsRuntimeOwner = nullptr; return false; }` 경로(cpp:458-463) → 함수가 false 반환, 사일런트 실패. CreateRagdoll이 호출되더라도 가드(cpp:579)에서 차단됨

### B.4 생성된 Body가 Bodies[BoneIndex]에 저장되는 경로 확인
- [확인됨] `Bodies.assign(Asset->Bones.size(), nullptr);`로 본 카운트만큼 사전 할당 — cpp:401
- [확인됨] `Bodies[BoneIndex] = Body;`로 직접 인덱스 저장 — cpp:454
- [확인됨] BoneIndex가 PhysicsAsset BodySetup 배열 인덱스가 아니라 SkeletalMesh 본 인덱스로 키잉됨 — 이게 ApplyPhysicsToBones에서 `Bodies[BoneIndex]` 직접 조회를 가능하게 함 (cpp:672)

### B.5 body 초기 WorldTransform이 GetBoneWorldTransformByIndex(BoneIndex)에서 오는지
- [확인됨] InstantiatePhysicsAssetBodies: GetBoneWorldTransformByIndex 호출 → BodyDesc.WorldTransform 채움 — SkeletalMeshComponent.cpp:420-433
- [확인됨] CreateRagdoll도 진입 시 동일하게 본 world transform으로 body teleport — cpp:604-607
- [확인됨] GetBoneWorldTransformByIndex 본문은 본 글로벌 행렬에 GetWorldMatrix() 곱 — SkinnedMeshComponent.cpp:291-302
- [확인됨] 본 글로벌 행렬은 BuildBoneEditGlobalMatrices가 parent-first 누적으로 산출 — SkinnedMeshComponent.cpp:940-960
- **한 점에 모이는 버그 가능성:** 모든 body가 같은 위치(예: 원점)에서 생성되려면 GetBoneWorldTransformByIndex가 0행렬을 반환해야 함. 본 데이터가 정상이고 BuildBoneEditGlobalMatrices가 parent-first 누적을 정상 수행한다면 — 코드상 발생 불가. 단 PhysicsAsset의 BoneName이 모두 SkeletalMesh의 루트(인덱스 0) 본 이름과 같다면 모든 body가 루트 본 위치에 모임. **데이터 의존 — 코드 차원에서는 정상**

### B.6 BodySetup이 본 대비 오프셋(local pose)을 갖는지 — write-back 역보정 필요성
- [없음] BodySetup에 본 대비 변환(local pose, RelativeTransform 등) **없음** — BodySetup.h:12-37 멤버 전체에서 그런 필드 없음. AggregateGeom(Shape별 LocalTransform은 있음 — PhysicsGeometry 정의에 의존) 외 본 대비 offset transform 없음
- [확인됨] ApplyPhysicsToBones는 "body world == bone world" 전제로 동작 — `// InstantiatePhysicsAssetBodies 가 본 world == body actor pose 로 생성했기 때문에 body→bone 별도 오프셋 보정은 없다.` — [KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:660-661](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:660)
- **귀결:** 본 transform과 body actor pose가 1:1 동일하다는 약속이 유지되는 한 write-back은 정확. 그러나 BodySetup의 AggregateGeom Sphere/Box/Capsule의 LocalTransform은 본 기준 형상 오프셋이며, body world (= 본 world)와 별개. 형상 중심이 본 위치와 다른 경우(예: 어깨 본 + 위팔 캡슐) write-back 정확성에는 무관

### B.7 현재 프로젝트에 실제 PhysicsAsset 인스턴스/에셋이 존재하는가
- [확인됨] USkeletalMesh가 PhysicsAsset 포인터 보유 — `UPhysicsAsset* PhysicsAsset = nullptr;` — [KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMesh.h:60](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMesh.h:60)
- [확인됨] 생성/연결 경로: MeshEditorWidget — `new UPhysicsAsset()` 후 `SkeletalMesh->PhysicsAsset = PhysicsAsset;` — [KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.cpp:1531](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.cpp:1531), [2262](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.cpp:2262)
- [불명확] USkeletalMesh::Serialize가 PhysicsAsset 포인터/경로를 round-trip하는지 — SkeletalMesh.h:20에서 Serialize 선언만 보고, 본문 .cpp 미확인 (SkeletalMesh.cpp 미열람). PhysicsAsset 멤버가 직렬화 대상에 포함되지 않으면 에셋 로드 시 nullptr 유지 → InstantiatePhysicsAssetBodies(Runtime) 1인자 오버로드(cpp:385-386)는 `Mesh->PhysicsAsset`을 그대로 넘기므로 즉시 false 반환
- [불명확] 실제 프로젝트 콘텐츠에 PhysicsAsset이 만들어져 저장된 캐릭터가 있는지 — 콘텐츠 디렉터리(Content/*) 미스캔. 코드만 있고 데이터가 없을 가능성 미해소

---

## C. 본 배열 규약 (write-back 정확성 전제)

### C.1 본 배열이 BoneIndex 오름차순 = 부모→자식 순서인지 코드 근거
- [확인됨] 명시 주석 — `// asset bone order가 parent-first라는 전제에 맞춰 부모 global을 누적한다.` — [KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.cpp:956-957](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.cpp:956)
- [확인됨] 동일 전제 ApplyPhysicsToBones에도 — `// 본 인덱스 오름차순 = parent-first 가 엔진 규약이라 단순 순회로 부모 글로벌이 항상 채워진 뒤 자식이 사용한다.` — SkeletalMeshComponent.cpp:652-653
- [확인됨] FSkeletalMesh::NormalizeBonePoseData도 동일 가정 (`Bone.ParentIndex < BoneIndex` 검사 후 부모 누적) — [KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h:219-221](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h:219)
- [불명확] 임포터(FBX import 경로)가 실제로 parent-first 순서로 본 배열을 만드는지 — Asset 임포트 코드 미확인. 규약은 일관되나 입력 데이터가 어기는 케이스는 코드 차원에서 검증되지 않음

### C.2 본 트랜스폼이 local(부모기준)인지 world인지, 저장 형식
- [확인됨] FBone은 local과 global 모두 보유. 형식은 **FMatrix** — [KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h:26-31](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h:26)
  - ReferenceLocalPose (local, 부모 기준)
  - ReferenceGlobalPose / SkinBindGlobalPose (global)
  - InverseBindPoseMatrix
- [확인됨] write-back 산출물 SetBoneLocalTransforms 인자는 **TArray<FTransform> LocalPose** — FTransform로 변환되어 SkinnedMeshComponent에 들어감 — SkeletalMeshComponent.cpp:657-658, SkinnedMeshComponent.cpp:608-636

### C.3 루트 본의 부모 처리(컴포넌트 월드 기준 변환)
- [확인됨] ApplyPhysicsToBones: `(ParentIndex >= 0)` 분기, 루트는 ParentGlobal = Identity — [KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:666-669](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:666)
- [확인됨] body world → component local 변환을 GetWorldInverseMatrix() 곱으로 처리 (루트 본 LocalMatrix = ComponentGlobal 직접 사용) — cpp:662, 684-687
- [확인됨] BuildBoneEditGlobalMatrices도 동일 패턴 — SkinnedMeshComponent.cpp:957-958

---

## D. 좌표계·단위 (거동 정상성 전제)

### D.1 엔진 핸드니스/up축/단위
- [불명확] 명시 핸드니스/up축/단위 상수 grep 미발견 — `handedness | UpAxis | WorldToMeter | UnitScale` 매칭은 Docs / ThirdParty(fmod) / FBX 임포터 보고서 외 엔진 코드 본문에 없음
- [확인됨] PhysX 중력 vector는 z-down: `SceneDesc.gravity = PxVec3(0.0f, 0.0f, -9.81f);` — [KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:142](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:142) → 엔진이 **z-up**임을 시사 (코드 측 명시 상수는 없음)
- [확인됨] 좌표 변환 함수: PxVec3 ↔ FVector 1:1 컴포넌트 매핑 (X→x, Y→y, Z→z), PxQuat ↔ FQuat도 1:1 — PhysXRuntime.cpp:13-31. 핸드니스 swap/축 부호 변환 없음. 엔진과 PhysX가 같은 핸드니스 가정

### D.2 PxTolerancesScale이 엔진 단위와 맞는가
- [확인됨] PhysX 생성 시 **기본 PxTolerancesScale 사용** — `Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *Foundation, PxTolerancesScale());` — [KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:120](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:120)
- 기본값: length=1.0, speed=10.0 → **미터(m)·m/s 가정**
- [불명확] 엔진 단위 자체가 cm인지 m인지 확인 불가 (D.1 참조). UE 본가 호환을 가정하면 cm일 가능성이 높으나, 코드 측 unit-scale 처리 없음
- 만약 엔진이 cm: TolerancesScale length=100, speed=981로 설정 필요 (UE 패턴). 불일치 시 sleep threshold/contact offset/penetration depth가 부적절해져 ragdoll 떨림·관통·즉시 sleep 등 거동 비정상 가능

---

## 지금 당장 막고 있는 것 (우선순위)

순서대로 막혀 있음. 위가 풀려야 아래가 의미를 가짐.

1. **[P0] CreateRagdoll() 호출자 0건** — 외부 트리거 없음
   - 왜 막혔는지: AActor/GameMode/Lua/캐릭터 사망 핸들러 어디서도 USkeletalMeshComponent::CreateRagdoll()을 호출하지 않음. bSimulatingPhysics가 평생 false → ApplyPhysicsToBones 진입 불가
   - 근거: grep `CreateRagdoll` 호출자 매칭 0건 (선언/정의/로그 제외)

2. **[P0] InstantiatePhysicsAssetBodies() 호출자 0건** — Bodies 배열 영구 비어 있음
   - 왜 막혔는지: CreateRagdoll의 사전조건(`!PhysicsRuntimeOwner || Bodies.empty()`) 가드(cpp:579)를 절대 통과 못 함. 호출자가 풀려도 1번이 풀린 효과 없음
   - 근거: grep `InstantiatePhysicsAssetBodies` 호출자 매칭 0건 (선언/정의/자기위임 제외)

3. **[P0] FPhysXRuntime 명시 Initialize 호출 없음** — Scene이 nullptr인 상태로 Simulate가 매 프레임 no-op
   - 왜 막혔는지: World::InitWorld에서 `make_unique<FPhysXRuntime>()`만 하고 Initialize() 호출 없음(World.cpp:314). Lazy init은 CreateRigidBody 첫 호출에서만 일어나므로, 2번이 안 풀리면 평생 Scene 미생성. Simulate는 Scene==nullptr이면 무성공 종료
   - 근거: World.cpp:299-316 본문, PhysXRuntime.cpp:230, 251
   - 부가 위험: World.cpp:368에서 PhysicsRuntime nullptr 가드 없음 (PhysicsScene 가드만). 현재는 InitWorld가 항상 만들지만 의존이 어긋남

4. **[P1] PhysicsAsset 데이터(`SkeletalMesh->PhysicsAsset`) 실제 존재 보장 불명** — InstantiatePhysicsAssetBodies(Runtime) 1인자 오버로드가 즉시 false 반환할 가능성
   - 왜 막혔는지: 코드 경로(에디터 저장)는 있지만 실제 `.sketbin` 등에 PhysicsAsset가 직렬화되어 로드 시 복원되는지 미확인. nullptr이면 cpp:386 → cpp:395 early return false
   - 근거: SkeletalMesh::Serialize 본문 미확인 (.cpp 미열람), 콘텐츠 디렉터리 미스캔

5. **[P1] 본 ↔ PhysicsAsset BoneName 매핑 실측 불가** — 4번이 풀려야 검증 가능
   - 왜 막혔는지: 매핑은 BoneName 문자열 동일성(SkinnedMeshComponent.cpp:283 == 비교) 단일 기준. 임포터·에디터·런타임이 동일 본 이름 규약을 따른다는 보장은 코드 차원에서 검증되지 않음. 불일치 시 사일런트 skip(SkeletalMeshComponent.cpp:414-418)이라 디버그 로그 외 외부 신호 없음
   - 근거: BodySetup.h:16(이름만), SkinnedMeshComponent.cpp:276-289, SkeletalMeshComponent.cpp:414

6. **[P2] PxTolerancesScale 단위 일치 미검증** — 1-3번 해결되어 동작해도 거동 비정상 가능
   - 왜 막혔는지: PxTolerancesScale 기본값(m 단위)을 그대로 사용. 엔진 단위 자체가 명시되지 않아 일치 여부 판단 불가
   - 근거: PhysXRuntime.cpp:120, 엔진 unit-scale grep 결과 매칭 없음

7. **[P2] PhysX vs 엔진 핸드니스/축 매핑 검증 부재** — ToPxVec3/ToFVector 1:1 컴포넌트 복사로 가정만 됨
   - 왜 막혔는지: 변환 함수가 X→x, Y→y, Z→z 단순 복사(PhysXRuntime.cpp:13-31). 두 시스템이 같은 핸드니스라는 가정이 어디에도 명시되지 않음
   - 근거: PhysXRuntime.cpp:13-31, 142(z-down gravity)

---

## 진단 적용 범위·미진단 항목

- [미진단] `FSkeletalMesh::Bones` 부모-자식 정렬을 실제 임포터가 보장하는지 (FBX 임포터 경로 별도 분석 필요)
- [미진단] 콘텐츠 디렉터리 내 `.sketbin` / `.physass`(또는 이에 준하는 파일)의 실재 여부
- [미진단] USkeletalMesh::Serialize 본문 — PhysicsAsset 포인터/경로 round-trip 여부
- [미진단] PhysicsGeometry.h 본문 — FKAggregateGeom / FKSphereElem / FKBoxElem / FKCapsuleElem 형상 정의가 본 대비 어디서 잰 LocalTransform을 갖는지 (write-back에 영향은 없으나 형상 위치 시각화 검증에 필요)
