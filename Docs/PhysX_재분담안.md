# PhysX 과제 — (b)(c) 이관 모델 재탐색 + 4인 재분담안

> **PROJECT_NAME: GitDirectory13**
> 작성일: 2026-05-29 · 범위: **재탐색 + 재분담 설계** (코드 수정 없음)

---

## 0. 소스 직독 규약 준수 선언

- 모든 사실 주장은 `.h`/`.cpp`/`.hlsl`/`.vcxproj`/`.bat` **소스 직독**으로만 근거화.
- `.md`(`inventory.md`, `준협님엔진.md`)는 **열지도 인용하지도 않음** — 사실 근거 0건.
- "없음" 주장은 `rg` 명령 + **0건 결과**로 입증.
- 인용 형식: `경로/파일.cpp:LINE`.
- **방법론 노트**: 초기 심볼 카운트가 `bc` 미설치로 전부 `0` 오판 → `rg -n` 직접 매칭으로 전면 재검증함. 본 문서 수치는 재검증 후 값.

---

## 1. 분담 추상화 모델 (PhysX 4층 계층)

| 층 | 멤버 | 책임 (한 줄) | 담당 PhysX 객체·호출 |
|---|---|---|---|
| ① 시뮬 환경 | M1 | "무대를 깔고 유지" | `PxScene` 플래그·락·디스패처, NvCloth Solver |
| ② 시뮬 등록 객체 | M2 | "객체를 정의하고 등록" | `PxRigidActor`/`PxShape`, `createRigidDynamic`/`createShape`/`attachShape`/`addActor`, `userData` |
| ③ 객체 간 규칙·동기화 | M3 | "객체 사이 관계 + 외부 반영" | `PxD6Joint`(twist/swing limit), `getGlobalPose`→bone write-back |
| ④ 결과 소비·시각화 | M4 | "시뮬 결과를 받아 그리기" | (직접 호출 없음) DOF, Cloth 렌더, ShowFlag |

### 1.1 핵심 변경 — (b)(c) 이관

| # | 항목 | 기존 | 이관 후 | 이관 근거 |
|---|---|---|---|---|
| (b) | `FBodyInstance` + 본단위 바디 등록 API | M1 | **M2** | 데이터모델 생산자가 등록까지 일관 담당 |
| (c) | 본별 `PxRigidDynamic` + `PxD6Joint` 조인트 API | M1 | **M3** | 만드는 사람 = 쓰는 사람 일치 |

---

# PHASE 1 — 재탐색 (소스 직독)

## 표 1-0. 심볼 위치 인벤토리 [GitDirectory13]

| 카테고리 | 발견 파일·줄번호 | 발견 없음이면 검색 명령·결과 |
|---|---|---|
| PhysX 초기화·씬 | `PxCreateFoundation` `KraftonEngine/Source/Engine/Physics/PhysXPhysicsScene.cpp:56`, `PxCreatePhysics` `:59`, `PxDefaultCpuDispatcherCreate` `:414`, `PxSceneDesc` `:420`, `createScene` `:425`, `simulate`/`fetchResults` `:650-651` | 전부 존재 |
| PhysX 바디·shape | `createRigidDynamic` `PhysXPhysicsScene.cpp:492`, `createRigidStatic` `:493`, `addActor` `:497`, `PxBoxGeometry` `:700`, `PxCapsuleGeometry` `:712`, `userData` `:496`/`:777` | ⚠ `createShape`+`attachShape` **미사용** → `PxRigidActorExt::createExclusiveShape` `:719` 사용 |
| PhysX 조인트 | — | `rg -n -e "PxD6Joint" -e "PxD6JointCreate" -e "PxJoint" -e "setTwistLimit" -e "setSwingLimit" KraftonEngine/Source` → **0건** (영역3 전량 신규 신호) |
| 콜백·락·플래그 | `PxSimulationEventCallback` `PhysXPhysicsScene.cpp:88`, `onContact` `:108`, `onTrigger` `:188` | 락/플래그 `rg -n -e "PxSceneFlag" -e "eENABLE_CCD" -e "eENABLE_PCM" -e "eENABLE_ACTIVE_ACTORS" -e "PxSceneReadLock" -e "PxSceneWriteLock" -e "eREQUIRE_RW_LOCK" KraftonEngine/Source` → **0건** (SceneDesc는 gravity/dispatcher/filterShader/callback만 `:420-425`) |
| 엔진 측 바디·자산 | — | `rg -n -e "BodyInstance" -e "BodySetup" -e "PhysicsAsset" -e "AggregateGeom" -e "KSphereElem" -e "KBoxElem" -e "KSphylElem" -e "KConvexElem" KraftonEngine/Source` → **0건**. **영역2(b) 0% 신규 확정** |
| 메시·스켈레톤 | `USkeletalMesh` `KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMesh.h:13`, `USkeleton` `KraftonEngine/Source/Engine/Animation/Skeleton/Skeleton.h:9`, `FReferenceSkeleton` `KraftonEngine/Source/Engine/Animation/Skeleton/SkeletonTypes.h:32`, `USkeletalMeshComponent` `KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h:17`, `USkinnedMeshComponent` `.../SkinnedMeshComponent.h:20`, `UPrimitiveComponent` `KraftonEngine/Source/Engine/Component/PrimitiveComponent.h:59`, bone `ParentIndex` `SkeletonTypes.h:15` / 글로벌포즈 누적 `KraftonEngine/Source/Engine/Mesh/Skeletal/SkeletalMeshAsset.h:219` | 전부 존재 |
| 틱·월드 | `UWorld::Tick` `KraftonEngine/Source/Engine/GameFramework/World.cpp:342`, `PhysicsScene` 멤버 `World.h:136`, `PhysicsScene->Tick` `World.cpp:363`, `TickComponent` `SkeletalMeshComponent.h:74`, `TG_DuringPhysics`/`TG_PostPhysics` `KraftonEngine/Source/Engine/Core/TickFunction.h:20-21` | 틱그룹 **존재** (CMC 사용 `KraftonEngine/Source/Engine/Component/Movement/CharacterMovementComponent.cpp:27`) |
| ShowFlag·디버그 | `FShowFlags` `KraftonEngine/Source/Engine/Render/Types/ViewTypes.h:46`, 사용처 `KraftonEngine/Source/Engine/Render/Command/DrawCommandBuilder.cpp:314`, `DrawDebugHelpers` `KraftonEngine/Source/Engine/Debug/DrawDebugHelpers.h:22`, `DebugDrawQueue` `World.cpp:349` | 전부 존재 |
| 렌더 패스·DOF | `ERenderPass` `KraftonEngine/Source/Engine/Render/Types/RenderTypes.h:35`, `FFXAAPass` `.../RenderPass/FXAAPass.h:5`, `FBloomPass` `.../RenderPass/BloomPass.h:6` | DOF `rg -ni -e "depthoffield" -e "\bdof\b" -e "circleofconfusion" KraftonEngine/Source KraftonEngine/Shaders` → **0건**. **영역4 DOF 신규** |
| Cloth·CUDA | — | `rg -ni -e "nvcloth" -e "nv::cloth" -e "\bcloth\b" -e "\bsolver\b" -e "\bcuda\b" -e "cudart" KraftonEngine/Source KraftonEngine/Shaders` → **0건**; 빌드파일도 0건. **영역5 외부빌드 신규** |
| 빌드 (PhysX·NvCloth·CUDA 링크) | PhysX 4.1.2 NuGet `KraftonEngine/packages.config`, include `KraftonEngine/KraftonEngine.vcxproj:131`, DLL xcopy `KraftonEngine.vcxproj:251` | NvCloth/CUDA `rg -ni -e "nvcloth" -e "cuda" KraftonEngine/KraftonEngine.vcxproj KraftonEngine/packages.config` → **0건** |

## 표 1-A. 프로젝트 현황 [GitDirectory13]

| 항목 | 값/상태 | 근거 (`경로:줄번호`) |
|---|---|---|
| 빌드 시스템 | MSBuild + VS `.sln`/`.vcxproj` + NuGet 복원 | `msbuild "%SOLUTION_DIR%KraftonEngine.sln" /p:Configuration=Game /p:Platform=x64` `GameBuild.bat:27` |
| 렌더러 백엔드 | **Direct3D 11** | `D3D11CreateDeviceAndSwapChain(...)` `KraftonEngine/Source/Engine/Render/Device/D3DDevice.cpp:109` |
| C++ 표준 / 툴셋 | `stdcpp20` / `v143` | `KraftonEngine/KraftonEngine.vcxproj:240`, `KraftonEngine.vcxproj:49` |
| PhysX 통합 현재 상태 (완료율 추정) | **Actor 단위 강체 시뮬 ~완료** (init·씬·등록·sim·raycast·force·mass·contact/trigger 콜백). 본단위(b)·조인트(c)·Cloth·DOF는 0% → **전체 의도 기능 대비 약 30%** | `Initialize` `PhysXPhysicsScene.cpp:401`, `RegisterComponent`(actor compound) `:473`, `Tick`(sim) `:586` |
| 외부 SDK 통합 전례 | NuGet(PhysX·DirectXTK) + ThirdParty(lua/rmlui/fmod/fbx) 다수 | `lua51.lib;rmlui.lib;fmod_vc.lib;libfbxsdk.lib` `KraftonEngine.vcxproj:246`, `KraftonEngine/packages.config` |

## 표 1-B. (b)(c) 이관 시 건드릴 코드 위치 실측 [GitDirectory13]

| 이관 항목 | 신규/수정 파일 | 핵심 함수·심볼 | 인접 기존 코드 (`경로:줄번호`) |
|---|---|---|---|
| (b) `FBodyInstance` 클래스 정의 | `<신규>` `KraftonEngine/Source/Engine/Physics/BodyInstance.h` | `class FBodyInstance { UPrimitiveComponent* OwnerComponent; physx::PxRigidActor* ActorHandle; ... }` | 현 `FBodyMapping{ AActor*; PxRigidActor* Actor; UPrimitiveComponent* RootComp; TArray<...> Components; }` `PhysXPhysicsScene.h:83`; `UPrimitiveComponent` 정의 `PrimitiveComponent.h:59` |
| (b) 바디 등록 흐름 (`CreatePhysicsState` 대응) | 수정 `PrimitiveComponent.cpp` / `PhysXPhysicsScene.cpp` | 현재 컴포넌트당 1바디 → 본 수만큼 `createRigidDynamic`+`createExclusiveShape`+`addActor`+`userData` 반복 | `BeginPlay()→RegisterComponent(this)` `PrimitiveComponent.cpp:64`; PhysX측 `createRigidDynamic` `PhysXPhysicsScene.cpp:492`·`createExclusiveShape` `:719`·`addActor` `:497`·`Actor->userData` `:496` |
| (b) `InstantiatePhysicsAssetBodies` | `<신규>` (SkeletalMeshComponent 메서드) | PhysicsAsset의 BodySetup 전체 → 본별 `PxRigidDynamic` (`BodySetup`/`PhysicsAsset` 심볼 0건) | 본 포즈 푸시 지점 `SetAnimationPose(Out.Pose,...)` `SkeletalMeshComponent.cpp:441`; 등록 트리거 `BeginPlay()` `PrimitiveComponent.cpp:51` |
| (b) 컴포넌트 멤버 추가 (`Bodies`/`Constraints`) | 수정 `SkeletalMeshComponent.h` | `TArray<FBodyInstance*> Bodies` / `TArray<FConstraintInstance*> Constraints` (UPROPERTY/codegen `.generated.h` `:7`) | 마지막 멤버 `UAnimInstance* AnimInstance = nullptr;` **뒤** `SkeletalMeshComponent.h:90` |
| (c) D6 조인트 생성 | `<신규>` (M3 조인트 모듈) | `PxD6JointCreate`, `setMotion(eTWIST/eSWING1/eSWING2)`, `setTwistLimit`, `setSwingLimit` — 전부 0건 | 본 로컬프레임: 부모 인덱스 `SkeletonTypes.h:15` + 글로벌포즈 누적 `SkeletalMeshAsset.h:219`; `PxPhysics* Physics` 핸들 `PhysXPhysicsScene.h:76` |
| (c) 본 역주입 (write-back) | `<신규>` write-back 경로 | `getGlobalPose()` → bone transform 주입 (현재 RootComp **1점**에만 기록) | post-sim `getGlobalPose()` `PhysXPhysicsScene.cpp:664` → `RootComp->SetWorldLocation/SetRelativeRotation` `:668-669`; 본 기록 대상 `SetBoneLocalTransforms` `SkinnedMeshComponent.cpp:608`; 기존 anim→bone 단방향 `SetAnimationPose` `SkeletalMeshComponent.cpp:441` |

## 표 1-C. 4층 모델의 코드상 정합성 [GitDirectory13]

| 층 | 담당자 | 책임이 위치/위치 가능한 파일 (`경로:줄번호`) | 분리 장애물 | 정합성 |
|---|---|---|---|---|
| ① 시뮬 환경 | M1 | 백엔드 선택·소유 `World.cpp:309`, 씬 생성/플래그 `PhysXPhysicsScene.cpp:401`, sim tick `World.cpp:363` | Cloth Solver 전무(0건) → 외부빌드 스파이크 | **상** (씬 골격 완비) |
| ② 등록 객체 | M2 | actor-level 등록 `PhysXPhysicsScene.cpp:473` + shape 부착 `:686`; 트리거 `PrimitiveComponent.cpp:64` | 본단위(`FBodyInstance`/`BodySetup`) 0건 → (b) 신규 | **중** (actor有, bone無) |
| ③ 규칙·동기화 | M3 | write-back 현재 RootComp 1점만 `PhysXPhysicsScene.cpp:668` | 조인트 0건, 본 write-back 0건 → (c) 전량 신규 | **하** (0% 존재) |
| ④ 결과 소비 | M4 | 패스 enum `RenderTypes.h:35`, Bloom/FXAA 패스 `BloomPass.h:6`, ShowFlag `ViewTypes.h:46` | DOF 0건 → 신규 패스 | **중** (패스 프레임워크有, DOF無) |

## 표 1-D. (b)(c) 미이관 시 문제점 — 본 프로젝트 코드 기준 [GitDirectory13]

| 축 | 본 프로젝트에서 나타날 구체 증상 (소스 인용) |
|---|---|
| 학습 | M2·M3가 PhysX 호출 집결 파일(1217줄 `PhysXPhysicsScene.cpp`)의 `RegisterComponent` `:473`·`AddShapeForComponent` `:686`와 (신규) 조인트 코드를 **한 줄도 작성 안 함**. `Px*` 심볼 사용처는 전부 이 파일 1개 → M1만 풀스택. |
| 운영(병목) | M1이 인터페이스 오너(`IPhysicsScene.h:26`, 순수가상 ~14개) + 백엔드 결선(`World.cpp:309`) + 단일 구현파일 보유. (b)는 `PrimitiveComponent.cpp:64`+`PhysXPhysicsScene.cpp:492`, (c)는 같은 파일 → M2·M3·M5가 M1 확정까지 대기. |
| 코드 위생 | 씬+바디+shape+콜백+raycast가 이미 1파일 1217줄(`PhysXPhysicsScene.cpp:1217`). (b)본단위 바디+(c)조인트+Cloth 추가 시 `RegisterComponent`/`AddShapeForComponent`/`Tick`을 4인이 동시 편집 → 머지 충돌 + 경계 소멸. |
| bus factor | M1 부재 시 즉시 막힘: 씬 init `PhysXPhysicsScene.cpp:401`, 모두가 의존하는 `IPhysicsScene` 시그니처 `IPhysicsScene.h:26`, 백엔드 wiring `World.cpp:309`. 대체 인력 0 (M1=1). |

---

# PHASE 2 — 재분담안

## 표 2-A. 4인 담당 매핑 [GitDirectory13]

| 멤버 | 담당 층 | 5개 영역 매핑 | 직접 작성할 PhysX 호출 | 만지는 파일 (`경로:줄번호`) |
|---|---|---|---|---|
| M1 | ① 시뮬 환경 | 영역1 잔여 + 영역5 Cloth 시뮬/외부빌드 + 조율 | `PxScene` 플래그·락(현 SceneDesc는 플래그 미설정 → CCD/PCM 추가 여지), NvCloth Solver/Cloth(신규) | 씬/플래그 `PhysXPhysicsScene.cpp:420-425`, 백엔드 결선 `World.cpp:309`, 인터페이스 조율 `IPhysicsScene.h:26`, Cloth `<신규>` |
| M2 | ② 등록 객체 | 영역2 데이터모델·에디터 + **(b) 이관분** | `createRigidDynamic`/`createExclusiveShape`/`addActor`/`userData` | `RegisterComponent` `PhysXPhysicsScene.cpp:473`, `AddShapeForComponent` `:686`, 등록 트리거 `PrimitiveComponent.cpp:64`, `Bodies` 멤버 `SkeletalMeshComponent.h:90`, `BodyInstance.h` `<신규>` |
| M3 | ③ 규칙·동기화 | 영역3 래그돌 + **(c) 이관분** + 본 write-back | `PxD6JointCreate`, `setMotion`, `setTwistLimit`, `setSwingLimit`, `getGlobalPose`→bone | 조인트 모듈 `<신규>`, write-back 확장 `PhysXPhysicsScene.cpp:664-669`, anim 합류 `SkeletalMeshComponent.cpp:441`, 본 부모 `SkeletonTypes.h:15` |
| M4 | ④ 결과 소비 | 영역4 DOF + ShowFlag 오너 + 영역5 Cloth 렌더측 | (직접 호출 없음, **PR 리뷰로 학습**) | DOF 패스 `<신규>`(패턴 `BloomPass.h:6`+등록 `RenderPassRegistry.h:35`), `ERenderPass` `RenderTypes.h:35`, `FShowFlags` `ViewTypes.h:66`, Cloth 렌더 정점버퍼 `SkeletalMeshSceneProxy.h:28` |

## 표 2-B. 인터페이스 합의 항목 (1주차 동결) [GitDirectory13]

| 합의 항목 | 책임자 | 소비자 | 동결 시한 | 근거 파일 (`경로:줄번호`) |
|---|---|---|---|---|
| `FBodyInstance` 시그니처 | M2 | M3 | 1주차 | `<신규>` `BodyInstance.h` (현 actor-level 구조 참고 `PhysXPhysicsScene.h:83`) |
| 본단위 바디 등록 API | M2 | M3 | 1주차 | 단일바디 등록 흐름 확장 `PrimitiveComponent.cpp:64` / `PhysXPhysicsScene.cpp:473` |
| 조인트 생성 API | M3 | (자기 소비) | 1주차 | `PxD6Joint` 0건(신규) / `PxPhysics*` 핸들 `PhysXPhysicsScene.h:76` |
| 본 역주입 우선순위 (ragdoll vs anim) | M3 | 애니 경로 | 1주차 | 틱순서 `World.cpp:363`(physics) → `:366`(anim tick) → `SetAnimationPose` `SkeletalMeshComponent.cpp:441`가 덮어씀; 해결수단 `TG_PostPhysics` `TickFunction.h:21` |
| `FShowFlags` enum 추가분 | M4 | M2·M3·M4 | 1주차 | 현 마지막 멤버 `bParticle` 뒤 `ViewTypes.h:66` (물리 디버그 선례 `bShowCollisionShape` `:65`) |
| Cloth 시뮬↔렌더 정점버퍼 인터페이스 | M1+M4 합의 | (서로 소비) | 1주차 | 렌더 소비처 `FDynamicVertexBuffer` `SkeletalMeshSceneProxy.h:28` / `FVertexBuffer` `Buffer.h:7`; NvCloth 0건(신규) |
| `PhysXPhysicsScene.cpp` 파일 분리 규약 | M1 조율 | M2·M3 | 1주차 | 현 단일 1217줄 `PhysXPhysicsScene.cpp:1217`; 분리 경계 후보 Init `:401`/Register `:473`/Shape `:686`/Raycast `:969` |

## 표 2-C. PR 리뷰 의무화 규약 [GitDirectory13]

| 규약 | 내용 | 적용 대상 (`경로:줄번호`) |
|---|---|---|
| 의무 리뷰어 | 모든 physics PR은 **본인 영역 외 멤버 최소 1명** 의무 리뷰 | 핵심 변경면 `PhysXPhysicsScene.cpp:1` 외, 공유 인터페이스 `IPhysicsScene.h:26` |
| LGTM 금지 | 최소 1개의 **질문/이해확인 코멘트** 필수 | 동일 (예: 등록 흐름 `PhysXPhysicsScene.cpp:473`, write-back `:664`) |
| 영역별 학습 노트 | 영역 마무리 시 "내가 배운 것" 1쪽 노트 → 팀 공유 | 영역2 `:686` / 영역3 `SkeletalMeshComponent.cpp:441` / 영역4 `RenderTypes.h:35` |
| 주 1회 코드 워크스루 | 담당자가 자기 영역 코드를 15분 설명 | 씬 `PhysXPhysicsScene.cpp:401`, 조인트 모듈 `<신규>`, DOF 패스 `<신규>` |
| M4 특례 | M4는 비물리 구현 영역이므로 **physics PR 전부 의무 리뷰** (학습 통로) | M4 본업 `RenderTypes.h:35`; 리뷰 대상 physics 코어 `PhysXPhysicsScene.cpp:1` |

## 표 2-D. 작업량 균형 + 착수 순서 [GitDirectory13]

| 멤버 | 추정 작업량 | 임계경로 위치 | 착수 시점 | 근거 |
|---|---|---|---|---|
| M1 | **상** (씬 잔여는 中이나 NvCloth+CUDA 외부빌드 스파이크가 上) | `IPhysicsScene.h:26` 인터페이스 동결 = 전원 선행 | 즉시(0일) | 백엔드 `World.cpp:309`, 씬 `PhysXPhysicsScene.cpp:401`, Cloth 0건 |
| M2 | **상** ((b) 전량 신규 + actor→bone 모델 확장) | `FBodyInstance` 시그니처가 M3 선행 | 인터페이스 합의 직후 | `PhysXPhysicsScene.cpp:473`/`:686`, `SkeletalMeshComponent.h:90` |
| M3 | **상** ((c) 조인트 0% + write-back 우선순위 충돌 해결) | M2 바디 등록 API에 의존 | M2 API 동결 후 | `PxD6Joint` 0건, write-back `PhysXPhysicsScene.cpp:664`, anim 합류 `SkeletalMeshComponent.cpp:441` |
| M4 | **중** (DOF 신규지만 패스 프레임워크 존재 + ShowFlag) | 임계경로 아님(독립 병렬 가능) | 즉시 병렬 | `RenderTypes.h:35`, `BloomPass.h:6`, `ViewTypes.h:66` |

### 권장 착수 순서 (의존성 고려)
- [ ] **1순위** M1: `IPhysicsScene` 인터페이스 동결 + 씬 플래그 정책 (`IPhysicsScene.h:26`, `PhysXPhysicsScene.cpp:420`)
- [ ] **2순위** M2: `FBodyInstance` 시그니처 + 본단위 등록 API 동결 (`PhysXPhysicsScene.cpp:473`, `SkeletalMeshComponent.h:90`)
- [ ] **3순위** M3: M2 바디 API 위에서 조인트 + write-back 우선순위 (`PhysXPhysicsScene.cpp:664`, `SkeletalMeshComponent.cpp:441`)
- [ ] **병렬** M4 DOF(`RenderTypes.h:35`) / M1 Cloth 외부빌드(NvCloth 0건)

### 1주차 인터페이스 합의 회의 안건
- [ ] 표 2-B 7개 항목 전체 동결

### 리스크 편중 경고
- [ ] **M1의 NvCloth 1.1.6 + CUDA 10.0 환경 스파이크**: 현재 Cloth/CUDA 심볼·빌드링크 **0건**(`rg ... nvcloth/cuda → 0`). 외부빌드 + DLL 배포(`KraftonEngine.vcxproj:251` PhysX xcopy 패턴 차용) 전부 신규 → M1 단일 임계.

## 표 2-E. 마감 vs 학습 트레이드오프 [GitDirectory13]

| 충돌 지점 | 마감 우선 | 학습 우선 | 본 안의 선택 | 근거 (`경로:줄번호`) |
|---|---|---|---|---|
| (b)(c) 이관에 따른 합의 비용 | M1 단독 설계 | 4인 공동 설계 | **1주차 1회 회의로 동결** | 공유 인터페이스 `IPhysicsScene.h:26`, 등록 흐름 `PhysXPhysicsScene.cpp:473` |
| M4 physics 구현 미참여 | 그대로 | M4도 Cloth 시뮬 분담 | **PR 리뷰 의무화로 학습 확보** | M4 본업 `RenderTypes.h:35`; 리뷰 대상 `PhysXPhysicsScene.cpp:1` |
| 영역별 학습 노트 작성 | 생략 | 작성 | **작성 (영역당 0.5일)** | 영역2 `PhysXPhysicsScene.cpp:686` / 영역3 `SkeletalMeshComponent.cpp:441` |

---

## 부록 — 재탐색 핵심 결론 5선

1. **(b) 영역2 = 0% 신규**: `BodyInstance`/`BodySetup`/`PhysicsAsset`/`K*Elem` 전부 0건. 현재는 Actor 단위 compound(`FBodyMapping` `PhysXPhysicsScene.h:83`)만 존재.
2. **(c) 영역3 = 0% 신규**: `PxD6Joint*`/`setTwistLimit`/`setSwingLimit` 0건. write-back은 RootComp 1점만(`PhysXPhysicsScene.cpp:668`).
3. **ragdoll vs anim 충돌 실재**: `PhysicsScene->Tick`(`World.cpp:363`) → `TickManager.Tick`(`World.cpp:366`)에서 anim이 본을 덮어씀. 해결수단 `TG_PostPhysics`(`TickFunction.h:21`) 기존재.
4. **영역4 DOF / 영역5 Cloth·CUDA 모두 0건** → 신규(외부빌드 포함). 단, 렌더 패스 프레임워크(`RenderPassRegistry.h:35`)·정점버퍼(`SkeletalMeshSceneProxy.h:28`)는 재사용 가능.
5. **빌드**: PhysX 4.1.2 NuGet, D3D11(`D3DDevice.cpp:109`), C++20/v143(`KraftonEngine.vcxproj:240/49`).
