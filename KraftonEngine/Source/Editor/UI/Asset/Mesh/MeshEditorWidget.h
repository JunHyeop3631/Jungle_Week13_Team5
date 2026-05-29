#pragma once
#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Editor/Viewport/Asset/MeshEditorViewportClient.h"
#include "Editor/UI/Dialog/FbxImportOptionsDialog.h"
#include "Asset/AssetRegistry.h"
#include "Physics/Asset/PhysicsAsset.h"
#include "Math/Matrix.h"
#include "Editor/UI/Asset/Physics/PhysicsShapeGizmoTarget.h"

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

    FPhysicsShapeGizmoTarget ShapeGizmoTarget;

    // 패널 너비
    float BoneTreeWidth = 220.f;
    float DetailsWidth  = 300.f;
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
	void RenderPhysicsBoneTree(const FSkeletalMesh* Asset, int32 BoneIndex);
	void RenderPhysicsDetailsPanel();
	void RenderBodySetupDetails(UBodySetup* Setup);
	void RenderConstraintDetails(UPhysicsConstraintSetup* Constraint);
	void UpdatePhysicsShapeGizmo();
	void SavePhysicsAsset();

	// Physics 콜리전 셰이프 오버레이
	void DrawPhysicsShapeOverlays(ImDrawList* DL, ImVec2 VPMin, ImVec2 VPSize) const;
	bool WorldToScreenPhysics(const FMatrix& VP, FVector World, ImVec2 VPMin, ImVec2 VPSize, ImVec2& Out) const;
	void DrawWireSpherePh (ImDrawList* DL, FVector C, float R,
	                       unsigned int Col, const FMatrix& VP, ImVec2 VPMin, ImVec2 VPSize) const;
	void DrawWireBoxPh    (ImDrawList* DL, FVector C, FQuat Rot,
	                       float HX, float HY, float HZ,
	                       unsigned int Col, const FMatrix& VP, ImVec2 VPMin, ImVec2 VPSize) const;
	void DrawWireCapsulePh(ImDrawList* DL, FVector C, FQuat Rot,
	                       float Radius, float HalfH,
	                       unsigned int Col, const FMatrix& VP, ImVec2 VPMin, ImVec2 VPSize) const;

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
};
