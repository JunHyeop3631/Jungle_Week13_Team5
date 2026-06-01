#pragma once
#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Editor/Viewport/Asset/MeshEditorViewportClient.h"
#include "Editor/UI/Dialog/FbxImportOptionsDialog.h"
#include "Asset/AssetRegistry.h"
#include "Physics/Asset/PhysicsAsset.h"
#include "Math/Matrix.h"
#include "Math/Transform.h"
#include "Editor/UI/Asset/Physics/PhysicsShapeGizmoTarget.h"
#include "Editor/UI/Asset/Physics/PhysicsConstraintGizmoTarget.h"

struct FSkeletalMesh;
struct ImDrawList;
struct ImVec2;
class UAnimSequence;
class UAnimMontage;
class UAnimSingleNodeInstance;
class UBodySetup;
class UPhysicsConstraintSetup;

enum class EMeshEditorTab : uint8 { Skeleton, Mesh, Animation, Physics };

// Physics 탭 전용 UI 상태
struct FPhysicsEditTabState
{
    // 현재 편집 중인 PhysicsAsset (메시에 귀속, 탭 진입 시 생성/로드)
    UPhysicsAsset* PhysicsAsset = nullptr;

    // 선택 상태 (-1 = 미선택)
    int32 SelectedBodySetupIndex  = -1;
    int32 SelectedConstraintIndex = -1;

    // 셰이프 요소 선택 (기즈모 타겟용)
    enum class EShapeType : uint8 { None, Sphere, Box, Capsule };
    EShapeType SelectedShapeType      = EShapeType::None;
    int32      SelectedShapeElemIndex = -1;

    FPhysicsShapeGizmoTarget      ShapeGizmoTarget;
    FPhysicsConstraintGizmoTarget ConstraintGizmoTarget;

    // 우측 패널 탭 (0 = Details, 1 = Tools)
    int32 RightPanelTab = 0;

    // 스켈레톤 트리 검색 필터
    char BoneSearchText[128] = {};

    // 바디 자동 생성 툴 설정
    // 바디(셰이프) 생성 = 목표4 사이클1 구현됨(FMeshEditorWidget::GeneratePhysicsBodies).
    // 컨스트레인트(C4)/인접쌍 충돌 비활성화(C5)는 아직 미구현(다음 사이클).
    struct FBodyCreationSettings
    {
        // 본 "크기"(스키닝 정점 AABB 최대변, 폴백=자식거리 median)가 이 값 미만이면 바디 생략.
        // 단위 = m. 휴머노이드 사지 본 ~0.2~0.4m / 손가락·발가락 ~0.02~0.05m → 0.2 권고.
        // (실측: GeneratePhysicsBodies 가 본 크기 분포를 UE_LOG 로 출력 → 그 값으로 튜닝)
        float      MinBoneSize            = 0.2f;
        float      CapsuleRadiusRatio     = 0.25f;     // 셰이프 반지름 = 본길이 × 이 비율
        EShapeType PrimitiveType          = EShapeType::Capsule; // 생성할 기본 셰이프
        bool       bOrientAlongBone       = true;      // 본 축(최장거리 자식)을 따라 셰이프 정렬
        bool       bSkipLeafBones         = true;      // 말단(자식 없는) 본은 바디 생략
        bool       bWalkPastSmallBones    = true;      // (C4 예정) 작은 본은 지나치고 다음 본으로
        bool       bCreateBodyForAllBones = false;     // 크기 필터 무시하고 모든 본에 바디 생성
        bool       bDisableCollisionByDefault = true;  // (C5 예정) 생성 시 인접 바디 충돌 비활성화
        int32      LodIndex               = 0;
        bool       bCreateConstraints     = true;      // (C4 예정) 부모-자식 컨스트레인트 자동 생성
        int32      AngularConstraintMode  = 1;         // (C4 예정) 0 Locked, 1 Limited, 2 Free
    };
    FBodyCreationSettings BodyCreation;

    // 패널 너비
    float BoneTreeWidth = 220.f;
    float DetailsWidth  = 300.f;

    // 그래프 패널 (좌측 하단)
    float GraphHeight = 200.f;
    float GraphPanX   = 0.f;
    float GraphPanY   = 0.f;
    float GraphZoom   = 1.f;
};

struct FAnimationTabState
{
	UAnimSequence* CurrentSequence    = nullptr;
	UAnimMontage*  CurrentMontage     = nullptr;
	int32          SelectedAnimIndex     = -1;
	int32          SelectedMontageIndex  = -1;
	bool           bMontageSelected      = false;     // true 면 좌측 패널이 montage 표시
	// 타임라인에서 선택된 Notify entry 인덱스 (현재 시퀀스의 DataModel->Notifies 기준).
	// -1 = 미선택. 시퀀스/몽타주 전환 시 -1 reset 필요.
	// 유효 시 좌상단 AssetDetails 패널이 시퀀스 정보 대신 Notify 의 UPROPERTY 편집 UI 를 그림.
	int32         SelectedNotifyIndex     = -1;
	int32         SelectedMorphCurveIndex = -1;
	int32         SelectedMorphKeyIndex   = -1;
	TArray<float> MorphPreviewWeights;
	TArray<uint8> MorphPreviewOverrideMask;
	bool          bMorphPreviewOverrideEnabled = false;

	// Animation tab asset browser cache.
	// Render 중 매 프레임 ListAnimationsForSkeleton() -> LoadAnimation() -> Serialize() 되는 것을 막는다.
	TArray<FAssetListItem> CachedAnimationFiles;
	FSkeletonBinding       CachedAnimationListBinding;
	bool                   bAnimationListDirty = true;

	float         AnimListWidth                = 200.0f;
	float         AnimDetailsWidth             = 280.0f;

	FFbxAnimationImportDialogState AnimationImportDialog;
};

class FMeshEditorWidget : public FAssetEditorWidget
{
public:
	FMeshEditorWidget();

	bool CanEdit(UObject* Object) const override;
	bool IsEditingObject(UObject* Object) const override;

	void Open(UObject* Object) override;
	void Close() override;
	void Tick(float DeltaTime) override;

	void CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const override;

	bool AllowsMultipleInstances() const override { return true; }

	void Render(float DeltaTime) override;

	bool IsMouseOverViewport() const { return IsOpen() && ViewportClient.IsMouseOverViewport(); }

	FMeshEditorViewportClient* GetViewportClient() { return &ViewportClient; }

	static void RecordImportDurationForAsset(const FString& AssetPath, double Seconds);
	static void ClearImportDurationForAsset(const FString& AssetPath);

	// ContentBrowser에서 Physics 탭으로 바로 열고 싶을 때 Open() 전에 true로 세팅
	static bool s_bOpenInPhysicsTab;

private:
	// Tab bar
	void RenderTabBar();

	// Per-tab layouts
	void RenderSkeletonLayout();
	void RenderMeshLayout();
	void RenderAnimationLayout(float TotalHeight);
	void RenderPhysicsLayout();

	// Shared helpers
	void RenderViewportPanel(ImVec2 Size);
	void RenderBoneTree(const FSkeletalMesh* Asset, int32 Index);
	void RenderMeshStatsOverlay(ImDrawList* DrawList, const ImVec2& ViewportPos) const;

	// Physics tab helpers
	void RenderPhysicsBoneTree(const FSkeletalMesh* Asset, int32 BoneIndex, bool bFlat = false);
	void RenderPhysicsDetailsPanel();
	void RenderBodySetupDetails(UBodySetup* Setup);
	void RenderConstraintDetails(UPhysicsConstraintSetup* Constraint);
	// 편집 툴바 (컨스트레인트 타입 변환, 바디쌍 충돌 토글)
	void RenderPhysicsToolbar();
	// 그래프 패널 (노드 기반 바디↔컨스트레인트 뷰)
	void RenderPhysicsGraphPanel();
	// 바디 자동 생성 툴 (목표: 언리얼 PhAT 의 "바디 생성" 패널)
	void RenderPhysicsToolsPanel();
	// 스켈레톤 전체에 바디/컨스트레인트를 일괄 생성한다.
	// TODO: 현재는 미구현 stub. 생성 로직 담당자가 채워넣을 것.
	void GeneratePhysicsBodies();

	// ── Physics 시뮬레이션 (래그돌 미리보기) ──────────────────
	// 하단 트랜스포트 바 (재생/일시정지/정지/속도)
	void RenderPhysicsTransportBar(float Width);
	void StartPhysicsSimulation();
	void StopPhysicsSimulation();
	void TickPhysicsSimulation(float DeltaTime);
	void UpdatePhysicsShapeGizmo();
	void SavePhysicsAsset();

	// Physics 통계 오버레이 (바디/프리미티브/컨스트레인트/충돌쌍 개수)
	void DrawPhysicsStatsOverlay(ImDrawList* DL, ImVec2 VPMin) const;

	// 뷰포트 클릭 레이 피킹 (바디 셰이프 우선, 없으면 컨스트레인트)
	void PickPhysicsAtScreen(float LocalX, float LocalY, float VpW, float VpH);

	// 선택된 컨스트레인트의 각 한계(트위스트 호 + 스윙 콘) 시각화
	void DrawConstraintLimitsOverlay(ImDrawList* DL, const FMatrix& VP, ImVec2 VPMin, ImVec2 VPSize) const;

	// Physics 콜리전 셰이프 오버레이 (현재는 컨스트레인트 각 한계 시각화만 담당)
	void DrawPhysicsShapeOverlays(ImDrawList* DL, ImVec2 VPMin, ImVec2 VPSize) const;
	bool WorldToScreenPhysics(const FMatrix& VP, FVector World, ImVec2 VPMin, ImVec2 VPSize, ImVec2& Out) const;

	// Animation tab helpers
	void ApplyAnimationToComponent();
	void ResetMorphPreviewOverrides();
	void EnsureMorphPreviewOverrideSize();
	void ApplyMorphPreviewOverrides(TArray<float>& InOutMorphWeights) const;
	void RefreshAnimationPreviewPose();
	void MarkAnimationListDirty();
	const TArray<FAssetListItem>& GetCachedAnimationFilesForCurrentSkeleton();

private:
	FMeshEditorViewportClient ViewportClient;

	// Tab state
	EMeshEditorTab     ActiveTab = EMeshEditorTab::Skeleton;
	FAnimationTabState AnimTabState;
	FPhysicsEditTabState PhysicsTabState;

	// Skeleton tab state
	int32 SelectedBoneIndex = -1;
	float HierarchyWidth    = 250.0f;
	float DetailsWidth      = 300.0f;

	uint32  InstanceId;
	FName   PreviewWorldHandle = FName::None;
	FString WindowIdSuffix;

	bool bPendingClose = false;

	// ── Physics 시뮬레이션 UI 상태 (트랜스포트 바 전용) ───────
	// 실제 시뮬레이션 런타임/바디/조인트 상태는 시뮬레이션 담당자가
	// 구현하면서 필요한 만큼 여기에 추가한다.
	bool                         bSimulating = false;
	bool                         bSimPaused  = false;
	float                        SimSpeed    = 1.0f;
};
