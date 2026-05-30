# Physics Infra 재인벤토리 (진단)

## 0. 메타

- **목표 사슬**: `World가 PhysicsRuntime 생성·관리 → PhysicsAsset 데이터 확보 → InstantiatePhysicsAssetBodies(Bodies 충전) → CreateRagdoll(bSimulatingPhysics=true) → World가 Simulate(dt) 구동 → ApplyPhysicsToBones write-back`
- **대체 대상(legacy) 1줄 명시**: `IPhysicsScene` 계열 — `Engine/Physics/IPhysicsScene.h` + 그 구현체 `FNativePhysicsScene` ([NativePhysicsScene.h/cpp](KraftonEngine/Source/Engine/Physics/NativePhysicsScene.h)) / `FPhysXPhysicsScene` ([PhysXPhysicsScene.h/cpp](KraftonEngine/Source/Engine/Physics/PhysXPhysicsScene.h)) — 그리고 World에서 보유하는 별개의 legacy nuget 기반 physics scene이 신규 `IPhysicsRuntime` 인프라로 대체될 “대체 대상”.
- **IPhysicsScene.h 제외 명시**: 본 인벤토리는 [IPhysicsScene.h](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h)와 그 구현·연결부를 탐색 대상에서 제외함. 발견되어도 인벤토리에 포함하지 않음. (`FNativePhysicsScene`, `FPhysXPhysicsScene`은 `class : public IPhysicsScene`로 상속 — 둘 다 legacy 범주로 분류, 본 인벤토리에서 제외.)
- **인벤토리 대상 = Engine/Physics root 파일 목록** (glob `KraftonEngine/Source/Engine/Physics/*`로 찾은 실제 경로 중 legacy 제외):
  - [IPhysicsRuntime.h](KraftonEngine/Source/Engine/Physics/IPhysicsRuntime.h)
  - [PhysXRuntime.h](KraftonEngine/Source/Engine/Physics/PhysXRuntime.h)
  - [PhysXRuntime.cpp](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp)
  - [PhysicsTypes.h](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h)
  - [BodyInstance.h](KraftonEngine/Source/Engine/Physics/BodyInstance.h)
  - [ConstraintInstance.h](KraftonEngine/Source/Engine/Physics/ConstraintInstance.h)
  - [PhysXCore.h](KraftonEngine/Source/Engine/Physics/PhysXCore.h)
  - [PhysXCore.cpp](KraftonEngine/Source/Engine/Physics/PhysXCore.cpp)
  - [PhysXSceneLock.h](KraftonEngine/Source/Engine/Physics/PhysXSceneLock.h)
  - (legacy로 제외: `IPhysicsScene.h`, `NativePhysicsScene.h/cpp`, `PhysXPhysicsScene.h/cpp`)

상태 태그: `[확인됨]` / `[없음]` / `[부분]` / `[불명확/검증필요]`

---

## A. PhysicsRuntime 생명주기 (World 소유 convention 기준)

### A.1 인터페이스 / 구현 존재
- [확인됨] `IPhysicsRuntime` 추상 인터페이스 선언 — [IPhysicsRuntime.h:5-36](KraftonEngine/Source/Engine/Physics/IPhysicsRuntime.h:5).
- [확인됨] `FPhysXRuntime` 구현 클래스 선언 — [PhysXRuntime.h:14-61](KraftonEngine/Source/Engine/Physics/PhysXRuntime.h:14).
- [확인됨] `FPhysXRuntime::Initialize()` 구현(Foundation/Physics/Scene/Vehicle SDK/Dispatcher/DefaultMaterial 생성) — [PhysXRuntime.cpp:326-388](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:326). 단 PhysXCore의 “공유 코어”(`AcquireSharedPhysXCore`)는 **사용하지 않고** 자체 `PxCreateFoundation/PxCreatePhysics`로 별도 인스턴스 생성 — [PhysXRuntime.cpp:333-339](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:333).
- [확인됨] `FPhysXRuntime::Simulate(dt)` 구현(차량 입력→Scene::simulate→fetchResults→Body CachedWorldTransform 동기화) — [PhysXRuntime.cpp:463-535](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:463).
- [확인됨] `FPhysXRuntime::Shutdown()` 구현 — [PhysXRuntime.cpp:390-461](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:390). 소멸자에서 호출 — [PhysXRuntime.cpp:321-324](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:321).

### A.2 World 소유 convention 준수 여부
- [부분] World가 IPhysicsScene(legacy)는 멤버로 보유 — [World.h:137](KraftonEngine/Source/Engine/GameFramework/World.h:137) `std::unique_ptr<IPhysicsScene> PhysicsScene;`.
- [확인됨] World가 `IPhysicsRuntime` 멤버를 보유 — [World.h:140](KraftonEngine/Source/Engine/GameFramework/World.h:140) `//std::unique_ptr<IPhysicsRuntime> PhysicsRuntime;`. 그 위 주석은 `// 추후 결정` — [World.h:139](KraftonEngine/Source/Engine/GameFramework/World.h:139).
- [확인됨] [World.cpp:315-316](KraftonEngine/Source/Engine/GameFramework/World.cpp:315)에서 `PhysicsRuntime = std::make_unique<FPhysXRuntime>(); PhysicsRuntime->Initialize();`로 사용, [World.cpp:370](KraftonEngine/Source/Engine/GameFramework/World.cpp:370)에서 `PhysicsRuntime->Simulate(DeltaTime);` 호출.  (진단 당시 IPhysicsRuntime이 주석처리 되었으나 사용자가 이 부분을 활성화 시킴. world의 physicsruntime 소유로 convension 확정)
- [확인됨] cpp 측 의도된 생명주기는 `InitWorld()`에서 생성+Initialize — [World.cpp:307-316](KraftonEngine/Source/Engine/GameFramework/World.cpp:307), `Tick()`에서 `bHasBegunPlay && PhysicsScene` 가드 안에서 `PhysicsRuntime->Simulate(DeltaTime)` 구동 — [World.cpp:364-371](KraftonEngine/Source/Engine/GameFramework/World.cpp:364). (legacy `PhysicsScene->Tick`과 “동시 존재” 코멘트 — [World.cpp:369](KraftonEngine/Source/Engine/GameFramework/World.cpp:369).)
- [없음] PhysicsRuntime의 명시적 Shutdown 호출은 World 어디에도 없음. `unique_ptr` 파괴 시 `~FPhysXRuntime()` → `Shutdown()` — [PhysXRuntime.cpp:321-324](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:321)로 처리되긴 함(헤더가 정상 선언될 경우).

### A.3 World 외 보유처
- [확인됨] `USkeletalMeshComponent`가 `IPhysicsRuntime* PhysicsRuntimeOwner = nullptr;`을 멤버로 들고 있음 — [SkeletalMeshComponent.h:117](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h:117). 이는 소유가 아닌 “참조”이며 `InstantiatePhysicsAssetBodies(Runtime)`에 외부 주입된 runtime 포인터를 기억하기 위한 필드 — [SkeletalMeshComponent.cpp:400](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:400).
- [불명확/검증필요] convention 위반(World 외 “생성·관리”)은 현 코드상 명시적으로 보이진 않음 — 모든 `make_unique<FPhysXRuntime>` 호출은 World.cpp 한 곳 — grep 결과 12개 파일 중 root 외 호출자는 없음(MeshEditor·Docs 등은 reference만).

---

## B. PhysicsAsset 데이터 확보 경로 (★최우선)

### B.1 데이터 모델 자체
- [확인됨] `UPhysicsAsset` 정의 — [PhysicsAsset.h:10-53](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.h:10). `BodySetups`/`ConstraintSetups`/`DisabledCollisionPairs` 보유.
- [확인됨] `UPhysicsAsset::Serialize` 구현 — [PhysicsAsset.cpp:91-135](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.cpp:91). 각 BodySetup/ConstraintSetup 라운드트립.
- [확인됨] `UBodySetup` 정의 — [BodySetup.h:26-48](KraftonEngine/Source/Engine/Physics/Asset/BodySetup.h:26). `BoneName` + `FKAggregateGeom` + 물리 파라미터.
- [확인됨] `UBodySetup::Serialize` 구현 — [BodySetup.cpp:4-18](KraftonEngine/Source/Engine/Physics/Asset/BodySetup.cpp:4).
- [확인됨] `FKAggregateGeom`(Sphere/Box/Capsule) 정의 — [PhysicsGeometry.h:10-50](KraftonEngine/Source/Engine/Physics/Asset/PhysicsGeometry.h:10).

### B.2 런타임 자동 로드 경로 (게임 진입 흐름)
- [확인됨] `USkeletalMesh`에 `UPhysicsAsset* PhysicsAsset = nullptr;` 포인터 멤버 존재 — [SkeletalMesh.h:60](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMesh.h:60).
- [없음] **`USkeletalMesh::Serialize`에 `PhysicsAsset` 직렬화 항목이 없음** — [SkeletalMesh.cpp:6-37](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMesh.cpp:6). `Vertices/Indices/Sections/MeshRanges/Bones/SkeletalMaterials/MorphTargets`만 라운드트립.
- [없음] 게임 진입 흐름에서 `Mesh->PhysicsAsset`이 채워지는 코드 경로가 grep 결과 발견되지 않음. `SkeletalMesh->PhysicsAsset = ...` 대입은 **에디터(`MeshEditorWidget.Physics.cpp:122,1751`)에서만** 발생.
- [확인됨] 결과적으로 `InstantiatePhysicsAssetBodies(Runtime)`이 `Mesh->PhysicsAsset`로 위임하는 1-인자 오버로드 — [SkeletalMeshComponent.cpp:383-387](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:383) — 는 런타임에 항상 `nullptr` 인자로 호출됨 (게임 진입 경로 한정).

### B.3 에디터 경로 (PhysicsAsset 생성/저장/편집)
- [확인됨] 에디터 별도 PhysicsAsset 파일 경로 규약 `MyMesh.uasset → MyMesh_Physics.uasset` — [MeshEditorWidget.Physics.cpp:65-71](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:65).
- [확인됨] 에디터 로드 경로(우선순위 mesh→file→empty) — [MeshEditorWidget.Physics.cpp:107-129](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:107). 파일 로드 시 `SkeletalMesh->PhysicsAsset = Loaded;` 대입 — [MeshEditorWidget.Physics.cpp:122](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:122).
- [확인됨] 에디터 저장 경로 `SavePhysicsAsset()` — [MeshEditorWidget.Physics.cpp:1703-1752](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1703). `EAssetPackageType::PhysicsAsset` 헤더로 별도 `.uasset`에 저장. 메모리상 `SkeletalMesh->PhysicsAsset = PhysicsAsset;` — [MeshEditorWidget.Physics.cpp:1751](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1751).
- [확인됨] 에디터 ContentBrowser에서 PhysicsAsset 식별 — [ContentBrowser.cpp:439-440](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowser.cpp:439), `PhysicsAssetElement::OnDoubleLeftClicked` 핸들러 존재 — [ContentBrowserElement.cpp:836](KraftonEngine/Source/Editor/UI/ContentBrowser/ContentBrowserElement.cpp:836).
- [확인됨] BodySetup 추가/삭제 UI — [MeshEditorWidget.Physics.cpp:393](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:393) (`GetOrCreateBodySetup`), [:509](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:509) (`RemoveBodySetup`).
- [확인됨] Geometry(Sphere/Box/Capsule) 추가 UI 핸들러 — [MeshEditorWidget.Physics.cpp:412-432](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:412).
- [확인됨] Constraint 생성/삭제 UI — [MeshEditorWidget.Physics.cpp:457](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:457), [:206](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:206).
- [확인됨] 셰이프 기즈모 reconcile — [MeshEditorWidget.Physics.cpp:1124-1159](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1124). 디테일 패널(`RenderBodySetupDetails`/`RenderConstraintDetails`) 분기 — [MeshEditorWidget.Physics.cpp:533-547](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:533).
- [없음] `GeneratePhysicsBodies()`는 stub — [MeshEditorWidget.Physics.cpp:1060-1066](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1060) (`TODO(physics): … 현재는 UI hook 만 열어둔 stub 상태`).

### B.4 “에디터만으로 최소 PhysicsAsset을 만들 수 있는가?”
- [확인됨] 에디터 UI 상에서: 본 우클릭 → Add Body → Sphere/Box/Capsule로 `UBodySetup` + `AggregateGeom` 요소 1개 생성 가능 — [MeshEditorWidget.Physics.cpp:386-432](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:386). Save 버튼으로 `_Physics.uasset` 직렬화 가능 — [:136, :1703-1752](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:136). 결론: **최소 PhysicsAsset 생성·저장은 에디터만으로 가능**.

### B.5 디스크상 기존 physics asset 데이터 경로와 로드 경로 매칭
- [확인됨] 에디터 저장 경로: `MakePhysicsAssetPath(MeshPath)` = mesh 파일과 동일 디렉토리에 `_Physics` suffix — [MeshEditorWidget.Physics.cpp:65-71](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:65).
- [확인됨] 에디터 로드 경로: 동일 규약으로 file system에서 `_Physics.uasset` 탐색 — [MeshEditorWidget.Physics.cpp:118-119](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:118).
- [없음] 런타임(게임 진입)에서 `_Physics.uasset` 또는 `Mesh->PhysicsAsset` 자동 매칭/로드 코드 부재 — grep `LoadPhysicsAssetFromFile` 단일 출현(에디터). USkeletalMesh::Serialize 미직렬화(B.2)와 종합하면, 디스크의 PhysicsAsset은 **에디터를 거치지 않은 게임 진입 흐름에 도달하지 못함**.

---

## C. Bone ↔ Body 대응

### C.1 BodySetup의 bone 참조 방식
- [확인됨] `UBodySetup::BoneName : FString` — [BodySetup.h:32](KraftonEngine/Source/Engine/Physics/Asset/BodySetup.h:32). bone “이름” 기반 참조.
- [확인됨] BoneName → BoneIndex 해석은 `USkinnedMeshComponent::FindBoneIndex` 선형 탐색 — [SkinnedMeshComponent.cpp:276-289](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.cpp:276). `Asset->Bones[i].Name` 일치 비교.
- [확인됨] 실패 처리: 못 찾으면 `UE_LOG("PhysicsAsset body skipped: bone not found. Bone=%s")`로 스킵 — [SkeletalMeshComponent.cpp:413-418](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:413). Constraint의 ParentBoneIndex/ChildBoneIndex 어느 한쪽이라도 Body 없으면 스킵 — [SkeletalMeshComponent.cpp:472-482](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:472).

### C.2 생성 Body의 저장 배열/인덱스
- [확인됨] `USkeletalMeshComponent::Bodies` — `TArray<FBodyInstance*>` — [SkeletalMeshComponent.h:115](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h:115).
- [확인됨] 배열을 본 인덱스 크기로 미리 할당하고 BoneIndex로 직접 인덱싱: `Bodies.assign(Asset->Bones.size(), nullptr); ... Bodies[BoneIndex] = Body;` — [SkeletalMeshComponent.cpp:401, :454](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:401). 즉 Body 배열의 인덱스 = bone 인덱스 (sparse, 빈 본은 nullptr).
- [확인됨] `GetBodyInstanceByBoneIndex` / `GetBodyInstanceByBoneName` — [SkeletalMeshComponent.cpp:561-574](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:561). 범위 가드 + 미존재 시 nullptr.
- [확인됨] `FBodyInstance::BoneIndex` 필드 존재 — [BodyInstance.h:11](KraftonEngine/Source/Engine/Physics/BodyInstance.h:11). 생성 시 `Desc.BoneIndex` → Body로 복사 — [PhysXRuntime.cpp:577](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:577).

### C.3 초기 WorldTransform 출처
- [확인됨] Body의 초기 `WorldTransform = BoneWorldTransform` (`GetBoneWorldTransformByIndex`) — [SkeletalMeshComponent.cpp:421, :433](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:421).
- [확인됨] `GetBoneWorldTransformByIndex`는 `BuildBoneEditGlobalMatrices`(component-local) × `GetWorldMatrix()`(actor) — [SkinnedMeshComponent.cpp:291-302](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.cpp:291).
- [없음] **body → bone 오프셋 필드 없음.** `FBodyInstance`에 별도 local offset 필드 없음 — [BodyInstance.h:5-36](KraftonEngine/Source/Engine/Physics/BodyInstance.h:5). 의도 주석: “body world == body actor pose 로 생성했기 때문에 body→bone 별도 오프셋 보정은 없다” — [SkeletalMeshComponent.cpp:661](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:661).
- [확인됨] BodySetup의 shape별 `LocalTransform`은 존재 (`FKBoxElem::Center/Rotation`, `FKCapsuleElem::Center/Rotation`, `FKSphereElem::Center`) — shape local transform으로만 들어가고 body actor pose 자체는 bone world와 동일하게 생성됨 — [SkeletalMeshComponent.cpp:80, 93, 104](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:80).

---

## D. Write-back 정확성 전제

### D.1 bone 인덱스 오름차순 = parent → child 가정
- [확인됨] write-back 측 의존: `// 본 인덱스 오름차순 = parent-first 가 엔진 규약이라 단순 순회로 부모 글로벌이 항상 채워진 뒤 자식이 사용한다.` — [SkeletalMeshComponent.cpp:652-653](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:652).
- [확인됨] 구현에서 `Asset->Bones[BoneIndex].ParentIndex`로 누적 — [SkeletalMeshComponent.cpp:664-670](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:664). `ParentIndex < BoneIndex` 가정 의존.
- [확인됨] 동일 가정이 에디터 트리 렌더링에도 사용 — [MeshEditorWidget.Physics.cpp:522-527](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:522) (`for (int32 i = BoneIndex+1; …) if (Bones[i].ParentIndex == BoneIndex)`).
- [불명확/검증필요] FBX 임포트 직후 본 배열이 실제로 parent-first로 정렬되는지는 본 인벤토리 범위 밖. 가정과 데이터의 정합성 검증 필요(별도 사이클).

### D.2 bone transform 저장 형식, local ↔ world 변환 경계
- [확인됨] PhysX → engine 변환 경계: `ApplyPhysicsToBones`에서 `BodyWorld → ComponentGlobal = BodyWorld.ToMatrix() * ComponentWorldInv;` → `LocalMatrix = ComponentGlobal * ParentGlobal.GetInverse();` — [SkeletalMeshComponent.cpp:678-688](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:678).
- [확인됨] 루트 본 처리: `ParentIndex < 0`이면 `ParentGlobal = Identity`라 `LocalMatrix = ComponentGlobal` — [SkeletalMeshComponent.cpp:667-687](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:667).
- [확인됨] 저장 형식: `TArray<FTransform> LocalPose` 후 `SetBoneLocalTransforms(LocalPose)` 호출 — [SkeletalMeshComponent.cpp:692, 696](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:692). 동작은 [SkinnedMeshComponent.cpp:608](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.cpp:608) (`SetBoneLocalTransforms`).
- [확인됨] Body 없는 본은 `RefLocal` 유지 → `ComponentGlobal = RefLocal * ParentGlobal` — [SkeletalMeshComponent.cpp:670-675](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:670).

### D.3 PhysX TolerancesScale, 중력/up축, 핸드니스/스케일
- [확인됨] TolerancesScale: 디폴트(`PxTolerancesScale()`) — [PhysXRuntime.cpp:339](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:339). 길이 단위 1.0 가정(미터/디폴트).
- [확인됨] 중력: `PxVec3(0, 0, -9.81f)` (Z-up, 음의 Z 방향) — [PhysXRuntime.cpp:366](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:366). Vehicle SDK basis도 up=`(0,0,1)`, forward=`(1,0,0)` — [PhysXRuntime.cpp:354](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:354).
- [확인됨] FVector ↔ PxVec3 변환: 단순 X/Y/Z 패스스루 — [PhysXRuntime.cpp:14-17](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:14). 스케일 변환 없음.
- [확인됨] FQuat ↔ PxQuat: X/Y/Z/W 순서 그대로 — [PhysXRuntime.cpp:19-32](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:19). 핸드니스 보정 없음.
- [확인됨] PxTransform = `(PxVec3, PxQuat)` 단순 합성 — [PhysXRuntime.cpp:34-42](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:34). FTransform.Scale은 `(1,1,1)`로 강제됨(역변환 시) — [PhysXRuntime.cpp:41](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:41).

### D.4 bone 단위 (cm vs m) 일치 여부
- [불명확/검증필요] PhysXRuntime은 `PxTolerancesScale()`(=1.0=meter) 디폴트로 초기화 — [PhysXRuntime.cpp:339](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:339). 중력 -9.81 m/s² — [PhysXRuntime.cpp:366](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:366).
- [불명확/검증필요] 엔진 측 본 transform 출처가 cm인지 m인지 본 인벤토리 범위 내 자료로 단정 불가. `FPhysicsShapeDesc`의 기본값은 `HalfExtent=(50,50,50)`, `Radius=50`, `HalfHeight=100` — [PhysicsTypes.h:91-93](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h:91), 차량 `FVehicle4WDesc`의 ChassisHalfExtents/WheelRadius 등은 `1.6/0.35` 등의 미터 스케일 — [PhysicsTypes.h:187-216](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h:187). **두 단위가 한 자료에 혼재**되어 있어 일관성 단정 불가 → [불명확].

### D.5 (그 외) PxScene 플래그
- [확인됨] `eENABLE_ACTIVE_ACTORS | eENABLE_CCD | eENABLE_PCM` — [PhysXRuntime.cpp:369-371](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:369). 활성 actor 추적 가능.
- [확인됨] CreateRigidBody 시 dynamic이면 `bUseGravity → eDISABLE_GRAVITY` 반전, CCD/LinearDamping/AngularDamping 적용 — [PhysXRuntime.cpp:560-566](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:560).
- [확인됨] CreateRigidBody 직후 Body 캐시는 `Desc.WorldTransform`로 설정, `bSimulating = (BodyType == Dynamic)` — [PhysXRuntime.cpp:580-583](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:580).

---

## E. 트리거/전환 경로

### E.1 호출자 grep (전 디렉토리)
- [없음] `InstantiatePhysicsAssetBodies` 호출자: 코드 내 출현 = **자기 정의(SkeletalMeshComponent.cpp:383, :389)와 1-인자→2-인자 위임 내부 호출이 전부**. 외부 호출자 없음 (Docs 제외). grep 결과는 `SkeletalMeshComponent.h/.cpp`만 (다른 cpp 0건).
- [없음] `CreateRagdoll` 호출자: 동일하게 외부 호출자 없음 — `SkeletalMeshComponent.h:84`(선언), `:576`(정의)만.
- [없음] `ApplyPhysicsToBones` 호출자: `SkeletalMeshComponent.cpp:622` (TickComponent 안 `if (bSimulatingPhysics)` 분기), `:636`(정의), `:99`(선언), `:661`(주석)뿐.
- [확인됨] `SetBodyType` 호출자: `CreateRagdoll` 안에서 `EPhysicsBodyType::Dynamic`으로 1회 — [SkeletalMeshComponent.cpp:610](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:610). Runtime 측 구현 — [PhysXRuntime.cpp:1085-1125](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:1085).
- [없음] 에디터 시뮬레이션 진입점은 stub — `FMeshEditorWidget::StartPhysicsSimulation()` `UE_LOG("[Physics] StartPhysicsSimulation() is not implemented yet (UI stub).");` — [MeshEditorWidget.Physics.cpp:1074-1078](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1074). `StopPhysicsSimulation`, `TickPhysicsSimulation`도 stub — [:1080-1088](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1080).
- [없음] `GeneratePhysicsBodies()` (다발 BodySetup 자동 생성) stub — [MeshEditorWidget.Physics.cpp:1058-1066](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1058).

### E.2 TickComponent의 anim ↔ physics write-back 분기
- [확인됨] 분기 플래그: `bSimulatingPhysics` (private bool, default false) — [SkeletalMeshComponent.h:120](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h:120). 접근자 `IsSimulatingPhysics()` — [:88](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h:88).
- [확인됨] 진입 조건: `CreateRagdoll()` 끝에서 `bSimulatingPhysics = true;` — [SkeletalMeshComponent.cpp:614](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:614). `CreateRagdoll`은 가드: `PhysicsRuntimeOwner` 비어있거나 `Bodies.empty()`면 `UE_LOG("CreateRagdoll skipped: physics asset bodies are not instantiated.")` 후 return — [:579-583](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:579).
- [확인됨] TickComponent 분기: `if (bSimulatingPhysics) { ApplyPhysicsToBones(); UMeshComponent::TickComponent(...); return; }`로 anim 평가 skip — [SkeletalMeshComponent.cpp:617-625](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:617).
- [확인됨] 해제 경로 부재. `bSimulatingPhysics = false`로 되돌리는 코드 없음(스택에서 ragdoll에서 다시 anim으로 복귀 불가).

### E.3 Bodies 충전 직전 단계 (CreateRagdoll의 사전조건)
- [확인됨] `CreateRagdoll`는 `PhysicsRuntimeOwner && !Bodies.empty()` 전제 — [SkeletalMeshComponent.cpp:579](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:579).
- [확인됨] 즉 누군가가 먼저 `InstantiatePhysicsAssetBodies(Runtime)`를 호출해야 함 — 이 호출자 없음(E.1).

---

## 지금 당장 막고 있는 것 (의존 순서대로)

> 해결책 비제시. 사실/근거만.

1. **[불명확/검증필요] World가 IPhysicsRuntime을 멤버로 “선언”하지 않음 — World.cpp의 사용이 컴파일 가능한지 검증 필요.**
   - 근거: [World.h:140](KraftonEngine/Source/Engine/GameFramework/World.h:140) `//std::unique_ptr<IPhysicsRuntime> PhysicsRuntime;` 주석 처리 ↔ [World.cpp:315-316, 370](KraftonEngine/Source/Engine/GameFramework/World.cpp:315) 사용.
   - 의존: 모든 후속 단계(2~6)의 전제. Runtime이 World 인스턴스 멤버로 살아있지 않으면 그 어떤 호출 사슬도 동작 불가.
   - 부수 결과: convention(“World가 생성·관리”)가 cpp 의도는 충족하지만 헤더 선언이 빠져있다는 의미에서 **현 시점 [부분]** 상태.

2. **[없음] USkeletalMesh::Serialize가 PhysicsAsset 필드를 라운드트립하지 않음 — 런타임 자동 로드 경로 부재.**
   - 근거: [SkeletalMesh.cpp:6-37](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMesh.cpp:6)에 PhysicsAsset 직렬화 코드 없음. 멤버는 [SkeletalMesh.h:60](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMesh.h:60)에 선언만 존재.
   - 의존: 1에 의존(Runtime이 있어야 의미 있음). 3의 InstantiatePhysicsAssetBodies(1-인자) 경로의 입력원이 됨.

3. **[없음] InstantiatePhysicsAssetBodies / CreateRagdoll 호출자 부재.**
   - 근거: grep 결과 코드 호출자 0건 (Docs/본인 정의 제외). 게임 entry-point/에디터 UI 어디서도 호출되지 않음.
   - 의존: 2 + 1. 호출이 일어나도 Bodies 충전에 필요한 PhysicsAsset 또는 Runtime이 없으면 가드(early return)에 막힘 — [SkeletalMeshComponent.cpp:395-398](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:395), [:579-583](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:579).

4. **[없음] 에디터의 시뮬레이션 진입점(`StartPhysicsSimulation`/`TickPhysicsSimulation`/`StopPhysicsSimulation`/`GeneratePhysicsBodies`)이 stub.**
   - 근거: [MeshEditorWidget.Physics.cpp:1058-1088](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1058).
   - 의존: 3을 부르는 “현실적 첫 호출자” 후보. 현 상태로는 에디터 prediction 경로로도 ragdoll write-back 검증 불가.

5. **[없음] body→bone offset 필드 부재 + Body actor pose == bone world 가정 검증 필요.**
   - 근거: [BodyInstance.h:5-36](KraftonEngine/Source/Engine/Physics/BodyInstance.h:5)에 offset 필드 없음. [SkeletalMeshComponent.cpp:661](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:661)의 의도 주석.
   - 의존: 3이 동작한 후 write-back 정확성에 직접 영향. Shape `LocalTransform`(BodySetup의 capsule/box/sphere center+rotation)만으로 bone↔body 대응이 정확한지 검증 필요(별도 사이클).

6. **[불명확/검증필요] bone 단위(cm vs m) 일관성, parent-first 본 순서 보장.**
   - 근거 단위: PhysXRuntime은 디폴트 PxTolerancesScale + `(50,50,50)`/`1.6` 혼재 — [PhysicsTypes.h:91, 187](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h:91), [PhysXRuntime.cpp:339, 366](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:339).
   - 근거 순서: 가정만 명시 — [SkeletalMeshComponent.cpp:652-653](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:652). 데이터 측 보장 확인은 별도 사이클.

(끝)
