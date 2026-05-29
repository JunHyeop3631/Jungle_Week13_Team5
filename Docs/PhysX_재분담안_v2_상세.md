# PhysX 과제 — (b)(c) 이관 모델 재분담안 v2 (역할 상세 기술 추가)

> **PROJECT_NAME: GitDirectory13** · 기준 문서: `PhysX_재분담안.md`
> 범위: **재분담 설계 + 멤버별 역할 상세 기술** (코드 수정 없음)
>
> **v1 대비 변경점**
> - 점검에서 발견한 갭 2개 반영: **(갭①) M3 디버그 렌더 담당 명시**, **(갭②) 호출 목록 보정** (`createShape`/`attachShape` → 실제 코드의 `PxRigidActorExt::createExclusiveShape` `PhysXPhysicsScene.cpp:719`)
> - 각 멤버에 대해 **담당 역할의 구체 기술** 신설 (§3): 무엇을·왜·어떤 PhysX 호출로·어느 파일에서·어떤 순서로 하는지

---

## 1. 분담 추상화 모델 (PhysX 4층 계층) — 호출 목록 보정본

| 층 | 멤버 | 책임 (한 줄) | 담당 PhysX 객체·호출 (실측 보정) |
|---|---|---|---|
| ① 시뮬 환경 | M1 | "무대를 깔고 유지" | `PxScene` 플래그·락·디스패처(`PhysXPhysicsScene.cpp:420-425`), NvCloth `Solver`(신규) |
| ② 시뮬 등록 객체 | M2 | "객체를 정의하고 등록" | `createRigidDynamic`(`:492`)·**`PxRigidActorExt::createExclusiveShape`(`:719`)**·`addActor`(`:497`)·`userData`(`:496`) |
| ③ 객체 간 규칙·동기화 | M3 | "객체 사이 관계 + 외부 반영 + 디버그 시각화" | `PxD6JointCreate`·`setMotion`·`setTwistLimit`·`setSwingLimit`(전부 신규), `getGlobalPose`→bone write-back(`:664`), **디버그 렌더(`DrawDebugHelpers.h:22`)** |
| ④ 결과 소비·시각화 | M4 | "시뮬 결과를 받아 그리기" | (직접 PhysX 호출 없음) DOF 패스, Cloth 렌더, `FShowFlags`(`ViewTypes.h:46`) |

> 보정 ①: v1의 `createShape`+`attachShape`는 본 엔진에서 미사용. 실제 등록은 `PxRigidActorExt::createExclusiveShape`(`PhysXPhysicsScene.cpp:719`)를 거친다. M2는 이 호출 패턴을 따라야 한다.
> 보정 ②: M3 책임에 **디버그 시각화**를 명시 추가 (v1 누락분).

### 1.1 핵심 변경 — (b)(c) 이관 (변동 없음)

| # | 항목 | 기존 | 이관 후 | 이관 근거 |
|---|---|---|---|---|
| (b) | `FBodyInstance` + 본단위 바디 등록 API | M1 | **M2** | 데이터모델 생산자가 등록까지 일관 담당 |
| (c) | 본별 `PxRigidDynamic` + `PxD6Joint` 조인트 API | M1 | **M3** | 만드는 사람 = 쓰는 사람 일치 |

---

## 2. 한눈 요약 — 누가 무엇을 담당하나

| 멤버 | 층 | 핵심 한 줄 | 신규 비중 | 작업량 |
|---|---|---|---|---|
| M1 | ① 시뮬 환경 | PhysX 씬을 운영하고, 모두가 쓸 인터페이스를 동결하고, Cloth 외부빌드를 책임진다 | 영역1 잔여 中 / Cloth 上 | 상 |
| M2 | ② 등록 객체 | 데이터모델(PhysicsAsset)을 정의하고, 그것을 본단위 PhysX 바디로 씬에 등록한다 | 영역2 전량 신규 | 상 |
| M3 | ③ 규칙·동기화 | 바디들을 조인트로 엮어 래그돌을 만들고, 시뮬 결과를 본에 되돌리고, 디버그로 보이게 한다 | 영역3 전량 신규 | 상 |
| M4 | ④ 결과 소비 | DOF를 만들고, 시각화 토글을 소유하고, Cloth 결과를 정점버퍼로 그린다 | 영역4 신규(프레임워크有) | 중 |

---

## 3. 멤버별 역할 상세 기술 (신설)

> 각 멤버에 대해: **(A) 담당 범위 → (B) 구체 로직·작업 분해 → (C) 직접 작성할 PhysX 호출 → (D) 만지는 파일 → (E) 다른 멤버와의 경계**.

---

### 3.1 M1 — ① 시뮬 환경 ("무대를 깔고 유지한다")

#### (A) 담당 범위
PhysX 씬이 *존재하고 안전하게 돌아가도록* 만드는 모든 것. 객체나 조인트 자체는 만들지 않는다 — 그 객체들이 등록되고 시뮬레이션될 **환경**을 책임진다. 더해서, 4명이 공유할 인터페이스(`IPhysicsScene`)의 동결과 Cloth 외부 솔버를 맡는다.

#### (B) 구체 로직·작업 분해
- **M1-1 씬 플래그·락 정책 보강 (영역1 잔여)**: 현재 `PxSceneDesc`는 gravity/dispatcher/filterShader/callback만 설정(`PhysXPhysicsScene.cpp:420-425`). 여기에 `eENABLE_CCD`·`eENABLE_PCM`·`eENABLE_ACTIVE_ACTORS` 추가 여부와 RW lock 정책을 결정·구현. (현재 이 플래그들 0건 — `rg` 0건 확인)
- **M1-2 인터페이스 동결·조율**: `IPhysicsScene`의 순수가상 인터페이스(`IPhysicsScene.h:26`)를 M2·M3가 의존하기 전에 동결. `PhysXPhysicsScene.cpp`(현 1217줄) 단일 파일을 영역별 `.cpp`로 분리하는 규약 수립 — 분리 경계 후보: Init(`:401`)/Register(`:473`)/Shape(`:686`)/Raycast(`:969`).
- **M1-3 Cloth 외부 솔버 (영역5 시뮬측)**: NvCloth 1.1.6 `Solver`·`Cloth` 생성, particle 시뮬 구동. 시뮬 결과를 M4 렌더측에 넘기는 인터페이스 정의.
- **M1-4 NvCloth+CUDA 외부 빌드**: `vcxproj` 링크·DLL 배포 (PhysX의 xcopy 패턴 차용 `KraftonEngine.vcxproj:251`). CUDA 10.0 환경 스파이크 — **최고 리스크**.

#### (C) 직접 작성할 PhysX 호출
`PxSceneFlag` 설정, `PxSceneReadLock`/`PxSceneWriteLock`(도입 시), NvCloth `nv::cloth::Factory`·`Solver`·`Cloth`.

#### (D) 만지는 파일
`PhysXPhysicsScene.cpp:420-425`(씬/플래그), `World.cpp:309`(백엔드 결선), `IPhysicsScene.h:26`(인터페이스 조율), `KraftonEngine.vcxproj`(Cloth 링크), `Source/Engine/Physics/Cloth/*`(신규).

#### (E) 다른 멤버와의 경계
- M1은 **환경만**: 바디·조인트를 직접 만들지 않는다. M2·M3가 M1이 동결한 인터페이스 위에서 자기 객체를 만든다.
- M1↔M4: Cloth 시뮬 결과 → 정점버퍼 인터페이스를 M4와 합의(표 4 참조).

---

### 3.2 M2 — ② 시뮬 등록 객체 ("객체를 정의하고 등록한다")

#### (A) 담당 범위
"무엇을 시뮬레이션할 것인가"를 정의하는 데이터모델과, 그 정의를 *실제 PhysX 바디로 만들어 M1의 씬에 등록*하는 흐름 전체. (b) 이관으로 바디 등록 코드를 직접 작성한다.

#### (B) 구체 로직·작업 분해
- **M2-1 데이터모델 정의 (영역2 핵심, 0% 신규)**: `FKAggregateGeom`(`FKSphereElem`/`FKBoxElem`/`FKSphylElem`/`FKConvexElem`), `UBodySetupCore`(`BoneName`), `UBodySetup`(AggGeom), `UPhysicsAsset`(`TArray<UBodySetup*>`). (현재 이 심볼 전부 0건 — 완전 신규)
- **M2-2 자산·컴포넌트 연결**: `USkeletalMesh.PhysicsAsset`(`SkeletalMesh.h:13`), `UStaticMesh.BodySetup`, `UPrimitiveComponent.BodyInstance`, `USkeletalMeshComponent.Bodies/Constraints` 멤버 추가 (마지막 멤버 `AnimInstance` 뒤 `SkeletalMeshComponent.h:90`).
- **M2-3 `FBodyInstance` 클래스 (b 이관)**: `UPrimitiveComponent* OwnerComponent` + `PxRigidActor* ActorHandle`. `userData=this` 역참조 매핑(`PhysXPhysicsScene.cpp:496` 패턴). 현 actor-level `FBodyMapping`(`:83`) 구조를 참고해 본단위로 확장.
- **M2-4 바디 등록 흐름 (b 이관, 핵심 PhysX 호출)**: 현재 "컴포넌트당 1바디"(actor compound)를 **"본 수만큼 바디"로 확장**. `BeginPlay()→RegisterComponent`(`PrimitiveComponent.cpp:64`) 트리거에서, PhysicsAsset의 각 BodySetup마다 `createRigidDynamic`(`:492`)→`createExclusiveShape`(`:719`)→`addActor`(`:497`)→`userData`(`:496`) 반복.
- **M2-5 `InstantiatePhysicsAssetBodies`**: PhysicsAsset 전체를 본별 바디로 일괄 생성 (M3 조인트 생성의 사전 단계). 본 포즈 푸시 지점 `SetAnimationPose`(`SkeletalMeshComponent.cpp:441`) 참고.
- **M2-6 에디터 UI**: PhysicsAsset 편집 패널 (Body/Shape 추가·삭제·크기 조정).

#### (C) 직접 작성할 PhysX 호출
`createRigidDynamic`, `PxRigidActorExt::createExclusiveShape`(`PxBoxGeometry`/`PxCapsuleGeometry`), `addActor`, `userData` 매핑.

#### (D) 만지는 파일
`PhysXPhysicsScene.cpp:473`(등록)·`:686`(shape)·`:719`(createExclusiveShape), `PrimitiveComponent.cpp:64`(트리거), `SkeletalMeshComponent.h:90`(Bodies 멤버), `Source/Engine/Physics/BodyInstance.h`(신규), 데이터모델·에디터(신규).

#### (E) 다른 멤버와의 경계
- M2가 **`FBodyInstance` 시그니처와 바디 등록 API를 동결**하면 M3가 그 위에서 조인트를 단다. → 표 4의 게이팅 1순위.
- M2는 바디를 *만들어 씬에 넣는 데까지*. 바디들 *사이*의 관계(조인트)는 M3.

---

### 3.3 M3 — ③ 객체 간 규칙·동기화 ("바디를 엮고, 결과를 되돌리고, 보이게 한다")

#### (A) 담당 범위
M2가 만든 본별 바디들을 **조인트로 엮어 래그돌을 구성**하고, 시뮬레이션 결과를 **본 트랜스폼에 되돌리며**(write-back), 그 상태를 **디버그로 시각화**한다. (c) 이관으로 조인트 생성 코드를 직접 작성한다. **(갭① 반영: 디버그 렌더 명시 담당)**

#### (B) 구체 로직·작업 분해
- **M3-1 조인트 생성 (c 이관, 0% 신규)**: 본 계층을 순회하며 부모-자식 바디 사이마다 `PxD6JointCreate`. `setMotion(eTWIST/eSWING1/eSWING2, eLIMITED)` + `setTwistLimit(PxJointAngularLimitPair)` + `setSwingLimit(PxJointLimitCone)`. 본 로컬 프레임은 부모 인덱스(`SkeletonTypes.h:15`)와 글로벌포즈 누적(`SkeletalMeshAsset.h:219`)으로 계산. (이 심볼 전부 0건 — 완전 신규)
- **M3-2 본 역주입 (write-back) 확장**: 현재 write-back은 RootComp **1점만**(`PhysXPhysicsScene.cpp:668`). 이를 **본 전체로 확장** — 본마다 `getGlobalPose()`(`:664`) → `SetBoneLocalTransforms`(`SkinnedMeshComponent.cpp:608`)에 주입.
- **M3-3 ragdoll vs anim 우선순위 (숨은 핵심 난점)**: physics tick(`World.cpp:363`) → anim tick(`:366`)에서 `SetAnimationPose`(`SkeletalMeshComponent.cpp:441`)가 본을 덮어쓴다. ragdoll 활성 시 anim 출력을 무시하는 규칙을, `TG_PostPhysics`(`TickFunction.h:21`) 틱그룹을 활용해 구현.
- **M3-4 디버그 렌더 (갭① 반영)**: `DrawDebugHelpers`(`:22`)+`DebugDrawQueue`(`World.cpp:349`) 재사용해 본별 캡슐 바디·조인트 limit cone 시각화. 이 디버그 콘텐츠의 **소유자는 M3** (M4의 ShowFlag 토글로 on/off).

#### (C) 직접 작성할 PhysX 호출
`PxD6JointCreate`, `joint->setMotion`, `setTwistLimit`, `setSwingLimit`, `body->getGlobalPose`.

#### (D) 만지는 파일
조인트 모듈(신규), write-back 확장 `PhysXPhysicsScene.cpp:664-669`, anim 합류 `SkeletalMeshComponent.cpp:441`, 본 부모 `SkeletonTypes.h:15`, 디버그 `DrawDebugHelpers.h:22`.

#### (E) 다른 멤버와의 경계
- M3는 M2가 동결한 바디 등록 API에 **의존**(게이팅 2순위) — M2 API 없이는 조인트를 달 바디가 없다.
- 디버그 *콘텐츠*(무엇을 그릴까)는 M3, 디버그 *토글*(`FShowFlags` enum)은 M4. 두 사람이 한 enum을 공유하므로 표 4에서 합의.

---

### 3.4 M4 — ④ 결과 소비·시각화 ("결과를 받아 그린다")

#### (A) 담당 범위
물리 시뮬레이션의 *결과를 소비하는 렌더 쪽*. 직접 PhysX를 호출하지 않는다 — DOF(물리와 독립), 시각화 토글 소유, Cloth 시뮬 결과의 정점버퍼 반영. physics 학습은 **PR 리뷰로** 확보.

#### (B) 구체 로직·작업 분해
- **M4-1 DOF 패스 (영역4, 신규지만 프레임워크 존재)**: 기존 포스트프로세스 패스 패턴(`FBloomPass`(`BloomPass.h:6`)/`FFXAAPass`)을 따라 `FDepthOfFieldPass` 신설, `ERenderPass`(`RenderTypes.h:35`)에 등록. F-Stop·초점거리·CoC 파라미터, 깊이버퍼 접근. (DOF 심볼 0건 — 신규)
- **M4-2 ShowFlag 오너 (공유 인프라)**: `FShowFlags`(`ViewTypes.h:46`)에 물리바디·ragdoll·DOF 토글 추가 (현 마지막 멤버 `bParticle` 뒤 `:66`, 물리 디버그 선례 `bShowCollisionShape` `:65`). **1주차에 필요한 토글 전부 선반영 후 동결** — M2·M3·M4 모두가 소비.
- **M4-3 Cloth 렌더측 (영역5 렌더)**: M1의 Cloth 시뮬 결과(particle 위치)를 받아 `FDynamicVertexBuffer`(`SkeletalMeshSceneProxy.h:28`)로 동적 갱신. 스키닝 경로와의 소유 구간 합의.
- **M4-4 PR 리뷰 (학습 통로)**: 본인 구현이 비물리 영역이므로, M1·M2·M3의 physics PR을 **전부 의무 리뷰**. LGTM 금지, 최소 1개 질문 코멘트.

#### (C) 직접 작성할 PhysX 호출
없음. (Cloth 렌더는 M1이 넘긴 particle 데이터를 *소비*만)

#### (D) 만지는 파일
DOF 패스(신규, 패턴 `BloomPass.h:6`+등록 `RenderPassRegistry.h:35`), `RenderTypes.h:35`, `FShowFlags` `ViewTypes.h:66`, Cloth 정점버퍼 `SkeletalMeshSceneProxy.h:28`.

#### (E) 다른 멤버와의 경계
- DOF는 물리와 완전 독립 → **1일차부터 병렬** 착수 가능 (임계경로 아님).
- ShowFlag *enum*은 M4 소유, 디버그 *콘텐츠*는 M3. Cloth는 시뮬(M1)↔렌더(M4) 경계를 표 4에서 합의.

---

## 4. 인터페이스 합의 항목 (1주차 동결) — 갭 반영본

| 합의 항목 | 책임자 | 소비자 | 동결 시한 | 근거 (`경로:줄번호`) |
|---|---|---|---|---|
| `FBodyInstance` 시그니처 | M2 | M3 | 1주차 | `BodyInstance.h`(신규), 참고 `PhysXPhysicsScene.h:83` |
| 본단위 바디 등록 API (`createExclusiveShape` 경유) | M2 | M3 | 1주차 | `PrimitiveComponent.cpp:64` / `PhysXPhysicsScene.cpp:473`·`:719` |
| 조인트 생성 API | M3 | (자기 소비) | 1주차 | `PxD6Joint` 0건(신규) / `PxPhysics*` 핸들 `:76` |
| 본 역주입 우선순위 (ragdoll vs anim) | M3 | 애니 경로 | 1주차 | physics→anim 틱 `World.cpp:363`→`:366`, 해결 `TG_PostPhysics` `TickFunction.h:21` |
| **디버그 콘텐츠 ↔ ShowFlag 토글 경계 (갭① 반영)** | M3(콘텐츠)+M4(토글) | M3·M4 | 1주차 | 콘텐츠 `DrawDebugHelpers.h:22` / 토글 `ViewTypes.h:66` |
| `FShowFlags` enum 추가분 | M4 | M2·M3·M4 | 1주차 | `ViewTypes.h:66`, 선례 `:65` |
| Cloth 시뮬↔렌더 정점버퍼 인터페이스 | M1+M4 | (서로 소비) | 1주차 | `SkeletalMeshSceneProxy.h:28` / NvCloth 0건(신규) |
| `PhysXPhysicsScene.cpp` 파일 분리 규약 | M1 조율 | M2·M3 | 1주차 | 현 1217줄, 경계 Init`:401`/Register`:473`/Shape`:686`/Raycast`:969` |

---

## 5. 착수 순서 + 작업량 (변동 없음, 재확인)

| 순위 | 멤버 | 작업 | 의존 |
|---|---|---|---|
| 1 | M1 | `IPhysicsScene` 동결 + 씬 플래그 정책 | 없음(선행) |
| 2 | M2 | `FBodyInstance` 시그니처 + 본단위 등록 API 동결 | M1 인터페이스 |
| 3 | M3 | M2 바디 API 위에서 조인트 + write-back + 디버그 | M2 API 동결 |
| 병렬 | M4 / M1 | M4 DOF(독립, 1일차) / M1 Cloth 외부빌드 스파이크 | 없음 |

작업량: M1·M2·M3 **상**, M4 **중**. 최대 리스크 = M1의 NvCloth 1.1.6 + CUDA 10.0 외부빌드(전부 0건 신규).

---

## 6. v1→v2 변경 요약 (점검 반영)

- [x] **(갭①) M3 디버그 렌더 담당 명시**: §3.3 M3-4 + §1 책임 컬럼 + 표 4 "디버그 콘텐츠↔토글 경계" 신규 행
- [x] **(갭②) 호출 목록 보정**: `createShape`/`attachShape` → `PxRigidActorExt::createExclusiveShape`(`:719`)로 §1·§3.2 보정
- [x] **멤버별 역할 상세 기술 신설**(§3): A 담당범위 / B 작업분해 / C PhysX 호출 / D 파일 / E 경계
- [x] 완료율 기준 명확화: 영역1 단독 ≈80% vs 5개영역 전체 ≈30% (분모 차이)
