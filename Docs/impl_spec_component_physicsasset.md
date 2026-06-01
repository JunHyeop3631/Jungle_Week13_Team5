# 구현 명세 — 컴포넌트 단위 PhysicsAsset 부착 (옵션 A · USkeletalMeshComponent 배치)

> 목적: 런타임/PIE/메인 레벨 월드에서 SkeletalMesh 액터가 PhysicsAsset(래그돌 등)을 사용할 수 있도록, **`USkeletalMeshComponent` 에 per-instance 로 PhysicsAsset 을 부착**하는 경로를 추가한다.
> 전제: 데이터 모델(`UPhysicsAsset`)·직렬화·인스턴스화(`InstantiatePhysicsAssetBodies`)·on/off(`SetSimulatingPhysics`)·런타임 진입은 이미 구현됨. **빠진 것은 "로드된 컴포넌트 → PhysicsAsset 연결"뿐**(조사 결과 G1~G4).
> 본 문서는 설계 명세이며 아직 코드는 수정하지 않았다.
>
> **개정 사유**: 초안은 base `USkinnedMeshComponent` 에 배치했으나, 모든 물리 상태(Bodies/Constraints/bSimulatingPhysics/InstantiatePhysicsAssetBodies/SetSimulatingPhysics)가 이미 `USkeletalMeshComponent` 에 있어 **물리 응집도·기존 자산(소멸자·PostEditProperty override) 재사용·교차클래스 훅 제거** 측면에서 파생 클래스 배치가 우수하다는 판단에 따라 재작성한다.

---

## 0. 핵심 설계 결정

1. **배치 = `USkeletalMeshComponent`(파생).**
   - 물리 상태 전부가 이미 이 클래스에 있음 → 응집도 ↑, base `USkinnedMeshComponent` 는 "skinning 전용"으로 유지.
   - 기존 자산 재사용: 소멸자(`~USkeletalMeshComponent` [.cpp:122](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:122))·`PostEditProperty` override([.cpp:1352](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:1352), 이미 `Super::PostEditProperty` 호출 + if/else 체인)를 그대로 활용.
   - **교차클래스 가상 훅 불필요**: 시뮬중 자산 교체 정리를 같은 클래스의 `PostEditProperty` 에서 직접 수행.
   - 신규로 추가할 것은 **`PostDuplicate()` override 하나뿐**(base 의 mesh 로드 `Super::PostDuplicate()` 후 physics asset 로드).

2. **per-instance 슬롯에 저장한다 (공유 메시 금지).**
   `FMeshManager` 가 `USkeletalMesh` 를 캐시·공유한다(`TMap<FString,USkeletalMesh*> SkeletalMeshCache` [MeshManager.cpp:30](KraftonEngine/Source/Engine/Mesh/MeshManager.cpp:30)). 공유 `SkeletalMesh->PhysicsAsset` 에 쓰면 같은 메시를 쓰는 다른 인스턴스를 오염시킨다 → physics asset 경로/포인터는 **컴포넌트가 소유**(UE 의 `PhysicsAssetOverride` 패턴).

3. **경로(직렬화) + 해석된 포인터(런타임) 분리.**
   - `FSoftObjectPtr PhysicsAssetPath` — `UPROPERTY(Edit, Save)`, scene 영속. `SkeletalMeshPath`([SkinnedMeshComponent.h:123](KraftonEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.h:123)) 와 동일 패턴.
   - `UPhysicsAsset* PhysicsAssetOverride` — 경로에서 로드한 런타임 포인터. **컴포넌트가 소유(직접 new/delete)**, 직렬화 안 함.

4. **해석 우선순위**: `GetPhysicsAsset()` = `PhysicsAssetOverride ? PhysicsAssetOverride : (GetSkeletalMesh() ? GetSkeletalMesh()->PhysicsAsset : nullptr)`.
   → override 우선, 없으면 **에디터 세션이 메시에 꽂은 것**으로 폴백(에디터 프리뷰 호환).

---

## 1. 변경 파일 목록

| # | 파일 | 변경 |
|---|---|---|
| 1 | `Engine/Physics/Asset/PhysicsAsset.h/.cpp` | 런타임 로더 `static UPhysicsAsset* LoadFromFile(const FString&)` 추가 |
| 2 | `Engine/Component/Primitive/SkeletalMeshComponent.h` | `PhysicsAssetPath`(UPROPERTY)·`PhysicsAssetOverride` 멤버, `GetPhysicsAsset()`·`LoadPhysicsAssetFromPath()`·`PostDuplicate()` 선언 |
| 3 | `Engine/Component/Primitive/SkeletalMeshComponent.cpp` | 로더 헬퍼·`GetPhysicsAsset`·`PostDuplicate` 구현, `PostEditProperty` 분기, 소멸자 정리, 소비자 3곳 교체 |
| 4 | (선택) `Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp` | 에디터 `LoadPhysicsAssetFromFile` 를 `UPhysicsAsset::LoadFromFile` 로 위임(중복 제거) |
| 5 | (선택) `Engine/Lua/LuaScriptManager.cpp` | `SetPhysicsAssetPath` Lua 노출(런타임 자산 교체) |

> **base `USkinnedMeshComponent` 는 수정하지 않는다.** (초안 대비 핵심 차이)

---

## 2. 상세 변경

### 2.1 런타임 로더 — `UPhysicsAsset::LoadFromFile`  (파일 #1)

에디터 전용 anon-namespace 로더([MeshEditorWidget.Physics.cpp:76-93](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp:76))와 동일 로직을 **런타임 호출 가능한 static 메서드**로 신설. 원시도구(`FWindowsBinReader`/`FAssetPackageHeader`/`FAssetImportMetadata`/`FPaths`)는 런타임 모듈(`MeshManager` 등)에서 이미 사용 중.

```cpp
// PhysicsAsset.h  (public)
//   <Mesh>_Physics.uasset 같은 PhysicsAsset 패키지 로드. 실패 시 nullptr. 호출측이 소유/해제.
static UPhysicsAsset* LoadFromFile(const FString& Path);
```

```cpp
// PhysicsAsset.cpp  (상단 include 추가)
#include "Serialization/WindowsArchive.h"   // FWindowsBinReader
#include "Asset/AssetPackage.h"             // FAssetPackageHeader, EAssetPackageType, FAssetImportMetadata
#include "Platform/Paths.h"                 // FPaths::MakeProjectRelative

UPhysicsAsset* UPhysicsAsset::LoadFromFile(const FString& Path)
{
    const FString Normalized = FPaths::MakeProjectRelative(Path);
    FWindowsBinReader Reader(Normalized);
    if (!Reader.IsValid()) return nullptr;

    FAssetPackageHeader Header;
    Reader << Header;
    if (!Header.IsValid(EAssetPackageType::PhysicsAsset)) return nullptr;

    FAssetImportMetadata Metadata;   // PhysicsAsset 은 별도 소스 없음 — 읽고 버림
    Reader << Metadata;

    UPhysicsAsset* Asset = new UPhysicsAsset();
    Asset->Serialize(Reader);
    return Asset;
}
```

### 2.2 멤버/선언 — `USkeletalMeshComponent`  (파일 #2)

`UPhysicsAsset` 전방선언은 이미 있음([SkeletalMeshComponent.h:18](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h:18)). `FSoftObjectPtr` 는 base 헤더 include 로 사용 가능.

**public — "Editor / 직렬화 통합" 섹션([.h:101-104](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h:101))에 추가:**
```cpp
void PostDuplicate() override;   // 메시 로드(Super) 후 physics asset 해석

// per-instance override 우선, 없으면 메시(에디터 세션이 꽂은 것)로 폴백.
UPhysicsAsset* GetPhysicsAsset() const;
```

**protected 멤버 — Physics 카테고리(`bShowPhysicsBodies` [.h:168](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h:168)) 근처:**
```cpp
UPROPERTY(Edit, Save, Category="Physics", DisplayName="Physics Asset", AssetType="PhysicsAsset")
FSoftObjectPtr PhysicsAssetPath = "None";
```

**protected 멤버 — 런타임 상태(`bSimulatingPhysics` [.h:185](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h:185)) 근처:**
```cpp
// 경로에서 로드한 per-instance physics asset. 컴포넌트가 소유(직접 delete). 직렬화 안 함.
UPhysicsAsset* PhysicsAssetOverride = nullptr;
```

**private 헬퍼 — `EnterRagdollState()` 옆:**
```cpp
void LoadPhysicsAssetFromPath();   // PhysicsAssetPath → PhysicsAssetOverride 재로드(기존 것 해제)
```

### 2.3 구현 — `USkeletalMeshComponent`  (파일 #3)

`.cpp` 는 이미 `Physics/Asset/PhysicsAsset.h` 를 include 함([.cpp:21](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:21)) → 추가 include 불필요.

**(a) 해석자 + 로더 헬퍼 (신규 함수):**
```cpp
UPhysicsAsset* USkeletalMeshComponent::GetPhysicsAsset() const
{
    if (PhysicsAssetOverride) return PhysicsAssetOverride;
    USkeletalMesh* Mesh = GetSkeletalMesh();
    return Mesh ? Mesh->PhysicsAsset : nullptr;
}

void USkeletalMeshComponent::LoadPhysicsAssetFromPath()
{
    if (PhysicsAssetOverride) { delete PhysicsAssetOverride; PhysicsAssetOverride = nullptr; }

    if (!PhysicsAssetPath.empty() && PhysicsAssetPath != "None")
    {
        PhysicsAssetOverride = UPhysicsAsset::LoadFromFile(PhysicsAssetPath);
        if (!PhysicsAssetOverride)
            UE_LOG("PhysicsAsset load failed. Path=%s", PhysicsAssetPath.ToString().c_str());
    }
}
```

**(b) `PostDuplicate` override (신규) — scene 로드 시 base 가 메시 로드 → 이어서 physics asset 로드:**
```cpp
void USkeletalMeshComponent::PostDuplicate()
{
    Super::PostDuplicate();        // USkinnedMeshComponent: SkeletalMeshPath → SetSkeletalMesh
    LoadPhysicsAssetFromPath();    // 이어서 PhysicsAssetPath 해석
}
```

**(c) `PostEditProperty` 분기 추가 — 기존 else-if 체인([.cpp:1357~](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:1357)) 끝에:**
```cpp
    else if (std::strcmp(PropertyName, "PhysicsAssetPath") == 0 ||
             std::strcmp(PropertyName, "Physics Asset") == 0)
    {
        // 시뮬 중 자산 교체 → 안전 정리 후 재로드. (같은 클래스라 직접 호출, 가상 훅 불필요)
        if (bSimulatingPhysics) SetSimulatingPhysics(false);
        DestroyPhysicsAssetBodies();
        LoadPhysicsAssetFromPath();
    }
```
> `Super::PostEditProperty(PropertyName)` 는 함수 진입부([.cpp:1354](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:1354))에서 이미 호출되어 base 의 `SkeletalMeshPath`/material 처리는 그대로 동작한다.

**(d) 소멸자 정리 — `~USkeletalMeshComponent` ([.cpp:122-127](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:122)):**
```cpp
USkeletalMeshComponent::~USkeletalMeshComponent()
{
    ClearSkeletalClothBinding();
    DestroyPhysicsAssetBodies();              // PhysX 바디 먼저 파기(자산 포인터 미보존 → 순서 안전)
    if (PhysicsAssetOverride) { delete PhysicsAssetOverride; PhysicsAssetOverride = nullptr; }
    ClearAnimInstance();
}
```

### 2.4 소비자 교체 — `USkeletalMeshComponent`  (파일 #3 계속)

세 곳을 `Mesh->PhysicsAsset` → `GetPhysicsAsset()` 로:

**(a) `InstantiatePhysicsAssetBodies(IPhysicsScene&)` 무인자** ([.cpp:718-722](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:718)):
```cpp
bool USkeletalMeshComponent::InstantiatePhysicsAssetBodies(IPhysicsScene& Scene)
{
    return InstantiatePhysicsAssetBodies(Scene, GetPhysicsAsset());
}
```

**(b) `HasPhysicsAsset()`** ([.cpp:1091-1095](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:1091)):
```cpp
bool USkeletalMeshComponent::HasPhysicsAsset() const
{
    return GetPhysicsAsset() != nullptr;
}
```

**(c) `SetSimulatingPhysics(true)` 의 instantiate-on-demand** ([.cpp:1050-1066](KraftonEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp:1050)): 지역 변수만 교체.
```cpp
        UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();   // was: Mesh ? Mesh->PhysicsAsset : nullptr
        if (!Scene || !PhysicsAsset) { UE_LOG("...: no physics scene or physics asset."); return; }
        if (!InstantiatePhysicsAssetBodies(*Scene, PhysicsAsset)) { ...; return; }
```

---

## 3. 소유권 / 수명

- `PhysicsAssetOverride` 는 **컴포넌트 단독 소유**: `LoadPhysicsAssetFromPath` 교체 시 기존 것 `delete`, 소멸자에서 `delete`. (엔진 GC 없음 — 에디터 로더와 동일한 수동 관리.)
- 소멸 순서: `~USkeletalMeshComponent` 안에서 `DestroyPhysicsAssetBodies()`(PhysX 바디 파기) → `delete PhysicsAssetOverride`. 바디는 생성 시 셰이프 데이터를 PhysX 로 복사하므로 자산 포인터를 보존하지 않음 → 순서 안전.
- **공유 메시 불변**: 컴포넌트는 `SkeletalMesh->PhysicsAsset` 에 쓰지 않는다(read-only 폴백). 쓰기는 에디터 Physics 탭만.

---

## 4. 직렬화 / Scene 영속

- `PhysicsAssetPath` 는 `UPROPERTY(..., Save, ...)` → scene 직렬화기(`SceneSaveManager::SerializeProperties`)가 `SkeletalMeshPath` 와 동일하게 자동 저장/복원. **별도 Serialize 코드 불필요.** (단, 리플렉션 codegen 이 `SkeletalMeshComponent.generated.*` 를 재생성하므로 빌드 시 헤더 변경 반영 확인.)
- `PhysicsAssetOverride` 는 UPROPERTY 아님 → 저장 안 됨(경로에서 매번 재해석). 의도된 설계.

---

## 5. 엣지케이스 / 리스크

1. **시뮬 중 자산 교체**: `PostEditProperty` 의 `PhysicsAssetPath` 분기에서 `SetSimulatingPhysics(false)` + `DestroyPhysicsAssetBodies()` 후 재로드 → 같은 클래스 직접 호출(초안의 `OnPhysicsAssetChanged` 가상 훅 **불필요**).
2. **에디터 프리뷰 호환**: Physics 탭은 여전히 `SkeletalMesh->PhysicsAsset` 를 채운다. 컴포넌트 `PhysicsAssetPath` 가 "None" 이면 `GetPhysicsAsset()` 이 그 폴백 반환 → 프리뷰 무변경.
3. **AssetType 픽커 지원**: `AssetType="PhysicsAsset"` 가 디테일 패널 픽커에서 `_Physics.uasset` 를 목록화하는지 확인 필요(`EAssetPackageType::PhysicsAsset` [AssetPackage.h:19](KraftonEngine/Source/Engine/Asset/AssetPackage.h:19)). 미지원이어도 경로 직접 입력/Lua 설정으로 동작 → 검증 항목.
4. **`PostDuplicate` 다형 디스패치**: scene 로더가 컴포넌트의 most-derived `PostDuplicate` 를 호출하므로 `USkeletalMeshComponent::PostDuplicate` 가 불린다(→ Super 가 메시 로드, 이어서 physics 로드). 확인 항목.
5. **경로/실패**: `FPaths::MakeProjectRelative` 정규화. 로드 실패 시 `GetPhysicsAsset()` 은 메시 폴백/nullptr → `HasPhysicsAsset()` false, 진입 거부(현 동작 유지), 로그만 남김.

---

## 6. (선택) 편의 / 후속

- **규약 자동 기본값**: `SetSkeletalMesh`(or `PostDuplicate`)에서 `PhysicsAssetPath` 가 "None" 이고 `<Mesh>_Physics.uasset` 가 존재하면 자동 채움(옵션 B 편의를 A 위에). 기본 off, 명시 설정 우선.
- **에디터 중복 제거**: 에디터 `LoadPhysicsAssetFromFile`(anon ns) → `UPhysicsAsset::LoadFromFile` 위임.
- **Lua**: `SkeletalMeshComponent:SetPhysicsAssetPath(path)`(→ `PhysicsAssetPath` 설정 + `LoadPhysicsAssetFromPath`) 노출 시 게임플레이가 런타임에 자산 교체 가능. 기존 `HasPhysicsAsset`/`SetSimulatingPhysics` 와 결합.

---

## 7. 검증 방법

1. **빌드**: `Debug|x64`(에디터 포함). 헤더 변경 → `SkeletalMeshComponent.generated.*` 재생성 확인.
2. **Scene 영속**: SkeletalMesh 액터 배치 → 디테일 "Physics Asset" 에 `<Mesh>_Physics.uasset` 지정 → 저장/재오픈 시 경로 유지, `GetPhysicsAsset() != nullptr`.
3. **PIE 런타임**: PIE 진입 후 (Lua/트리거) `comp:SetSimulatingPhysics(true)` → 래그돌 낙하.
4. **다중 인스턴스**: 같은 메시 액터 2개에 서로 다른(또는 한쪽만) PhysicsAssetPath → 간섭 없음(공유 메시 오염 부재).
5. **에디터 프리뷰**: PhysicsAssetPath 미설정 메시를 Physics 탭에서 Simulate → 기존대로(폴백).
6. **수명**: 액터 삭제/레벨 언로드 시 `PhysicsAssetOverride` 누수/크래시 없음. 시뮬 중 경로 변경 시 정상 정리.

---

## 부록. 데이터 흐름 (옵션 A · 파생 배치 적용 후)

```
[Scene .Scene] ──(SerializeProperties)── USkeletalMeshComponent.PhysicsAssetPath (UPROPERTY Save)
        │
   PostDuplicate (Super=메시로드) / PostEditProperty (편집)
        ▼
LoadPhysicsAssetFromPath() ── UPhysicsAsset::LoadFromFile(path) ──▶ PhysicsAssetOverride (컴포넌트 소유)
        │
   GetPhysicsAsset() = PhysicsAssetOverride ?? GetSkeletalMesh()->PhysicsAsset(폴백)
        ▼
SetSimulatingPhysics(true)
   → InstantiatePhysicsAssetBodies(WorldScene, GetPhysicsAsset())
   → EnterRagdollState() → World.Simulate → ApplyPhysicsToBones (기존 경로)
```
```
클래스 책임 분리:
  USkinnedMeshComponent (base)   : SkeletalMeshPath, 메시/스키닝 — 수정 없음
  USkeletalMeshComponent (derived): PhysicsAssetPath/Override, Bodies/Constraints,
                                    Instantiate/SetSimulatingPhysics/GetPhysicsAsset — 물리 전담
```
