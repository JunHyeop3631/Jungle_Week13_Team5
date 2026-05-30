# Ragdoll Diagnosis

대상 경로 확정:
- [IPhysicsRuntime.h](KraftonEngine/Source/Engine/Physics/IPhysicsRuntime.h)
- [PhysXRuntime.h](KraftonEngine/Source/Engine/Physics/PhysXRuntime.h), [PhysXRuntime.cpp](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp)
- [SkeletalMeshComponent.h](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h), [SkeletalMeshComponent.cpp](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp)
- [SkinnedMeshComponent.h](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.h), [SkinnedMeshComponent.cpp](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.cpp)
- [BodyInstance.h](KraftonEngine/Source/Engine/Physics/BodyInstance.h), [ConstraintInstance.h](KraftonEngine/Source/Engine/Physics/ConstraintInstance.h), [PhysicsTypes.h](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h)
- [PhysicsAsset.h](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.h), [PhysicsAsset.cpp](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.cpp)
- [BodySetup.h](KraftonEngine/Source/Engine/Physics/Asset/BodySetup.h), [BodySetup.cpp](KraftonEngine/Source/Engine/Physics/Asset/BodySetup.cpp)
- [PhysicsConstraintSetup.h](KraftonEngine/Source/Engine/Physics/Asset/PhysicsConstraintSetup.h)
- [PhysicsGeometry.h](KraftonEngine/Source/Engine/Physics/Asset/PhysicsGeometry.h)
- [SkeletalMesh.h](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMesh.h), [SkeletalMesh.cpp](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMesh.cpp)
- [SkeletalMeshAsset.h](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h)
- [World.h](KraftonEngine/Source/Engine/GameFramework/World.h), [World.cpp](KraftonEngine/Source/Engine/GameFramework/World.cpp)
- [MeshEditorWidget.Physics.cpp](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp)

---

## A. PhysicsRuntime 현재 존재 형태

- [없음] FPhysXRuntime을 인스턴스화하는 코드 — [World.cpp:315](KraftonEngine/Source/Engine/GameFramework/World.cpp#L315) `//PhysicsRuntime = std::make_unique<FPhysXRuntime>();` 주석 처리됨. 활성 인스턴스화 코드 0건.
- [없음] 인스턴스 보유 주체 — [World.h:140](KraftonEngine/Source/Engine/GameFramework/World.h#L140) `//std::unique_ptr<IPhysicsRuntime> PhysicsRuntime;` 멤버 선언 자체가 주석 처리됨. 다른 클래스/전역/지역 어디에도 보유처 없음.
- [없음] Initialize/Foundation 생성 보장 위치 — [PhysXRuntime.cpp:107-164](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L107) `FPhysXRuntime::Initialize()` 본체는 구현돼 있으나 호출되는 곳 0건. [World.cpp:316](KraftonEngine/Source/Engine/GameFramework/World.cpp#L316) `//PhysicsRuntime->Initialize();` 주석 처리.
- [없음] `Simulate(dt)` 호출 — [World.cpp:370](KraftonEngine/Source/Engine/GameFramework/World.cpp#L370) `//PhysicsRuntime->Simulate(DeltaTime);` 주석 처리. 활성 호출 0건. dt 출처는 주석 인자상 `UWorld::Tick`의 DeltaTime이었음.

참고:
- [World.cpp:309-312](KraftonEngine/Source/Engine/GameFramework/World.cpp#L309) `PhysicsScene = std::make_unique<FPhysXPhysicsScene>(); PhysicsScene->Initialize(this);` — IPhysicsScene 경로는 별개로 살아 있음 (ragdoll 진단과 무관).

---

## B. CreateRagdoll 인프라 — 호출 사슬

- [확인됨] `CreateRagdoll()` 본문 구현 — [SkeletalMeshComponent.cpp:576-615](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L576). 가드: `!PhysicsRuntimeOwner || Bodies.empty()` 시 조기 리턴. 본문은 `SetBodyTransform(teleport)` + `SetBodyType(Dynamic)` 루프 + `bSimulatingPhysics = true`.
- [없음] `CreateRagdoll()` 호출자 — 전체 코드베이스 grep 결과 정의처([SkeletalMeshComponent.h:84](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h#L84), [SkeletalMeshComponent.cpp:576](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L576)) 외 호출 0건. Lua 바인딩([LuaScriptManager.cpp:1278-1295](KraftonEngine/Source/Engine/Lua/LuaScriptManager.cpp#L1278))에도 등록 안 됨.
- [없음] `InstantiatePhysicsAssetBodies(Runtime)` 호출자 — 전체 코드베이스 grep 결과 정의처([SkeletalMeshComponent.cpp:383, 389](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L383)) 외 호출 0건.
- [확인됨] Runtime 참조 수신 경로 — `InstantiatePhysicsAssetBodies(IPhysicsRuntime& Runtime, ...)`의 인자로 받아 [SkeletalMeshComponent.cpp:400](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L400) `PhysicsRuntimeOwner = &Runtime;` 멤버로 저장. `CreateRagdoll()`은 이 멤버를 참조([SkeletalMeshComponent.cpp:579, 607, 610](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L579)).
- [확인됨] `TickComponent`의 물리 write-back 분기 — [SkeletalMeshComponent.cpp:617-634](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L617). 분기 플래그: `bSimulatingPhysics`. true → `ApplyPhysicsToBones()` + `UMeshComponent::TickComponent`. false → 기존 `EvaluateAnimInstance` 경로.
- [확인됨] body type 전환 경로 — `IPhysicsRuntime::SetBodyType` ([IPhysicsRuntime.h:26](KraftonEngine/Source/Engine/Physics/IPhysicsRuntime.h#L26)), 구현 [PhysXRuntime.cpp:488-528](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L488) (kinematic↔dynamic flag + wakeUp). `CreateRagdoll()`에서 [SkeletalMeshComponent.cpp:610](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L610) `SetBodyType(Body, Dynamic)` 호출.
- [없음] `UPrimitiveComponent::SetSimulatePhysics`와 ragdoll 경로 연결 — [PrimitiveComponent.cpp:117-122](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp#L117) `SetSimulatePhysics`는 `bSimulatePhysics` 플래그 + `NotifyPhysicsBodyDirty()`(IPhysicsScene 경로). `SkeletalMeshComponent`는 이 함수 override 안 함. `bSimulatingPhysics`(별도 멤버)와도 무관.

호출 그래프:
```
[트리거 없음] ─✕→ InstantiatePhysicsAssetBodies(Runtime) ─✕→ CreateRagdoll() ─✕→ Simulate(dt) ─✕→ ApplyPhysicsToBones
     ▲                  ▲                                   ▲                    ▲                    ▲
     │                  │                                   │                    │                    │
   호출자 0           호출자 0                          호출자 0       Runtime 인스턴스 0       bSimulatingPhysics 진입 0
```
- 모든 화살표 끊김.
- 단, `Bodies != empty` & `PhysicsRuntimeOwner != null` 인 상태가 외부에서 만들어지기만 하면 `CreateRagdoll()` 본문은 동작하도록 작성돼 있음.
- 마찬가지로 `bSimulatingPhysics == true`만 되면 `TickComponent` → `ApplyPhysicsToBones` 분기는 코드상 연결돼 있음.

---

## C. Bone ↔ PhysicsAsset bone 대응

- [확인됨] `UBodySetup` 본 참조 방식 — [BodySetup.h:32](KraftonEngine/Source/Engine/Physics/Asset/BodySetup.h#L32) `FString BoneName;` (문자열). 인덱스 필드 없음.
- [확인됨] `BoneName → BoneIndex` 해석 경로 — [SkeletalMeshComponent.cpp:413](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L413) `const int32 BoneIndex = FindBoneIndex(BodySetup->BoneName);`. `FindBoneIndex` 정의 [SkinnedMeshComponent.cpp:276-289](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.cpp#L276) — 선형 탐색 `Asset->Bones[i].Name == BoneName`, 실패 시 `-1`.
- [확인됨] `FindBoneIndex == -1` 처리 — [SkeletalMeshComponent.cpp:414-418](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L414) `if (BoneIndex < 0 || BoneIndex >= Bodies.size())` → `UE_LOG("PhysicsAsset body skipped: bone not found")` + `continue`. 실패 본은 조용히 skip + 로그 1줄. 증상 예측: 이름 mismatch가 있으면 해당 body가 생성되지 않고 `Bodies[i]` 슬롯이 `nullptr`로 남음 → constraint도 부모/자식 한쪽이 빠지면 같이 skip. ragdoll 진입 후엔 해당 본은 anim/ref pose에 머무름(`ApplyPhysicsToBones`의 `Body && Body->bValid` 분기 [SkeletalMeshComponent.cpp:677](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L677)).
- [확인됨] 생성 Body의 `Bodies[BoneIndex]` 저장 — [SkeletalMeshComponent.cpp:401, 454](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L401) `Bodies.assign(Asset->Bones.size(), nullptr); ... Bodies[BoneIndex] = Body;`. 본 배열과 같은 크기 + 인덱스 일치.
- [확인됨] 초기 WorldTransform 출처 — [SkeletalMeshComponent.cpp:421, 433](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L421) `GetBoneWorldTransformByIndex(BoneIndex, BoneWorldTransform); BodyDesc.WorldTransform = BoneWorldTransform;`. `GetBoneWorldTransformByIndex` 정의 [SkinnedMeshComponent.cpp:291-302](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.cpp#L291) — `GlobalMatrices[BoneIndex] * GetWorldMatrix()`. 한 점 응집 버그 가능성: 본 ref pose 자체가 정상이면 본별로 다른 transform이 나옴. 본 ref pose 자체가 모두 origin이거나 `GetReferenceLocalPose` 전부 Identity일 때만 응집(데이터 의존). 코드 경로 자체는 본별로 다른 위치 반환을 의도함.
- [없음] `BodySetup`의 본 대비 오프셋(local pose) — [BodySetup.h:30-46](KraftonEngine/Source/Engine/Physics/Asset/BodySetup.h#L30)에 본 기준 transform 필드 없음. 본 대비 오프셋은 `FKAggregateGeom` 내부 각 elem의 `Center/Rotation`만 존재([PhysicsGeometry.h:10-37](KraftonEngine/Source/Engine/Physics/Asset/PhysicsGeometry.h#L10)) — 이건 shape local-to-body 오프셋이지 body-to-bone 오프셋이 아님. 즉 body 원점 = 본 월드 transform이라는 전제로 [SkeletalMeshComponent.cpp:660-662](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L660) 주석 "body world == body actor pose 로 생성했기 때문에 body→bone 별도 오프셋 보정은 없다." 일관됨.
- [불명확] 실제 PhysicsAsset 데이터 인스턴스 존재 여부 — 디스크상 `Content/Data/hirasawa-yui/IdleWithSkin_SkeletalMesh_Physics.uasset` 파일 1개 확인됨. 그러나 런타임 자동 로드 경로 [없음]: [SkeletalMesh.cpp:6-37](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMesh.cpp#L6) `USkeletalMesh::Serialize`에 `PhysicsAsset` 직렬화 항목 없음. `USkeletalMesh::PhysicsAsset`([SkeletalMesh.h:60](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMesh.h#L60))은 default `nullptr`. 유일한 채움 경로는 에디터 — [MeshEditorWidget.Physics.cpp:109-128](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp#L109) `RenderPhysicsLayout` 안에서 `MakePhysicsAssetPath`/`LoadPhysicsAssetFromFile` 호출 시 `SkeletalMesh->PhysicsAsset = Loaded;`. 즉 MeshEditor 탭을 열지 않은 일반 런타임 흐름에서는 `Mesh->PhysicsAsset == nullptr` → [SkeletalMeshComponent.cpp:386, 395-398](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L386) 가드에서 false 리턴.

---

## D. write-back 정확성 전제

- [확인됨] 본 인덱스 오름차순 = parent→child 가정 — [SkeletalMeshAsset.h:216-222](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h#L216) `NormalizeBonePoseData()`에서 `Bone.ParentIndex >= 0 && Bone.ParentIndex < BoneIndex` 조건으로 부모 글로벌 누적. [SkinnedMeshComponent.cpp:956-959](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.cpp#L956) `BuildBoneEditGlobalMatrices`도 동일 가정. [SkeletalMeshComponent.cpp:652-693](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L652) `ApplyPhysicsToBones`도 동일 가정 + 코멘트로 명시.
- [확인됨] 본 트랜스폼 저장 형식 — [SkeletalMeshAsset.h:19-49](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h#L19) `FBone`: `FMatrix ReferenceLocalPose / ReferenceGlobalPose / SkinBindGlobalPose / LocalMatrix / GlobalMatrix / InverseBindPoseMatrix`. 모두 `FMatrix`(행렬).
- [확인됨] local vs world — 본 ref pose는 local, edit pose도 local matrix. body 트랜스폼은 world (`PxRigidActor::getGlobalPose`). 변환 경계: [SkeletalMeshComponent.cpp:662, 684](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L662) `ComponentWorldInv = GetWorldInverseMatrix(); ComponentGlobal = BodyWorld.ToMatrix() * ComponentWorldInv;`. 그 후 `LocalMatrix = ComponentGlobal * ParentGlobal.GetInverse()` ([SkeletalMeshComponent.cpp:685-687](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L685)).
- [확인됨] 루트 본 부모 처리 — [SkeletalMeshComponent.cpp:666-687](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L666) `ParentIndex >= 0 ? ComponentLocalGlobals[ParentIndex] : FMatrix::Identity`. 루트는 component global == local (parent inv 곱 안 함). 컴포넌트 월드 기준은 `ComponentWorldInv`로 흡수.
- [확인됨] PhysX TolerancesScale — [PhysXRuntime.cpp:120](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L120) `PxCreatePhysics(PX_PHYSICS_VERSION, *Foundation, PxTolerancesScale());` 기본값(length=1.0, mass=1000.0). 별도 스케일링 없음.
- [확인됨] 중력/up축 — [PhysXRuntime.cpp:142](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L142) `SceneDesc.gravity = PxVec3(0.0f, 0.0f, -9.81f);` (Z-up, 부호 −Z). 9.81 값은 m/s² 표준값.
- [확인됨] FVector ↔ PxVec3 변환 — [PhysXRuntime.cpp:13-21](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L13) 컴포넌트 단순 복사(스케일 변환 없음). 핸드니스 변환도 없음.
- [확인됨] FQuat ↔ PxQuat 변환 — [PhysXRuntime.cpp:18-31](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L18) 컴포넌트 단순 복사 `(X,Y,Z,W) ↔ (x,y,z,w)`.
- [불명확] 본 단위(cm vs m) — `FBone` 행렬 값의 단위는 코드에 명시 없음. 임포터/에셋이 어떤 단위로 행렬을 저장하는지 검증 불가. `PxTolerancesScale` 기본은 m 단위 가정. 본이 cm 단위로 저장돼 있으면 body 위치/관성/중력 스케일과 100배 불일치 가능 — 데이터 측에서만 검증 가능.

---

## 지금 당장 막고 있는 것 (우선순위)

1. [담당자 결정 대기] FPhysXRuntime 인스턴스가 어디에도 존재하지 않음 — [World.cpp:140, 315-316, 370](KraftonEngine/Source/Engine/GameFramework/World.cpp#L140) 전부 주석. `InstantiatePhysicsAssetBodies(Runtime)`에 넘길 Runtime 자체가 없어 호출 자체가 불가.
2. [확인됨/막힘] `InstantiatePhysicsAssetBodies` 호출자 0건 — 본문 구현은 됐지만 외부 트리거가 없어 `Bodies`/`PhysicsRuntimeOwner`가 채워지지 않음. (1번 해결 전엔 진입 불가 — 의존)
3. [확인됨/막힘] `CreateRagdoll` 호출자 0건 — 본문은 가드 통과 시 동작 가능하나 `Bodies/PhysicsRuntimeOwner` 미충전 상태에서는 [SkeletalMeshComponent.cpp:579-583](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L579) 가드로 조기 리턴. (2번 해결 전엔 진입 불가 — 의존)
4. [확인됨/막힘] 런타임에서 `USkeletalMesh::PhysicsAsset` 자동 로드 경로 없음 — [SkeletalMesh.cpp:6-37](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMesh.cpp#L6) `Serialize`에 PhysicsAsset 항목 없음. MeshEditor 탭 진입 없이는 `nullptr`. 게임 진입 흐름에선 `InstantiatePhysicsAssetBodies`가 [SkeletalMeshComponent.cpp:395-398](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L395)에서 false 리턴.
5. [확인됨/막힘] `Simulate(dt)` 호출 0건 — [World.cpp:370](KraftonEngine/Source/Engine/GameFramework/World.cpp#L370) 주석. 본문 자체는 [PhysXRuntime.cpp:228-247](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp#L228)에 구현됨. (1번 해결과 동일 결정에 의존)
6. [확인됨/막힘] `bSimulatingPhysics` 진입 조건이 `CreateRagdoll` 통과뿐 — 다른 진입 경로 없음. ([SkeletalMeshComponent.cpp:614](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L614) 외에 set 위치 없음.) 3번 해결 전엔 write-back 코드 도달 불가.
7. [불명확/검증 필요] 본 단위(cm/m) 일치 여부 — 데이터/임포터 차원에서만 확정 가능. PxTolerancesScale은 기본값(m 기준). 본 행렬이 cm 단위면 단위 불일치 의심.
8. [불명확/검증 필요] PhysicsAsset 인스턴스 데이터 검증 — 디스크 파일 1개(`IdleWithSkin_SkeletalMesh_Physics.uasset`)는 존재하지만, 그 안의 `BodySetup.BoneName`이 실제 mesh의 본 이름과 1:1로 매치하는지(`FindBoneIndex` 실패율) 데이터 inspect 전엔 알 수 없음.
