# 01. 강체와 바디 생성

> 라인 번호는 확인 시점(2026-06-01) 스냅샷. 심볼명으로 재확인할 것. 좌표 규약은 [06](06_coordinate_math.md) 참조.

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
`Source/Engine/Physics/Asset/BodySetup.h`:
```cpp
FString         BoneName;
FKAggregateGeom AggregateGeom;          // 이 본의 모든 콜리전 형상
float Mass = 1.0f;                       // ⚠ 2.5 참조 — 런타임에서 density로 소비됨
float LinearDamping = 0.01f, AngularDamping = 0.05f;
float Friction = 0.7f, Restitution = 0.3f;   // ⚠ 2.5 참조 — 래그돌 셰이프에는 미적용
bool  bSimulatePhysics = true;
bool  bEnableGravity = true;             // ⚠ 런타임에서 무시(강제 true)
EBodyPhysicsType  PhysicsType;           // Default(래그돌) / Kinematic
```
형상 요소 (`Source/Engine/Physics/Asset/PhysicsGeometry.h`):
- `FKSphereElem{ Center, Radius }`
- `FKBoxElem{ Center, Rotation, HalfX, HalfY, HalfZ }`
- `FKCapsuleElem{ Center, Rotation, Radius, HalfHeight }`
- `FKAggregateGeom{ TArray<...> SphereElems / BoxElems / CapsuleElems }` — 한 본에 여러 형상 가능.

### 2.2 형상 크기·배치 산정 (자동 생성)
`FMeshEditorWidget::GeneratePhysicsBodies()` (`Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:1143`).
> 참고: 같은 파일 `:1050`에 "GeneratePhysicsBodies는 아직 stub" 주석이 남아 있으나 **현재는 전체 구현이 존재**한다(stale 주석 — 규칙2의 실증).

크기 척도 = **본별 스키닝 정점 AABB의 최대 변** (`:1180~1223`):
```cpp
if (V.BoneWeights[k] < kAutoGenSkinWeightMin /*0.2*/) continue;   // 충분히 스키닝된 정점만
// 본별 min/max 누적 → MaxEdge(); 정점 없으면 자식거리 median 폴백; 둘 다 없으면 0
```
형상은 **가장 먼 자식 방향**으로 눕힌다 (`:1260~1293`):
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

### 2.3 본 → 바디 좌표 보정 (래그돌 경로엔 추가 보정 없음)
래그돌 셰이프는 자동생성 단계에서 이미 X 장축으로 정렬됐으므로, 런타임 변환은 `Center/Rotation`을 **그대로** 넘긴다. `AppendPhysicsShapes` (`SkeletalMeshComponent.cpp:77`):
```cpp
// Capsule
ShapeDesc.LocalTransform = MakeUnitScaleTransform(Capsule.Center, Capsule.Rotation);
ShapeDesc.Radius = Capsule.Radius;
// PhysX PxCapsuleGeometry expects the half distance between the two sphere centers.
ShapeDesc.HalfHeight = std::max(0.0f, Capsule.HalfHeight - Capsule.Radius);   // ★ -Radius 보정
```
- **캡슐 HalfHeight 의미 차이**: 엔진 자산의 `HalfHeight`는 center→tip(반쪽 전체 길이)이지만, PhysX `PxCapsuleGeometry`는 **두 반구 중심 사이 거리의 절반**(원기둥부 반길이)을 원한다 → `PhysXHalfHeight = max(0, EngineHalfHeight − Radius)`.
- `Source/Engine/Physics/PhysXHelpers.h:78`: `OutGeometry = PxCapsuleGeometry(Desc.Radius, Desc.HalfHeight);` → 장축 +X.
- Box/Sphere는 보정 없음: `PxBoxGeometry(HalfExtent.X/Y/Z)`, `PxSphereGeometry(Radius)`.
- (대조: 래그돌이 아닌 `UCapsuleComponent`는 Z→X 보정이 필요하다 — [06 부록 A](06_coordinate_math.md) 참조.)

### 2.4 바디 인스턴스화 (자산 → PhysX actor)
`USkeletalMeshComponent::InstantiatePhysicsAssetBodies` (`SkeletalMeshComponent.cpp:724`):
```cpp
BodyDesc.BodyType = BodySetup->bSimulatePhysics ? EPhysicsBodyType::Dynamic : EPhysicsBodyType::Kinematic;
BodyDesc.WorldTransform = BoneWorldTransform;       // ★ 바디 월드 = 본 월드 (오프셋 0)
BodyDesc.Mass = std::max(0.001f, BodySetup->Mass);
BodyDesc.bUseGravity = true;  BodyDesc.bEnableCCD = true;   // ⚠ BodySetup 값 무시, 강제 true
BodyDesc.Aggregate = PhysicsAggregate;              // 자기충돌 제어용(05 문서)
AppendPhysicsShapes(*BodySetup, BodyDesc);
if (BodyDesc.Shapes.empty()) continue;              // geom 없으면 skip
FBodyInstance* Body = Scene.CreateRigidBody(BodyDesc);
```
- 바디의 월드 트랜스폼 = 본 월드 트랜스폼이라 **본↔바디 오프셋이 0** (write-back에서 별도 보정 불필요 — [05](05_runtime_and_collision.md)).

PhysX actor 생성 `FPhysXRuntime::CreateRigidBody` (`PhysXRuntime.cpp:704`):
```cpp
PxRigidDynamic* Dynamic = Physics->createRigidDynamic(Pose);
if (Kinematic) Dynamic->setRigidBodyFlag(eKINEMATIC, true);
Dynamic->setActorFlag(eDISABLE_GRAVITY, !Desc.bUseGravity);
Dynamic->setRigidBodyFlag(eENABLE_CCD, Desc.bEnableCCD);
Dynamic->setLinearDamping(Desc.LinearDamping);  Dynamic->setAngularDamping(Desc.AngularDamping);
for (shape) CreateShape_AssumesLocked(Body, ShapeDesc);
PxRigidBodyExt::updateMassAndInertia(*Dynamic, Desc.Mass);   // ★ 2.5 참조
```

### 2.5 ⚠ 질량·재질의 실제 의미 (코드로 확인된 동작)
**(a) "Mass"는 사실상 density로 소비된다.**
`PxRigidBodyExt::updateMassAndInertia(PxRigidBody&, PxReal density, ...)`의 스칼라 오버로드는 2번째 인자를 **밀도(density)**로 해석한다(질량 직접 지정은 `setMassAndUpdateInertia`). 이 엔진은 두 곳에서 모두 `updateMassAndInertia`(density 오버로드)를 호출한다:
- `CreateShape_AssumesLocked` (`PhysXRuntime.cpp:898`): `updateMassAndInertia(*Dynamic, Desc.Material.Density /*=1.0*/)`
- `CreateRigidBody` 말미 (`PhysXRuntime.cpp:761`): `updateMassAndInertia(*Dynamic, Desc.Mass)` ← **셰이프 루프 뒤에 호출되어 최종 권위**

→ 실효 질량 = `Desc.Mass(=density, 기본 1.0) × 형상 부피`. 즉 큰 본일수록 자동으로 무거워진다. **`Mass`라는 이름과 실제 의미(density)가 어긋난다 — 잠재적 네이밍/의미 버그**(추후 수정 대상 후보).

**(b) BodySetup의 Friction/Restitution은 래그돌 셰이프에 적용되지 않는다.**
`ApplyBodyMaterial`(`SkeletalMeshComponent.cpp:69`)이 `Desc.Material`에 BodySetup의 Friction/Restitution을 채우지만, 실제 셰이프 생성은 `createExclusiveShape(*Actor, geom, *DefaultMaterial)`로 **고정 `DefaultMaterial`만** 쓴다(`PhysXRuntime.cpp:880`). `DefaultMaterial = Physics->createMaterial(0.5f, 0.5f, 0.3f)` (`:374`) → 모든 래그돌 셰이프는 마찰 0.5/0.5, 반발 0.3. (BodySetup Friction 기본 0.7은 미반영.) `Desc.Material`에서 실제로 소비되는 값은 `Density`(질량용)뿐.

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
