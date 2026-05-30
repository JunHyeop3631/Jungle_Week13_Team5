# Physics Scene Migration Survey — IPhysicsScene → FPhysXRuntime

## 0. 조사 범위와 정책

- **조사 대상 (legacy 3겹)**
  - 인터페이스: [KraftonEngine/Source/Engine/Physics/IPhysicsScene.h](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h)
  - 구현체: [KraftonEngine/Source/Engine/Physics/PhysXPhysicsScene.h](KraftonEngine/Source/Engine/Physics/PhysXPhysicsScene.h) / [.cpp](KraftonEngine/Source/Engine/Physics/PhysXPhysicsScene.cpp)
  - 구현체: [KraftonEngine/Source/Engine/Physics/NativePhysicsScene.h](KraftonEngine/Source/Engine/Physics/NativePhysicsScene.h) / [.cpp](KraftonEngine/Source/Engine/Physics/NativePhysicsScene.cpp)
- **대조 대상 (신규, 읽기 전용)**
  - 인터페이스: [KraftonEngine/Source/Engine/Physics/IPhysicsRuntime.h](KraftonEngine/Source/Engine/Physics/IPhysicsRuntime.h)
  - 구현체: [KraftonEngine/Source/Engine/Physics/PhysXRuntime.h](KraftonEngine/Source/Engine/Physics/PhysXRuntime.h) / [.cpp](KraftonEngine/Source/Engine/Physics/PhysXRuntime.cpp)
- **이관 정책 (재진단 금지, 입력 사실)**: legacy 기능을 신규 `FPhysXRuntime` 으로 옮기되, **기존 호출처의 시그니처는 최대한 보존**한다. 따라서 본 표의 호출처는 "전부 고칠 목록"이 아니라 "신규 Runtime이 호환 형태로 흡수해야 할 계약" 이다.
- 구현체-only public 메서드(인터페이스에 없는데 구현체에만 있는 것)는 **없음** — [PhysXPhysicsScene.h:36-69](KraftonEngine/Source/Engine/Physics/PhysXPhysicsScene.h:36), [NativePhysicsScene.h:19-52](KraftonEngine/Source/Engine/Physics/NativePhysicsScene.h:19) 의 public 영역 전부 `IPhysicsScene` override 이다.

---

## 1. 메인 매핑 표

호출처 라인은 직접 호출 라인만 표기. 가독성 위해 `UPrimitiveComponent*` → `Comp`, `const FVector&` → `FVector`, `const AActor*` → `AActor*` 로 축약. 정식 시그니처는 인터페이스 파일의 해당 라인 참조.

난이도 기준
- **[낮음]**: 신규에 동일 기능 [있음], 시그니처도 호환 → 단순 위임.
- **[중간]**: 신규에 기능 [있음/일부], 시그니처 불일치 → 어댑터/오버로드 추가.
- **[높음]**: 신규에 [없음] 또는 [일부] → 신규 Runtime 에 기능 신설 필요.
- **[미사용]**: 외부 호출처 0건.

| # | legacy API 시그니처 | 기능 요약 | 호출처 (파일:라인, 건수) | FPhysXRuntime 대조 | 시그니처 차이 | 이관 난이도 |
|---|---|---|---|---|---|---|
| 1 | `void Initialize(UWorld* InWorld)` ([IPhysicsScene.h:41](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:41)) | 물리 시스템 부트스트랩 + World 보유 | [World.cpp:312](KraftonEngine/Source/Engine/GameFramework/World.cpp:312) (1건) | **[있음]** `IPhysicsRuntime::Initialize()` = 0 ([IPhysicsRuntime.h:17](KraftonEngine/Source/Engine/Physics/IPhysicsRuntime.h:17)), `FPhysXRuntime::Initialize()` ([PhysXRuntime.h:20](KraftonEngine/Source/Engine/Physics/PhysXRuntime.h:20)) | `UWorld*` 인자 없음, 반환 `void→bool` | [중간] — `Initialize(UWorld*)` 오버로드 또는 신규 인터페이스에 World 의존 추가 필요 |
| 2 | `void Shutdown()` ([IPhysicsScene.h:42](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:42)) | 물리 리소스 해제 | [World.cpp:416](KraftonEngine/Source/Engine/GameFramework/World.cpp:416) (1건) | **[일부]** 구현체 `FPhysXRuntime::Shutdown()` ([PhysXRuntime.h:21](KraftonEngine/Source/Engine/Physics/PhysXRuntime.h:21))는 있으나 `IPhysicsRuntime` 인터페이스엔 **없음** | 동일 | [중간] — 인터페이스에 `virtual void Shutdown() = 0` 승격 필요 |
| 3 | `void RegisterComponent(UPrimitiveComponent* Comp)` ([IPhysicsScene.h:48](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:48)) | 컴포넌트 등록 — actor 단위로 PxRigidActor + compound shape 생성 | [PrimitiveComponent.cpp:64](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:64), [:223](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:223), [:415](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:415) (3건) | **[일부]** `CreateRigidBody(FPhysicsBodyDesc) → FBodyInstance*` ([IPhysicsRuntime.h:10](KraftonEngine/Source/Engine/Physics/IPhysicsRuntime.h:10)) + `CreateShape(FBodyInstance*, FPhysicsShapeDesc)` ([:13](KraftonEngine/Source/Engine/Physics/IPhysicsRuntime.h:13)). Comp 단위 등록 entrypoint **없음** | 인자: `UPrimitiveComponent*` → `FPhysicsBodyDesc` 빌드 필요 ([PhysicsTypes.h:103-123](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h:103)). 반환 `void` → `FBodyInstance*`. Comp↔Body 매핑 보유 책임 부재 | [높음] — 호환 유지하려면 Runtime이 Comp 기반 등록 헬퍼 + Comp↔BodyInstance 매핑 신규 보유. compound shape(액터 단위) 정책도 신규로 옮겨야 함 ([PhysXPhysicsScene.cpp:443-493](KraftonEngine/Source/Engine/Physics/PhysXPhysicsScene.cpp:443) 참조) |
| 4 | `void UnregisterComponent(UPrimitiveComponent* Comp)` ([IPhysicsScene.h:49](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:49)) | 등록 해제 — actor 마지막 shape 시 actor 자체 release | [PrimitiveComponent.cpp:45](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:45), [:86](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:86), [:227](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:227), [:417](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:417) (4건) | **[일부]** `DestroyRigidBody(FBodyInstance*)` ([IPhysicsRuntime.h:11](KraftonEngine/Source/Engine/Physics/IPhysicsRuntime.h:11)) | `UPrimitiveComponent*` → `FBodyInstance*`. Comp→Body 매핑 lookup + shape detach 정책(actor 유지 vs 해제) 부재 | [중간] — #3 의 매핑이 있으면 위임 가능. compound 정책 부재로 일부 [높음] 요소 포함 |
| 5 | `void RebuildBody(UPrimitiveComponent* Comp)` ([IPhysicsScene.h:57](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:57)) | SimulatePhysics/ObjectType/Response 변경 시 actor 통째 재등록 (PhysX), 또는 BodyState 갱신 (Native) | [PrimitiveComponent.cpp:113](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:113) (1건, `NotifyPhysicsBodyDirty` 경유) | **[없음]** | — | [높음] — `DestroyRigidBody`+`CreateRigidBody` 조합 + compound 친구 컴포넌트 함께 재등록 정책 신규로 ([PhysXPhysicsScene.cpp:533-561](KraftonEngine/Source/Engine/Physics/PhysXPhysicsScene.cpp:533) 동작 참조) |
| 6 | `void Tick(float DeltaTime)` ([IPhysicsScene.h:63](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:63)) | 시뮬레이션 step + post-simulate 이벤트 디스패치 | [World.cpp:367](KraftonEngine/Source/Engine/GameFramework/World.cpp:367) (1건) | **[있음]** `Simulate(float)` ([IPhysicsRuntime.h:24](KraftonEngine/Source/Engine/Physics/IPhysicsRuntime.h:24)) — 현재 World.cpp:370 에서 동시 호출 중 (이중 PxFoundation 문제의 원인 중 하나) | 이름만 다름 (`Tick` ↔ `Simulate`) | [중간] — 위임 + 호출처 한곳만 정리. 단, `Tick` 내부의 post-event 처리([PhysXPhysicsScene.cpp:563-690](KraftonEngine/Source/Engine/Physics/PhysXPhysicsScene.cpp:563) 추정 범위)도 같이 이관 필요 |
| 7 | `void AddForce(Comp, FVector Force)` ([IPhysicsScene.h:69](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:69)) | 강체에 force 누적 | [PrimitiveComponent.cpp:479](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:479) (1건) | **[없음]** | — | [높음] — 신규 메서드 신설. 호환 위해 `(Comp, FVector)` 시그니처 유지 가능하려면 #3 매핑 전제 |
| 8 | `void AddForceAtLocation(Comp, FVector Force, FVector WorldLocation)` ([IPhysicsScene.h:70](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:70)) | 월드 위치에 force 적용 | [PrimitiveComponent.cpp:487](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:487) (1건) | **[없음]** | — | [높음] — #7 과 동일 |
| 9 | `void AddTorque(Comp, FVector Torque)` ([IPhysicsScene.h:71](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:71)) | torque 누적 | [PrimitiveComponent.cpp:495](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:495) (1건) | **[없음]** | — | [높음] — #7 과 동일 |
| 10 | `FVector GetLinearVelocity(Comp) const` ([IPhysicsScene.h:77](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:77)) | linear velocity 조회 | [PrimitiveComponent.cpp:503](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:503) (1건) | **[없음]** | — | [높음] — 신규 메서드 신설 |
| 11 | `void SetLinearVelocity(Comp, FVector Vel)` ([IPhysicsScene.h:78](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:78)) | linear velocity 쓰기 | [PrimitiveComponent.cpp:512](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:512) (1건) | **[없음]** | — | [높음] — 신규 메서드 신설 |
| 12 | `FVector GetAngularVelocity(Comp) const` ([IPhysicsScene.h:79](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:79)) | angular velocity 조회 | [PrimitiveComponent.cpp:520](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:520) (1건) | **[없음]** | — | [높음] — 신규 메서드 신설 |
| 13 | `void SetAngularVelocity(Comp, FVector Vel)` ([IPhysicsScene.h:80](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:80)) | angular velocity 쓰기 | [PrimitiveComponent.cpp:529](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:529) (1건) | **[없음]** | — | [높음] — 신규 메서드 신설 |
| 14 | `void SetMass(Comp, float Mass)` ([IPhysicsScene.h:86](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:86)) | 런타임 mass 변경 | [PrimitiveComponent.cpp:538](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:538) (1건) | **[일부]** `FPhysicsBodyDesc::Mass` ([PhysicsTypes.h:114](KraftonEngine/Source/Engine/Physics/PhysicsTypes.h:114))는 **생성 시점**만 제공. 런타임 setter 부재 | 생성-시점-only vs 런타임 setter | [높음] — 런타임 mass setter 신규 추가 |
| 15 | `float GetMass(Comp) const` ([IPhysicsScene.h:87](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:87)) | mass 조회 | **(0건)** — 컴포넌트가 멤버 직접 반환 ([PrimitiveComponent.cpp:557-559](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:557)) | **[없음]** | — | **[미사용]** |
| 16 | `void SetCenterOfMass(Comp, FVector LocalOffset)` ([IPhysicsScene.h:94](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:94)) | local CoM offset 쓰기 | [PrimitiveComponent.cpp:547](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:547) (1건) | **[없음]** | — | [높음] — 신규 메서드 신설 |
| 17 | `FVector GetCenterOfMass(Comp) const` ([IPhysicsScene.h:95](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:95)) | CoM 조회 | **(0건)** — 컴포넌트가 멤버 직접 반환 ([PrimitiveComponent.cpp:550-554](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:550)) | **[없음]** | — | **[미사용]** |
| 18 | `bool Raycast(FVector Start, FVector Dir, float MaxDist, FHitResult& OutHit, ECollisionChannel TraceChannel, AActor* IgnoreActor) const` ([IPhysicsScene.h:105-107](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:105)) | 채널 응답 기반 raycast | [World.cpp:227](KraftonEngine/Source/Engine/GameFramework/World.cpp:227) (1건, `UWorld::PhysicsRaycast` 래퍼) ← 외부: [SpringArmComponent.cpp:121](KraftonEngine/Source/Engine/Component/Camera/SpringArmComponent.cpp:121) (1건) | **[없음]** | — | [높음] — query API 신설. 호환 위해 World 래퍼 시그니처([World.h:151-153](KraftonEngine/Source/Engine/GameFramework/World.h:151)) 유지 가능 |
| 19 | `bool RaycastByObjectTypes(FVector Start, FVector Dir, float MaxDist, FHitResult& OutHit, uint32 ObjectTypeMask, AActor* IgnoreActor) const` ([IPhysicsScene.h:119-120](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:119)) | ObjectType 마스크 raycast | [World.cpp:235](KraftonEngine/Source/Engine/GameFramework/World.cpp:235) (1건, `UWorld::PhysicsRaycastByObjectTypes` 래퍼) ← 외부: [CharacterMovementComponent.cpp:336](KraftonEngine/Source/Engine/Component/Movement/CharacterMovementComponent.cpp:336) (1건) | **[없음]** | — | [높음] — #18 과 동일, ObjectType filter 정책도 같이 |
| 20 | `bool SphereSweepShapeComponents(FVector Start, FVector Dir, float MaxDist, float Radius, FHitResult& OutHit, ECollisionChannel TraceChannel, AActor* IgnoreActor) const` ([IPhysicsScene.h:126-129](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:126)) | sphere sweep query | [World.cpp:243](KraftonEngine/Source/Engine/GameFramework/World.cpp:243) (1건, `UWorld::PhysicsSphereSweepShapeComponents` 래퍼) ← 외부: [ParticleModuleCollision.cpp:44](KraftonEngine/Source/Engine/Particles/Module/ParticleModuleCollision.cpp:44) (1건) | **[없음]** | — | [높음] — sweep query 신설 |

---

## 2. [미사용] API 목록

호출처 0건. 이관 시 생략 후보.

- **`float IPhysicsScene::GetMass(UPrimitiveComponent*) const`** ([IPhysicsScene.h:87](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:87))
  - 컴포넌트가 자체 멤버를 직접 반환 ([PrimitiveComponent.cpp:557-559](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:557))
  - 백엔드 사이드(`Root->GetMass()` 등)는 **컴포넌트 메서드**로 호출되므로 무관 ([PhysXPhysicsScene.cpp:247](KraftonEngine/Source/Engine/Physics/PhysXPhysicsScene.cpp:247), [NativePhysicsScene.cpp:37/47](KraftonEngine/Source/Engine/Physics/NativePhysicsScene.cpp:37))
- **`FVector IPhysicsScene::GetCenterOfMass(UPrimitiveComponent*) const`** ([IPhysicsScene.h:95](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:95))
  - 컴포넌트가 자체 멤버를 직접 반환 ([PrimitiveComponent.cpp:550-554](KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp:550))
  - 백엔드 사이드 호출은 컴포넌트 메서드 ([PhysXPhysicsScene.cpp:249/948](KraftonEngine/Source/Engine/Physics/PhysXPhysicsScene.cpp:249), [NativePhysicsScene.cpp:38/48](KraftonEngine/Source/Engine/Physics/NativePhysicsScene.cpp:38))

---

## 3. [호환불가-검토] 목록

호환성 정책(호출처 시그니처 보존)을 곧이곧대로 적용했을 때 정책 충돌 또는 신규 인터페이스 디자인 결정이 필요한 항목. **결정은 이관 계획 사이클로**.

- **`Initialize(UWorld* InWorld)` (#1)**
  - 호출처는 단 한 군데(`World::InitWorld` [World.cpp:312](KraftonEngine/Source/Engine/GameFramework/World.cpp:312)). 시그니처 보존 비용 낮음.
  - 그러나 `IPhysicsRuntime::Initialize()` 는 인자 없음이 정책. 호환 시 신규 인터페이스에 `UWorld*` 의존성을 들이게 됨 — 신규의 layering 의도(엔진 독립?)와 충돌 가능성 → **검토 필요**.
- **`RegisterComponent / UnregisterComponent / RebuildBody / AddForce / AddTorque / Get|SetLinearVelocity / Get|SetAngularVelocity / SetMass / SetCenterOfMass` (#3, #4, #5, #7-14, #16)**
  - 모두 `UPrimitiveComponent*` 가 1번 인자. 호환 유지 시 신규 Runtime 이 `UPrimitiveComponent*` 의존을 갖게 됨 (현재 신규는 `FBodyInstance*` 핸들 기반 설계).
  - 호환 layer를 World/Component 쪽에 둘지(=신규 Runtime은 핸들 그대로), Runtime 내부에 둘지(=Runtime이 Comp↔Body 매핑 보유) 결정 필요 → **검토 필요**.
- **`Raycast / RaycastByObjectTypes / SphereSweepShapeComponents` (#18-20)**
  - 호출처는 `UWorld::PhysicsRaycast*` 래퍼([World.h:151-165](KraftonEngine/Source/Engine/GameFramework/World.h:151)) 한 단계 뒤. 래퍼 시그니처 유지 = 외부(SpringArm/CharacterMovement/Particle) 변경 없음.
  - 신규 인터페이스에 query 면을 추가하려면 `FHitResult` / `ECollisionChannel` / `AActor*` (엔진 타입) 의존이 들어옴 → 신규 layering 정책 결정 필요 → **검토 필요**.

---

## 4. [불명확] 목록

- 없음. 모든 호출처와 신규 대조가 식별되었다.

---

## 5. 부수 종속 (표 외 참고)

본 표의 API 면 자체는 아니지만 legacy 헤더 제거에 영향을 주는 부수 종속.

- **`enum class EPhysicsBackend`** ([IPhysicsScene.h:17](KraftonEngine/Source/Engine/Physics/IPhysicsScene.h:17))
  - 소비처: [ProjectSettings.h:6](KraftonEngine/Source/Engine/Core/ProjectSettings.h:6) (include), [ProjectSettings.h:31](KraftonEngine/Source/Engine/Core/ProjectSettings.h:31) (멤버 기본값), [ProjectSettings.cpp:85](KraftonEngine/Source/Engine/Core/ProjectSettings.cpp:85), [:87](KraftonEngine/Source/Engine/Core/ProjectSettings.cpp:87), [EditorProjectSettingsWidget.cpp:97](KraftonEngine/Source/Editor/UI/Panel/EditorProjectSettingsWidget.cpp:97), [World.cpp:308](KraftonEngine/Source/Engine/GameFramework/World.cpp:308)
  - `IPhysicsScene.h` 삭제 전 enum 의 새 거처 필요(또는 백엔드 토글 자체 폐기 결정).
- **World 소유 형태**
  - legacy: `std::unique_ptr<IPhysicsScene> PhysicsScene` ([World.h:137](KraftonEngine/Source/Engine/GameFramework/World.h:137))
  - 신규(현재 공존): `std::unique_ptr<IPhysicsRuntime> PhysicsRuntime` ([World.h:140](KraftonEngine/Source/Engine/GameFramework/World.h:140))
  - 동시 생성/Tick 경로 ([World.cpp:308-316](KraftonEngine/Source/Engine/GameFramework/World.cpp:308), [:364-371](KraftonEngine/Source/Engine/GameFramework/World.cpp:364))이 `BODY_CREATION_FAILURE_DIAGNOSIS.md` 가 지적한 이중 `PxCreateFoundation` 의 직접 원인.

---

## 6. 집계

- 메서드 수: **20개** (인터페이스 전수, 구현체-only public 없음)
- [미사용]: **2개** (#15, #17)
- [중간]: **3개** (#1, #2, #6)
- [높음]: **15개** (#3, #4, #5, #7, #8, #9, #10, #11, #12, #13, #14, #16, #18, #19, #20)
- [낮음]: 0개
- 외부 호출처 총 건수: **PrimitiveComponent 내부 14건 + World 내부 5건 + 외부 호출처 3건(SpringArm/CharacterMovement/ParticleCollision)** + AActor/SceneSaveManager 의 `RegisterComponent` 는 `AActor::RegisterComponent(UActorComponent*)` 로 본 인터페이스와 **무관** (grep 노이즈 — 표에서 제외 확인).
