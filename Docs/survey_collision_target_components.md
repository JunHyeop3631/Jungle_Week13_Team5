# 조사 명세서 — 현재 collision 처리에 사용 가능한 Component / Asset

> 상태: **조사(survey) 전용. 코드 변경/구현 계획 없음.** 현재 코드 직접 검증 기준(브랜치 `feature/joint`).
> 배경: 전투 히트가 `UWorld::PhysicsOverlapSphere` → `IPhysicsScene::OverlapSphere`(PhysX) 로 전환됨. 이제 타격 대상이 맞으려면 **PhysX query shape(=`eSCENE_QUERY_SHAPE`)** 가 씬에 등록돼 있어야 한다. 본 문서는 "현재 어떤 component/asset 을 붙이면 액터가 PhysX collision/query 에 참여하는가" 를 **조사만** 한 결과다.

---

## 0. 한 줄 결론

현재 액터에 **PhysX query shape** 를 줄 수 있는 수단은 **3가지뿐**:
1. **Shape 컴포넌트** `UBoxComponent` / `USphereComponent` / `UCapsuleComponent` — 단, `CollisionEnabled` 를 `QueryOnly`/`QueryAndPhysics` 로 켜야 함(기본 `NoCollision`).
2. **PhysicsAsset 래그돌 바디**(`USkeletalMeshComponent::InstantiatePhysicsAssetBodies`) — 시뮬 시 생성되는 Sphere/Box/Capsule 바디는 기본적으로 query 가능.
3. (간접) 위 셰이프 컴포넌트를 mesh 액터에 **부착**.

➡️ **StaticMesh/SkeletalMesh 컴포넌트 단독은 collision shape 를 만들지 않는다**(아래 §3).

---

## 1. 컴포넌트가 PhysX query shape 가 되는 경로 (메커니즘)

```
UPrimitiveComponent::BeginPlay
  └ IsCollisionEnabled() (= CollisionEnabled != NoCollision) 면
     └ IPhysicsScene::RegisterComponent(this)
        └ FPhysXRuntime::BuildBodyDescFromComponent(Comp, Desc)   [PhysXRuntime.cpp:820]
           ├ Cast<UBoxComponent>      → ShapeType=Box
           ├ Cast<USphereComponent>   → ShapeType=Sphere
           ├ Cast<UCapsuleComponent>  → ShapeType=Capsule (+ X축 정렬 보정 [:846])
           └ 그 외 타입 → return false  (바디 생성 안 됨)   [:848]
           · ShapeDesc.bSceneQueryShape = Comp->IsQueryCollisionEnabled()
           · ShapeDesc.bSimulationShape = !trigger && IsPhysicsCollisionEnabled()
        └ CreateRigidBody → CreateShape_AssumesLocked
           └ Shape->setFlag(eSCENE_QUERY_SHAPE, Desc.bSceneQueryShape)   (overlap 후보가 되는 비트)
           └ ApplyComponentFilterData(word0=ObjectType, word1=Block, word2=Overlap)
```

**핵심 조건**: overlap 에 잡히려면 `eSCENE_QUERY_SHAPE` 필요 → `bSceneQueryShape = IsQueryCollisionEnabled()` → **`CollisionEnabled ∈ {QueryOnly, QueryAndPhysics}`** 여야 함.
- `ECollisionEnabled` [CollisionTypes.h:54]: `NoCollision`(0) / `QueryOnly`(1) / `PhysicsOnly`(2) / `QueryAndPhysics`(3).
- `IsQueryCollisionEnabled()` 는 QueryOnly·QueryAndPhysics 에서 true.

**기본값(주의)** [PrimitiveComponent.h:245-250]:
| 멤버 | 기본값 |
|---|---|
| `CollisionEnabled` | **`NoCollision`** ← 즉 기본 상태로는 query shape 없음 → 안 맞음 |
| `ObjectType` | **`WorldStatic`** |
| `ResponseContainer` | 전 채널 **Block** |

---

## 2. Shape 컴포넌트 (직접 collision 수단) — `Component/Shape/`

| 컴포넌트 | 지오메트리 | BuildBodyDesc 지원 | 기본 CollisionEnabled | 기본 ObjectType | query shape? |
|---|---|---|---|---|---|
| `UBoxComponent` | Box(half-extent) | ✅ [:828] | NoCollision | WorldStatic | **켜야** Y |
| `USphereComponent` | Sphere(radius) | ✅ [:833] | NoCollision | WorldStatic | **켜야** Y |
| `UCapsuleComponent` | Capsule(radius/halfheight) | ✅ [:838] (X축 보정 [:846]) | NoCollision | WorldStatic | **켜야** Y |

- 세 타입 모두 `UShapeComponent → UPrimitiveComponent` 파생.
- **사용법**: 액터에 부착 → `SetCollisionEnabled(QueryOnly 또는 QueryAndPhysics)` → (전투 마스크에 맞춰) `SetCollisionObjectType(...)`.
- `CapsuleComponent` 는 캐릭터 루트로도 쓰이지만(이동 §) 기본 `NoCollision` 이므로 그대로는 PhysX query shape 가 아님 — query 로 쓰려면 명시적 enable 필요.

---

## 3. Mesh 컴포넌트 — collision shape **없음**

- `UStaticMeshComponent`, `USkeletalMeshComponent`(애님 모드) 는 `BuildBodyDescFromComponent` 의 Box/Sphere/Capsule Cast 에 걸리지 않음 → **`else` 분기에서 false 반환** [PhysXRuntime.cpp:848] → PhysX 바디/shape 미생성.
- 결과: **메시만 있는 액터는 새 전투 overlap 으로 맞지 않는다.** (참고: 메시의 삼각형 단위 충돌은 PhysX 가 아니라 비-PhysX `MeshTriangleBVH` 경로로, 에디터 피킹 전용 — 게임플레이 overlap 과 무관.)
- 메시 액터를 맞히려면 §2 의 셰이프 컴포넌트를 함께 부착해야 함.

---

## 4. PhysicsAsset 래그돌 바디 (자동 query 가능)

- `USkeletalMeshComponent::InstantiatePhysicsAssetBodies` → `AppendPhysicsShapes` 가 `UBodySetup.AggregateGeom` 의 `FKSphereElem`/`FKBoxElem`/`FKCapsuleElem` 를 `FPhysicsShapeDesc` 로 변환 [SkeletalMeshComponent.cpp:75~].
- `FPhysicsShapeDesc` 기본값 `bSceneQueryShape = true` [PhysicsTypes.h:107] → **래그돌 바디는 별도 설정 없이 query 가능**(overlap 후보).
- 단 **인스턴스화 시점에만** 존재(시뮬/래그돌 진입 후). 평상시 애니메이션 상태에는 바디가 없다.
- ObjectType: AppendPhysicsShapes 가 명시 설정하지 않음 → 필터 데이터의 word0 이 컴포넌트 ObjectType/기본값을 따름(대체로 WorldStatic). 전투 마스크와의 정합은 §5 참고.
- 데이터 소스: `UPhysicsAsset`(`<Mesh>_Physics.uasset`) + `UBodySetup`(본별 셰이프) — 에디터 Physics 탭/자동생성(`GeneratePhysicsBodies`)으로 작성.

---

## 5. 전투 overlap 마스크와의 정합 (참고)

새 전투 overlap 마스크(현재 구현):
```
mask = ObjectTypeBit(Pawn) | ObjectTypeBit(WorldDynamic)   (+ bHitWorldStatic 이면 WorldStatic)
```
`ECollisionChannel` [CollisionTypes.h:17]: WorldStatic=0, WorldDynamic=1, **Pawn=2**, Projectile=3, Trigger=4.

| 대상 구성 | 기본 ObjectType | 마스크에 걸리나? |
|---|---|---|
| 셰이프 컴포넌트(기본) | WorldStatic | `bHitWorldStatic=true`(현 기본) 일 때만 |
| 셰이프 컴포넌트(적으로 의도) | **Pawn 또는 WorldDynamic 로 설정 필요** | Y |
| 래그돌 바디 | 대체로 WorldStatic | `bHitWorldStatic` 의존 |

➡️ "적 타격 대상" 으로 쓰려면 `SetCollisionObjectType(Pawn 또는 WorldDynamic)` 권장(WorldStatic 의존 회피). 동시에 `TargetActorTag`("HitTarget") 태그도 부여(전투 노티파이의 tag 필터).

---

## 6. 현재 가용 수단 요약 표

| Component / Asset | 셰이프 | 기본 CollisionEnabled | 기본 ObjectType | query shape 기본 | 비고 |
|---|---|---|---|---|---|
| `UBoxComponent` | Box | NoCollision | WorldStatic | ❌(켜야) | Shape/BoxComponent |
| `USphereComponent` | Sphere | NoCollision | WorldStatic | ❌(켜야) | Shape/SphereComponent |
| `UCapsuleComponent` | Capsule | NoCollision | WorldStatic | ❌(켜야) | Shape/CapsuleComponent, X축 보정 [:846] |
| `UStaticMeshComponent` | — | NoCollision | WorldStatic | ❌(불가) | BuildBodyDesc 미지원 |
| `USkeletalMeshComponent`(애님) | — | NoCollision | WorldStatic | ❌(불가) | BuildBodyDesc 미지원 |
| `USkeletalMeshComponent`(래그돌) + `UPhysicsAsset`/`UBodySetup` | Sphere/Box/Capsule | (인스턴스화) | (대체로 WorldStatic) | ✅(기본 true) | 시뮬 중에만 존재 |

---

## 7. 조사 중 확인된 갭/관찰 (참고용, 구현 제안 아님)

1. **메시 단독 비대응**: 가장 흔한 대상(스켈레탈 캐릭터)이 평상시엔 PhysX query shape 가 없음 → 별도 셰이프 컴포넌트 부착 또는 래그돌 인스턴스화가 전제.
2. **기본 ObjectType=WorldStatic**: 적 대상도 기본은 WorldStatic 이라, 전투 마스크가 Pawn/WorldDynamic 중심이면 명시 설정 필요. (현 `bHitWorldStatic=true` 기본값이 이를 임시로 커버.)
3. **셰이프 자동 추종 없음**: 셰이프 컴포넌트는 부착된 본/소켓을 자동으로 따라가지 않음(어태치 트랜스폼 기반). 손/무기 본 추종 타격 볼륨이 필요하면 부착 위치 수동 관리 필요.
4. **래그돌 바디는 시뮬 중에만**: 평시 애니메이션 캐릭터를 "PhysicsAsset 바디로 맞히기" 는 인스턴스화 상태가 아니면 불가.
5. **BodySetup.CollisionEnabled 미사용**: `UBodySetup` 의 CollisionEnabled 필드는 AppendPhysicsShapes 에서 참조되지 않음(셰이프는 기본 query=true 로 생성).

---

## 부록. 근거 파일

- `Component/Shape/{Box,Sphere,Capsule}Component.{h,cpp}` — 셰이프 지오메트리
- `Component/PrimitiveComponent.h:135-148, 245-250` — CollisionEnabled/ObjectType/Response API·기본값
- `Core/Types/CollisionTypes.h:17(ECollisionChannel), 54(ECollisionEnabled)`
- `Physics/PhysXRuntime.cpp:820(BuildBodyDescFromComponent), 846(캡슐 축), CreateShape_AssumesLocked(eSCENE_QUERY_SHAPE/ApplyComponentFilterData)`
- `Physics/PhysicsTypes.h:105-107` — FPhysicsShapeDesc 기본 플래그(bSceneQueryShape=true)
- `Component/Primitive/SkeletalMeshComponent.cpp:75~(AppendPhysicsShapes), InstantiatePhysicsAssetBodies`
- `Physics/Asset/{PhysicsAsset,BodySetup,PhysicsGeometry}.{h,cpp}` — 래그돌 데이터 모델
