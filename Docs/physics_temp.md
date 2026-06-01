# Physics 인프라 전수 조사 (physics_temp.md)

> 작성 목적: ragdoll 생성 / joint 세부 설정 / PhysicsAsset 로 body 저장 / **SkeletalMesh 본 기준 임의 depth 까지 body 자동 생성(언리얼 PhAT 스타일)** 기능을 추가하기 위해, 현재 KraftonEngine 의 물리 관련 인프라를 전부 찾아 한 곳에 정리한 문서.
>
> 작성일 기준 브랜치: `feature/joint`. **이 문서는 실제 코드를 직접 읽어 검증한 내용**이며, 코드는 일절 수정하지 않았다.

---

## 0. 가장 먼저 — 정식 인터페이스 이름 정정 ⚠️

- **현재 정식 물리 씬 추상 인터페이스는 `IPhysicsScene` 이다.** (구현체: `FPhysXRuntime`)
- 과거의 `IPhysicsRuntime` 는 커밋 `45d16370 refactor: Rename IPhysicsRuntime to IPhysicsScene` 로 **전면 개명**되었고, 소스 전체에서 `IPhysicsRuntime` 매치는 **0건**(완전 제거 확인). `IPhysicsScene` 은 11개 파일 45곳에서 사용 중.
- ⚠️ 일부 오래된 메모/문서(`Docs/PhysX_재분담안*.md`, `준협님엔진.md`, 진단 문서들)는 아직 `IPhysicsRuntime` 를 정식인 것처럼 적고 있으나 **이는 stale 이다.** 신규 코드는 반드시 `IPhysicsScene` 를 쓸 것.

근거: [IPhysicsScene.h:9](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:9) (`class IPhysicsScene`), grep 결과 `IPhysicsRuntime` 0건.

---

## 1. 디렉터리 / 파일 맵

### 1.1 런타임 코어 (PhysX 래핑)
`KraftonEngine/Source/Engine/Physics/`
| 파일 | 역할 |
|---|---|
| [IPhysicsScene.h](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h) | 물리 씬 순수 가상 인터페이스 (35개 메서드) |
| [PhysXRuntime.h](KraftonEngine/Source/Engine/Physics/PhysXRuntime.h) / [.cpp](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp) | `FPhysXRuntime` — IPhysicsScene 의 PhysX 4.1 구현 (~1700줄) |
| [PhysXCore.h](KraftonEngine/Source/Engine/Physics/PhysXCore.h) / [.cpp](KraftonEngine/Source/Engine/Physics/PhysXCore.cpp) | 공유 싱글턴(PxFoundation/PxPhysics/PVD/Extensions/Vehicle SDK) ref-count 관리 |
| [PhysXHelpers.h](KraftonEngine/Source/Engine/Physics/PhysXHelpers.h) | FVector↔PxVec3 등 변환, 지오메트리 빌더 |
| [PhysXSceneLock.h](KraftonEngine/Source/Engine/Physics/PhysXSceneLock.h) | `PHYSX_SCENE_READ_LOCK` / `PHYSX_SCENE_WRITE_LOCK` 매크로 |
| [PhysicsTypes.h](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h) | 모든 enum / 핸들 / Desc 구조체 |
| [BodyInstance.h](KraftonEngine/Source/Engine/Physics/BodyInstance.h) | `FBodyInstance` 런타임 바디 |
| [ConstraintInstance.h](KraftonEngine/Source/Engine/Physics/ConstraintInstance.h) | `FConstraintInstance` 런타임 조인트 |

### 1.2 물리 자산 데이터 모델 (UObject 직렬화 자산)
`KraftonEngine/Source/Engine/Physics/Asset/`
| 파일 | 역할 |
|---|---|
| [PhysicsAsset.h](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.h) / [.cpp](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.cpp) | `UPhysicsAsset` — 한 캐릭터의 모든 물리 설정 최상위 자산 |
| [BodySetup.h](KraftonEngine/Source/Engine/Physics/Asset/BodySetup.h) / [.cpp](KraftonEngine/Source/Engine/Physics/Asset/BodySetup.cpp) | `UBodySetup` — 본 단위 물리 설계도 |
| [PhysicsConstraintSetup.h](KraftonEngine/Source/Engine/Physics/Asset/PhysicsConstraintSetup.h) / [.cpp](KraftonEngine/Source/Engine/Physics/Asset/PhysicsConstraintSetup.cpp) | `UPhysicsConstraintSetup` — 관절 제한 설계도 |
| [PhysicsGeometry.h](KraftonEngine/Source/Engine/Physics/Asset/PhysicsGeometry.h) / [.cpp](KraftonEngine/Source/Engine/Physics/Asset/PhysicsGeometry.cpp) | `FKSphereElem`/`FKBoxElem`/`FKCapsuleElem`/`FKAggregateGeom` |

### 1.3 스켈레톤 / 본 인프라
| 파일 | 역할 |
|---|---|
| [SkeletalMeshAsset.h](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h) | `FBone`, `FSkeletalMesh` (본 배열, parent-first) |
| [SkeletalMesh.h](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMesh.h) | `USkeletalMesh` (→ `UPhysicsAsset* PhysicsAsset` 포인터 보유) |
| [Skeleton.h](KraftonEngine/Source/Engine/Animation/Skeleton/Skeleton.h), [SkeletonTypes.h](KraftonEngine/Source/Engine/Animation/Skeleton/SkeletonTypes.h) | `USkeleton`, `FReferenceSkeleton`, `FReferenceBone` |
| [SkinnedMeshComponent.h](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.h) / .cpp | 본 인덱스/이름 lookup, 본 월드 트랜스폼 접근 |
| [SkeletalMeshComponent.h](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h) / [.cpp](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp) | **래그돌 통합 지점** (Bodies/Constraints, Instantiate/CreateRagdoll/ApplyPhysicsToBones) |

### 1.4 에디터 / 툴링 (ImGui)
| 파일 | 역할 |
|---|---|
| [MeshEditorWidget.h](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.h) | Physics 탭 상태 `FPhysicsEditTabState` + `FBodyCreationSettings` |
| [MeshEditorWidget.Physics.cpp](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp) | Physics 탭 전체 UI (1868줄): 바디/셰이프/조인트 편집, 그래프, 시뮬, 저장 |
| [PhysicsShapeGizmoTarget.h](KraftonEngine/Source/Editor/UI/Asset/Physics/PhysicsShapeGizmoTarget.h) | 셰이프(구/박스/캡슐) 기즈모 타겟 |
| [PhysicsConstraintGizmoTarget.h](KraftonEngine/Source/Editor/UI/Asset/Physics/PhysicsConstraintGizmoTarget.h) | 조인트 앵커 프레임 기즈모 타겟 |
| [PhysicsShapeDebugComponent.h](KraftonEngine/Source/Engine/Component/Debug/PhysicsShapeDebugComponent.h) | 물리 셰이프 디버그 시각화 컴포넌트 |
| [PhysicsShapeDebugSceneProxy.h](KraftonEngine/Source/Engine/Render/Proxy/PhysicsShapeDebugSceneProxy.h) | 위 컴포넌트의 씬 프록시 |

### 1.5 World 통합
| 파일 | 역할 |
|---|---|
| [World.h](KraftonEngine/Source/Engine/GameFramework/World.h) / [World.cpp](KraftonEngine/Source/Engine/GameFramework/World.cpp) | `std::unique_ptr<IPhysicsScene> PhysicsScene` 소유, Tick 에서 `Simulate` 호출 |
| [PrimitiveComponent.cpp](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp) | BeginPlay/EndPlay 에서 RegisterComponent/Unregister |

---

## 2. 레이어 A — 런타임 / 씬 추상화

### 2.1 IPhysicsScene 인터페이스
[IPhysicsScene.h:9-71](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:9). 순수 가상 메서드 그룹:

- **수명주기**: `Initialize()`, `Shutdown()`, `Simulate(float DeltaTime)` — [14-16](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:14)
- **컴포넌트 등록**: `RegisterComponent`/`UnregisterComponent`/`RebuildBody` — [18-20](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:18)
- **리지드 바디**: `FBodyInstance* CreateRigidBody(const FPhysicsBodyDesc&)`, `DestroyRigidBody` — [22-23](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:22)
- **셰이프**: `FPhysicsShapeHandle CreateShape(FBodyInstance*, const FPhysicsShapeDesc&)` — [25](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:25)
- **조인트(D6 한정)**: `FConstraintInstance* CreateD6Joint(const FPhysicsConstraintDesc&)`, `DestroyJoint` — [27-28](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:27) ← **래그돌/조인트 핵심**
- **차량 4W**: CreateVehicle4W / Destroy / SetInput / GetWheelTransforms — [30-33](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:30)
- **힘/속도/질량**: AddForce/AddTorque, Get/SetLinearVelocity, Get/SetMass, Get/SetCenterOfMass — [35-47](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:35)
- **씬 쿼리**: `Raycast`, `RaycastByObjectTypes`, `SphereSweepShapeComponents` — [49-59](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:49)
- **바디 트랜스폼**: `GetBodyTransform`, `SetBodyTransform(bool bTeleport)`, `SetKinematicTarget`, `SetBodyType` — [61-66](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:61)
- **디버그/통계**: GetPhysicsStats, ExtractPhysicsDebugLines, ExtractVehicleDebugLines — [68-70](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:68)

> 📌 **조인트는 D6 한 종류만 노출**된다. PhysX 의 다른 조인트(Revolute/Spherical/Prismatic/Fixed)는 인터페이스에 없다. D6 의 motion limit 조합으로 ball/hinge/prismatic 을 흉내내는 방식.

### 2.2 FPhysXRuntime (구현체)
`PhysXRuntime.h`/`.cpp`. 핵심 멤버: `PxScene*`, `PxDefaultCpuDispatcher*`, `PxMaterial* DefaultMaterial`, `TArray<FBodyInstance*> Bodies`, `TArray<FConstraintInstance*> Joints`, `TArray<FVehicle4WInstance*> Vehicles`, `uint64 NextSerial`.

- **Initialize**: `AcquireSharedPhysXCore()` 로 Foundation/Physics 공유 → `PxDefaultCpuDispatcherCreate(4)` → `PxSceneDesc`(gravity `(0,0,-9.81)`) → `createScene` → **`DefaultMaterial = createMaterial(0.5, 0.5, 0.3)`** [PhysXRuntime.cpp:374](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:374).
- **Simulate(dt)**: WRITE_LOCK 안에서 kinematic 동기화 → `Scene->simulate(dt)` → `fetchResults(true)`; 이후 READ_LOCK 으로 pose 추출 → 컴포넌트에 write-back. **고정 타임스텝/서브스텝 없음(원본 dt 직접 전달).**
- **CreateRigidBody** [PhysXRuntime.cpp:697](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:697): Static→`createRigidStatic`, 그 외→`createRigidDynamic`(+Kinematic 플래그), damping/CCD/gravity 적용, Desc.Shapes 순회하며 셰이프 생성, `updateMassAndInertia(Desc.Mass)`, `addActor`.
- **CreateShape_AssumesLocked** [PhysXRuntime.cpp:802](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:802): `BuildGeometry` 로 PxGeometry 생성 → `PxRigidActorExt::createExclusiveShape(*Actor, geom, *DefaultMaterial)` → localPose/flag 설정 → `updateMassAndInertia(Desc.Material.Density)`.
- **CreateD6Joint** [PhysXRuntime.cpp:845](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:845): `PxD6JointCreate(Physics, ParentActor, ParentLocalFrame, ChildActor, ChildLocalFrame)` → `setMotion(eX/eY/eZ/eTWIST/eSWING1/eSWING2, …)` → `setTwistLimit(PxJointAngularLimitPair(min,max))` → `setSwingLimit(PxJointLimitCone(swing1,swing2))` → (bBreakable 시 `setBreakForce`). [873-885](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:873)
- **SetBodyType** [PhysXRuntime.cpp:1596](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:1596): **Static 변환은 무시**(actor 재생성 필요). Kinematic↔Dynamic 은 `setRigidBodyFlag(eKINEMATIC, …)` 토글 + Dynamic 진입 시 `wakeUp()`.

### 2.3 PhysXCore (공유 싱글턴)
[PhysXCore.cpp](KraftonEngine/Source/Engine/Physics/PhysXCore.cpp). `AcquireSharedPhysXCore()`/`ReleaseSharedPhysXCore()` 가 ref-count. 1회 초기화: `PxCreateFoundation` → `PxCreatePvd`(127.0.0.1:5425) → `PxCreatePhysics` → `PxInitExtensions` → `PxInitVehicleSDK` → `PxVehicleSetBasisVectors`(Z-up, X-forward). 
- 메모: 과거 **이중 Foundation 버그는 커밋 `01ba85ac` 에서 단일 `PxCreateFoundation` 으로 수정됨**.
- ⚠️ `PxCreateCooking` 미호출 → convex/triangle mesh 베이킹 경로 미연결(아래 §8 참조).

### 2.4 변환 / 락 / 타입
- **PhysXHelpers** [PhysXHelpers.h](KraftonEngine/Source/Engine/Physics/PhysXHelpers.h): `ToPxVec3/ToPxQuat/ToPxTransform` 및 역변환(성분 복사), `ToPxD6Motion(EPhysicsMotionType)`, `GetPxActor/GetPxDynamic`, `BuildGeometry`(Box/Sphere/Capsule 만 true, Convex/TriangleMesh 는 false).
- **PhysXSceneLock**: write(생성·힘·simulate) / read(pose·stat·query) 구분. lock 잡은 채 PhysX 데이터 복사 후, lock 해제하고 엔진 객체 갱신.
- **PhysicsTypes.h** 의 enum/핸들/Desc 는 §4 에서 상술.

---

## 3. 레이어 B — 물리 자산 데이터 모델 (저장 단위)

> 언리얼의 PhAT 자산 모델을 거의 그대로 모사. **에디터에서 편집·직렬화되는 영속 데이터**.

### 3.1 UPhysicsAsset — [PhysicsAsset.h](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.h)
`UCLASS()` / `GENERATED_BODY()`. 한 캐릭터(스켈레톤)의 모든 물리 설정 컨테이너.
- 필드(private): `TArray<UBodySetup*> BodySetups`, `TArray<UPhysicsConstraintSetup*> ConstraintSetups`, `TArray<FDisabledCollisionPair> DisabledCollisionPairs`, `FString AssetPathFileName`.
- 조회/생성 API: `FindBodySetup(BoneName)`, `FindConstraintSetup(Parent, Child)`, `GetOrCreateBodySetup`, `GetOrCreateConstraintSetup`, `RemoveBodySetup`, `RemoveConstraintSetup`, `GetBodySetups()/Mutable`, `GetConstraints()/Mutable`. [20-32](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.h:20)
- 충돌쌍 비활성화: `IsCollisionDisabled`/`SetCollisionDisabled` (본 이름 순서 무관) — 인접 바디 떨림 방지. [37-39](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.h:37)
- 직렬화: `Serialize(FArchive&)` — AssetPathFileName → BodySetups → ConstraintSetups → DisabledCollisionPairs. [41](KraftonEngine/Source/Engine/Physics/Asset/PhysicsAsset.h:41)
- **본↔바디 매핑은 본 "이름" 기준 선형 탐색만 존재**. 인덱스 캐시 맵은 없음(인스턴스화 시점에 컴포넌트가 인덱스로 변환).

### 3.2 UBodySetup — [BodySetup.h](KraftonEngine/Source/Engine/Physics/Asset/BodySetup.h)
본 1개에 귀속되는 물리 바디 설계도.
- `FString BoneName` — 귀속 본
- `FKAggregateGeom AggregateGeom` — 충돌 셰이프 모음
- `float Mass=1.0`, `LinearDamping=0.01`, `AngularDamping=0.05`, `Friction=0.7`, `Restitution=0.3`
- `bool bSimulatePhysics=true` (false 면 kinematic)
- `bool bEnableGravity=true`
- `EBodyPhysicsType PhysicsType` {Default, Kinematic}
- `EBodyCollisionEnabled CollisionEnabled` {NoCollision, QueryOnly, PhysicsOnly, QueryAndPhysics}
- `Serialize(FArchive&)`

### 3.3 FKAggregateGeom + 셰이프 요소 — [PhysicsGeometry.h](KraftonEngine/Source/Engine/Physics/Asset/PhysicsGeometry.h)
| 구조체 | 필드 |
|---|---|
| `FKSphereElem` | `FVector Center`, `float Radius` |
| `FKBoxElem` | `FVector Center`, `FQuat Rotation`, `float HalfX/HalfY/HalfZ` |
| `FKCapsuleElem` | `FVector Center`, `FQuat Rotation`, `float Radius`, `float HalfHeight` |
| `FKAggregateGeom` | `TArray<FKSphereElem> SphereElems`, `TArray<FKBoxElem> BoxElems`, `TArray<FKCapsuleElem> CapsuleElems`, `IsEmpty()`, `GetTotalPrimCount()` |

> ⚠️ **Sphere/Box/Capsule 3종만 존재.** 언리얼의 Sphyl/Convex/TaperedCapsule 같은 요소는 없음. (자동 생성 기능은 이 3종 내에서 동작해야 함.)

### 3.4 UPhysicsConstraintSetup — [PhysicsConstraintSetup.h](KraftonEngine/Source/Engine/Physics/Asset/PhysicsConstraintSetup.h)
관절(조인트) 제한 설계도.
- `FString ParentBoneName`, `ChildBoneName`
- `FVector ParentAnchorPos`, `FQuat ParentAnchorRot` — **부모 본 로컬 공간 기준 앵커 프레임 (자식 앵커는 없음 → 인스턴스화 시 계산)**
- `EConstraintMotion TwistMotion/Swing1Motion/Swing2Motion` {Locked, Limited, Free}
- `float TwistLimitAngle/Swing1LimitAngle/Swing2LimitAngle` — **degree 단위** (인스턴스화 시 radian 변환)
- `bool bLockLinearMotion=true` — true 면 3축 선형 Locked, false 면 Free (Limited 중간값 없음)
- `Serialize(FArchive&)`

> ⚠️ Drive(모터)·breakable·소프트 한계·linear limit 거리 등은 **Setup 에 필드가 없다**. 런타임 Desc(`FPhysicsConstraintDesc`) 에는 breakable 필드가 있으나 Setup→Desc 변환에서 채워지지 않음.

---

## 4. 레이어 C — 런타임 인스턴스 & 디스크립터

### 4.1 FBodyInstance — [BodyInstance.h](KraftonEngine/Source/Engine/Physics/BodyInstance.h)
런타임 바디 래퍼. 필드: `UPrimitiveComponent* OwnerComponent`, `FString BodyName/BoneName`, `int32 BoneIndex`, `EPhysicsBodyType BodyType`, `FPhysicsActorHandle ActorHandle`, `TArray<FPhysicsShapeHandle> ShapeHandles`, `FTransform CachedWorldTransform`, `bool bValid/bSimulating`, `Reset()`.

### 4.2 FConstraintInstance — [ConstraintInstance.h](KraftonEngine/Source/Engine/Physics/ConstraintInstance.h)
런타임 조인트 래퍼. 필드: `FString ConstraintName`, `FBodyInstance* ParentBody/ChildBody`, `FPhysicsJointHandle JointHandle`, `FPhysicsConstraintDesc Desc`, `bool bValid`, `Reset()`.

### 4.3 PhysicsTypes.h 핵심 — [PhysicsTypes.h](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h)
- enum: `EPhysicsBodyType`{Static,Dynamic,Kinematic}, `EPhysicsShapeType`{Box,Sphere,Capsule,Convex,TriangleMesh}, `EPhysicsMotionType`{Locked,Limited,Free}, `EVehicle4WWheelIndex`.
- 핸들(opaque): `FPhysicsActorHandle`/`FPhysicsShapeHandle`/`FPhysicsJointHandle`/`FPhysicsVehicleHandle` — `void* NativePtr` + `uint64 Serial`(use-after-free 방지).
- `FPhysicalMaterialDesc`{StaticFriction, DynamicFriction, Restitution, Density}.
- `FPhysicsShapeDesc`{Name, ShapeType, LocalTransform, HalfExtent, Radius, HalfHeight, ConvexVertices, bSimulation/Trigger/SceneQueryShape, Material}.
- `FPhysicsBodyDesc`{OwnerComponent, BodyName, BoneName, BoneIndex, BodyType, WorldTransform, Mass, Linear/AngularDamping, bUseGravity/bEnableCCD/bStartAwake, Shapes}.
- `FPhysicsConstraintDesc`{ConstraintName, Parent/ChildBody, **Parent/ChildLocalFrame**, LinearX/Y/Z, Twist/Swing1/Swing2, TwistLimitRadiansMin/Max, Swing1/2LimitRadians, **bBreakable/BreakForce/BreakTorque**} — 기본 limit: twist ±45°, swing 30°. [125-151](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h:125)
- `FPhysicsStats`, `FPhysicsDebugLine`, 차량 관련 `FVehicle4WInput/Desc/Instance`.

> ⚠️ **죽은 코드**: [PhysicsTypes.h:240](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h:240) 의 `struct RagdoleBone`(오타 포함, name/offset/halfSize/parentIndex/body/ConstraintInstance) 는 어디서도 사용되지 않는다. 초기 래그돌 설계 잔재로 보임. 실제 본↔바디 매핑은 `USkeletalMeshComponent::Bodies` 배열이 담당.

---

## 5. 레이어 D — 스켈레톤 / 본 인프라  *(본-depth 자동 생성의 데이터 소스)*

### 5.1 FBone / FSkeletalMesh — [SkeletalMeshAsset.h](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h)
`struct FBone` [19-70](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h:19):
- `FString Name`, `int32 ParentIndex`(-1=루트)
- `FMatrix ReferenceLocalPose` (부모 상대 로컬), `ReferenceGlobalPose` (스켈레톤 공간 글로벌), `SkinBindGlobalPose`, `InverseBindPoseMatrix`, (레거시 `LocalMatrix`/`GlobalMatrix`)
- 접근자: `GetReferenceLocalPose()`, `GetReferenceGlobalPose()`, `GetSkinBindGlobalPose()`, `GetInverseBindPose()`

`struct FSkeletalMesh`:
- `TArray<FBone> Bones` — **parent-first 정렬 보장** (부모 인덱스 < 자식 인덱스)
- `NormalizeBonePoseData()` [208-223](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h:208): 단순 전방 순회로 `ReferenceGlobalPose = ReferenceLocalPose * 부모.ReferenceGlobalPose` 누적. → **자동 생성 로직도 같은 순회 패턴을 그대로 쓸 수 있음.**
- `BoundsCenter/Extent`, 버텍스/인덱스/머티리얼/모프 등.

### 5.2 USkeletalMesh — [SkeletalMesh.h](KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMesh.h)
- `UPhysicsAsset* PhysicsAsset = nullptr` — **물리 자산은 여기에 붙는다.** (저장/로드 대상)
- `USkeleton* Skeleton`, `FSkeletalMesh* SkeletalMeshAsset`(런타임 본 배열 보유), `GetSkeletalMeshAsset()`.

### 5.3 USkeleton / FReferenceSkeleton — [Skeleton.h](KraftonEngine/Source/Engine/Animation/Skeleton/Skeleton.h), [SkeletonTypes.h](KraftonEngine/Source/Engine/Animation/Skeleton/SkeletonTypes.h)
- `FReferenceBone`{Name, ParentIndex, LocalBindPose, GlobalBindPose, InverseBindPose}, `FReferenceSkeleton`{`TArray<FReferenceBone> Bones`, FindBoneIndex}.
- `USkeleton` 은 `TMap<FString,int32> BoneNameToIndex` 캐시 보유(빠른 이름→인덱스).

### 5.4 본 트랜스폼 접근 — [SkinnedMeshComponent.h](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.h) / .cpp
- `int32 FindBoneIndex(const FString&)` — 메시 본 배열 선형 탐색.
- `bool GetBoneWorldTransformByIndex/ByName(…, FTransform&)` — `BuildBoneEditGlobalMatrices()` * 컴포넌트 월드.
- `BuildBoneEditGlobalMatrices(TArray<FMatrix>&)` — parent-first 순회로 component-local 글로벌 누적.
- 컴포넌트 로컬: `GetBoneLocationByIndex/RotationByIndex/QuatByIndex/LocalTransformByIndex`.
- 포즈 배열: `GetCurrentBoneGlobalTransforms/Matrices`, `BuildSkinMatrices`.

### 5.5 자식 본 순회(이미 있는 패턴)
[AnimNode_LayeredBlendPerBone.cpp](KraftonEngine/Source/Engine/Animation/Nodes/AnimNode_LayeredBlendPerBone.cpp) 의 `BuildBoneMaskFromRoot` 가 **루트에서 BFS 로 모든 자손 본을 수집**하는 코드를 이미 보여준다(자식 = `Bones[i].ParentIndex == parent` 선형 탐색). → 자동 생성의 "depth 까지 순회" 에 그대로 응용 가능.

### 5.6 본 import 출처
[FbxSkeletonImporter.cpp](KraftonEngine/Source/Engine/Mesh/Importer/Fbx/FbxSkeletonImporter.cpp) `ImportSkeleton`: FBX `EvaluateGlobalTransform`/`EvaluateLocalTransform` → 엔진 행렬 → `FBone.ReferenceLocalPose/GlobalPose`. 즉 본의 bind pose 가 자동 생성 시 셰이프 크기/방향 산정의 기준값.

---

## 6. 레이어 E — 컴포넌트 통합 / 래그돌 (USkeletalMeshComponent)

[SkeletalMeshComponent.h](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h) / [.cpp](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp). **이미 래그돌 인스턴스화·시뮬·write-back 경로가 구현되어 있다.**

상태 멤버: `TArray<FBodyInstance*> Bodies`(**본 인덱스로 인덱싱**), `TArray<FConstraintInstance*> Constraints`, `IPhysicsScene* PhysicsSceneOwner`, `bool bSimulatingPhysics`.

### 6.1 InstantiatePhysicsAssetBodies — [.cpp:389](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:389)
1. `DestroyPhysicsAssetBodies()` 로 기존 정리, `Bodies.assign(본 개수, nullptr)`.
2. `PhysicsAsset->GetBodySetups()` 순회: BoneName→인덱스(`FindBoneIndex`), 본 월드 트랜스폼(`GetBoneWorldTransformByIndex`) 획득 → `FPhysicsBodyDesc` 구성(BodyType = bSimulatePhysics?Dynamic:Kinematic, **bEnableCCD=true**) → `AppendPhysicsShapes` → `Scene.CreateRigidBody` → `Bodies[BoneIndex] = Body`.
3. `PhysicsAsset->GetConstraints()` 순회: parent/child 바디 찾기 → 두 바디 월드 트랜스폼 → **앵커/조인트 프레임 계산**(아래) → degree→radian 변환 → `Scene.CreateD6Joint`.

**조인트 프레임 계산** [.cpp:495-503](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:495):
```
ParentLocalFrame = (ParentAnchorPos, ParentAnchorRot)              // 부모 바디 로컬
JointWorldFrame  = ParentLocalFrame * ParentBodyWorld             // 월드
ChildLocalFrame  = JointWorldFrame * ChildBodyWorld^{-1}          // 자식 바디 로컬(역산)
```
LinearMotion = bLockLinearMotion ? Locked : Free. Twist/Swing 은 `ToPhysicsMotion`. Twist limit 은 ±DegreesToRadians(angle) 대칭.

### 6.2 셰이프 변환 AppendPhysicsShapes — [.cpp:75-111](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:75)
- Sphere → ShapeType=Sphere, LocalTransform=Center, Radius.
- Box → HalfExtent=(HalfX,HalfY,HalfZ), LocalTransform=Center+Rotation.
- Capsule → Radius, **`HalfHeight = max(0, Capsule.HalfHeight - Capsule.Radius)`** (PhysX `PxCapsuleGeometry` 는 두 반구 중심 사이 절반거리를 받음).
- `ApplyBodyMaterial` 이 `ShapeDesc.Material` 에 BodySetup 의 Friction/Restitution 을 채움. **단, 런타임 CreateShape 는 이 값을 무시하고 전역 DefaultMaterial 을 씀(§8 갭).**

> ⚠️ **캡슐 축**: PhysX 캡슐 기본 축은 로컬 **X축**. 래그돌 경로는 `FKCapsuleElem.Rotation` 을 그대로 LocalPose 로 넣을 뿐 별도 축 보정(예: Z→X 90° 회전)을 하지 않는다. 즉 **자동 생성 시 본 방향을 PhysX X축에 맞도록 Rotation 을 직접 산정**해야 한다.

### 6.3 CreateRagdoll — [.cpp:576](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:576)
사전조건: 이미 인스턴스화돼 있어야 함(`PhysicsSceneOwner` & `Bodies`). 각 바디에 대해 (1) 현재 본 월드 트랜스폼을 `SetBodyTransform(teleport=true)` 로 강제 동기화(anim pose ↔ ref pose 격차 튐 방지) → (2) `SetBodyType(Dynamic)`. 마지막에 `bSimulatingPhysics = true`.

### 6.4 TickComponent 분기 & ApplyPhysicsToBones — [.cpp:617](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:617) / [.cpp:636](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:636)
- `bSimulatingPhysics` 면 AnimInstance 평가를 건너뛰고 `ApplyPhysicsToBones()` 호출.
- `ApplyPhysicsToBones`: parent-first 순회로 각 바디의 월드 트랜스폼을 component-local global → local pose 로 변환해 `SetBoneLocalTransforms(LocalPose)`. 바디 없는 본은 ref pose 유지.

> 📌 **에디터 프리뷰 주의**: PreviewWorld 는 `BeginPlay` 를 호출하지 않아 `TickComponent` 가 디스패치되지 않는다. 따라서 에디터는 `Simulate` 직후 `ApplyPhysicsToBones` 를 **직접** 호출해 write-back 한다([SkeletalMeshComponent.h:88-90](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h:88) 주석, 그리고 §7.3). 이게 과거 "Simulate 무반응" 진단의 핵심이었다.

조회 API: `GetBodyInstanceByBoneIndex/ByBoneName`, `GetPhysicsBodies()`, `GetPhysicsConstraints()`, `IsSimulatingPhysics()`. 소멸자에서 `DestroyPhysicsAssetBodies()`.

---

## 7. 레이어 F — 에디터 / 툴링 (Physics 탭)

### 7.1 상태 구조체 — [MeshEditorWidget.h:24-72](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.h:24)
`FPhysicsEditTabState`: 편집 중 `UPhysicsAsset* PhysicsAsset`, 선택(Body/Constraint/Shape) 인덱스, `FPhysicsShapeGizmoTarget`/`FPhysicsConstraintGizmoTarget`, 우측 패널 탭(Details/Tools), 본 검색 텍스트, 그래프 팬/줌, 그리고 ↓

**`FBodyCreationSettings`** [MeshEditorWidget.h:49-61](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.h:49) — **자동 생성 기능의 기존 설정 슬롯**:
| 필드 | 기본값 | 의미 |
|---|---|---|
| `float MinBoneSize` | 20.0 | 이 길이보다 작은 본은 건너뜀 |
| `EShapeType PrimitiveType` | Capsule | 생성할 기본 셰이프 |
| `bool bOrientAlongBone` | true | 본 축을 따라 셰이프 정렬 |
| `bool bWalkPastSmallBones` | true | 작은 본은 지나치고 다음 본으로 |
| `bool bCreateBodyForAllBones` | false | 모든 본에 바디 생성 |
| `bool bDisableCollisionByDefault` | true | 인접 바디 충돌 비활성화 |
| `int32 LodIndex` | 0 | |
| `bool bCreateConstraints` | true | 부모-자식 컨스트레인트 자동 생성 |
| `int32 AngularConstraintMode` | 1 | 0 Locked / 1 Limited / 2 Free |

> ⚠️ **"임의 depth 까지" 를 위한 명시적 max-depth 필드는 아직 없다.** 현재는 `MinBoneSize`/`bWalkPastSmallBones`/`bCreateBodyForAllBones` 로 간접 제어. 사용자가 원하는 "bone depth N" 제어를 위해 **이 구조체에 `int32 MaxBoneDepth`(또는 루트 본 + 깊이) 필드를 추가**해야 한다.

### 7.2 Physics 탭 UI — [MeshEditorWidget.Physics.cpp](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp)
- `RenderPhysicsLayout()` [100]: 탭 진입 시 PhysicsAsset 로드/생성, 4분할(본 트리 / 뷰포트 / 디테일 / 그래프).
- **본 트리 우클릭**: Add Body, Add Shape(Sphere/Box/Capsule), Add Constraint to(다른 바디), Delete Body.
- **컨스트레인트 리스트**: "Parent -> Child" 표시, 우클릭 Delete.
- **디테일 패널**: Body 선택 시 Mass/Damping/Friction/Restitution/PhysicsType/CollisionEnabled + 셰이프별 Center/크기/Remove + (+Sphere/+Box/+Capsule). Constraint 선택 시 앵커 Pos/Rot + 축별 motion 콤보 + 한계 각도 + Lock Linear.
- **툴바**: 컨스트레인트 프리셋(Ball&Socket / Hinge / Prismatic), 바디쌍 Enable/Disable Collision.
- **그래프 패널** `RenderPhysicsGraphPanel`: 노드 기반 바디↔컨스트레인트 뷰(팬/줌/클릭 선택).
- **기즈모**: `UpdatePhysicsShapeGizmo` 가 선택에 맞춰 셰이프/앵커 기즈모 reconcile. `PickPhysicsAtScreen` 으로 뷰포트 레이 피킹.
- **오버레이**: `DrawPhysicsStatsOverlay`(개수), `DrawConstraintLimitsOverlay`(트위스트 호 + 스윙 콘 시각화).

### 7.3 시뮬레이션 프리뷰 — [.cpp:1080-1229](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1080)
- `RenderPhysicsTransportBar`: Simulate / Pause·Resume / Stop / Speed 슬라이더(0.05~2.0x).
- `StartPhysicsSimulation` [1080]: PreviewWorld 의 `IPhysicsScene` 획득 → `MeshComp->InstantiatePhysicsAssetBodies(*Scene, PA)` → `CreateRagdoll()` → `bSimulating=true`.
- `TickPhysicsSimulation` [1168]: **PreviewWorld 가 BeginPlay 미호출이므로** 위젯이 직접 `Scene->Simulate(SimDt)` 후 같은 틱에 `MeshComp->ApplyPhysicsToBones()` 호출.
- `StopPhysicsSimulation` [1139]: `DestroyPhysicsAssetBodies()`. ⚠️ `bSimulatingPhysics` 를 끄는 공개 API 가 없어 컴포넌트는 파기 전까지 ragdoll 모드 유지(알려진 한계).

### 7.4 저장 / 로드 — [.cpp:1816 SavePhysicsAsset](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1816)
- 저장: 메시 경로 → `<Mesh>_Physics.uasset` 으로 변환, `FAssetPackageHeader` + `FAssetImportMetadata` + `PhysicsAsset.Serialize` 기록, 메시에 링크, dirty 해제.
- 로드: 탭 진입 시 `Mesh->PhysicsAsset`(세션) → 파일(`LoadPhysicsAssetFromFile`) → 빈 자산 순으로 우선.
- ContentBrowser: `*_Physics.uasset` 더블클릭 → 연관 메시 열고 `s_bOpenInPhysicsTab=true` 로 Physics 탭 진입([MeshEditorWidget.h:129](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.h:129)).

### 7.5 ⭐ 자동 생성 stub — [.cpp:1012 / 1060](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1060)
- `RenderPhysicsToolsPanel()` [1012]: Tools 탭에 위 `FBodyCreationSettings` 컨트롤 전부 + "Re-generate Bodies" 버튼을 이미 그림.
- **`GeneratePhysicsBodies()` [1060] 는 빈 stub** — 본문은 `UE_LOG("… not implemented yet (UI stub)")` 한 줄. TODO 주석: "스켈레톤 순회하며 BodySetup+셰이프 생성, bCreateConstraints 시 부모-자식 컨스트레인트, bDisableCollisionByDefault 시 인접 충돌 비활성화".
  → **사용자가 추가하려는 "본 depth 자동 body 생성" 로직이 정확히 들어갈 자리.**

### 7.6 디버그 시각화 컴포넌트
- [PhysicsShapeDebugComponent.h](KraftonEngine/Source/Engine/Component/Debug/PhysicsShapeDebugComponent.h): TargetMeshComponent + PhysicsAsset 을 참조해 셰이프를 그림, `SetSelection`(body/kind/elem) 으로 하이라이트.
- [PhysicsShapeDebugSceneProxy.h](KraftonEngine/Source/Engine/Render/Proxy/PhysicsShapeDebugSceneProxy.h): 구/박스/캡슐 삼각형 메시 캐시, 선택=노랑/바디=초록, AlphaBlend·NoDepth(항상 위에, PhAT 스타일).

---

## 8. 알려진 갭 / 리스크 (직접 검증 + 진단문서)

| # | 항목 | 상태 | 근거 |
|---|---|---|---|
| G1 | **자동 생성 미구현** | `GeneratePhysicsBodies()` stub | [.Physics.cpp:1060](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1060) ✅직접확인 |
| G2 | **max-depth 필드 부재** | `FBodyCreationSettings` 에 깊이 제어 필드 없음 | [MeshEditorWidget.h:49](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.h:49) ✅ |
| G3 | **per-body 머티리얼 무시** | CreateShape 가 BodySetup 의 Friction/Restitution 대신 전역 `DefaultMaterial(0.5,0.5,0.3)` 사용 | [PhysXRuntime.cpp:821](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:821) ✅ |
| G4 | **캡슐 축 보정 없음** | 래그돌 경로는 X축 정렬 보정 안 함 → Rotation 직접 산정 필요 | [SkeletalMeshComponent.cpp:104](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:104) ✅ |
| G5 | **D6 외 조인트 없음 / Drive 미노출** | 인터페이스에 CreateD6Joint 만, Setup 에 drive/soft-limit/linear-distance 필드 없음 | [IPhysicsScene.h:27](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:27) ✅ |
| G6 | **SetBodyType Static 불가** | actor 재생성 필요로 무시 | [PhysXRuntime.cpp:1605](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp:1605) ✅ |
| G7 | **Convex/TriangleMesh 미구현** | BuildGeometry false, PxCooking 미초기화 | [PhysXHelpers.h](KraftonEngine/Source/Engine/Physics/PhysXHelpers.h) ✅ / Cooking 은 진단문서 |
| G8 | **죽은 RagdoleBone 구조체** | 미사용 | [PhysicsTypes.h:240](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h:240) ✅ |
| G9 | **단위(cm/m) / 좌표 handedness** | 엔진 LH vs PhysX RH, 변환 헬퍼는 성분 복사뿐 — **미검증 리스크** | 진단문서 주장(`RAGDOLL_INFRA_DIAGNOSIS.md`), 코드상 명시 단위 상수 없음. ⚠️실제 거동으로 검증 필요 |
| G10 | **고정 타임스텝 없음** | 원본 dt 직접 simulate | PhysXRuntime Simulate, 진단문서 |
| G11 | **bSimulatingPhysics 해제 API 없음** | Stop 후에도 ragdoll 모드 잔존 | [.Physics.cpp:1161](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1161) ✅ |

---

## 9. 사용자 목표 → 인프라 매핑 (어디에 무엇을 붙이나)

### 목표 1. **Ragdoll 생성**
- **이미 동작 경로 존재**: `InstantiatePhysicsAssetBodies` → `CreateRagdoll` → (Tick 또는 에디터 직접호출) `ApplyPhysicsToBones`.
- 필요한 것은 "유효한 BodySetup/Constraint 가 든 PhysicsAsset" 데이터뿐. 즉 **목표 4(자동 생성)가 채워지면 래그돌은 거의 바로 굴러간다.**
- 게임 런타임에서 쓰려면: World 가 BeginPlay 후 Simulate 를 돌리므로 TickComponent 경로가 정상 작동(에디터 프리뷰만 직접 호출 필요).

### 목표 2. **Joint(컨스트레인트) 세부 설정**
- 데이터: `UPhysicsConstraintSetup`(asset) → `FPhysicsConstraintDesc`(runtime) → `PxD6Joint`.
- 편집 UI: Physics 탭 디테일 패널(앵커/축별 motion/한계각/Lock Linear) + 프리셋 + 한계 시각화 오버레이가 이미 있음.
- **확장 포인트**: 세부 제어를 더 원하면 — (a) `UPhysicsConstraintSetup` 에 drive(스프링/댐퍼/타깃)·soft-limit·linear-limit-distance·breakable 필드 추가, (b) `FPhysicsConstraintDesc`/`CreateD6Joint` 에 `setDrive`/`setLinearLimit`/`setBreakForce` 매핑 추가(현재 breakable 만 부분 존재), (c) 그에 맞춰 디테일 패널 UI 확장.

### 목표 3. **PhysicsAsset 로 body 저장**
- **이미 동작**: `UPhysicsAsset::Serialize` + 에디터 `SavePhysicsAsset()` → `<Mesh>_Physics.uasset`. BodySetup/ConstraintSetup/DisabledPairs 모두 직렬화됨.
- BodySetup 의 신규 필드를 추가하면 `UBodySetup::Serialize` 와 (필요시) 버전 가드에 항목을 추가해야 함.

### 목표 4. ⭐ **SkeletalMesh 본 기준 임의 depth 까지 body 자동 생성** (언리얼 PhAT)
- **삽입 지점**: `FMeshEditorWidget::GeneratePhysicsBodies()` [stub, .Physics.cpp:1060](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1060).
- **입력 데이터**: `FSkeletalMesh::Bones`(parent-first), 각 `FBone.ReferenceGlobalPose`/`ReferenceLocalPose`, `ParentIndex`, `Name`. 자식 탐색은 `BuildBoneMaskFromRoot` BFS 패턴 응용.
- **권장 알고리즘 스케치**(코드 미작성, 설계만):
  1. `FBodyCreationSettings` 에 **`int32 MaxBoneDepth`(또는 시작 루트 본 + 깊이)** 필드 추가(현재 부재 — G2).
  2. 루트(또는 지정 본)부터 BFS/DFS, depth 카운트. `depth > MaxBoneDepth` 면 중단. `bWalkPastSmallBones`/`MinBoneSize` 로 작은 본 스킵.
  3. 각 대상 본에 대해 자식 본까지의 거리로 **본 길이** 산정 → `FKCapsuleElem`(Radius=본 길이·비율, HalfHeight=길이/2) 생성. **캡슐 Rotation 은 본 축을 PhysX 로컬 X축에 맞추도록 계산**(G4). `PrimitiveType` 에 따라 Sphere/Box 분기.
  4. `UPhysicsAsset::GetOrCreateBodySetup(BoneName)` 에 AggregateGeom 추가.
  5. `bCreateConstraints` 시 부모-자식 `GetOrCreateConstraintSetup(Parent, Child)` 생성, `AngularConstraintMode` 적용. 앵커는 자식 본 위치를 부모 바디 로컬로 환산.
  6. `bDisableCollisionByDefault` 시 인접/부모-자식 쌍 `SetCollisionDisabled`.
- **검증 도구**: 생성 직후 Physics 탭 그래프/오버레이로 확인, "Simulate" 로 래그돌 거동 즉시 테스트 가능.
- **주의**: 만들고 나서 단위/handedness(G9), 캡슐 축(G4) 때문에 거동이 이상할 수 있음 → 먼저 Box/Sphere 로 검증 후 Capsule 적용 권장.

---

## 10. World 통합 (참고)
- `UWorld` 가 `std::unique_ptr<IPhysicsScene> PhysicsScene` 소유, `GetPhysicsScene()` 제공.
- InitWorld 에서 `make_unique<FPhysXRuntime>()` + `Initialize()`. Tick 에서 **`bHasBegunPlay && PhysicsScene`** 가드 후 `Simulate(dt)`. EndPlay 에서 `Shutdown()`.
- `UPrimitiveComponent::BeginPlay/EndPlay` 가 `RegisterComponent/UnregisterComponent`, `NotifyPhysicsBodyDirty`→`RebuildBody`.
- (이 절의 세부 줄번호는 World.cpp 를 직접 열어 재확인 권장 — 에디터 프리뷰 BeginPlay 미호출 이슈의 근원.)

---

## 11. 기존 관련 문서 (Docs/) — stale 주의
| 문서 | 내용 | 주의 |
|---|---|---|
| `RAGDOLL_INFRA_DIAGNOSIS.md` | 래그돌 인프라 진단, 단위/handedness/캡슐축 리스크 | `IPhysicsRuntime` 명칭은 stale. 리스크 분류는 유효 |
| `RAGDOLL_SIMULATE_NOREACT_DIAGNOSIS.md` | 에디터 Simulate 무반응 = PreviewWorld BeginPlay 미호출 → write-back 차단 | 핵심 진단 유효(현재 코드는 에디터가 ApplyPhysicsToBones 직접 호출로 우회) |
| `PhysX_재분담안.md`, `PhysX_재분담안_v2_상세.md` | 작업 재분담(M1~M4) 계획 | 계획 문서. 명칭/진척도 stale 가능 |
| `준협님엔진.md` | 타 엔진 비교 진단 | 비교용. 수치 stale 가능 |

> 이 문서(`physics_temp.md`)는 위 문서들과 달리 **현재 코드를 직접 읽어 교차검증**했고, `IPhysicsScene` 명칭/자산 3종 셰이프/자동생성 stub 위치 등을 실측 반영했다.

---

## 부록. 한눈에 보는 데이터 흐름
```
[USkeletalMesh.PhysicsAsset] ──(편집/저장)── Physics 탭 UI ── SavePhysicsAsset → <Mesh>_Physics.uasset
        │  (UPhysicsAsset: BodySetups[], ConstraintSetups[], DisabledPairs)
        ▼
USkeletalMeshComponent::InstantiatePhysicsAssetBodies(IPhysicsScene&, UPhysicsAsset*)
        │   BodySetup→FPhysicsBodyDesc(+AppendPhysicsShapes)→Scene.CreateRigidBody → Bodies[boneIdx]
        │   ConstraintSetup→FPhysicsConstraintDesc(프레임 계산)→Scene.CreateD6Joint → Constraints[]
        ▼
CreateRagdoll()  : 본 월드→body teleport, SetBodyType(Dynamic), bSimulatingPhysics=true
        ▼
IPhysicsScene::Simulate(dt)  (FPhysXRuntime: PxScene simulate/fetchResults)
        ▼
ApplyPhysicsToBones()  : body 월드 → 본 local pose → SetBoneLocalTransforms (parent-first)
        │   (게임=TickComponent 자동 / 에디터=위젯이 직접 호출)
        ▼
[스킨드 메시 갱신]
```
