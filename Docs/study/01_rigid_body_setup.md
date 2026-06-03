# 01. 강체와 바디 생성

> 라인 번호는 확인 시점(2026-06-03) 스냅샷. 심볼명으로 재확인할 것. 좌표 규약은 [06](06_coordinate_math.md) 참조.
> 에디터 물리 코드는 `MeshEditorWidget.Physics.cpp` → `PhysicsEditorWidget.cpp`로 분리됐다(클래스는 여전히 `FMeshEditorWidget`). [00](00_overview.md) 규칙4 참조.

---

## 1. 이론

### 1.1 rigid body
강체(rigid body)는 변형 없이 위치·자세만 갖는 물체다. 시뮬레이션 상태는 위치/회전 + 선/각속도. 시간 적분으로 중력·접촉·조인트 임펄스를 받아 움직인다.

### 1.2 mass / inertia / center of mass
- **mass(질량)**: 선형 운동의 관성. `a = F/m`.
- **inertia tensor(관성 텐서)**: 회전 운동의 관성. 형상과 질량분포로 결정.
- **center of mass(질량중심)**: 토크·세차의 기준점.

물리 엔진은 보통 두 방식으로 질량특성을 정한다.
- **질량 직접 지정**: 질량 값을 주고 형상으로 관성텐서를 계산.
- **밀도(density) 지정**: 밀도를 주고 형상 부피로 질량·관성을 계산 (`mass = density × volume`).

### 1.3 BodySetup의 역할
"본 단위 물리 설계도". 어떤 형상(들)으로, 어떤 질량/마찰/감쇠로 강체를 만들지 자산에 저장한다. 런타임은 이 설계도를 읽어 PhysX actor를 만든다.

---

## 2. 코드 대조

### 2.1 설계도: `UBodySetup` / `FKAggregateGeom`
`Source/Engine/Physics/Asset/BodySetup.h` (필드 `:34~47`):
```cpp
FString         BoneName;                 // :34
FKAggregateGeom AggregateGeom;            // :35  이 본의 모든 콜리전 형상
float Mass = 1.0f;                        // :37  ⚠ 2.5 참조 — 런타임에서 density로 소비됨
float LinearDamping = 0.01f, AngularDamping = 0.05f;   // :38,:39
float Friction = 0.7f, Restitution = 0.3f;             // :40,:41  ⚠ 2.5 참조 — 래그돌 셰이프에는 미적용
bool  bSimulatePhysics = true;            // :42
bool  bEnableGravity = true;              // :45  ⚠ 런타임에서 무시(강제 true)
EBodyPhysicsType  PhysicsType = Default;  // :46  Default(=0, 래그돌) / Kinematic(=1)
EBodyCollisionEnabled CollisionEnabled = QueryAndPhysics;   // :47  ★ 신규 필드
```
- ★ **신규**: `EBodyCollisionEnabled{ NoCollision=0, QueryOnly=1, PhysicsOnly=2, QueryAndPhysics=3 }`(`:10~16`)와 `CollisionEnabled` 필드(`:47`)가 추가됐다. `EBodyPhysicsType`는 `{ Default=0, Kinematic=1 }`(`:20~24`) — 첫 값이 `Dynamic`이 아니라 `Default`임에 주의.
형상 요소 (`Source/Engine/Physics/Asset/PhysicsGeometry.h`):
- `FKSphereElem{ Center, Radius }`
- `FKBoxElem{ Center, Rotation, HalfX, HalfY, HalfZ }`
- `FKCapsuleElem{ Center, Rotation, Radius, HalfHeight }`
- `FKAggregateGeom{ TArray<...> SphereElems / BoxElems / CapsuleElems }` — 한 본에 여러 형상 가능.

### 2.2 형상 크기·배치 산정 (자동 생성)
`FMeshEditorWidget::GeneratePhysicsBodies()` (`Source/Editor/UI/Asset/Mesh/PhysicsEditorWidget.cpp:1164`).
> 참고: 같은 파일 `:1071`(+`MeshEditorWidget.h:170`)에 "GeneratePhysicsBodies는 아직 stub" 주석이 남아 있으나 **현재는 전체 구현이 존재**한다(stale 주석 — 규칙2의 실증).
> 신규: 재생성 시 `PA->ClearDisabledCollisionPairs()`를 먼저 호출(`:1195`)해 멱등성을 보장한다.

크기 척도 = **본별 스키닝 정점 AABB의 최대 변** (`:1203~1230`):
```cpp
if (V.BoneWeights[k] < kAutoGenSkinWeightMin /*0.2*/) continue;   // :1210 충분히 스키닝된 정점만
// 본별 min/max 누적 → MaxEdge(); 정점 없으면 자식거리 median 폴백(:1233~1244); 둘 다 없으면 0
```
형상은 **가장 먼 자식 방향**으로 눕힌다 (`:1281~1323`):
```cpp
const float   L = Lmax;                                  // 최장 자식까지 거리
const FVector d = Bones[cStar].GetReferenceLocalPose().GetLocation().Normalized();
const FVector Center = d * (L * 0.5f);                    // 본↔자식 중점
const FQuat   Rot = S.bOrientAlongBone ? AutoGen_AlignXToDir(d) : FQuat::Identity; // X→본방향
const float   Rad = AutoGen_Clamp(L * S.CapsuleRadiusRatio, kAutoGenMinRadius, L * 0.5f);
```
- **Box**: `HalfX = L*0.5`(장축), `HalfY = HalfZ = Rad`.
- **Sphere**: `Center`, `Radius = Rad`.
- **Capsule**(기본): `Center`, `Rotation = Rot`, `Radius = clamp(...)`, **`HalfHeight = L*0.5`** (center→tip). 주석: "런타임 변환부가 -Radius 적용".
- 자식 없음/퇴화(L≈0): 정점 AABB 기반 **구 폴백** (geom을 비우면 인스턴스화가 통째로 skip하므로 반드시 채움).

→ 형상 데이터(Center/Rotation)는 **본 로컬 좌표**. 캡슐·박스 장축은 [06](06_coordinate_math.md)의 **X 장축** 규약(AlignXToDir).

### 2.3 본 → 바디 좌표 보정 + ★ 컴포넌트 스케일 베이킹 (신규)
래그돌 셰이프는 자동생성 단계에서 이미 X 장축으로 정렬됐으므로, 런타임 변환은 `Center/Rotation`을 회전 그대로 넘긴다. **단, 커밋 `d14280e3 fix: ragdoll scale issue` 이후 `AppendPhysicsShapes`가 컴포넌트의 균등 스케일(`PhysicsAssetScale`)을 셰이프 크기·중심에 베이크한다.** `AppendPhysicsShapes` (`SkeletalMeshComponent.cpp:95`):
```cpp
void AppendPhysicsShapes(const UBodySetup& BodySetup, FPhysicsBodyDesc& BodyDesc, float PhysicsAssetScale)   // ★ 3번째 인자 신규
...
// Capsule (:119~130)
ShapeDesc.LocalTransform = MakeUnitScaleTransform(Capsule.Center * PhysicsAssetScale, Capsule.Rotation);
ShapeDesc.Radius = Capsule.Radius * PhysicsAssetScale;
// PhysX PxCapsuleGeometry expects the half distance between the two sphere centers.
ShapeDesc.HalfHeight = std::max(0.0f, Capsule.HalfHeight - Capsule.Radius) * PhysicsAssetScale;   // :127 ★ -Radius 보정 + 스케일
```
- ★ **스케일 베이킹**: Sphere(`:102~103`)/Box(`:113~114`)/Capsule(`:124~127`) 모두 center·치수에 `* PhysicsAssetScale`를 곱한다. 스케일은 `GetPhysicsAssetUniformScale`(`:82`)로 구하며 **균등 스케일 가정**(비균등은 근사+경고, `:792~800`). **현재 씬 scale=1에선 기하학적 no-op**이지만 코드는 항상 곱한다.
- **캡슐 HalfHeight 의미 차이**: 엔진 자산의 `HalfHeight`는 center→tip(반쪽 전체 길이)이지만, PhysX `PxCapsuleGeometry`는 **두 반구 중심 사이 거리의 절반**(원기둥부 반길이)을 원한다 → `PhysXHalfHeight = max(0, EngineHalfHeight − Radius)`.
- `Source/Engine/Physics/PhysXHelpers.h:79`: `OutGeometry = PxCapsuleGeometry(Desc.Radius, Desc.HalfHeight);` → 장축 +X.
- Box/Sphere는 `-Radius`류 보정 없음: `PxBoxGeometry(HalfExtent.X/Y/Z)`, `PxSphereGeometry(Radius)`.
- 재질 채움: `ApplyBodyMaterial`(`:74~80`)이 각 셰이프 `ShapeDesc.Material`에 BodySetup Friction/Restitution + `Density=1.0`을 넣는다(소비는 2.5 참조).
- (대조: 래그돌이 아닌 `UCapsuleComponent`는 Z→X 보정이 필요하다 — [06 부록 A](06_coordinate_math.md) 참조.)

### 2.4 바디 인스턴스화 (자산 → PhysX actor)
`USkeletalMeshComponent::InstantiatePhysicsAssetBodies` (`SkeletalMeshComponent.cpp:774` — 실제 구현; `:769`는 `GetPhysicsAsset()`을 채워 넘기는 1-인자 오버로드):
```cpp
BodyDesc.BoneIndex = BoneIndex;                                              // :853 (신규)
BodyDesc.BodyType = BodySetup->bSimulatePhysics ? EPhysicsBodyType::Dynamic : EPhysicsBodyType::Kinematic;  // :854
BodyDesc.WorldTransform = BoneWorldTransform;       // :855 바디 월드 = 본 월드
BodyDesc.Mass = std::max(0.001f, BodySetup->Mass);  // :856
BodyDesc.LinearDamping = BodySetup->LinearDamping;  BodyDesc.AngularDamping = BodySetup->AngularDamping;   // :857,:858
BodyDesc.bUseGravity = true;  BodyDesc.bEnableCCD = true;  BodyDesc.bStartAwake = true;   // :859~861 ⚠ BodySetup 값 무시, 강제 true
BodyDesc.Aggregate = PhysicsAggregate;              // :862 자기충돌 제어용(05 문서)
AppendPhysicsShapes(*BodySetup, BodyDesc, PhysicsAssetScale);   // :864
if (BodyDesc.Shapes.empty()) continue;              // :865 geom 없으면 skip
FBodyInstance* Body = Scene.CreateRigidBody(BodyDesc);
Bodies[BoneIndex] = Body;                           // :877 본 인덱스로 색인
BodyToBoneOffsets[BoneIndex] = BoneWorld.ToMatrix() * BodyWorld.ToMatrix().GetInverse();   // :884 ★ 신규
```
- 바디의 월드 트랜스폼 = 본 월드 트랜스폼이라 본↔바디 오프셋이 사실상 0이지만, **이제 코드는 생성 후 실제 바디 포즈로 `BodyToBoneOffsets[BoneIndex]`를 명시적으로 계산해 둔다**(`:878~885`). write-back([05](05_runtime_and_collision.md))과 래그돌 진입에서 이 오프셋을 항상 곱한다(scale=1·오프셋0이면 identity여서 동작 동일). → scale 베이킹 인프라([08](08_ragdoll_skinning_physics.md) 5장).
- ★ **per-pair 자기충돌 필터(신규)**: self-collision ON일 때 각 바디에 `BodyFilterIndex`(0~31 제한, 초과 시 경고)와 `RagdollFilterGroupId`를 부여하고 `DisabledCollisionPairs`를 읽어 `Scene.SetRagdollBodyFilter(...)`로 배선(`:804~809`, `:984~1007`). → [05](05_runtime_and_collision.md) 2.7.

PhysX actor 생성 `FPhysXRuntime::CreateRigidBody` (`PhysXRuntime.cpp:1370`):
```cpp
PxRigidDynamic* Dynamic = Physics->createRigidDynamic(Pose);                  // :1388
if (Kinematic) Dynamic->setRigidBodyFlag(eKINEMATIC, true);                   // :1391
Dynamic->setActorFlag(eDISABLE_GRAVITY, !Desc.bUseGravity);                   // :1395
Dynamic->setRigidBodyFlag(eENABLE_CCD, Desc.bEnableCCD);                      // :1396
Dynamic->setLinearDamping(Desc.LinearDamping);  Dynamic->setAngularDamping(Desc.AngularDamping);   // :1397,:1398
Dynamic->setMaxDepenetrationVelocity(1.0f);   // :1403 ★ 신규(커밋 ef46f3ed) — 진입 폭발 방지 클램프
for (shape) CreateShape_AssumesLocked(Body, ShapeDesc);                       // :1425~1428
PxRigidBodyExt::updateMassAndInertia(*Dynamic, Desc.Mass);   // :1432 ★ 2.5 참조
```
- ★ `setMaxDepenetrationVelocity(1.0f)`(`:1403`)는 진입 시 캡슐 겹침을 1 m/s로 분리속도 클램프해 폭발을 막는다([09](09_ragdoll_session_fixes.md) 1장).

### 2.5 ⚠ 질량·재질의 실제 의미 (코드로 확인된 동작)
**(a) "Mass"는 사실상 density로 소비된다.**
`PxRigidBodyExt::updateMassAndInertia(PxRigidBody&, PxReal density, ...)`의 스칼라 오버로드는 2번째 인자를 **밀도(density)**로 해석한다(질량 직접 지정은 `setMassAndUpdateInertia`). 이 엔진은 두 곳에서 모두 `updateMassAndInertia`(density 오버로드)를 호출한다:
- `CreateShape_AssumesLocked` (`PhysXRuntime.cpp:1644`): `updateMassAndInertia(*Dynamic, Desc.Material.Density /*=1.0*/)`
- `CreateRigidBody` 말미 (`PhysXRuntime.cpp:1432`): `updateMassAndInertia(*Dynamic, Desc.Mass)` ← **셰이프 루프 뒤에 호출되어 최종 권위**

→ 실효 질량 = `Desc.Mass(=density, 기본 1.0) × 형상 부피`. 즉 큰 본일수록 자동으로 무거워진다. **`Mass`라는 이름과 실제 의미(density)가 어긋난다 — 잠재적 네이밍/의미 버그**(추후 수정 대상 후보).

**(b) BodySetup의 Friction/Restitution은 래그돌 셰이프에 적용되지 않는다.**
`ApplyBodyMaterial`(`SkeletalMeshComponent.cpp:74`)이 각 셰이프 `ShapeDesc.Material`에 BodySetup의 Friction/Restitution을 채우지만, 실제 셰이프 생성은 `createExclusiveShape(*Actor, *GeometryPtr, *DefaultMaterial)`로 **고정 `DefaultMaterial`만** 쓴다(`PhysXRuntime.cpp:1599`). `DefaultMaterial = Physics->createMaterial(0.5f, 0.5f, 0.3f)` (`:983`) → 모든 래그돌 셰이프는 마찰 0.5/0.5, 반발 0.3. (BodySetup Friction 기본 0.7은 미반영.) `Material`에서 실제로 소비되는 값은 `Density`(질량용)뿐. (구조 변경: `Material`은 이제 `FPhysicsBodyDesc`가 아니라 셰이프별 `FPhysicsShapeDesc.Material`에 있다 — `PhysicsTypes.h:125`.)

> 위 (a)(b)와 "gravity/CCD 강제 true"는 [07](07_glossary_and_gaps.md)의 관찰/이슈 목록에도 정리한다.

### 2.6 등장 객체 매핑
| 엔진 | PhysX | 비고 |
|---|---|---|
| `UBodySetup` | (자산) | 본 단위 설계도 |
| `FKAggregateGeom` | (없음) | 형상 컨테이너 |
| `FKSphere/Box/CapsuleElem` | `PxSphere/Box/CapsuleGeometry` | `BuildGeometry` (`PhysXHelpers.h:68`) |
| `FBodyInstance` | `PxRigidDynamic`(또는 `PxRigidStatic`) | `ActorHandle.NativePtr` |
| `FPhysicsShapeHandle` | `PxShape` | exclusive shape |
| `DefaultMaterial` | `PxMaterial` | 0.5/0.5/0.3 고정 |
