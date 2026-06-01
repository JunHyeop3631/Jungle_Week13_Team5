# DIAGNOSE: NvCloth Pin/Constraint 처리 인프라 점검

> 진단 전용. 코드 수정 없음. 미확인 항목은 `[미확인]` 표기.
> 경로 표기는 `KraftonEngine/Source/...` 상대. 라인은 점검 시점 스냅샷.

**핵심 결론(1줄):** pin 인프라(wrapper + skeletal bridge + cooking)는 구현 완료 상태이나, `FNvClothScene` 인스턴스화·`CreateGridCloth/CreateClothInstance`·`BindSkeletalCloth` 의 **프로덕션 호출부가 코드에 존재하지 않아** 전체 pin 경로가 dormant(미연결)이다. pin 데이터의 asset/DCC/페인팅 원천도 없음(절차적 grid의 `bPinTopRow` 하드코딩만 존재).

---

## A. 코드 탐색

- NvCloth wrapper/통합 레이어 위치
  - `[확인: Engine/Physics/Cloth/IClothScene.h:6]` 추상 인터페이스 `IClothScene`
  - `[확인: Engine/Physics/Cloth/NvClothScene.h:21]` 구현 `FNvClothScene : public IClothScene`
  - `[확인: Engine/Physics/Cloth/NvClothScene.cpp:1]` `nv::cloth` 직접 사용 유일 파일
  - `[확인: Engine/Physics/Cloth/ClothTypes.h]` 엔진측 desc/handle/stats 타입
  - `[확인: Engine/Component/Primitive/SkeletalMeshComponent.cpp:23]` skeletal attach bridge (유일 소비자)
- `setMotionConstraints` / `setSeparationConstraints` / `setVirtualParticles` 호출부
  - motion: `getMotionConstraints()` range write `[확인: NvClothScene.cpp:1280-1289]`, `clearMotionConstraints()` `[확인: NvClothScene.cpp:1276,1297]`
  - separation: `getSeparationConstraints()` range write `[확인: NvClothScene.cpp:1301-1310]`, `clearSeparationConstraints()` `[확인: NvClothScene.cpp:1297]`
  - `setVirtualParticles` / `getNumVirtualParticles` `[확인: 엔진 호출 없음 — ThirdParty Cloth.h:334-335 에만 선언]`
- particle `invMass` 직접 설정(=0 고정)
  - fabric cook 입력 `[확인: NvClothScene.cpp:633-642]` (`ClothMeshDesc.invMasses`)
  - grid 자동 pin `[확인: NvClothScene.cpp:310-318]` (`InvMass = bPinned ? 0.0f : 1.0f`)
  - instance 초기 particle `[확인: NvClothScene.cpp:65-68,729]` (`ToPxParticle`, w=InvMass)
  - 런타임 pin `[확인: NvClothScene.cpp:885,894]` (w=0.0f 직접 기록)
- pin 관련 식별자 grep
  - `pin`: `FClothPinnedParticle` `[확인: ClothTypes.h:115]`, `bPinTopRow` `[확인: ClothTypes.h:165]`, `bPinned` `[확인: SkeletalMeshComponent.h:28]`
  - `constraint`: `FClothConstraintDesc` `[확인: ClothTypes.h:87]`
  - `MotionConstraint`: `FClothMotionConstraint` `[확인: ClothTypes.h:75]`
  - `maxDistance`: `MaxDistance` `[확인: SkeletalMeshComponent.h:27]` → motion constraint radius `[확인: SkeletalMeshComponent.cpp:519]`
  - `anchor`: `[확인: 엔진 미사용 — ThirdParty ClothFabricCooker.h:132 tetherAnchors 내부 cooking 전용]`
  - `fixed`: `[미확인]` (cloth 경로에 식별자 없음)

### A. 발견 위치 표

| 파일:라인 | 함수 | 역할 | 빈도 |
|---|---|---|---|
| NvClothScene.cpp:605-671 | `CreateClothFabric` | invMass 포함 mesh→fabric cook (`NvClothCookFabricFromMesh`) | 1회(생성) |
| NvClothScene.cpp:712-763 | `CreateClothInstance` | invMass(w) 포함 particle로 `createCloth` | 1회(생성) |
| NvClothScene.cpp:280-343 | `BuildGridDescriptions` | grid 정점/인덱스/`bPinTopRow` pin 생성 | 1회(생성) |
| NvClothScene.cpp:853-899 | `SetPinnedParticlePositions` | pin 정점 위치(w=0) 갱신 | per-frame |
| NvClothScene.cpp:1256-1315 | `ApplyClothConstraints` | motion/separation constraint write + scale/bias/stiffness | per-frame |
| NvClothScene.cpp:1317-1356 | `ApplyClothCollision` | sphere/capsule collision 갱신 | per-frame |
| NvClothScene.cpp:978-1003 | `SimulateCloth` | 동기 begin/simulateChunk/end | per-frame |
| SkeletalMeshComponent.cpp:457-524 | `BuildSkeletalClothPinnedParticles` | 본→cloth-local pin/motion 좌표 산출 | per-frame |
| SkeletalMeshComponent.cpp:621-662 | `TickSkeletalCloth` | pin/constraint/collision set→simulate→캐시 | per-frame |
| SkeletalMeshComponent.cpp:409-444 | `BindSkeletalCloth` | attachment 검증·바인딩 | **호출부 없음** |

---

## B. Pin 데이터 생성 (Source)

- pin 정보의 원천 (DCC export / 에디터 페인팅 / 자동 생성)
  - DCC export: `[확인: 경로 없음]` (FBX 등 import에 cloth/pin 채널 없음)
  - 에디터 페인팅: `[확인: 없음 — MeshEditorWidget.Physics.cpp 에 cloth/pin/invMass 참조 0건]`
  - 자동 생성: `[확인: NvClothScene.cpp:310]` 절차적 grid의 `bPinTopRow && Row==0` 하드코딩만 존재
- vertex별 pin 값 표현
  - `[확인]` 두 표현 병존: (1) `InvMass==0` 하드 pin (cook/init/runtime), (2) per-particle motion constraint sphere(`Center`+`Radius`) `[확인: ClothTypes.h:75-79]`
  - bool mask: `FSkeletalClothParticleAttachment.bPinned`/`bMotionConstrained` `[확인: SkeletalMeshComponent.h:28-29]`
- pin = particle 단위 / virtual particle 포함
  - `[확인]` particle 단위만. virtual particle 미사용(A 참조)
- pin 데이터가 저장되는 asset 구조체/필드명
  - `[확인: asset 저장 없음]` — pin은 런타임 desc에만 존재: `FClothGridDesc.bPinTopRow`(ClothTypes.h:165), `FSkeletalClothBindingDesc.Attachments[]`(SkeletalMeshComponent.h:34). 둘 다 `UPROPERTY` 아님 → 직렬화 비대상
- LOD별 pin 데이터 보유
  - `[확인: 없음]` (cloth asset/LOD 개념 자체 부재)

---

## C. Import / Build 파이프라인

- asset → Fabric/Cloth 생성 시점 (build/load/첫 tick)
  - `[확인]` build/load/첫-tick 자동 경로 **없음**. 명시적 API 호출 시에만 생성: fabric=`NvClothCookFabricFromMesh` `[NvClothScene.cpp:648]`, cloth=`Factory->createCloth` `[NvClothScene.cpp:732]`
- particle index ↔ mesh vertex index 매핑 (welded/unwelded, 중복 정점)
  - `[확인]` grid는 row-major 1:1 `[NvClothScene.cpp:327-329]`
  - `[확인]` 일반 경로는 `FClothFabricDesc.Particles` 입력 순서 = particle index, render도 동일 index 재사용 `[NvClothScene.cpp:1043-1051]`
  - `[확인: 엔진측 weld/dedup 코드 없음]`. cooker 내부 welding 여부는 `[미확인: ThirdParty 내부]`
- pin 데이터가 `ClothMeshDesc`/fabric cooking에 들어가는 경로
  - `[확인: NvClothScene.cpp:640-642]` `MeshDesc.invMasses` 로 전달
  - `[확인: ThirdParty ClothMeshDesc.h:101-108]` invMass>0=시뮬, 0=static → tether·zero-stretch constraint 생성 근거
- cooking 실패/경고 로그 처리
  - `[확인: NvClothScene.cpp:655-658]` 실패 시 `{}` 반환만, **로그 없음**
  - `[확인: NvClothScene.cpp:41-43]` `reportError` 빈 구현(전부 침묵)
  - `[확인: NvClothScene.cpp:49-52]` assert `Ignore=true`(전부 무시)

---

## D. 런타임 적용 (Skeletal attach 핵심)

- 매 프레임 본 transform → motion/pin 위치 갱신 흐름
  - `[확인: SkeletalMeshComponent.cpp:621-646]` `TickSkeletalCloth`→`BuildSkeletalClothPinnedParticles`→`SetPinnedParticlePositions`/`SetClothConstraints`
  - 본 fetch: `GetBoneWorldTransformByIndex` `[확인: SkeletalMeshComponent.cpp:496]`
- constraint 위치 계산 공간 (월드/로컬/컴포넌트)
  - `[확인]` **cloth-local**. `ClothWorldInv = (ClothLocalTransform·WorldMatrix)^-1` `[SkeletalMeshComponent.cpp:404-406,487,502,549]`
- 본 행렬 fetch 타이밍 (애니메이션 평가 후 / race)
  - `[확인: SkeletalMeshComponent.cpp:994-996]` `EvaluateAnimInstance` 직후 동일 tick 동기 호출 → 애니메이션 평가 **이후**
  - `[확인]` ragdoll 경로도 `ApplyPhysicsToBones` 직후 호출 `[SkeletalMeshComponent.cpp:988-989]`. 단일 스레드 동기 → race 없음
- `setMotionConstraintScaleBias` / maxDistance 동적 변경
  - scale/bias: 매 tick `1.0/0.0` 고정 재설정 `[SkeletalMeshComponent.cpp:482-483 → NvClothScene.cpp:1292]`
  - radius(maxDistance): 매 tick `Attachment.MaxDistance`로 기록 `[SkeletalMeshComponent.cpp:519]`. 단 attachment 값 자체를 런타임 변경하는 코드는 `[확인: 없음]`(정적)
- simulate ↔ skinning ↔ render 순서
  - `[확인]` ① pin/constraint/collision set → ② `SimulateCloth`(동기) → ③ `Get*` 캐시 `[SkeletalMeshComponent.cpp:636-660]` → ④ mesh skinning `UMeshComponent::TickComponent` `[:990,997]` → ⑤ render는 캐시 소비 `[RenderCollector.cpp:222-247]`
- teleport/순간이동 시 constraint reset (`clearMotionConstraints`/inertia)
  - `[확인]` solver `teleport`/`setTranslation`/`setRotation`/`clearInertia` **미사용**(엔진 grep 0건; ThirdParty Cloth.h:152-190 에만 선언)
  - `[확인]` 대체: `bResetSkeletalClothPinsNextTick`(bind 시 true) → `SetPinnedParticlePositions(..., bResetPreviousParticles=true)` 로 **pin 정점의 이전위치만** 리셋 `[SkeletalMeshComponent.cpp:440,634-638; NvClothScene.cpp:875-888]`
  - `[확인]` 전체 cloth teleport/inertia reset은 **없음** → 후속 후보

---

## E. 좌표/스케일 일관성

- cloth solver 단위·좌표계 vs 엔진 좌표계 변환 지점
  - `[확인]` 단위 변환 없음(엔진 좌표·단위를 그대로 PxVec 패킹 `ToPxVec3` `[NvClothScene.cpp:60-63]`). gravity 기본 `-980` cm/s² 가정 `[ClothTypes.h:123]`
  - `[확인]` world↔cloth-local 변환만 존재(D 참조), handedness/up-axis 재매핑 없음
- 본 non-uniform scale 적용 시 pin 위치 처리
  - `[확인]` pin 타깃 좌표는 `TransformPoint(BoneWorld.ToMatrix(), LocalPosition)` 행렬곱이라 scale 반영됨 `[SkeletalMeshComponent.cpp:501]`
  - `[미확인]` cloth particle은 위치(xyz)만 가지므로 non-uniform scale에 따른 cloth mesh 자체 변형/보정 처리는 명시적 코드 없음
- up-axis / handedness 변환이 pin 좌표에도 적용되는지
  - `[확인: 별도 변환 없음]` cloth 경로 전 구간 동일 엔진 좌표계 사용

---

## F. 안정성/엣지

- pin 정점 0개 / 전부 pin 분기
  - `[확인]` pin 0개: `!Pins.empty()` 가드로 set 스킵 `[SkeletalMeshComponent.cpp:632]`; motion 비어있으면 `clearMotionConstraints` `[NvClothScene.cpp:1274]`
  - `[미확인]` 전부 pin(전 정점 invMass=0)일 때 solver 동작 분기 없음(특수 처리 부재)
- NaN/Inf 가드 (constraint 위치 검증)
  - `[확인: 없음]` pin/constraint 위치 NaN·Inf 검증 코드 부재. `NormalizeOrDefault`/`NormalizeOrFallback` 는 length≈0만 가드(NaN 아님) `[NvClothScene.cpp:90-100,137-145]` → 후속 후보
- 비동기 cloth면 본 데이터 double-buffering
  - `[확인]` `SimulateCloth` 동기 단일 호출(begin/simulateChunk/end 즉시) `[NvClothScene.cpp:989-998]` → 비동기 아님, double-buffering 불필요·부재

---

## 요약 표 1 — 현재 Pin 처리 흐름 (생성→import→런타임)

| 단계 | 흐름 | 근거 |
|---|---|---|
| 생성(Source) | pin 원천 = 절차적 grid `bPinTopRow` 하드코딩뿐. asset/DCC/페인팅 없음 | NvClothScene.cpp:310 / ClothTypes.h:165 |
| import/build | `invMass`(0=pin) → `NvClothCookFabricFromMesh` 가 tether·zero-stretch 생성. cooking 에러 전부 침묵 | NvClothScene.cpp:640-658 / ClothMeshDesc.h:101-108 |
| 런타임 | `TickSkeletalCloth` 가 본 world→cloth-local 로 pin(w=0)·motion constraint 매 tick 갱신 후 동기 `SimulateCloth`. teleport는 pin 이전위치 리셋만 | SkeletalMeshComponent.cpp:621-662 / NvClothScene.cpp:875-888 |
| (연결 상태) | `FNvClothScene` 인스턴스화·`CreateGridCloth/Instance`·`BindSkeletalCloth` 호출부 부재 → 경로 전체 dormant | grep: 정의 외 호출부 0건 |

## 요약 표 2 — 후속 implement 후보

| 컴포넌트 | 후속 후보(1줄) |
|---|---|
| (시스템 연결) | `FNvClothScene` 소유/생성 + `BindSkeletalCloth` 호출 진입점 부재 — cloth subsystem wiring 필요 |
| ClothTypes / Asset | pin/attachment 의 asset 직렬화 부재(`UPROPERTY` 아님) — DCC/에디터 pin 데이터 영속화 경로 필요 |
| MeshEditorWidget | cloth pin 페인팅/편집 UI 없음 — vertex별 pin·MaxDistance 저작 도구 후보 |
| NvClothScene::CreateClothFabric | cooking 실패·ErrorCallback·assert 전부 침묵 — 진단 로그 노출 필요 |
| NvClothScene (mapping) | mesh vertex↔particle weld/dedup 처리 없음 — 중복 정점 메시 대비 매핑 정책 필요 |
| NvClothScene (teleport) | solver `setTranslation`/`teleport`/`clearInertia` 미사용 — 컴포넌트 순간이동 시 폭발 방지 inertia reset 후보 |
| NvClothScene (가드) | pin/constraint 위치 NaN·Inf 가드 부재 — 입력 검증 후보 |
| BuildSkeletalClothPinnedParticles | non-uniform scale 시 cloth mesh 변형 보정 부재 — scale 일관성 처리 후보 |
