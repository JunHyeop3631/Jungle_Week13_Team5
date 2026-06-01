# 구현 명세서 — 물리 바디 디버그 렌더 (솔리드/와이어 둘 다 토글)

> 사이클: **spec → implement**. 본 문서 = 구현 명세(코드 미수정).
> 목표: 물리 바디(셰이프) 디버그 표시를 **솔리드 채움 + 와이어프레임 라인**으로, 각각 독립 토글(둘 다 ON 기본).
> 점검 결론(직전 턴): 라인 배치 인프라·NoDepth 라인 패턴·프록시→라인 라우팅 모두 존재 → 신규 셰이더/패스/토폴로지 불필요.
> 참조: 점검 근거는 본 문서 §1. 인프라 맵 `Docs/physics_temp.md`.

---

## 0. 변경 파일 요약 (8개)

| # | 파일 | 변경 |
|---|---|---|
| F1 | `Render/Proxy/PhysicsShapeDebugSceneProxy.h` | `FColoredLine` 구조체 + `CachedWire` + 토글 멤버 + getter |
| F2 | `Render/Proxy/PhysicsShapeDebugSceneProxy.cpp` | 와이어 빌더(AppendWire*) + RebuildGeometry 와이어 생성 + 토글 복사 |
| F3 | `Render/Command/DrawCommandBuilder.cpp` | `PhysicsShapeWire` 라인 배처 + 분기 와이어 emit + NoDepth 라인 커맨드 |
| F4 | `Component/Debug/PhysicsShapeDebugComponent.h` | `bShowSolid`/`bShowWire` + getter/setter |
| F5 | `Editor/Viewport/Asset/MeshEditorViewportClient.h/.cpp` | `UpdatePhysicsShapeDebug` 에 토글 2개 인자 추가 → 컴포넌트 전달 |
| F6 | `Editor/UI/Asset/Mesh/MeshEditorWidget.h` | `FPhysicsEditTabState` 에 `bShowBodySolid`/`bShowBodyWire` |
| F7 | `Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp` | 토글 체크박스 UI |
| F8 | `Editor/UI/Asset/Mesh/MeshEditorWidget.cpp` | `UpdatePhysicsShapeDebug` 호출에 토글 전달 |

> 직렬화 무변경: 토글은 에디터 UI 상태 + 컴포넌트 런타임 상태(비직렬화). `UPhysicsAsset`/셰이프 데이터 무관.

---

## 1. STEP 0 — 검증된 touch point (점검 근거)

| 점검 | 코드 (파일:행) | 결과 |
|---|---|---|
| 물리 프록시 현재 emit(솔리드 삼각형) | `PhysicsShapeDebug` 분기 → `PhysicsShapeSolid.AddTriangle` [DrawCommandBuilder.cpp:354-365](KraftonEngine/Source/Engine/Render/Command/DrawCommandBuilder.cpp:354) | 확인 |
| 프록시→라인 배치 전례 | `WireShape` → `EditorLines.AddLine(Start,End,Color)` [DrawCommandBuilder.cpp:342-352](KraftonEngine/Source/Engine/Render/Command/DrawCommandBuilder.cpp:342) | 확인(미러 대상) |
| LINELIST 라인 패스 | `D3D11_PRIMITIVE_TOPOLOGY_LINELIST`, AlphaBlend [EditorLinesPass.cpp:8-10](KraftonEngine/Source/Engine/Render/RenderPass/EditorLinesPass.cpp:8) | 확인 |
| AddLine 시그니처 | `AddLine(const FVector&, const FVector&, const FVector4& Color)` [LineGeometry.h:26](KraftonEngine/Source/Engine/Render/Geometry/LineGeometry.h:26) | 확인 |
| **NoDepth 라인 패턴** | `BoneLinesRS = EditorLinesRS; BoneLinesRS.DepthStencil = NoDepth; EmitLineCommand(DebugBoneLines, …)` [DrawCommandBuilder.cpp:753-756](KraftonEngine/Source/Engine/Render/Command/DrawCommandBuilder.cpp:753) | 확인(와이어도 동일 적용) |
| 솔리드 NoDepth emit | PhysicsShapeSolid AlphaBlend+NoDepth [DrawCommandBuilder.cpp:761-772](KraftonEngine/Source/Engine/Render/Command/DrawCommandBuilder.cpp:761) | 확인 |
| 배처 Create/Clear | PhysicsShapeSolid.Create/Clear/Release [DrawCommandBuilder.cpp:38,102,58] | 확인(Wire 동일 추가) |
| 프록시 지오 빌드 | RebuildGeometry: BodySetup 순회·본 월드 변환·솔리드 테셀레이션 [PhysicsShapeDebugSceneProxy.cpp:164-229](KraftonEngine/Source/Engine/Render/Proxy/PhysicsShapeDebugSceneProxy.cpp:164) | 확인(와이어도 같은 루프) |
| 프록시 플래그 | `EditorOnly\|NeverCull\|PhysicsShapeDebug` [PhysicsShapeDebugSceneProxy.cpp:148-150](KraftonEngine/Source/Engine/Render/Proxy/PhysicsShapeDebugSceneProxy.cpp:148) | 확인 |
| FWireLine 전례 | `struct FWireLine { FVector Start; FVector End; }` [ShapeSceneProxy.h:9-13](KraftonEngine/Source/Engine/Render/Proxy/ShapeSceneProxy.h:9) | 확인 |
| 디버그 컴포넌트 설정 | `UpdatePhysicsShapeDebug(PA, SelBody, SelKind, SelElem)` [MeshEditorViewportClient.cpp:21-37](KraftonEngine/Source/Editor/Viewport/Asset/MeshEditorViewportClient.cpp:21) | 확인(인자 확장) |
| 위젯 호출부 | `ViewportClient.UpdatePhysicsShapeDebug(PhysicsTabState.…)` [MeshEditorWidget.cpp:347-349](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.cpp:347) | 확인(토글 전달) |

---

## 2. 토글 모델 + 데이터 흐름
- **토글 2개 독립**: `bShowBodySolid`(기본 true), `bShowBodyWire`(기본 true). 둘 다 ON = 솔리드+와이어 겹쳐 표시. 하나만 ON도 가능.
- **흐름**:
  `FPhysicsEditTabState`(UI 상태) → 체크박스 → `MeshEditorWidget.cpp` 호출 → `UpdatePhysicsShapeDebug(…, bSolid, bWire)` → `UPhysicsShapeDebugComponent`(런타임 플래그) → `FPhysicsShapeDebugSceneProxy::RebuildGeometry`(플래그 복사 + 솔리드·와이어 둘 다 캐싱) → `DrawCommandBuilder`(플래그별 솔리드/와이어 emit) → 솔리드=AlphaBlend·NoDepth, 와이어=EditorLines(LINELIST)·NoDepth.
- 프록시는 매 프레임 재생성(`MarkRenderStateDirty` [MeshEditorViewportClient.cpp:36]) → 토글 변경 즉시 반영.

---

## 3. 컴포넌트별 명세

### F4 — UPhysicsShapeDebugComponent (플래그)
[PhysicsShapeDebugComponent.h](KraftonEngine/Source/Engine/Component/Debug/PhysicsShapeDebugComponent.h) `SetSelection` 인근:
```cpp
bool bShowSolid = true;
bool bShowWire  = true;
bool GetShowSolid() const { return bShowSolid; }
bool GetShowWire()  const { return bShowWire; }
void SetShowFlags(bool bInSolid, bool bInWire) { bShowSolid = bInSolid; bShowWire = bInWire; }
```

### F1/F2 — FPhysicsShapeDebugSceneProxy (와이어 캐시 + 빌더)
**F1 헤더** [PhysicsShapeDebugSceneProxy.h](KraftonEngine/Source/Engine/Render/Proxy/PhysicsShapeDebugSceneProxy.h):
```cpp
// 와이어 세그먼트(셰이프별 단일 색). 솔리드와 달리 셰이딩 없음.
struct FColoredLine { FVector Start; FVector End; FVector4 Color; };

// class FPhysicsShapeDebugSceneProxy 내부:
const TArray<FColoredLine>& GetCachedWire() const { return CachedWire; }
bool ShouldDrawSolid() const { return bShowSolid; }
bool ShouldDrawWire()  const { return bShowWire; }
private:
    TArray<FColoredLine> CachedWire;
    bool bShowSolid = true;
    bool bShowWire  = true;
```
**F2 .cpp** `RebuildGeometry` [PhysicsShapeDebugSceneProxy.cpp:164](KraftonEngine/Source/Engine/Render/Proxy/PhysicsShapeDebugSceneProxy.cpp:164):
- 맨 앞에 `CachedWire.clear();` 추가, 컴포넌트에서 토글 복사: `bShowSolid = Comp->GetShowSolid(); bShowWire = Comp->GetShowWire();`
- 각 elem 루프(현재 `AppendSolid*` 호출부, [205-227])에서 **와이어도 함께 생성**:
```cpp
// 예: 스피어
AppendSolidSphere(CachedSolid, WorldCenter, E.Radius, PickSolidColor(bSel, bBodySel));
AppendWireSphere (CachedWire,  WorldCenter, E.Radius, PickWireColor(bSel, bBodySel));   // 신규
// box/capsule 동일하게 AppendWireBox/AppendWireCapsule 추가
```
> 솔리드·와이어 둘 다 무조건 캐싱하고, **표시 여부는 DrawCommandBuilder 가 플래그로 결정**(토글 시 재빌드 불필요, 매 프레임 재생성이라 어차피 즉시 반영). 빌드 비용은 에디터 디버그 수준이라 무시.

### F3 — DrawCommandBuilder (와이어 배처 + emit)
[DrawCommandBuilder.cpp](KraftonEngine/Source/Engine/Render/Command/DrawCommandBuilder.cpp):
- **배처 멤버 추가**: `FLineGeometry PhysicsShapeWire;` — Create/Release/Clear 를 `PhysicsShapeSolid` 와 나란히([38/58/102]).
- **PhysicsShapeDebug 분기** [354-365] 를 플래그 분기로:
```cpp
else if (Proxy->HasProxyFlag(EPrimitiveProxyFlags::PhysicsShapeDebug))
{
    const FPhysicsShapeDebugSceneProxy* ShapeProxy = static_cast<const FPhysicsShapeDebugSceneProxy*>(Proxy);
    if (ShapeProxy->ShouldDrawSolid())
    {
        const TArray<FColoredVertex>& Solid = ShapeProxy->GetCachedSolid();
        for (size_t i = 0; i + 2 < Solid.size(); i += 3)
            PhysicsShapeSolid.AddTriangle(Solid[i].Pos, Solid[i+1].Pos, Solid[i+2].Pos,
                                          Solid[i].Color, Solid[i+1].Color, Solid[i+2].Color);
    }
    if (ShapeProxy->ShouldDrawWire())
    {
        for (const FColoredLine& L : ShapeProxy->GetCachedWire())
            PhysicsShapeWire.AddLine(L.Start, L.End, L.Color);
    }
}
```
- **emit** `BuildEditorLineCommands` [756 인근](KraftonEngine/Source/Engine/Render/Command/DrawCommandBuilder.cpp:756), `DebugBoneLines` emit 다음에 NoDepth 라인으로:
```cpp
EmitLineCommand(DebugBoneLines, EditorShader, BoneLinesRS);
EmitLineCommand(PhysicsShapeWire, EditorShader, BoneLinesRS);   // 신규: LINELIST + NoDepth(=BoneLinesRS) → 솔리드처럼 항상 위
```
> `BoneLinesRS` 는 이미 EditorLinesRS+NoDepth([753-754]). 와이어를 여기 태우면 솔리드(NoDepth)와 동일하게 메시에 안 가려짐. 기즈모는 이후 패스라 그대로 위에 남음.

### F5 — UpdatePhysicsShapeDebug (인자 확장)
[MeshEditorViewportClient.h:57](KraftonEngine/Source/Editor/Viewport/Asset/MeshEditorViewportClient.h:57) / [.cpp:21](KraftonEngine/Source/Editor/Viewport/Asset/MeshEditorViewportClient.cpp:21):
```cpp
void UpdatePhysicsShapeDebug(UPhysicsAsset* PhysicsAsset, int32 SelBodyIndex, int32 SelKind, int32 SelElemIndex,
                             bool bShowSolid, bool bShowWire);
// .cpp 본문: SetSelection 다음에
PhysicsShapeDebugComponent->SetShowFlags(bShowSolid, bShowWire);
```

### F6/F7 — 에디터 토글 + UI
**F6** [MeshEditorWidget.h FPhysicsEditTabState](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.h:24):
```cpp
bool bShowBodySolid = true;   // 바디 솔리드 채움 표시
bool bShowBodyWire  = true;   // 바디 와이어프레임 표시
```
**F7** [MeshEditorWidget.Physics.cpp](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.Physics.cpp) — Tools 패널(또는 Physics 레이아웃 상단)에 "Display" 그룹:
```cpp
ImGui::Checkbox("Show Body Solid", &PhysicsTabState.bShowBodySolid);
ImGui::Checkbox("Show Body Wireframe", &PhysicsTabState.bShowBodyWire);
```

### F8 — 호출부 전달
[MeshEditorWidget.cpp:347](KraftonEngine/Source/Editor/UI/Asset/Mesh/MeshEditorWidget.cpp:347):
```cpp
ViewportClient.UpdatePhysicsShapeDebug(
    PhysicsTabState.PhysicsAsset,
    PhysicsTabState.SelectedBodySetupIndex, SelKind, SelElemIndex,
    PhysicsTabState.bShowBodySolid, PhysicsTabState.bShowBodyWire);   // 신규 인자
```

---

## 4. 와이어 지오메트리 빌더 의사코드 (F2, anon namespace)
> 입력은 **월드 공간**(RebuildGeometry 가 WorldCenter/WorldRot 전달, 솔리드 빌더와 동일 규약). 색은 셰이프별 단일 `Color`.
```cpp
// 풀 알파 와이어 색(선택 하이라이트). 솔리드(PickSolidColor)의 불투명 버전.
FVector4 PickWireColor(bool bSel, bool bBodySel)
{
    if (bSel)     return {1.0f, 0.85f, 0.10f, 1.0f};
    if (bBodySel) return {0.20f, 1.00f, 0.55f, 1.0f};
    return                {0.15f, 0.80f, 0.42f, 1.0f};
}

void PushLine(TArray<FColoredLine>& Out, const FVector& A, const FVector& B, const FVector4& C)
{ Out.push_back({A, B, C}); }

// 구 = 3 직교 대원(XY,YZ,ZX), Seg=24
void AppendWireSphere(Out, C, R, Col) {
    const int Seg = 24; const float dA = kPi2/Seg;
    for plane in {(X,Y),(Y,Z),(Z,X)}:
        for i in 0..Seg-1:
            a0=i*dA; a1=(i+1)*dA;
            P0 = C + R*(axisU*cos(a0)+axisV*sin(a0));
            P1 = C + R*(axisU*cos(a1)+axisV*sin(a1));
            PushLine(Out, P0, P1, Col);
}

// 박스 = 8 꼭짓점(회전) → 12 엣지
void AppendWireBox(Out, C, Rot, HX,HY,HZ, Col) {
    V[8] = (±HX,±HY,±HZ);  P[k] = C + Rot.RotateVector(V[k]);
    static const int E[12][2] = {{0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}};
    for e in E: PushLine(Out, P[e0], P[e1], Col);
}

// 캡슐 = 양끝 링 2 + 세로선 4 + 캡 호(반구당 2)
void AppendWireCapsule(Out, C, Rot, Radius, HalfH, Col) {
    Up=Rot*(0,0,1); Right=Rot*(1,0,0); Fwd=Rot*(0,1,0);
    TopC=C+Up*HalfH; BotC=C-Up*HalfH; const int Seg=24;
    Radial(a)= Right*cos(a)+Fwd*sin(a);
    // 1) 양끝 링(Right-Fwd 평면)
    for i in 0..Seg-1: ring at TopC, ring at BotC  // PushLine 인접 세그먼트
    // 2) 세로선 4 (±Right, ±Fwd 방향)
    for d in {Right,-Right,Fwd,-Fwd}: PushLine(TopC+d*Radius, BotC+d*Radius, Col);
    // 3) 캡 반원 호: 각 끝점에서 (Right-Up), (Fwd-Up) 평면의 반원(위/아래 부호)
    for hemi in {Top:+Up, Bot:-Up}:
        half-circle in (Right, hemiUp) plane;  half-circle in (Fwd, hemiUp) plane;
}
```
> 퇴화(R≈0, HalfH≈0)는 솔리드 빌더와 동일하게 자연 처리(루프가 작은 도형 생성). 별도 가드 불필요하나, 0-길이 세그먼트는 렌더에 무해.

---

## 5. 구현 순서 / 의존성
1. **F4**(컴포넌트 플래그) — 단독.
2. **F1/F2**(프록시 와이어 캐시+빌더) — F4 사용. 컴파일.
3. **F3**(DrawCommandBuilder 배처+emit) — F1 getter 사용. 컴파일 → 이 시점에 **솔리드+와이어 둘 다 그려짐**(토글은 항상 true 기본).
4. **F5**(뷰포트 클라이언트 인자) — 컴포넌트 setter 연결.
5. **F6/F7/F8**(에디터 토글+UI+전달) — 토글 동작 완성. 컴파일.
- 각 단계 Debug|x64 컴파일.

---

## 6. 검증
- [ ] 단계별 컴파일 통과.
- [ ] 기본(둘 다 ON): 바디가 반투명 솔리드 + 와이어 겹쳐 표시, 메시에 안 가려짐(NoDepth).
- [ ] "Show Body Solid" OFF → 와이어만. "Show Body Wireframe" OFF → 솔리드만. 둘 다 OFF → 안 보임.
- [ ] 선택 하이라이트: 선택 셰이프/바디가 와이어·솔리드 모두 강조색.
- [ ] sphere/box/capsule 와이어 형상 정확(대원 3·박스 12엣지·캡슐 링+세로+캡호).
- [ ] Re-generate Bodies 후에도 즉시 반영(프록시 매 프레임 재생성).
- (런타임/시각 항목은 에디터 필요 — 환경에선 컴파일까지.)

---

## 7. 미결정 / 주의
1. **와이어 색**: PickWireColor 값(풀 알파)은 임시안 — 솔리드와 구분되게 톤 조정 가능(미결, 기본값 제시).
2. **UI 위치**: Tools 패널 "Display" 그룹 권고. 트랜스포트 바/오버레이 등 대안 가능(미결, 기능 무관).
3. **세그먼트 수(Seg=24)**: 시각 품질/성능 트레이드오프. 고정 상수로 시작, 필요 시 조정.
4. **와이어 depth**: 솔리드와 동일 NoDepth(항상 위, PhAT 스타일)로 통일. Default depth(가림) 원하면 `EditorLinesRS` 그대로 emit하는 별도 옵션 가능(범위 밖).
5. **FColoredLine 정의 위치**: 프록시 헤더에 둠(솔리드 `FColoredVertex` 와 대칭). 공유 필요 없으면 이대로.
