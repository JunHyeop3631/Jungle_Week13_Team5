# PHYSICS_AUDIT — PhysX 4.1 ragdoll 선행조사

## 0. 확정된 파일 경로

- [확인됨] `KraftonEngine/Source/Engine/Physics/IPhysicsRuntime.h`
- [확인됨] `KraftonEngine/Source/Engine/Physics/PhysXRuntime.h`
- [확인됨] `KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp`
- [확인됨] `KraftonEngine/Source/Engine/Physics/BodyInstance.h`
- [확인됨] `KraftonEngine/Source/Engine/Physics/ConstraintInstance.h`
- [확인됨] `KraftonEngine/Source/Engine/Physics/PhysicsTypes.h`

---

## A. 추상 인터페이스 (IPhysicsRuntime.h)

### A.1 선언된 순수가상 메서드 전체 목록 + 시그니처
- [확인됨] `FBodyInstance* CreateRigidBody(const FPhysicsBodyDesc& Desc)` — IPhysicsRuntime.h:10
- [확인됨] `void DestroyRigidBody(FBodyInstance* Body)` — IPhysicsRuntime.h:11
- [확인됨] `FPhysicsShapeHandle CreateShape(FBodyInstance* Body, const FPhysicsShapeDesc& Desc)` — IPhysicsRuntime.h:13
- [확인됨] `FConstraintInstance* CreateD6Joint(const FPhysicsConstraintDesc& Desc)` — IPhysicsRuntime.h:15
- [확인됨] `void DestroyJoint(FConstraintInstance* Joint)` — IPhysicsRuntime.h:16
- [확인됨] `bool GetBodyTransform(const FBodyInstance* Body, FTransform& OutTransform) const` — IPhysicsRuntime.h:18
- [확인됨] `void SetBodyTransform(FBodyInstance* Body, const FTransform& Transform, bool bTeleport = true)` — IPhysicsRuntime.h:19
- [확인됨] `void SetKinematicTarget(FBodyInstance* Body, const FTransform& Transform)` — IPhysicsRuntime.h:20
- [확인됨] `void GetPhysicsStats(FPhysicsStats& OutStats) const` — IPhysicsRuntime.h:22
- [확인됨] `void ExtractPhysicsDebugLines(TArray<FPhysicsDebugLine>& OutLines) const` — IPhysicsRuntime.h:23

### A.2 body 생성/제거, joint 생성/제거, scene step 메서드가 인터페이스에 있는지
- [확인됨] body 생성: `CreateRigidBody` — IPhysicsRuntime.h:10
- [확인됨] body 제거: `DestroyRigidBody` — IPhysicsRuntime.h:11
- [확인됨] joint 생성: `CreateD6Joint` — IPhysicsRuntime.h:15
- [확인됨] joint 제거: `DestroyJoint` — IPhysicsRuntime.h:16
- [없음] scene step (`Simulate`/`Tick`) — IPhysicsRuntime.h에 선언 없음. `FPhysXRuntime::Simulate`는 비-virtual 멤버로만 존재 (PhysXRuntime.h:22)
- [없음] `Initialize`/`Shutdown` — 인터페이스에 없음. `FPhysXRuntime::Initialize/Shutdown`은 비-virtual (PhysXRuntime.h:20-21)

### A.3 ragdoll에 필요한데 인터페이스에 빠진 메서드
- [없음] `Simulate(float DeltaTime)` — 인터페이스에 노출 안 됨
- [없음] 본별 force/impulse/velocity 적용 API (`AddForce`, `AddImpulse`, `SetLinearVelocity`, `SetAngularVelocity`)
- [없음] kinematic↔dynamic 모드 토글 API (애니메이션→래그돌 전환 시 필수)
- [없음] joint drive 설정 API (`PxD6Joint::setDrive`, 활성 래그돌/PD 제어용)
- [없음] sleep/wake 제어 (`PutToSleep`/`WakeUp`)
- [없음] 본별 batch 트랜스폼 read-back API (write-back 효율용; 현재는 1개씩 `GetBodyTransform` 호출)
- [없음] raycast/sweep/overlap 쿼리 (래그돌 충돌 디버깅·게임플레이용)

---

## B. PhysX 구현체 (PhysXRuntime.h/.cpp)

### B.1 PxFoundation/PxPhysics/PxScene/PxMaterial/CpuDispatcher 초기화
- [확인됨] `PxCreateFoundation` — PhysXRuntime.cpp:114
- [확인됨] `PxCreatePhysics` — PhysXRuntime.cpp:120
- [확인됨] `PxInitExtensions` — PhysXRuntime.cpp:127
- [확인됨] `PxDefaultCpuDispatcherCreate(4)` (4 thread) — PhysXRuntime.cpp:134
- [확인됨] `PxSceneDesc` + `createScene` — PhysXRuntime.cpp:141-149; gravity `(0,0,-9.81)` (Z-up 가정), flags: `eENABLE_ACTIVE_ACTORS | eENABLE_CCD | eENABLE_PCM`
- [확인됨] `createMaterial(0.5, 0.5, 0.3)` (DefaultMaterial) — PhysXRuntime.cpp:156
- [확인됨] 멤버 슬롯 모두 존재: `Foundation, Physics, Scene, Dispatcher, DefaultMaterial` — PhysXRuntime.h:40-44

### B.2 PhysX SDK 버전 4.1 근거
- [확인됨] include path `ThirdParty\PhysX41\include\physx` — KraftonEngine.vcxproj:131,138,145,152,159,166,173,180,189,213,235,267,299,323,355,387
- [확인됨] 버전 매크로 `PX_PHYSICS_VERSION_MAJOR=4`, `PX_PHYSICS_VERSION_MINOR=1`, `PX_PHYSICS_VERSION_BUGFIX=2` — ThirdParty/PhysX41/include/physx/PxPhysicsVersion.h:52-54
- [확인됨] lib 디렉토리 분기 (debug/release) — KraftonEngine.vcxproj:245(debug), 277/333/365/397(release)
- [확인됨] 링크 lib 일체: `PhysX_64.lib;PhysXCommon_64.lib;PhysXFoundation_64.lib;PhysXCooking_64.lib;PhysXExtensions_static_64.lib;PhysXPvdSDK_static_64.lib;PhysXVehicle_static_64.lib;PhysXCharacterKinematic_static_64.lib;PhysXTask_static_64.lib` — KraftonEngine.vcxproj:246,278,334,366,398
- [확인됨] DLL 복사 post-build — KraftonEngine.vcxproj:251,283,339,371,403

### B.3 simulate()/fetchResults() 호출 지점, dt 처리
- [확인됨] `Scene->simulate(DeltaTime)` + `Scene->fetchResults(true)` — PhysXRuntime.cpp:235-236
- [확인됨] dt 가변 (입력 그대로 전달, 양수 가드만) — PhysXRuntime.cpp:230
- [없음] 고정 dt/substep 로직 (래그돌 안정성 위해 PhysX 권장값 1/60 고정 또는 sub-step 필요할 수 있음)

### B.4 IPhysicsRuntime 중 미구현(스텁/TODO)
- [확인됨] 인터페이스의 모든 순수가상 메서드는 `FPhysXRuntime`에서 override 구현됨 — PhysXRuntime.h:24-37
- [확인됨] Convex / TriangleMesh shape는 미지원 (false 반환) — PhysXRuntime.cpp:86-89
- [없음] TODO/스텁 표시 코드 검색에서 인터페이스 메서드 본문은 모두 채워져 있음

### B.5 ragdoll에 필요한 PhysX API의 래핑 여부
- [확인됨] `PxD6JointCreate` — PhysXRuntime.cpp:389
- [확인됨] `PxD6Axis` motion 설정 (eX/eY/eZ/eTWIST/eSWING1/eSWING2) — PhysXRuntime.cpp:401-406
- [확인됨] `setTwistLimit(PxJointAngularLimitPair)` — PhysXRuntime.cpp:407
- [확인됨] `setSwingLimit(PxJointLimitCone)` — PhysXRuntime.cpp:408
- [확인됨] `setBreakForce` — PhysXRuntime.cpp:412
- [확인됨] `PxCapsuleGeometry(Radius, HalfHeight)` — PhysXRuntime.cpp:84
- [확인됨] `PxBoxGeometry`, `PxSphereGeometry` — PhysXRuntime.cpp:78,82
- [확인됨] `PxRigidBodyExt::updateMassAndInertia(Dynamic, Mass)` — PhysXRuntime.cpp:304; shape 추가 시에도 호출 (Density 인자, 주의) — PhysXRuntime.cpp:369
- [확인됨] `PxRigidActorExt::createExclusiveShape` — PhysXRuntime.cpp:352
- [확인됨] CCD 플래그 `PxRigidBodyFlag::eENABLE_CCD` per-body — PhysXRuntime.cpp:273
- [확인됨] Kinematic 플래그 `PxRigidBodyFlag::eKINEMATIC` + `setKinematicTarget` — PhysXRuntime.cpp:268, 483
- [없음] `PxD6Joint::setDrive` / `PxD6JointDrive` (active ragdoll/PD 제어용)
- [없음] `setProjectionLinearTolerance`/`setProjectionAngularTolerance` (래그돌 안정화에 자주 사용)
- [없음] joint local frame 회전 보정 헬퍼 (UE의 PxD6Joint twist 축 X-axis 가정 처리 — 현재 코드는 호출자가 ParentLocalFrame/ChildLocalFrame를 정확히 만들 것이라 가정)

---

## C. BodyInstance.h

### C.1 보유 멤버
- [확인됨] `UPrimitiveComponent* OwnerComponent` — BodyInstance.h:7
- [확인됨] `FString BodyName, BoneName; int32 BoneIndex` — BodyInstance.h:9-11
- [확인됨] `EPhysicsBodyType BodyType` (Static/Dynamic/Kinematic) — BodyInstance.h:13
- [확인됨] `FPhysicsActorHandle ActorHandle` (`void* NativePtr` → `PxRigidActor*` 캐스팅) — BodyInstance.h:15, PhysicsTypes.h:36-42
- [확인됨] `TArray<FPhysicsShapeHandle> ShapeHandles` — BodyInstance.h:16
- [확인됨] `FTransform CachedWorldTransform` — BodyInstance.h:18
- [확인됨] `bool bValid, bSimulating` — BodyInstance.h:20-21
- [없음] `PxRigidDynamic*` 직접 멤버는 없음 — `void* NativePtr`로 추상화 (런타임에서 `static_cast<PxRigidActor*>` 후 `is<PxRigidDynamic>()` — PhysXRuntime.cpp:67-71)

### C.2 public 인터페이스
- [확인됨] struct이며 모든 멤버 public, 메서드는 `Reset()` 1개뿐 — BodyInstance.h:23-35
- [없음] body 자체에는 transform get/set/mass 메서드 없음 — 모두 `IPhysicsRuntime` 측 메서드 경유 (런타임이 actor 핸들 조작)

### C.3 트랜스폼 get/set, 질량/관성 설정 경로
- [확인됨] 트랜스폼 get: `IPhysicsRuntime::GetBodyTransform` — IPhysicsRuntime.h:18 / PhysXRuntime.cpp:444
- [확인됨] 트랜스폼 set: `IPhysicsRuntime::SetBodyTransform` (teleport)/`SetKinematicTarget` — IPhysicsRuntime.h:19-20 / PhysXRuntime.cpp:456,474
- [확인됨] 질량 설정 경로: `FPhysicsBodyDesc::Mass`를 `CreateRigidBody`가 `updateMassAndInertia`로 적용 — PhysXRuntime.cpp:304
- [불명확] shape 추가 시 다시 `updateMassAndInertia(Dynamic, Desc.Material.Density)` 호출 (PhysXRuntime.cpp:369) — Mass와 Density 혼용으로 의도/단위 불명확
- [없음] 런타임 중 mass/inertia 갱신 인터페이스 (`SetMass`, `SetInertiaTensor`)
- [없음] linear/angular velocity get/set 인터페이스

### C.4 본 1개 → BodyInstance 1개 매핑 가능 여부
- [확인됨] `BoneName`/`BoneIndex` 슬롯이 `FBodyInstance` 및 `FPhysicsBodyDesc`에 존재 — BodyInstance.h:10-11, PhysicsTypes.h:91-92
- [확인됨] `USkeletalMeshComponent::Bodies`가 BoneIndex로 색인된 배열 — SkeletalMeshComponent.h:107, SkeletalMeshComponent.cpp:401(`Bodies.assign(BoneCount, nullptr)`), 454(`Bodies[BoneIndex] = Body`)
- [확인됨] `GetBodyInstanceByBoneIndex/Name` 헬퍼 존재 — SkeletalMeshComponent.cpp:561-574

---

## D. ConstraintInstance.h

### D.1 PxD6Joint/PxJoint 래핑 여부 및 보유 멤버
- [확인됨] `FPhysicsJointHandle JointHandle` (`void* NativePtr` → 런타임에서 `PxJoint*`로 캐스팅) — ConstraintInstance.h:12, PhysicsTypes.h:52-58
- [확인됨] 실제로 `PxD6Joint*`를 보관 — `PxD6JointCreate` 결과를 `Constraint->JointHandle = {Joint, ...}` — PhysXRuntime.cpp:389,419
- [확인됨] `FString ConstraintName` — ConstraintInstance.h:7
- [확인됨] `FBodyInstance* ParentBody, ChildBody` — ConstraintInstance.h:9-10
- [확인됨] `FPhysicsConstraintDesc Desc` (원본 desc 보존) — ConstraintInstance.h:13
- [확인됨] `bool bValid` + `Reset()` — ConstraintInstance.h:15,17-25

### D.2 twist/swing limit, motion(eLIMITED 등) 설정 인터페이스
- [확인됨] `EPhysicsMotionType { Locked, Limited, Free }` enum — PhysicsTypes.h:29-34
- [확인됨] `FPhysicsConstraintDesc`에 `LinearX/Y/Z, Twist, Swing1, Swing2` motion 필드 — PhysicsTypes.h:118-124
- [확인됨] twist limit min/max(radian), swing1/swing2 limit (radian) 필드 — PhysicsTypes.h:126-129
- [확인됨] 런타임 매핑 `ToPxD6Motion` — PhysXRuntime.cpp:43-55
- [확인됨] PhysX 호출: `setMotion(eX/eY/eZ/eTWIST/eSWING1/eSWING2)`, `setTwistLimit(PxJointAngularLimitPair)`, `setSwingLimit(PxJointLimitCone)` — PhysXRuntime.cpp:401-408
- [확인됨] breakable 필드 + `setBreakForce` — PhysicsTypes.h:131-133, PhysXRuntime.cpp:410-413
- [없음] 런타임 중 limit/motion 변경 인터페이스 (생성 시점에만 적용; 변경하려면 `ConstraintInstance.Desc` 수정 후 재생성 필요)
- [없음] joint drive (PxD6JointDrive) 설정 필드/메서드

### D.3 두 BodyInstance를 부모-자식으로 연결하는 메서드
- [확인됨] `FPhysicsConstraintDesc::ParentBody/ChildBody` + `ParentLocalFrame/ChildLocalFrame` — PhysicsTypes.h:112-116
- [확인됨] `IPhysicsRuntime::CreateD6Joint(Desc)`가 두 actor를 연결 — IPhysicsRuntime.h:15 / PhysXRuntime.cpp:389-394
- [확인됨] 상위 사용처 `USkeletalMeshComponent::InstantiatePhysicsAssetBodies`에서 부모-자식 본 인덱스로 BodyInstance 조회 후 `CreateD6Joint` 호출 — SkeletalMeshComponent.cpp:465-530

---

## E. PhysicsTypes.h (타입/변환 레이어)

### E.1 엔진 ↔ PhysX 변환 함수
- [없음] PhysicsTypes.h에는 변환 함수 없음 (선언/공용 헤더 부재)
- [확인됨] 변환 함수는 PhysXRuntime.cpp 익명 namespace에 file-local로만 존재:
  - `ToPxVec3(FVector)` — PhysXRuntime.cpp:13-16
  - `ToPxQuat(FQuat)` — PhysXRuntime.cpp:18-21
  - `ToFVector(PxVec3)` — PhysXRuntime.cpp:23-26
  - `ToFQuat(PxQuat)` — PhysXRuntime.cpp:28-31
  - `ToPxTransform(FTransform)` — PhysXRuntime.cpp:33-36
  - `ToFTransform(PxTransform)` — PhysXRuntime.cpp:38-41
- [없음] `Mat4 ↔ PxMat44` 변환 없음 (현재 필요 없음 — Transform만 사용)
- [없음] 변환 함수가 공개 헤더가 아니라서 다른 TU에서 재사용 불가 (래그돌 hook이 별도 TU라면 중복 정의 필요)

### E.2 좌표계 가정: 핸드니스, up축, 단위
- [확인됨] PhysX scene gravity `PxVec3(0, 0, -9.81)` — PhysXRuntime.cpp:142 → **Z-up** 가정
- [확인됨] `FVector(V.X, V.Y, V.Z)` 1:1 매핑 (축 순서 변환 없음) — PhysXRuntime.cpp:14-16
- [확인됨] `FQuat(X,Y,Z,W) ↔ PxQuat(x,y,z,w)` 1:1 매핑 — PhysXRuntime.cpp:18-31
- [불명확] 핸드니스: UE 기본은 LH·Z-up·X-forward이고 PhysX는 핸드니스 중립이지만 회전 적용 시 호환됨. 코드에서 핸드니스 변환·반전 없음 → 엔진 측이 LH라면 그대로 사용 가능. 엔진 좌표계가 LH인지 코드 미확인 → **불명확**
- [확인됨] 단위 (cm/m): 코드에 unit scale 없음 (`PxTolerancesScale()` 기본값 사용 — PhysXRuntime.cpp:120). 기본 length=1, speed=10이며 m·m/s 가정. 엔진이 cm 단위면 `PxTolerancesScale.length = 100, speed = 981`로 변경 필요 — **불명확** (엔진 단위 미확인)

### E.3 PhysicsTypes.h 자체 정의된 enum/desc/struct
- [확인됨] `EPhysicsBodyType {Static, Dynamic, Kinematic}` — PhysicsTypes.h:13-18
- [확인됨] `EPhysicsShapeType {Box, Sphere, Capsule, Convex, TriangleMesh}` — PhysicsTypes.h:20-27
- [확인됨] `EPhysicsMotionType {Locked, Limited, Free}` — PhysicsTypes.h:29-34
- [확인됨] 핸들 `FPhysicsActorHandle / FPhysicsShapeHandle / FPhysicsJointHandle` — PhysicsTypes.h:36-58
- [확인됨] `FPhysicalMaterialDesc`, `FPhysicsShapeDesc`, `FPhysicsBodyDesc`, `FPhysicsConstraintDesc`, `FPhysicsStats`, `FPhysicsDebugLine` — PhysicsTypes.h:60-153
- [확인됨] **빌드 막을 가능성 큰 문법 오류**: `struct RagdoleBone` 안에 변수명 누락된 멤버 `FConstraintInstance* = nullptr;` — PhysicsTypes.h:163. 같은 struct 닫는 `};`이 없고 (line 164에 빈 줄, 165에 `};`만 존재) 헤더 끝에 `}` 누락 가능성도 있음 — 컴파일 차단 위험. `RagdoleBone` 자체가 다른 곳에서 참조되는지 미확인 — **불명확**(사용처 grep 미실행), 단 정의 자체는 invalid

---

## F. 프로젝트 인프라

### F.1 빌드 시스템에서 PhysX 4.1 의존성
- [확인됨] vcxproj 단일 빌드 (`KraftonEngine.vcxproj`) — CMake는 vcpkg 내부 헬퍼만 존재(KraftonEngine\packages\NVIDIA.PhysX.4.1.2\...), 본 빌드는 vcxproj
- [확인됨] include path: `ThirdParty\PhysX41\include\physx;ThirdParty\PhysX41\include\pxshared` — KraftonEngine.vcxproj:131 등
- [확인됨] lib dir (Debug): `$(ProjectDir)ThirdParty\PhysX41\lib\debug` — KraftonEngine.vcxproj:245
- [확인됨] lib dir (Release/Standalone/ObjViewer/EditorRelease): `$(ProjectDir)ThirdParty\PhysX41\lib\release` — KraftonEngine.vcxproj:277,333,365,397
- [확인됨] 링크 라이브러리(공통): `PhysX_64.lib;PhysXCommon_64.lib;PhysXFoundation_64.lib;PhysXCooking_64.lib;PhysXExtensions_static_64.lib;PhysXPvdSDK_static_64.lib;PhysXVehicle_static_64.lib;PhysXCharacterKinematic_static_64.lib;PhysXTask_static_64.lib` — KraftonEngine.vcxproj:246,278,334,366,398
- [확인됨] post-build DLL 복사 (debug/release 분기) — KraftonEngine.vcxproj:251,283,339,371,403
- [확인됨] 일부 Config(Debug/Release without WITH_EDITOR=1)에서는 LibraryPath/AdditionalDependencies 분기에 PhysX 누락 — KraftonEngine.vcxproj:132,139,160 (해당 Config로 빌드 시 link error 가능) — **불명확**(해당 Config가 실제 사용되는지)
- [확인됨] vcpkg 폴더 `KraftonEngine\packages\NVIDIA.PhysX.4.1.2` 존재 (사용 안 됨, ThirdParty가 우선)

### F.2 새 레이어가 빌드 타깃에 포함됐는지
- [확인됨] `PhysXRuntime.cpp`가 `<ClCompile>` 항목으로 vcxproj에 포함 (grep 결과 매칭) — KraftonEngine.vcxproj
- [확인됨] `IPhysicsRuntime.h`, `PhysXRuntime.h`, `BodyInstance.h`, `ConstraintInstance.h`, `PhysicsTypes.h`가 vcxproj.filters에 등록 (grep 결과 매칭)

### F.3 새 물리 레이어를 호출하는 상위 코드
- [없음] `FPhysXRuntime`을 인스턴스화하는 코드 없음 — `new FPhysXRuntime` 검색 결과 0건; `FPhysXRuntime` 자체 grep도 PhysXRuntime.h/.cpp 외 매칭 없음
- [없음] `Runtime.Simulate(...)` / `Runtime->Simulate(...)` 호출 지점 없음 — grep 결과 0건 (호출자 미연결)
- [확인됨] `USkeletalMeshComponent::InstantiatePhysicsAssetBodies(IPhysicsRuntime& Runtime, ...)`가 인터페이스 사용 — SkeletalMeshComponent.cpp:383-533, 호출하려면 외부에서 Runtime 참조를 전달해야 함 (현재 호출자 없음)
- [없음] 게임/씬/엔진 루프에서 `IPhysicsRuntime` 소유·tick 진입점 없음 — World/Scene/Engine 클래스에 `PhysicsRuntime` 멤버나 Tick 호출 grep 미발견

---

## G. 메시/스켈레톤 연동 hook

### G.1 스켈레탈 메시·본 계층 자료구조
- [확인됨] `USkeletalMeshComponent` (UCLASS) — SkeletalMeshComponent.h:20
- [확인됨] 베이스 `USkinnedMeshComponent`가 본 트랜스폼/스키닝 소유 — SkeletalMeshComponent.h:17-18 (주석), SkinnedMeshComponent.h
- [확인됨] `USkeletalMesh` (asset) → `FSkeletalMesh::Bones` 배열 — SkeletalMeshComponent.cpp:394-395, 700-701
- [확인됨] 본 인덱스 조회 `FindBoneIndex(FString)` — SkeletalMeshComponent.cpp:413,472-473,573 (정의는 SkinnedMeshComponent에 grep 매칭)
- [확인됨] 본 월드 트랜스폼 조회 `GetBoneWorldTransformByIndex(int32, FTransform&)` — SkeletalMeshComponent.cpp:421 (정의 SkinnedMeshComponent)
- [확인됨] 본 포즈 푸시 `SetAnimationPose(Out.Pose, Out.MorphWeights)` — SkeletalMeshComponent.cpp:729 (정의 SkinnedMeshComponent)

### G.2 물리 결과 → 본/메시 월드행렬 write-back hook
- [확인됨] write-back 인프라 시도: `USkeletalMeshComponent::Bodies` 가 BoneIndex로 색인된 배열 (cheap lookup 의도) — SkeletalMeshComponent.h:74-75 주석, 107
- [확인됨] 본 생성 경로: `InstantiatePhysicsAssetBodies` 가 PhysicsAsset → BodySetup → bone world transform → `CreateRigidBody` → `Bodies[BoneIndex]=Body` — SkeletalMeshComponent.cpp:389-456
- [확인됨] joint 생성 경로: PhysicsAsset의 `UPhysicsConstraintSetup`로부터 `CreateD6Joint` — SkeletalMeshComponent.cpp:465-530
- [확인됨] 정리 경로: `DestroyPhysicsAssetBodies` — SkeletalMeshComponent.cpp:535-559
- [없음] write-back 실제 구현 없음 — `TickComponent`는 `EvaluateAnimInstance`만 호출 (애니메이션 평가 → SetAnimationPose). 물리 결과를 본 local/world transform에 반영하는 코드 없음 — SkeletalMeshComponent.cpp:581-590
- [없음] `CreateRagdoll()` 빈 본문 — SkeletalMeshComponent.cpp:576-579 (stub)
- [없음] 애니메이션→래그돌 모드 전환 로직 (모든 본을 Kinematic→Dynamic으로 토글) 없음
- [없음] `PxScene->getActiveActors()` / `eENABLE_ACTIVE_ACTORS` 콜백 활용한 efficient write-back 경로 없음 (flag는 켜져 있음 — PhysXRuntime.cpp:145)

---

## H. 발견된 추가 위험·관찰

- [확인됨] `FBodyInstance`에 `OwnerComponent` 가 `UPrimitiveComponent*` 인데, PhysX shape `userData`로 `Body->OwnerComponent` 저장 → component 단위 콜백·쿼리에 사용 가능 — PhysXRuntime.cpp:362
- [확인됨] `actor->userData = Body` 로 reverse lookup 가능 — PhysXRuntime.cpp:295
- [확인됨] `Bodies/Joints`는 `TArray<FBodyInstance*>`로 관리되며 destroy 시 `std::remove` linear erase — 본 수 ~50–200 규모면 OK, 대규모면 비효율
- [확인됨] `Initialize()`는 `CreateRigidBody`에서 lazy 호출됨 (PhysXRuntime.cpp:251) — Simulate 직전 명시적 초기화 없이도 동작
- [확인됨] 같은 폴더에 `IPhysicsScene.h`, `PhysXPhysicsScene.cpp`, `NativePhysicsScene.cpp`, `PhysXCore.cpp` 가 공존 — 메모리상 legacy로 분류된 레이어 (조사 범위 외)
- [불명확] `FPhysXRuntime::CreateShape`가 매 shape 추가 시 `updateMassAndInertia(Dynamic, Desc.Material.Density)`를 호출 — PhysXRuntime.cpp:367-370. 본 desc의 `Mass` 기반 1회 호출(line 304)과 충돌 가능성. 의도된 동작인지 불명
- [불명확] `SetBodyTransform(bTeleport=false)` 경로가 `SetKinematicTarget`으로 fallback — PhysXRuntime.cpp:464-468. dynamic body에 대해선 의도하지 않은 kinematic 전환 발생 가능 (PhysXRuntime.cpp:482에서 강제로 `eKINEMATIC` 플래그 setting)

---

## I. Ragdoll 구현 선행조건 우선순위 체크리스트

### P0 — 빌드/연결 차단 요소 (이거 해결 전엔 런타임 진입 자체가 불가)
- [ ] **PhysicsTypes.h:163 `RagdoleBone` 구조체 컴파일 오류**(`FConstraintInstance* = nullptr;`) 수정 — 변수명 추가 또는 struct 제거
- [ ] `FPhysXRuntime` 인스턴스 소유자 결정 (World/Engine 글로벌 vs 씬별) 및 라이프사이클 코드 작성
- [ ] 게임/엔진 루프에서 `FPhysXRuntime::Simulate(dt)` 호출 지점 추가 (Tick 단계에서)
- [ ] PhysX `PxTolerancesScale` 엔진 단위(cm/m)에 맞춰 검토 — 단위 불일치 시 래그돌 거동 비정상

### P1 — 인터페이스 보강 (래그돌 동작에 직접 필요)
- [ ] `IPhysicsRuntime::Simulate(float)` 를 순수가상으로 인터페이스에 노출 (현재 비-virtual)
- [ ] kinematic↔dynamic 토글 API 추가 (애니메이션→래그돌 전환)
- [ ] `AddForce/AddImpulse/SetLinearVelocity/SetAngularVelocity` 본별 적용 API
- [ ] `PxD6JointDrive` 설정 인터페이스 (active ragdoll로 확장 시 필요, 패시브만이면 보류 가능)
- [ ] joint local frame 보정/구성 헬퍼 (twist 축 정확히 매핑하기 위한 utility)

### P2 — write-back 경로 (시뮬레이션 결과를 메시에 반영)
- [ ] `USkeletalMeshComponent::CreateRagdoll()` 구현 (Bodies를 Dynamic으로 전환 + AnimInstance evaluation suppress)
- [ ] `TickComponent`에 ragdoll 모드 분기 추가: `Runtime.GetBodyTransform` → 본 world/local transform → `SetBoneLocalTransforms` (또는 신규 hook)
- [ ] `eENABLE_ACTIVE_ACTORS` 콜백 또는 `Scene->getActiveActors()` 활용한 효율적 write-back

### P3 — 안정성/품질
- [ ] simulate dt clamp/substep (큰 dt에서 조인트 폭주 방지)
- [ ] 변환 함수(`ToPx*`/`ToF*`)를 공용 헤더로 분리 (래그돌 hook이 별도 TU일 때 재사용)
- [ ] vcxproj 일부 Config(135-139,160-162)에서 PhysX lib/dir 누락 분기 통일 또는 해당 Config 비활성 결정
- [ ] CreateShape 내 `updateMassAndInertia` 중복 호출 정책 정리 (Density vs Mass 의도 확정)

### P4 — 옵션
- [ ] raycast/sweep/overlap 쿼리 인터페이스 (래그돌 디버깅·게임플레이용)
- [ ] joint projection/breakable 정책 정리
- [ ] Convex/TriangleMesh shape 지원 (현재 false 반환)
