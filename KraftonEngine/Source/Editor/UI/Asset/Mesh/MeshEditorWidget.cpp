#include "MeshEditorWidget.h"
#include "Physics/Asset/PhysicsAsset.h"
#include "Physics/Asset/BodySetup.h"
#include "Physics/Asset/PhysicsConstraintSetup.h"
#include "Asset/AssetPackage.h"
#include "Serialization/WindowsArchive.h"

#include <filesystem>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Runtime/Engine.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Viewport/Viewport.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "Settings/EditorSettings.h"
#include "UI/Toolbar/ViewportToolbar.h"
#include "Slate/SlateApplication.h"
#include "Render/Shader/ShaderManager.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Animation/Montage/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/Instance/AnimSingleNodeInstance.h"
#include "Animation/AnimationManager.h"
#include "Animation/Sequence/AnimDataModel.h"
#include "Asset/AssetRegistry.h"
#include "UI/Asset/Animation/AnimationTransportBar.h"
#include "UI/Asset/Animation/AnimationTimelinePanel.h"
#include "UI/Asset/Animation/AnimSequencePropertyPanel.h"
#include "UI/Asset/Animation/AnimMontagePropertyPanel.h"
#include "UI/Util/EditorFileUtils.h"
#include "Editor/UI/Util/EditorTextureManager.h"
#include "Platform/Paths.h"
#include "Object/Object.h"
#include "Core/Logging/Log.h"
#include "Render/Types/MinimalViewInfo.h"
#include "Math/Quat.h"
#include "Component/Debug/GizmoComponent.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

// Paths.h가 끌어오는 Windows.h는 GetCurrentTime을 GetTickCount로 치환한다.
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

namespace
{
	ID3D11ShaderResourceView* LoadTabIcon(const wchar_t* FileName)
	{
		const FString Path = FPaths::ToUtf8(
			FPaths::Combine(FPaths::AssetDir(), L"Editor/ToolIcons/", FileName));
		return FEditorTextureManager::Get().GetOrLoadIcon(Path);
	}

	FString FormatMeshStatCount(size_t Value)
	{
		FString Result = std::to_string(Value);
		for (int32 InsertPos = static_cast<int32>(Result.length()) - 3; InsertPos > 0; InsertPos -= 3)
		{
			Result.insert(static_cast<size_t>(InsertPos), ",");
		}
		return Result;
	}

	FString FormatMeshStatSeconds(double Seconds)
	{
		char Buffer[64] = {};
		std::snprintf(Buffer, sizeof(Buffer), "%.3f sec", Seconds);
		return FString(Buffer);
	}

	bool IsSameSkeletonBindingForAnimationList(const FSkeletonBinding& A, const FSkeletonBinding& B)
	{
		return A.SkeletonPath == B.SkeletonPath
			&& A.SkeletonAssetGuid == B.SkeletonAssetGuid
			&& A.CompatibilitySignature == B.CompatibilitySignature;
	}

	TMap<FString, double> GMeshImportDurationsByAssetPath;

	// MyCar.uasset  →  MyCar_Physics.uasset (같은 디렉토리)
	FString MakePhysicsAssetPath(const FString& MeshPath)
	{
		const size_t DotPos = MeshPath.find_last_of('.');
		if (DotPos != FString::npos)
			return MeshPath.substr(0, DotPos) + "_Physics" + MeshPath.substr(DotPos);
		return MeshPath + "_Physics.uasset";
	}

	// 파일에서 PhysicsAsset 로드. 실패 시 nullptr 반환.
	UPhysicsAsset* LoadPhysicsAssetFromFile(const FString& Path)
	{
		const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

		FWindowsBinReader Reader(NormalizedPath);
		if (!Reader.IsValid()) return nullptr;

		FAssetPackageHeader Header;
		Reader << Header;
		if (!Header.IsValid(EAssetPackageType::PhysicsAsset)) return nullptr;

		FAssetImportMetadata Metadata;
		Reader << Metadata;

		UPhysicsAsset* Asset = new UPhysicsAsset();
		Asset->Serialize(Reader);
		return Asset;
	}

	double GetRecordedImportDurationSeconds(const USkeletalMesh* Mesh)
	{
		if (!Mesh)
		{
			return -1.0;
		}

		const FString& AssetPath = Mesh->GetAssetPathFileName();
		if (AssetPath.empty() || AssetPath == "None")
		{
			return -1.0;
		}

		auto It = GMeshImportDurationsByAssetPath.find(AssetPath);
		return It != GMeshImportDurationsByAssetPath.end() ? It->second : -1.0;
	}

	FMorphTargetCurve& FindOrAddMorphCurve(UAnimSequence* Seq, const FString& MorphTargetName)
	{
		TArray<FMorphTargetCurve>& Curves = Seq->GetMutableMorphTargetCurves();
		for (FMorphTargetCurve& Curve : Curves)
		{
			if (Curve.MorphTargetName == MorphTargetName)
			{
				return Curve;
			}
		}
		FMorphTargetCurve NewCurve;
		NewCurve.MorphTargetName = MorphTargetName;
		Curves.push_back(std::move(NewCurve));
		return Curves.back();
	}

	void AddOrUpdateMorphCurveKey(FMorphTargetCurve& Curve, float TimeSeconds, float Value)
	{
		constexpr float TimeTolerance = 1.0e-4f;
		for (FRawFloatCurveKey& Key : Curve.Curve.Keys)
		{
			if (std::fabs(Key.TimeSeconds - TimeSeconds) <= TimeTolerance)
			{
				Key.Value = Value;
				return;
			}
		}
		FRawFloatCurveKey NewKey;
		NewKey.TimeSeconds   = TimeSeconds;
		NewKey.Value         = Value;
		NewKey.Interpolation = 2;
		Curve.Curve.Keys.push_back(NewKey);
		std::sort(
			Curve.Curve.Keys.begin(),
			Curve.Curve.Keys.end(),
			[](const FRawFloatCurveKey& A, const FRawFloatCurveKey& B)
			{
				return A.TimeSeconds < B.TimeSeconds;
			}
		);
	}

	EUberLitDefines::ELightingModel GetLightingModelForViewMode(EViewMode ViewMode)
	{
		switch (ViewMode)
		{
		case EViewMode::Unlit:       return EUberLitDefines::ELightingModel::Unlit;
		case EViewMode::Lit_Gouraud: return EUberLitDefines::ELightingModel::Gouraud;
		case EViewMode::Lit_Lambert: return EUberLitDefines::ELightingModel::Lambert;
		case EViewMode::Lit_Phong:
		case EViewMode::LightCulling:
		default:                     return EUberLitDefines::ELightingModel::Phong;
		}
	}
}

static uint32 GNextMeshEditorInstanceId = 0;
bool FMeshEditorWidget::s_bOpenInPhysicsTab = false;

void FMeshEditorWidget::RecordImportDurationForAsset(const FString& AssetPath, double Seconds)
{
	if (AssetPath.empty() || AssetPath == "None" || Seconds < 0.0)
	{
		return;
	}

	GMeshImportDurationsByAssetPath[AssetPath] = Seconds;
}

void FMeshEditorWidget::ClearImportDurationForAsset(const FString& AssetPath)
{
	if (AssetPath.empty() || AssetPath == "None")
	{
		return;
	}

	GMeshImportDurationsByAssetPath.erase(AssetPath);
}

FMeshEditorWidget::FMeshEditorWidget()
	: InstanceId(GNextMeshEditorInstanceId++)
{
	const FString Id = std::to_string(InstanceId);
	PreviewWorldHandle = FName("MeshEditorPreview_" + Id);
	WindowIdSuffix = "###MeshEditor_" + Id;
}

bool FMeshEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<USkeletalMesh>();
}

bool FMeshEditorWidget::IsEditingObject(UObject* Object) const
{
	if (FAssetEditorWidget::IsEditingObject(Object))
	{
		return true;
	}

	const USkeletalMesh* CurrentMesh = Cast<USkeletalMesh>(EditedObject);
	const USkeletalMesh* RequestedMesh = Cast<USkeletalMesh>(Object);
	if (!IsOpen() || !CurrentMesh || !RequestedMesh)
	{
		return false;
	}

	const FString& CurrentPath = CurrentMesh->GetAssetPathFileName();
	return !CurrentPath.empty()
		&& CurrentPath != "None"
		&& CurrentPath == RequestedMesh->GetAssetPathFileName();
}

void FMeshEditorWidget::Open(UObject* Object)
{
	FAssetEditorWidget::Open(Object);

	FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
	WorldContext.World->SetWorldType(EWorldType::EditorPreview);
	WorldContext.World->InitWorld();

	AActor* Actor = WorldContext.World->SpawnActor<AActor>();
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(EditedObject))
	{
		USkeletalMeshComponent* Comp = Actor->AddComponent<USkeletalMeshComponent>();
		Comp->SetSkeletalMesh(Mesh);
		Actor->SetRootComponent(Comp);
	}
	Actor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));

	ADirectionalLightActor* LightActor = WorldContext.World->SpawnActor<ADirectionalLightActor>();
	LightActor->InitDefaultComponents();
	LightActor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
	UDirectionalLightComponent* LightComp = LightActor->GetComponentByClass<UDirectionalLightComponent>();
	LightComp->SetShadowBias(0.0f);
	LightComp->PushToScene();

	AStaticMeshActor* FloorActor = WorldContext.World->SpawnActor<AStaticMeshActor>();
	FloorActor->InitDefaultComponents("Content/Data/BasicShape/Cube.OBJ");
	FloorActor->SetActorLocation(FVector(0.0f, 0.0f, -0.05f));
	FloorActor->SetActorScale(FVector(10.0f, 10.0f, 0.02f));

	ImVec2 ViewportSize = ImGui::GetContentRegionAvail();

	ViewportClient.Initialize(GEngine->GetRenderer().GetFD3DDevice().GetDevice(), static_cast<uint32>(ViewportSize.x), static_cast<uint32>(ViewportSize.y));
	ViewportClient.SetPreviewWorld(WorldContext.World);
	ViewportClient.SetPreviewActor(Actor);
	ViewportClient.SetPreviewMeshComponent(Actor->GetComponentByClass<USkeletalMeshComponent>());

	ViewportClient.CreatePreviewGizmo();
	ViewportClient.CreateBoneDebugComponent();
	ViewportClient.ResetCameraToPreviousBounds();

	WorldContext.World->SetEditorPOVProvider(&ViewportClient);

	ViewportClient.SetSelectedBone(Cast<USkeletalMesh>(EditedObject), -1);

	FSlateApplication::Get().RegisterViewport(&ViewportClient);

	// 디스크의 기존 AnimSequence .uasset 들을 목록에 채워 둔다(런타임 Load/Save 만으론 안 잡힘).
	FAnimationManager::Get().RefreshAvailableAnimations();

	// ContentBrowser에서 Physics 에셋을 더블클릭하면 Physics 탭으로 바로 진입
	ActiveTab = s_bOpenInPhysicsTab ? EMeshEditorTab::Physics : EMeshEditorTab::Skeleton;
	s_bOpenInPhysicsTab = false;

	AnimTabState      = FAnimationTabState {};
	SelectedBoneIndex = -1;
}

void FMeshEditorWidget::Close()
{
	// Physics 정리: FAssetEditorWidget::Close() 전에 해야 EditedObject 가 살아있음
	{
		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
		// 저장된 적 있으면 USkeletalMesh 가 소유 → delete 금지 (dangling pointer 방지)
		// 저장 안 한 채로 닫으면 편집 위젯이 소유 → delete
		bool bMeshOwns = SkeletalMesh
			&& PhysicsTabState.PhysicsAsset != nullptr
			&& SkeletalMesh->PhysicsAsset == PhysicsTabState.PhysicsAsset;
		if (!bMeshOwns)
			delete PhysicsTabState.PhysicsAsset;
		PhysicsTabState.PhysicsAsset = nullptr;
	}

	FAssetEditorWidget::Close();

	if (UWorld* PreviewWorld = ViewportClient.GetPreviewWorld())
	{
		FScene& PreviewScene = PreviewWorld->GetScene();
		GEngine->GetRenderer().GetResources().ReleaseShadowResourcesForScene(&PreviewScene);

		if (PreviewWorldHandle.IsValid())
		{
			GEngine->DestroyWorldContext(PreviewWorldHandle);
		}
	}

	FSlateApplication::Get().UnregisterViewport(&ViewportClient);

	ViewportClient.Release();
}

void FMeshEditorWidget::Tick(float DeltaTime)
{
	if (ViewportClient.IsRenderable())
	{
		ViewportClient.Tick(DeltaTime);
	}

	// 본 디버그 라인은 Physics 탭에서는 숨긴다 (콜리전 셰이프 편집에 방해됨)
	ViewportClient.SetBoneDebugVisible(ActiveTab != EMeshEditorTab::Physics);

	if (ActiveTab == EMeshEditorTab::Physics)
	{
		if (ViewportClient.IsGizmoHolding())
		{
			MarkDirty();
		}
		else
		{
			// 셰이프가 선택돼 있는 동안 매 프레임 바인딩/위치/가시성을 유지한다.
			// (단발성 클릭만으로는 본 포즈 갱신·타겟 전환에 따라 기즈모가 사라지므로)
			// 드래그 중에는 SetTarget 재호출로 위치가 흔들리지 않도록 건드리지 않는다.
			UpdatePhysicsShapeGizmo();
		}
	}

	if (ActiveTab == EMeshEditorTab::Animation)
	{
		USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
		if (!Comp) return;
		UAnimSingleNodeInstance* NodeInst = Comp->GetAnimNodeInstance(FName::None);
		if (!NodeInst) return;

		NodeInst->UpdateAnimation(DeltaTime);

		USkeletalMesh* Mesh = Comp->GetSkeletalMesh();
		if (!Mesh) return;
		FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
		if (!Asset || Asset->Bones.empty()) return;

		FPoseContext Out;
		Out.SkeletalMesh = Mesh;
		Out.Pose.resize(Asset->Bones.size());
		Out.ResetToRefPose();

		NodeInst->EvaluatePose(Out);
		ApplyMorphPreviewOverrides(Out.MorphWeights);

		Comp->SetAnimationPose(Out.Pose, Out.MorphWeights);
	}
}

void FMeshEditorWidget::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (IsOpen())
	{
		OutClients.push_back(const_cast<FMeshEditorViewportClient*>(&ViewportClient));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Render entry point
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::Render(float DeltaTime)
{
	// 1프레임 지연 close (SRV lifetime issue)
	if (bPendingClose)
	{
		Close();
		bPendingClose = false;
		return;
	}
	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);

	bool bWindowOpen = true;
	FString VisibleTitle = "Mesh Editor";
	const FString AssetPath = SkeletalMesh ? SkeletalMesh->GetAssetPathFileName() : FString();
	if (!AssetPath.empty())
	{
		VisibleTitle += " - ";
		VisibleTitle += AssetPath;
	}
	if (IsDirty())
	{
		VisibleTitle += " *";
	}

	ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_None;
	if (ViewportClient.IsMouseOverViewport())
	{
		WindowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
	}

	FString WindowTitle = VisibleTitle + WindowIdSuffix;
	ImGui::SetNextWindowSize(ImVec2(1280.0f, 720.0f), ImGuiCond_Once);
	if (ConsumeFocusRequest())
	{
		ImGui::SetNextWindowFocus();
	}

	if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen, WindowFlags))
	{
		// 접힌 동안엔 hover 를 보고하지 않음
		ImGui::End();
		if (!bWindowOpen)
		{
			Close();
		}
		return;
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
	{
		FSlateApplication::Get().BringViewportToFront(&ViewportClient);
	}

	RenderTabBar();
	ImGui::Separator();

	const float AvailableHeight = ImGui::GetContentRegionAvail().y;

	switch (ActiveTab)
	{
	case EMeshEditorTab::Skeleton:
		RenderSkeletonLayout();
		break;
	case EMeshEditorTab::Mesh:
		RenderMeshLayout();
		break;
	case EMeshEditorTab::Animation:
		RenderAnimationLayout(AvailableHeight);
		break;
	case EMeshEditorTab::Physics:
		RenderPhysicsLayout();
		break;
	}

	ImGui::End();

	if (!bWindowOpen)
	{
		bPendingClose = true;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tab bar
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::RenderTabBar()
{
	// 언리얼 Persona 모드 툴바: 평평한 버튼 + 선택 시 액센트 밑줄.
	constexpr float BarHeight = 30.0f;
	ImDrawList*     DrawList  = ImGui::GetWindowDrawList();
	const ImVec2    BarPos    = ImGui::GetCursorScreenPos();
	const float     BarWidth  = ImGui::GetContentRegionAvail().x;
	DrawList->AddRectFilled(BarPos, ImVec2(BarPos.x + BarWidth, BarPos.y + BarHeight),
	                        IM_COL32(38, 38, 38, 255));

	auto TabButton = [&](const char* Label, const wchar_t* IconFile, EMeshEditorTab Tab)
	{
		const bool      bActive = (ActiveTab == Tab);
		constexpr float IconSz  = 18.0f;
		constexpr float PadX    = 14.0f;
		constexpr float Gap     = 8.0f;

		const ImVec2 Pos    = ImGui::GetCursorScreenPos();
		const ImVec2 TextSz = ImGui::CalcTextSize(Label);
		const float  Width  = PadX + IconSz + Gap + TextSz.x + PadX;

		ImGui::InvisibleButton(Label, ImVec2(Width, BarHeight));
		const bool bHovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked())
		{
			const EMeshEditorTab PreviousTab = ActiveTab;
			ActiveTab = Tab;
			if (PreviousTab != ActiveTab)
			{
				if (ActiveTab == EMeshEditorTab::Skeleton)
				{
					if (USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent())
					{
						Comp->ApplyBoneEditBasePose();
					}
				}
				if (PreviousTab == EMeshEditorTab::Physics)
				{
					PhysicsTabState.ShapeGizmoTarget.Unbind();
					if (UGizmoComponent* Gizmo = ViewportClient.GetGizmo())
						Gizmo->Deactivate();
				}
				// Physics 탭 진입: 직전 스켈레탈 본 기즈모가 남아 있으면 끄고,
				// 셰이프를 선택하기 전까지는 기즈모를 띄우지 않는다.
				if (ActiveTab == EMeshEditorTab::Physics)
				{
					PhysicsTabState.SelectedShapeType      = FPhysicsEditTabState::EShapeType::None;
					PhysicsTabState.SelectedShapeElemIndex = -1;
					PhysicsTabState.ShapeGizmoTarget.Unbind();
					if (UGizmoComponent* Gizmo = ViewportClient.GetGizmo())
						Gizmo->Deactivate();
				}
			}
		}

		if (bActive || bHovered)
		{
			DrawList->AddRectFilled(Pos, ImVec2(Pos.x + Width, Pos.y + BarHeight),
				bActive ? IM_COL32(41, 41, 41, 255) : IM_COL32(255, 255, 255, 20));
		}

		const float IconY = Pos.y + (BarHeight - IconSz) * 0.5f;
		if (ID3D11ShaderResourceView* Icon = LoadTabIcon(IconFile))
		{
			DrawList->AddImage(reinterpret_cast<ImTextureID>(Icon),
			                   ImVec2(Pos.x + PadX, IconY),
			                   ImVec2(Pos.x + PadX + IconSz, IconY + IconSz));
		}

		DrawList->AddText(ImVec2(Pos.x + PadX + IconSz + Gap, Pos.y + (BarHeight - TextSz.y) * 0.5f),
		                  bActive ? IM_COL32(255, 255, 255, 255) : IM_COL32(190, 190, 190, 255),
		                  Label);

		if (bActive)
		{
			DrawList->AddRectFilled(ImVec2(Pos.x, Pos.y + BarHeight - 2.0f),
			                        ImVec2(Pos.x + Width, Pos.y + BarHeight),
			                        IM_COL32(64, 132, 224, 255));
		}
		ImGui::SameLine(0.0f, 0.0f);
	};

	TabButton("Skeleton", L"Skeleton.png", EMeshEditorTab::Skeleton);
	TabButton("Mesh", L"SkeletalMesh.png", EMeshEditorTab::Mesh);
	TabButton("Animation", L"Animation.png", EMeshEditorTab::Animation);
	TabButton("Physics", L"Sphere_64x.png", EMeshEditorTab::Physics);

	ImGui::NewLine();
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared: viewport panel
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::RenderViewportPanel(ImVec2 Size)
{
	ImVec2 ViewportPos = ImGui::GetCursorScreenPos();
	ViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y);

	FViewport* VP = ViewportClient.GetViewport();
	if (!VP || Size.x <= 0 || Size.y <= 0)
	{
		ImGui::Dummy(Size);
		return;
	}

	VP->RequestResize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));

	if (VP->GetSRV())
	{
		ImGui::Image((ImTextureID)VP->GetSRV(), Size);

		// Physics 탭: 콜리전 셰이프 와이어프레임 오버레이
		if (ActiveTab == EMeshEditorTab::Physics)
		{
			ImDrawList* OverlayDL = ImGui::GetWindowDrawList();
			DrawPhysicsShapeOverlays(OverlayDL, ViewportPos, Size);
		}
	}
	else
	{
		ImGui::Dummy(Size);
	}

	// ImGui가 계산한 hover(다른 창에 가려지면 false)를 입력 소유권 중재에 보고.
	FSlateApplication::Get().SetViewportImGuiHovered(&ViewportClient, ImGui::IsItemHovered());

	constexpr float ToolbarHeight = 28.0f;
	ImDrawList*     DrawList      = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(ViewportPos, ImVec2(ViewportPos.x + Size.x, ViewportPos.y + ToolbarHeight), IM_COL32(40, 40, 40, 255));

	FViewportToolbarContext Context;
	Context.Renderer              = &GEngine->GetRenderer();
	Context.Gizmo                 = ViewportClient.GetGizmo();
	Context.Settings              = &FEditorSettings::Get().MeshEditorViewportSettings;
	Context.RenderOptions         = &ViewportClient.GetRenderOptions();
	Context.ToolbarLeft           = ViewportPos.x;
	Context.ToolbarTop            = ViewportPos.y;
	Context.ToolbarWidth          = Size.x;
	Context.bReservePlayStopSpace = false;
	Context.bShowAddActor         = false;
	Context.OnCoordSystemToggled  = [&]()
	{
		FGizmoToolSettings& Settings = FEditorSettings::Get().MeshEditorViewportSettings.Gizmo;
		Settings.CoordSystem         = (Settings.CoordSystem == EEditorCoordSystem::World) ? EEditorCoordSystem::Local : EEditorCoordSystem::World;
		ViewportClient.ApplyTransformSettingsToGizmo();
	};
	Context.OnSettingsChanged = [&]()
	{
		ViewportClient.ApplyTransformSettingsToGizmo();
	};
	Context.OnRenderViewModeExtras = [&]()
	{
		const EBoneDebugDrawMode CurrentBoneDrawMode = ViewportClient.GetBoneDebugDrawMode();
		int32                    BoneDrawMode        = static_cast<int32>(CurrentBoneDrawMode);
		ImGui::Text("Bone Display");
		ImGui::RadioButton("Selected Bone", &BoneDrawMode, static_cast<int32>(EBoneDebugDrawMode::SelectedOnly));
		ImGui::RadioButton("All Bones", &BoneDrawMode, static_cast<int32>(EBoneDebugDrawMode::AllBones));
		if (BoneDrawMode != static_cast<int32>(CurrentBoneDrawMode))
		{
			ViewportClient.SetBoneDebugDrawMode(static_cast<EBoneDebugDrawMode>(BoneDrawMode));
		}

		FViewportRenderOptions& RenderOptions = ViewportClient.GetRenderOptions();
		bool bWeightBoneHeatMap = RenderOptions.bWeightBoneHeatMap;
		if (ImGui::Checkbox("Weight Bone HeatMap", &bWeightBoneHeatMap))
		{
			RenderOptions.bWeightBoneHeatMap = bWeightBoneHeatMap;
			RenderOptions.WeightBoneHeatMapBoneIndex = SelectedBoneIndex;
			if (bWeightBoneHeatMap)
			{
				FShaderManager::Get().GetOrCreateUberLitPermutation(
					GetLightingModelForViewMode(RenderOptions.ViewMode),
					EUberLitDefines::EVertexFactory::SkeletalMesh,
					EShaderErrorMode::Notification,
					true);
			}
		}
	};

	FViewportToolbar::Render(Context);
	RenderMeshStatsOverlay(DrawList, ViewportPos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Skeleton tab
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::RenderSkeletonLayout()
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);

	// Left: bone hierarchy
	ImGui::BeginChild("BoneHierarchy", ImVec2(HierarchyWidth, 0), true);
	ImGui::Text("Bone Hierarchy");
	ImGui::Separator();
	if (SkeletalMesh)
	{
		const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
		if (Asset)
		{
			for (int32 i = 0; i < static_cast<int32>(Asset->Bones.size()); ++i)
			{
				if (Asset->Bones[i].ParentIndex == -1)
				{
					RenderBoneTree(Asset, i);
				}
			}
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// Splitter
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::Button("##skelSplitter", ImVec2(4.0f, -1.0f));
	if (ImGui::IsItemActive())
	{
		HierarchyWidth += ImGui::GetIO().MouseDelta.x;
		HierarchyWidth = std::max(100.0f, std::min(HierarchyWidth, ImGui::GetWindowWidth() - DetailsWidth - 100.0f));
	}
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}
	ImGui::PopStyleColor(3);

	ImGui::SameLine();

	// Center: viewport
	ImGui::BeginGroup();
	{
		float  ViewportWidth = ImGui::GetContentRegionAvail().x - DetailsWidth - ImGui::GetStyle().ItemSpacing.x;
		ImVec2 Size          = ImVec2(ViewportWidth, ImGui::GetContentRegionAvail().y);
		RenderViewportPanel(Size);
	}
	ImGui::EndGroup();

	ImGui::SameLine();

	// Right: bone details
	ImGui::BeginChild("BoneDetails", ImVec2(DetailsWidth, 0), true);
	ImGui::Text("Bone Details");
	ImGui::Separator();

	if (SkeletalMesh && SelectedBoneIndex != -1)
	{
		FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
		FBone& Bone = Asset->Bones[SelectedBoneIndex];

		ImGui::Text("Name: %s", Bone.Name.c_str());
		ImGui::Text("Index: %d", SelectedBoneIndex);
		ImGui::Dummy(ImVec2(0, 10));

		USkeletalMeshComponent* PreviewMeshComponent = ViewportClient.GetPreviewMeshComponent();
		FTransform LocalTransform = PreviewMeshComponent
			? PreviewMeshComponent->GetBoneEditBaseLocalTransformByIndex(SelectedBoneIndex)
			: FTransform(Bone.GetReferenceLocalPose());

		FVector Location = LocalTransform.Location;
		if (ImGui::DragFloat3("Location", &Location.X, 0.1f))
		{
			LocalTransform.Location = Location;
			if (PreviewMeshComponent)
				PreviewMeshComponent->SetBoneEditBaseLocalTransformByIndex(SelectedBoneIndex, LocalTransform);
			else
			{
				Bone.ReferenceLocalPose = LocalTransform.ToMatrix();
				Bone.SyncLegacyPoseDataFromSeparated();
			}
		}

		FVector Rotation = LocalTransform.GetRotator().ToVector();
		if (ImGui::DragFloat3("Rotation", &Rotation.X, 0.1f))
		{
			LocalTransform.SetRotation(FRotator(Rotation));
			if (PreviewMeshComponent)
				PreviewMeshComponent->SetBoneEditBaseLocalTransformByIndex(SelectedBoneIndex, LocalTransform);
			else
			{
				Bone.ReferenceLocalPose = LocalTransform.ToMatrix();
				Bone.SyncLegacyPoseDataFromSeparated();
			}
		}

		FVector Scale = LocalTransform.Scale;
		if (ImGui::DragFloat3("Scale", &Scale.X, 0.1f, 0.01f))
		{
			LocalTransform.Scale = Scale;
			if (PreviewMeshComponent)
				PreviewMeshComponent->SetBoneEditBaseLocalTransformByIndex(SelectedBoneIndex, LocalTransform);
			else
			{
				Bone.ReferenceLocalPose = LocalTransform.ToMatrix();
				Bone.SyncLegacyPoseDataFromSeparated();
			}
		}
	}
	else
	{
		ImGui::TextDisabled("Select a bone to edit.");
	}

	ImGui::EndChild();
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh tab
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::RenderMeshLayout()
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);

	// Left: mesh info
	const float StatsWidth = 220.0f;
	ImGui::BeginChild("MeshInfo", ImVec2(StatsWidth, 0), true);
	ImGui::Text("Mesh Info");
	ImGui::Separator();
	if (SkeletalMesh)
	{
		const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
		if (Asset)
		{
			ImGui::Text("Vertices:  %s", FormatMeshStatCount(Asset->Vertices.size()).c_str());
			ImGui::Text("Triangles: %s", FormatMeshStatCount(Asset->Indices.size() / 3).c_str());
			ImGui::Text("Bones:     %zu", Asset->Bones.size());
			ImGui::Text("Morphs:    %zu", Asset->MorphTargets.size());
			USkeletalMeshComponent* PreviewMeshComponent = ViewportClient.GetPreviewMeshComponent();
			if (!Asset->MorphTargets.empty() && PreviewMeshComponent)
			{
				ImGui::Dummy(ImVec2(0, 8));
				ImGui::Separator();
				ImGui::TextUnformatted("Morph Preview");
				if (ImGui::SmallButton("Reset Morphs"))
				{
					PreviewMeshComponent->ClearMorphTargetWeights();
				}
				for (int32 MorphIndex = 0; MorphIndex < static_cast<int32>(Asset->MorphTargets.size()); ++MorphIndex)
				{
					const FMorphTarget& MorphTarget = Asset->MorphTargets[MorphIndex];
					float               Weight      = PreviewMeshComponent->GetMorphTargetWeightByIndex(MorphIndex);
					ImGui::PushID(MorphIndex);
					const char* Label = MorphTarget.Name.empty() ? "Unnamed" : MorphTarget.Name.c_str();
					if (ImGui::SliderFloat(Label, &Weight, -1.0f, 1.0f, "%.3f"))
					{
						PreviewMeshComponent->SetMorphTargetWeightByIndex(MorphIndex, Weight);
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%zu vertex deltas", MorphTarget.Deltas.size());
					}
					ImGui::PopID();
				}
			}
			ImGui::Dummy(ImVec2(0, 8));
			const FString& Path = SkeletalMesh->GetAssetPathFileName();
			if (!Path.empty() && Path != "None")
			{
				ImGui::TextWrapped("Path:\n%s", Path.c_str());
			}
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// Center: viewport (full remaining width)
	ImGui::BeginGroup();
	{
		ImVec2 Size = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
		RenderViewportPanel(Size);
	}
	ImGui::EndGroup();
}

// ─────────────────────────────────────────────────────────────────────────────
// Animation tab
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::ApplyAnimationToComponent()
{
	USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
	if (!Comp || !AnimTabState.CurrentSequence)
	{
		return;
	}
	Comp->PlayAnimation(AnimTabState.CurrentSequence, /*bLooping=*/true);
	Comp->SetPlaying(false);
	Comp->SetPlayRate(1.0f);
	ResetMorphPreviewOverrides();
}

void FMeshEditorWidget::EnsureMorphPreviewOverrideSize()
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
	FSkeletalMesh* MeshAsset    = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
	const size_t   MorphCount   = MeshAsset ? MeshAsset->MorphTargets.size() : 0;
	if (AnimTabState.MorphPreviewWeights.size() != MorphCount)
	{
		AnimTabState.MorphPreviewWeights.assign(MorphCount, 0.0f);
	}
	if (AnimTabState.MorphPreviewOverrideMask.size() != MorphCount)
	{
		AnimTabState.MorphPreviewOverrideMask.assign(MorphCount, 0);
	}
}

void FMeshEditorWidget::ResetMorphPreviewOverrides()
{
	AnimTabState.MorphPreviewWeights.clear();
	AnimTabState.MorphPreviewOverrideMask.clear();
	AnimTabState.bMorphPreviewOverrideEnabled = false;
}

void FMeshEditorWidget::ApplyMorphPreviewOverrides(TArray<float>& InOutMorphWeights) const
{
	if (!AnimTabState.bMorphPreviewOverrideEnabled)
	{
		return;
	}
	const size_t Count = AnimTabState.MorphPreviewWeights.size();
	if (Count == 0 || AnimTabState.MorphPreviewOverrideMask.size() != Count)
	{
		return;
	}
	if (InOutMorphWeights.size() < Count)
	{
		InOutMorphWeights.resize(Count, 0.0f);
	}
	for (size_t Index = 0; Index < Count; ++Index)
	{
		if (AnimTabState.MorphPreviewOverrideMask[Index] != 0)
		{
			InOutMorphWeights[Index] = AnimTabState.MorphPreviewWeights[Index];
		}
	}
}

void FMeshEditorWidget::RefreshAnimationPreviewPose()
{
	USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
	if (!Comp) return;
	UAnimSingleNodeInstance* NodeInst = Comp->GetAnimNodeInstance(FName::None);
	if (!NodeInst) return;
	USkeletalMesh* Mesh = Comp->GetSkeletalMesh();
	if (!Mesh) return;
	FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
	if (!Asset || Asset->Bones.empty()) return;

	FPoseContext Out;
	Out.SkeletalMesh = Mesh;
	Out.Pose.resize(Asset->Bones.size());
	Out.ResetToRefPose();
	NodeInst->EvaluatePose(Out);
	ApplyMorphPreviewOverrides(Out.MorphWeights);
	Comp->SetAnimationPose(Out.Pose, Out.MorphWeights);
}

void FMeshEditorWidget::MarkAnimationListDirty()
{
	AnimTabState.bAnimationListDirty = true;
}

const TArray<FAssetListItem>& FMeshEditorWidget::GetCachedAnimationFilesForCurrentSkeleton()
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
	FSkeletonBinding CurrentBinding;

	if (SkeletalMesh)
	{
		CurrentBinding = SkeletalMesh->GetSkeletonBinding();
	}
	else
	{
		CurrentBinding.Reset();
	}

	if (AnimTabState.bAnimationListDirty ||
		!IsSameSkeletonBindingForAnimationList(AnimTabState.CachedAnimationListBinding, CurrentBinding))
	{
		AnimTabState.CachedAnimationFiles.clear();
		AnimTabState.CachedAnimationListBinding = CurrentBinding;

		if (SkeletalMesh)
		{
			AnimTabState.CachedAnimationFiles = FAssetRegistry::ListAnimationsForSkeleton(CurrentBinding, false);
		}

		AnimTabState.bAnimationListDirty = false;
	}

	return AnimTabState.CachedAnimationFiles;
}

void FMeshEditorWidget::RenderAnimationLayout(float TotalHeight)
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);

	constexpr float TimelineHeight = 210.0f;
	const float     ContentHeight  = TotalHeight - TimelineHeight - ImGui::GetStyle().ItemSpacing.y * 3.0f;

	// ─── Top: Asset Details | Viewport | Asset Browser (Persona 배치) ───

	// Left: 시퀀스 / 몽타주 디테일 패널 (선택 종류에 따라 분기)
	ImGui::BeginChild("AssetDetails", ImVec2(AnimTabState.AnimDetailsWidth, ContentHeight), true);
	if (AnimTabState.bMontageSelected && AnimTabState.CurrentMontage)
	{
		USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
		UAnimInstance* AnimInst = Comp ? Comp->GetAnimInstance() : nullptr;
		FAnimMontagePropertyPanel::Render(AnimTabState.CurrentMontage, Comp, AnimInst);
	}
	else if (AnimTabState.CurrentSequence)
	{
		UAnimSequence* Seq = AnimTabState.CurrentSequence;
		// Notify entry 가 타임라인에서 선택되어 있으면 Notify 의 UPROPERTY 편집 UI 를 표시.
		// 아니면 기존 시퀀스 메타 + Root Motion 패널.
		const int32 NotifyCount = static_cast<int32>(Seq->GetNotifies().size());
		const bool bShowNotifyDetails =
			AnimTabState.SelectedNotifyIndex >= 0 &&
			AnimTabState.SelectedNotifyIndex < NotifyCount;
		const bool bShowMorphDetails = AnimTabState.SelectedMorphCurveIndex >= 0 && AnimTabState.SelectedMorphCurveIndex
		< static_cast<int32>(Seq->GetMorphTargetCurves().size());

		if (bShowNotifyDetails)
		{
			FAnimationTimelinePanel::RenderNotifyDetails(Seq, AnimTabState.SelectedNotifyIndex);
		}
		else if (bShowMorphDetails)
		{
			if (FAnimationTimelinePanel::RenderMorphDetails(
				Seq,
				SkeletalMesh,
				AnimTabState.SelectedMorphCurveIndex,
				AnimTabState.SelectedMorphKeyIndex
			))
			{
				RefreshAnimationPreviewPose();
			}
		}
		else
		{
			ImGui::TextUnformatted("Asset Details");
			ImGui::Separator();
			ImGui::Text("Name:   %s", Seq->GetName().c_str());
			ImGui::Text("Length: %.3f s", Seq->GetPlayLength());
			ImGui::Text("FPS:    %.1f", Seq->GetFrameRate());
			ImGui::Text("Frames: %d", Seq->GetNumberOfFrames());
			ImGui::Dummy(ImVec2(0, 6));
			const FString& Path = Seq->GetAssetPathFileName();
			if (!Path.empty() && Path != "None")
			{
				ImGui::TextWrapped("Path:\n%s", Path.c_str());
			}

			// AnimSequence property 패널 — root motion 등 편집 가능한 항목.
			ImGui::Dummy(ImVec2(0, 12));
			FAnimSequencePropertyPanel::Render(Seq);

			USkeletalMeshComponent* PreviewMeshComponent = ViewportClient.GetPreviewMeshComponent();
			USkeletalMesh* PreviewMesh = PreviewMeshComponent ? PreviewMeshComponent->GetSkeletalMesh() : SkeletalMesh;
			FSkeletalMesh* MeshAsset = PreviewMesh ? PreviewMesh->GetSkeletalMeshAsset() : nullptr;
			if (MeshAsset && !MeshAsset->MorphTargets.empty())
			{
				ImGui::Dummy(ImVec2(0, 12));
				ImGui::Separator();
				ImGui::TextUnformatted("Morph Preview / Keys");
				EnsureMorphPreviewOverrideSize();
				if (ImGui::SmallButton("Clear Morph Preview"))
				{
					ResetMorphPreviewOverrides();
					RefreshAnimationPreviewPose();
				}
				for (int32 MorphIndex = 0; MorphIndex < static_cast<int32>(MeshAsset->MorphTargets.size()); ++
				     MorphIndex)
				{
					const FMorphTarget& MorphTarget   = MeshAsset->MorphTargets[MorphIndex];
					float               CurrentWeight = 0.0f;
					if (MorphIndex < static_cast<int32>(AnimTabState.MorphPreviewWeights.size()) && AnimTabState.
						MorphPreviewOverrideMask[MorphIndex] != 0)
					{
						CurrentWeight = AnimTabState.MorphPreviewWeights[MorphIndex];
					}
					else if (PreviewMeshComponent)
					{
						CurrentWeight = PreviewMeshComponent->GetMorphTargetWeightByIndex(MorphIndex);
					}

					ImGui::PushID(MorphIndex);
					const char* Label = MorphTarget.Name.empty() ? "Unnamed" : MorphTarget.Name.c_str();
					if (ImGui::SliderFloat(Label, &CurrentWeight, -1.0f, 1.0f, "%.3f"))
					{
						AnimTabState.MorphPreviewWeights[MorphIndex]      = CurrentWeight;
						AnimTabState.MorphPreviewOverrideMask[MorphIndex] = 1;
						AnimTabState.bMorphPreviewOverrideEnabled         = true;
						RefreshAnimationPreviewPose();
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Key"))
					{
						FMorphTargetCurve& Curve = FindOrAddMorphCurve(Seq, MorphTarget.Name);
						AddOrUpdateMorphCurveKey(
							Curve,
							PreviewMeshComponent && PreviewMeshComponent->GetAnimNodeInstance(FName::None)
							? PreviewMeshComponent->GetAnimNodeInstance(FName::None)->GetCurrentTime() : 0.0f,
							CurrentWeight
						);
						AnimTabState.MorphPreviewOverrideMask[MorphIndex] = 0;
						bool bAnyOverride                                 = false;
						for (uint8 Mask : AnimTabState.MorphPreviewOverrideMask)
						{
							if (Mask != 0)
							{
								bAnyOverride = true;
								break;
							}
						}
						AnimTabState.bMorphPreviewOverrideEnabled = bAnyOverride;
						FAnimationManager::Get().SaveAnimationPreservingMetadata(Seq);
						RefreshAnimationPreviewPose();
					}
					ImGui::PopID();
				}
			}
		}
	}
	else
	{
		ImGui::TextUnformatted("Asset Details");
		ImGui::Separator();
		ImGui::TextDisabled("No animation selected.");
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// Center: viewport
	ImGui::BeginGroup();
	{
		float  ViewportWidth = ImGui::GetContentRegionAvail().x - AnimTabState.AnimListWidth - ImGui::GetStyle().ItemSpacing.x;
		ImVec2 Size          = ImVec2(ViewportWidth, ContentHeight);
		RenderViewportPanel(Size);
	}
	ImGui::EndGroup();

	ImGui::SameLine();

	// Right: 에셋 브라우저 (애니메이션 목록)
	ImGui::BeginChild("AssetBrowser", ImVec2(AnimTabState.AnimListWidth, ContentHeight), true);
	ImGui::TextUnformatted("Asset Browser");
	ImGui::Separator();

	if (ImGui::Button("Load...", ImVec2(-1.0f, 0.0f)))
	{
		FEditorFileDialogOptions Opts;
		Opts.Filter                       = L"Animation Files (*.uasset)\0*.uasset\0All Files (*.*)\0*.*\0";
		Opts.Title                        = L"Load Animation";
		Opts.bReturnRelativeToProjectRoot = true;
		FString Path                      = FEditorFileUtils::OpenFileDialog(Opts);
		if (!Path.empty())
		{
			UAnimSequence* Seq = FAnimationManager::Get().LoadAnimation(Path);
			if (Seq && Seq->IsCompatibleWith(SkeletalMesh))
			{
				AnimTabState.CurrentSequence         = Seq;
				AnimTabState.SelectedAnimIndex       = -1;
				AnimTabState.SelectedNotifyIndex     = -1;
				AnimTabState.SelectedMorphCurveIndex = -1;
				AnimTabState.SelectedMorphKeyIndex   = -1;
				ApplyAnimationToComponent();
			}
		}
	}

	if (ImGui::Button("Import Animation FBX", ImVec2(-1.0f, 0.0f)))
	{
		FEditorFileDialogOptions Opts;
		Opts.Filter                       = L"FBX Files (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
		Opts.Title                        = L"Import Animation FBX";
		Opts.bReturnRelativeToProjectRoot = true;
		FString Path                      = FEditorFileUtils::OpenFileDialog(Opts);
		if (!Path.empty())
		{
			FFbxImportOptionsDialog::BeginAnimationImport(AnimTabState.AnimationImportDialog, Path);
		}
	}

	if (ImGui::Button("+ New Morph Animation", ImVec2(-1.0f, 0.0f)) && SkeletalMesh)
	{
		UAnimSequence*  Seq       = UObjectManager::Get().CreateObject<UAnimSequence>();
		UAnimDataModel* DataModel = UObjectManager::Get().CreateObject<UAnimDataModel>(Seq);
		DataModel->SetTiming(1.0f, 30.0f, 0);
		Seq->SetDataModel(DataModel);
		Seq->SetSkeletonBinding(SkeletalMesh->GetSkeletonBinding());
		Seq->SetFName(FName("MorphAnimation"));
		const FString AnimPath = FAnimationManager::GetAnimationPathForSkeleton(
			SkeletalMesh->GetAssetPathFileName(),
			"MorphAnimation",
			SkeletalMesh->GetSkeletonBinding().SkeletonPath
		);
		if (FAnimationManager::Get().SaveAnimation(Seq, AnimPath, SkeletalMesh->GetAssetPathFileName()))
		{
			AnimTabState.CurrentSequence         = Seq;
			AnimTabState.SelectedAnimIndex       = -1;
			AnimTabState.SelectedNotifyIndex     = -1;
			AnimTabState.SelectedMorphCurveIndex = -1;
			AnimTabState.SelectedMorphKeyIndex   = -1;
			ApplyAnimationToComponent();
			FAnimationManager::Get().RefreshAvailableAnimations();
			MarkAnimationListDirty();
		}
	}

	FAnimationImportRequest      AnimationImportRequest;
	const EFbxImportDialogResult AnimationImportDialogResult = FFbxImportOptionsDialog::RenderAnimationImportPopup(
		"Import Animation FBX Options",
		AnimTabState.AnimationImportDialog,
		SkeletalMesh ? SkeletalMesh->GetSkeletonBinding().SkeletonPath : FString("None"),
		AnimationImportRequest
	);

	if (AnimationImportDialogResult == EFbxImportDialogResult::Submitted)
	{
		TArray<UAnimSequence*> ImportedSequences;
		FAnimationManager::Get().ImportAnimationForSkeleton(AnimationImportRequest, &ImportedSequences);
		// 임포트 성공/스킵(이미 존재) 무관하게 디스크를 다시 스캔해 목록 갱신.
		FAnimationManager::Get().RefreshAvailableAnimations();
		MarkAnimationListDirty();
		if (!ImportedSequences.empty())
		{
			AnimTabState.CurrentSequence         = ImportedSequences[0];
			AnimTabState.SelectedAnimIndex       = -1;
			AnimTabState.SelectedNotifyIndex     = -1;
			AnimTabState.SelectedMorphCurveIndex = -1;
			AnimTabState.SelectedMorphKeyIndex   = -1;
			ApplyAnimationToComponent();
			FFbxImportOptionsDialog::RequestClose(AnimTabState.AnimationImportDialog);
		}
		else
		{
			AnimTabState.AnimationImportDialog.Error =
			"No animation was imported. Existing assets may have been skipped.";
		}
	}

	ImGui::Separator();

	if (ImGui::SmallButton("Refresh Animation List"))
	{
		FAnimationManager::Get().RefreshAvailableAnimations();
		FAnimationManager::Get().RefreshAvailableMontages();
		MarkAnimationListDirty();
	}

	// 디스크 스캔 — montage 목록 초기화 (최초 1회 + Refresh 시).
	static bool sMontagesScanned = false;
	if (!sMontagesScanned)
	{
		FAnimationManager::Get().RefreshAvailableMontages();
		sMontagesScanned = true;
	}

	const TArray<FAssetListItem>& AnimFiles     = GetCachedAnimationFilesForCurrentSkeleton();
	const TArray<FAssetListItem>& MontageFiles  = FAnimationManager::Get().GetAvailableMontageFiles();

	// asset 경로의 stem (확장자/디렉토리 제거) — 자동 montage 이름의 source 식별자.
	auto ExtractStem = [](const FString& Path) -> FString
	{
		const size_t LastSlash = Path.find_last_of("/\\");
		const size_t Start = (LastSlash == FString::npos) ? 0 : LastSlash + 1;
		const size_t LastDot = Path.find_last_of('.');
		const size_t End = (LastDot == FString::npos || LastDot < Start) ? Path.size() : LastDot;
		return Path.substr(Start, End - Start);
	};

	// + New Montage — 현재 선택된 sequence 가 있으면 source 로 새 montage 생성.
	// 이름은 sequence 의 asset path stem 사용 (UObject::GetName() 의 자동생성 ObjectName 회피).
	const bool bCanCreateMontage = (AnimTabState.CurrentSequence != nullptr) && !AnimTabState.bMontageSelected;
	if (!bCanCreateMontage) ImGui::BeginDisabled();
	if (ImGui::Button("+ New Montage (from selected sequence)", ImVec2(-1.0f, 0.0f)))
	{
		const FString Stem = ExtractStem(AnimTabState.CurrentSequence->GetAssetPathFileName());
		const FString MontageName = Stem + "_Montage";
		const FString PackagePath = FString("Content/Montages/") + MontageName + ".uasset";
		UAnimMontage* Montage = FAnimationManager::Get().CreateMontage(AnimTabState.CurrentSequence, MontageName);
		if (Montage)
		{
			FAnimationManager::Get().SaveMontage(Montage, PackagePath);
			FAnimationManager::Get().RefreshAvailableMontages();
			AnimTabState.CurrentMontage    = Montage;
			AnimTabState.bMontageSelected  = true;

			// 새 montage 의 인덱스 즉시 매핑 — list 의 hilight + 다음 클릭의 일관 동작 보장.
			const TArray<FAssetListItem>& Updated = FAnimationManager::Get().GetAvailableMontageFiles();
			AnimTabState.SelectedMontageIndex = -1;
			for (int32 j = 0; j < static_cast<int32>(Updated.size()); ++j)
			{
				if (Updated[j].FullPath == PackagePath)
				{
					AnimTabState.SelectedMontageIndex = j;
					break;
				}
			}
		}
	}
	if (!bCanCreateMontage) ImGui::EndDisabled();

	// 통합 리스트 — Sequence + Montage 한 selectable. 알파벳 정렬 (Walking_mixamo_com 옆에
	// Walking_mixamo_com_Montage 가 자연스럽게 인접). 시각 구분: Montage 는 노랑 + [M] prefix.
	struct FEntry
	{
		FString  DisplayName;
		FString  FullPath;
		bool     bIsMontage = false;
		int32    OriginalIndex = -1;   // AnimFiles 또는 MontageFiles 의 인덱스
	};
	TArray<FEntry> Entries;
	Entries.reserve(AnimFiles.size() + MontageFiles.size());
	for (int32 i = 0; i < static_cast<int32>(AnimFiles.size());    ++i) Entries.push_back({ AnimFiles[i].DisplayName,    AnimFiles[i].FullPath,    false, i });
	for (int32 i = 0; i < static_cast<int32>(MontageFiles.size()); ++i) Entries.push_back({ MontageFiles[i].DisplayName, MontageFiles[i].FullPath, true,  i });
	std::sort(Entries.begin(), Entries.end(),
		[](const FEntry& A, const FEntry& B) { return A.DisplayName < B.DisplayName; });

	ImGui::TextUnformatted("Animations & Montages");
	for (const FEntry& E : Entries)
	{
		const bool bSelected =
			E.bIsMontage
				? (AnimTabState.bMontageSelected && AnimTabState.SelectedMontageIndex == E.OriginalIndex)
				: (!AnimTabState.bMontageSelected && AnimTabState.SelectedAnimIndex == E.OriginalIndex);

		// 시각 구분 — Montage 는 노랑 톤. Sequence 는 기본 색.
		const ImU32 Color = E.bIsMontage ? IM_COL32(255, 200, 100, 255) : IM_COL32(255, 255, 255, 255);
		ImGui::PushStyleColor(ImGuiCol_Text, Color);

		const FString Label = (E.bIsMontage ? "[M] " : "      ") + E.DisplayName;
		if (ImGui::Selectable(Label.c_str(), bSelected))
		{
			if (E.bIsMontage)
			{
				AnimTabState.SelectedMontageIndex    = E.OriginalIndex;
				AnimTabState.bMontageSelected        = true;
				AnimTabState.SelectedNotifyIndex     = -1;
				AnimTabState.SelectedMorphCurveIndex = -1;
				AnimTabState.SelectedMorphKeyIndex   = -1;
				ResetMorphPreviewOverrides();
				if (UAnimMontage* M = FAnimationManager::Get().LoadMontage(E.FullPath))
				{
					AnimTabState.CurrentMontage = M;
				}
			}
			else
			{
				AnimTabState.SelectedAnimIndex       = E.OriginalIndex;
				AnimTabState.bMontageSelected        = false;
				AnimTabState.SelectedNotifyIndex     = -1;
				AnimTabState.SelectedMorphCurveIndex = -1;
				AnimTabState.SelectedMorphKeyIndex   = -1;
				if (UAnimSequence* Seq = FAnimationManager::Get().LoadAnimation(E.FullPath))
				{
					if (Seq->IsCompatibleWith(SkeletalMesh))
					{
						AnimTabState.CurrentSequence = Seq;
						ApplyAnimationToComponent();
					}
				}
			}
		}
		ImGui::PopStyleColor();

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s\n%s", E.bIsMontage ? "Montage" : "Sequence", E.FullPath.c_str());
		}
	}
	ImGui::EndChild();

	// ─── Bottom: Unreal 시퀀서 패널 ───
	UAnimSingleNodeInstance* NodeInst = nullptr;
	USkeletalMeshComponent*  Comp     = ViewportClient.GetPreviewMeshComponent();
	if (Comp && AnimTabState.CurrentSequence)
	{
		NodeInst = Comp->GetAnimNodeInstance(FName::None);
	}

	// 스페이스바: 재생/정지 토글 (메시 에디터 창 포커스 + 텍스트 입력 중 아닐 때)
	if (Comp && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
	    !ImGui::GetIO().WantTextInput &&
	    ImGui::IsKeyPressed(ImGuiKey_Space, false))
	{
		const bool bPlaying = NodeInst && NodeInst->IsPlaying();
		Comp->SetPlaying(!bPlaying);
	}

	FAnimationTimelinePanel::Render(NodeInst, Comp, AnimTabState.CurrentSequence, TimelineHeight,
		AnimTabState.SelectedNotifyIndex,
		AnimTabState.SelectedMorphCurveIndex,
		AnimTabState.SelectedMorphKeyIndex
	);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh stats overlay
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::RenderMeshStatsOverlay(ImDrawList* DrawList, const ImVec2& ViewportPos) const
{
	if (!DrawList || !EditedObject)
	{
		return;
	}

	size_t VertexCount   = 0;
	size_t TriangleCount = 0;
	size_t IndexCount    = 0;
	double ImportSeconds = -1.0;

	if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject))
	{
		if (const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset())
		{
			VertexCount   = Asset->Vertices.size();
			IndexCount    = Asset->Indices.size();
			TriangleCount = Asset->Indices.size() / 3;
		}
		ImportSeconds = GetRecordedImportDurationSeconds(SkeletalMesh);
	}

	FString Text =
		"Triangles: " + FormatMeshStatCount(TriangleCount) + "\n" +
		"Vertices: " + FormatMeshStatCount(VertexCount) + "\n" +
		"Indices: " + FormatMeshStatCount(IndexCount);

	if (ImportSeconds >= 0.0)
	{
		Text += "\nImport Time: " + FormatMeshStatSeconds(ImportSeconds);
	}

	const ImVec2 TextPos(ViewportPos.x + 8.0f, ViewportPos.y + 36.0f);
	DrawList->AddText(ImVec2(TextPos.x + 1.0f, TextPos.y + 1.0f), IM_COL32(0, 0, 0, 220), Text.c_str());
	DrawList->AddText(TextPos, IM_COL32(235, 238, 242, 255), Text.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Bone tree (Skeleton tab)
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::RenderBoneTree(const FSkeletalMesh* Asset, int32 Index)
{
	const FBone& Bone = Asset->Bones[Index];

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;

	if (Index == SelectedBoneIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	bool bHasChildren = false;
	for (int32 i = Index + 1; i < static_cast<int32>(Asset->Bones.size()); ++i)
	{
		if (Asset->Bones[i].ParentIndex == Index)
		{
			bHasChildren = true;
			break;
		}
	}

	if (!bHasChildren)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	bool bOpen = ImGui::TreeNodeEx(Bone.Name.c_str(), Flags);

	if (ImGui::IsItemClicked())
	{
		SelectedBoneIndex = Index;
		ViewportClient.SetSelectedBone(Cast<USkeletalMesh>(EditedObject), Index);
	}

	if (bOpen && bHasChildren)
	{
		for (int32 i = Index + 1; i < static_cast<int32>(Asset->Bones.size()); ++i)
		{
			if (Asset->Bones[i].ParentIndex == Index)
			{
				RenderBoneTree(Asset, i);
			}
		}
		ImGui::TreePop();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Physics tab
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::RenderPhysicsLayout()
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
	const FSkeletalMesh* Asset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;

	// PhysicsAsset 로드 우선순위:
	//   1) 메시에 이미 연결된 포인터 (이번 세션에서 Save 한 것)
	//   2) 파일에서 로드 (이전 세션에서 저장된 것)
	//   3) 없으면 빈 것 새로 생성
	if (!PhysicsTabState.PhysicsAsset)
	{
		if (SkeletalMesh && SkeletalMesh->PhysicsAsset)
		{
			PhysicsTabState.PhysicsAsset = SkeletalMesh->PhysicsAsset;
		}
		else if (SkeletalMesh)
		{
			const FString MeshPath = SkeletalMesh->GetAssetPathFileName();
			if (!MeshPath.empty() && MeshPath != "None")
			{
				const FString PhysicsPath = MakePhysicsAssetPath(MeshPath);
				if (UPhysicsAsset* Loaded = LoadPhysicsAssetFromFile(PhysicsPath))
				{
					PhysicsTabState.PhysicsAsset    = Loaded;
					SkeletalMesh->PhysicsAsset = Loaded;
				}
			}
		}

		if (!PhysicsTabState.PhysicsAsset)
			PhysicsTabState.PhysicsAsset = new UPhysicsAsset();
	}
	UPhysicsAsset* PhysicsAsset = PhysicsTabState.PhysicsAsset;

	// ── 툴바 ─────────────────────────────────────────────────
	if (IsDirty()) ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f), "●");
	else           ImGui::TextDisabled("●");
	ImGui::SameLine();
	if (ImGui::SmallButton("Save")) SavePhysicsAsset();
	ImGui::SameLine();
	ImGui::TextDisabled("Ctrl+S");

	// ── 왼쪽: 본 트리 ────────────────────────────────────────
	ImGui::BeginChild("##PhysBoneTree", ImVec2(PhysicsTabState.BoneTreeWidth, 0), true);
	ImGui::Text("Skeleton");
	ImGui::Separator();

	if (Asset)
	{
		for (int32 i = 0; i < (int32)Asset->Bones.size(); ++i)
		{
			if (Asset->Bones[i].ParentIndex == -1)
			{
				RenderPhysicsBoneTree(Asset, i);
			}
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Text("Constraints (%d)", (int32)PhysicsAsset->GetConstraints().size());
	ImGui::Separator();

	const auto& Constraints = PhysicsAsset->GetConstraints();
	for (int32 i = 0; i < (int32)Constraints.size(); ++i)
	{
		UPhysicsConstraintSetup* CS = Constraints[i];
		FString Label = CS->ParentBoneName + " -> " + CS->ChildBoneName;
		bool bSelected = (PhysicsTabState.SelectedConstraintIndex == i);
		ImGui::PushID(i + 10000);
		if (ImGui::Selectable(Label.c_str(), bSelected))
		{
			PhysicsTabState.SelectedConstraintIndex = i;
			PhysicsTabState.SelectedBodySetupIndex  = -1;
		}
		// Constraint 항목 우클릭 → 삭제 메뉴
		if (ImGui::BeginPopupContextItem("##ConstraintCtx"))
		{
			if (ImGui::MenuItem("Delete Constraint"))
			{
				PhysicsAsset->RemoveConstraintSetup(CS->ParentBoneName, CS->ChildBoneName);
				if (PhysicsTabState.SelectedConstraintIndex >= (int32)PhysicsAsset->GetConstraints().size())
					PhysicsTabState.SelectedConstraintIndex = -1;
				MarkDirty();
				ImGui::EndPopup();
				ImGui::PopID();
				break;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// ── 중앙: 뷰포트 ─────────────────────────────────────────
	ImGui::BeginGroup();
	{
		float ViewportWidth = ImGui::GetContentRegionAvail().x - PhysicsTabState.DetailsWidth - ImGui::GetStyle().ItemSpacing.x;
		RenderViewportPanel(ImVec2(ViewportWidth, ImGui::GetContentRegionAvail().y));
	}
	ImGui::EndGroup();

	ImGui::SameLine();

	// ── 오른쪽: 디테일 패널 ──────────────────────────────────
	ImGui::BeginChild("##PhysDetails", ImVec2(PhysicsTabState.DetailsWidth, 0), true);
	RenderPhysicsDetailsPanel();
	ImGui::EndChild();
}

void FMeshEditorWidget::RenderPhysicsBoneTree(const FSkeletalMesh* Asset, int32 BoneIndex)
{
	if (!Asset) return;

	const FBone& Bone       = Asset->Bones[BoneIndex];
	UPhysicsAsset* PhysicsAsset = PhysicsTabState.PhysicsAsset;
	const bool bHasBody     = PhysicsAsset && PhysicsAsset->FindBodySetup(Bone.Name) != nullptr;
	const bool bHasParent   = Bone.ParentIndex >= 0;

	// BodySetup 있는 본 → 초록, 없는 본 → 기본색
	if (bHasBody) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.f, 0.4f, 1.f));

	bool bHasChildren = false;
	for (int32 i = BoneIndex + 1; i < (int32)Asset->Bones.size(); ++i)
	{
		if (Asset->Bones[i].ParentIndex == BoneIndex) { bHasChildren = true; break; }
	}

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	if (SelectedBoneIndex == BoneIndex) Flags |= ImGuiTreeNodeFlags_Selected;
	if (!bHasChildren)                  Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	ImGui::PushID(BoneIndex);
	bool bOpen = ImGui::TreeNodeEx(Bone.Name.c_str(), Flags);
	if (bHasBody) ImGui::PopStyleColor();

	// ── 좌클릭: 본 선택 ────────────────────────────────────────
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		SelectedBoneIndex = BoneIndex;
		// Physics 탭에서는 기즈모를 본에 붙이지 않음 (셰이프 선택 시에만 활성화)
		ViewportClient.SetSelectedBone(Cast<USkeletalMesh>(EditedObject), -1);

		PhysicsTabState.SelectedBodySetupIndex  = -1;
		PhysicsTabState.SelectedConstraintIndex = -1;
		PhysicsTabState.SelectedShapeType       = FPhysicsEditTabState::EShapeType::None;
		PhysicsTabState.SelectedShapeElemIndex  = -1;
		PhysicsTabState.ShapeGizmoTarget.Unbind();

		if (PhysicsAsset)
		{
			const auto& Bodies = PhysicsAsset->GetBodySetups();
			for (int32 i = 0; i < (int32)Bodies.size(); ++i)
			{
				if (Bodies[i] && Bodies[i]->BoneName == Bone.Name)
				{
					PhysicsTabState.SelectedBodySetupIndex = i;
					break;
				}
			}
		}
	}

	// ── 우클릭: 컨텍스트 메뉴 ────────────────────────────────
	if (ImGui::BeginPopupContextItem("##BoneCtx"))
	{
		// 이 메뉴를 열면 해당 본도 선택
		SelectedBoneIndex = BoneIndex;

		// ── Add Body (서브메뉴로 모양 선택) ──────────────────
		if (ImGui::BeginMenu(bHasBody ? "Add Shape to Body" : "Add Body"))
		{
			using EShapeType = FPhysicsEditTabState::EShapeType;
			// AddShapeFn 으로 셰이프를 추가하고, 방금 추가한 요소를 곧바로 선택해
			// 그 중심에 기즈모가 뜨도록 한다.
			auto AddBodyWithShape = [&](EShapeType ShapeType, auto AddShapeFn, auto ElemCountFn)
			{
				UBodySetup* Setup = PhysicsAsset->GetOrCreateBodySetup(Bone.Name);
				AddShapeFn(Setup->AggregateGeom);
				// 선택 상태 갱신
				const auto& Bodies = PhysicsAsset->GetBodySetups();
				for (int32 i = 0; i < (int32)Bodies.size(); ++i)
				{
					if (Bodies[i] && Bodies[i]->BoneName == Bone.Name)
					{
						PhysicsTabState.SelectedBodySetupIndex  = i;
						PhysicsTabState.SelectedConstraintIndex = -1;
						break;
					}
				}
				// 방금 추가한 셰이프 요소를 선택 → 중심에 기즈모 표시
				PhysicsTabState.SelectedShapeType      = ShapeType;
				PhysicsTabState.SelectedShapeElemIndex = ElemCountFn(Setup->AggregateGeom) - 1;
				UpdatePhysicsShapeGizmo();
			};

			if (ImGui::MenuItem("  Sphere"))
			{
				AddBodyWithShape(EShapeType::Sphere,
					[](FKAggregateGeom& G) { G.SphereElems.push_back(FKSphereElem{}); },
					[](FKAggregateGeom& G) { return (int32)G.SphereElems.size(); });
				MarkDirty();
			}
			if (ImGui::MenuItem("  Box"))
			{
				AddBodyWithShape(EShapeType::Box,
					[](FKAggregateGeom& G) { G.BoxElems.push_back(FKBoxElem{}); },
					[](FKAggregateGeom& G) { return (int32)G.BoxElems.size(); });
				MarkDirty();
			}
			if (ImGui::MenuItem("  Capsule"))
			{
				AddBodyWithShape(EShapeType::Capsule,
					[](FKAggregateGeom& G) { G.CapsuleElems.push_back(FKCapsuleElem{}); },
					[](FKAggregateGeom& G) { return (int32)G.CapsuleElems.size(); });
				MarkDirty();
			}
			ImGui::EndMenu();
		}

		// ── Add Constraint to Parent ──────────────────────────
		if (bHasParent)
		{
			const FString& ParentName = Asset->Bones[Bone.ParentIndex].Name;
			FString ConstraintLabel   = "Add Constraint  (" + ParentName + " -> " + Bone.Name + ")";
			if (ImGui::MenuItem(ConstraintLabel.c_str()))
			{
				PhysicsAsset->GetOrCreateConstraintSetup(ParentName, Bone.Name);
				PhysicsTabState.SelectedConstraintIndex = (int32)PhysicsAsset->GetConstraints().size() - 1;
				PhysicsTabState.SelectedBodySetupIndex  = -1;
				MarkDirty();
			}
		}

		// ── Delete Body ───────────────────────────────────────
		if (bHasBody)
		{
			ImGui::Separator();
			if (ImGui::MenuItem("Delete Body"))
			{
				PhysicsAsset->RemoveBodySetup(Bone.Name);
				PhysicsTabState.SelectedBodySetupIndex = -1;
				MarkDirty();
			}
		}

		ImGui::EndPopup();
	}

	ImGui::PopID();

	if (bOpen && bHasChildren)
	{
		for (int32 i = BoneIndex + 1; i < (int32)Asset->Bones.size(); ++i)
		{
			if (Asset->Bones[i].ParentIndex == BoneIndex)
			{
				RenderPhysicsBoneTree(Asset, i);
			}
		}
		ImGui::TreePop();
	}
}

void FMeshEditorWidget::RenderPhysicsDetailsPanel()
{
	UPhysicsAsset* PhysicsAsset = PhysicsTabState.PhysicsAsset;
	if (!PhysicsAsset) { ImGui::TextDisabled("No PhysicsAsset."); return; }

	if (PhysicsTabState.SelectedBodySetupIndex >= 0 &&
		PhysicsTabState.SelectedBodySetupIndex < (int32)PhysicsAsset->GetBodySetups().size())
	{
		RenderBodySetupDetails(PhysicsAsset->GetBodySetupsMutable()[PhysicsTabState.SelectedBodySetupIndex]);
	}
	else if (PhysicsTabState.SelectedConstraintIndex >= 0 &&
		PhysicsTabState.SelectedConstraintIndex < (int32)PhysicsAsset->GetConstraints().size())
	{
		RenderConstraintDetails(PhysicsAsset->GetConstraintsMutable()[PhysicsTabState.SelectedConstraintIndex]);
	}
	else
	{
		ImGui::TextDisabled("Select a bone or constraint.");
	}
}

void FMeshEditorWidget::RenderBodySetupDetails(UBodySetup* Setup)
{
	if (!Setup) return;

	ImGui::Text("Body: %s", Setup->BoneName.c_str());
	ImGui::Separator();

	if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::DragFloat("Mass (kg)",       &Setup->Mass,           0.01f, 0.001f, 1000.f)) MarkDirty();
		if (ImGui::DragFloat("Linear Damping",  &Setup->LinearDamping,  0.001f, 0.f, 10.f))    MarkDirty();
		if (ImGui::DragFloat("Angular Damping", &Setup->AngularDamping, 0.001f, 0.f, 10.f))    MarkDirty();
		if (ImGui::DragFloat("Friction",        &Setup->Friction,       0.01f, 0.f, 1.f))      MarkDirty();
		if (ImGui::DragFloat("Restitution",     &Setup->Restitution,    0.01f, 0.f, 1.f))      MarkDirty();
		if (ImGui::Checkbox("Simulate Physics", &Setup->bSimulatePhysics))                      MarkDirty();
	}

	if (ImGui::CollapsingHeader("Collision Shapes", ImGuiTreeNodeFlags_DefaultOpen))
	{
		using EShapeType = FPhysicsEditTabState::EShapeType;

		// ── Sphere ────────────────────────────────────────────
		for (int32 i = 0; i < (int32)Setup->AggregateGeom.SphereElems.size(); ++i)
		{
			FKSphereElem& E = Setup->AggregateGeom.SphereElems[i];
			ImGui::PushID(i + 0);
			const bool bSel = (PhysicsTabState.SelectedShapeType == EShapeType::Sphere && PhysicsTabState.SelectedShapeElemIndex == i);
			if (bSel) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.7f, 0.55f, 0.f, 1.f));
			const bool bOpen = ImGui::TreeNodeEx("Sphere", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow);
			if (bSel) ImGui::PopStyleColor();
			if (ImGui::IsItemClicked())
			{
				PhysicsTabState.SelectedShapeType      = EShapeType::Sphere;
				PhysicsTabState.SelectedShapeElemIndex = i;
				UpdatePhysicsShapeGizmo();
			}
			if (bOpen)
			{
				if (ImGui::DragFloat3("Center", &E.Center.X, 0.1f)) { MarkDirty(); UpdatePhysicsShapeGizmo(); }
				if (ImGui::DragFloat("Radius",  &E.Radius,   0.1f, 0.1f, 500.f)) MarkDirty();
				if (ImGui::SmallButton("Remove"))
				{
					if (bSel) { PhysicsTabState.SelectedShapeType = EShapeType::None; PhysicsTabState.SelectedShapeElemIndex = -1; UpdatePhysicsShapeGizmo(); }
					Setup->AggregateGeom.SphereElems.erase(Setup->AggregateGeom.SphereElems.begin() + i);
					MarkDirty(); ImGui::TreePop(); ImGui::PopID(); return;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		// ── Box ───────────────────────────────────────────────
		for (int32 i = 0; i < (int32)Setup->AggregateGeom.BoxElems.size(); ++i)
		{
			FKBoxElem& E = Setup->AggregateGeom.BoxElems[i];
			ImGui::PushID(i + 100);
			const bool bSel = (PhysicsTabState.SelectedShapeType == EShapeType::Box && PhysicsTabState.SelectedShapeElemIndex == i);
			if (bSel) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.7f, 0.55f, 0.f, 1.f));
			const bool bOpen = ImGui::TreeNodeEx("Box", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow);
			if (bSel) ImGui::PopStyleColor();
			if (ImGui::IsItemClicked())
			{
				PhysicsTabState.SelectedShapeType      = EShapeType::Box;
				PhysicsTabState.SelectedShapeElemIndex = i;
				UpdatePhysicsShapeGizmo();
			}
			if (bOpen)
			{
				if (ImGui::DragFloat3("Center", &E.Center.X, 0.1f)) { MarkDirty(); UpdatePhysicsShapeGizmo(); }
				if (ImGui::DragFloat("Half X",  &E.HalfX,   0.1f, 0.1f, 500.f)) MarkDirty();
				if (ImGui::DragFloat("Half Y",  &E.HalfY,   0.1f, 0.1f, 500.f)) MarkDirty();
				if (ImGui::DragFloat("Half Z",  &E.HalfZ,   0.1f, 0.1f, 500.f)) MarkDirty();
				if (ImGui::SmallButton("Remove"))
				{
					if (bSel) { PhysicsTabState.SelectedShapeType = EShapeType::None; PhysicsTabState.SelectedShapeElemIndex = -1; UpdatePhysicsShapeGizmo(); }
					Setup->AggregateGeom.BoxElems.erase(Setup->AggregateGeom.BoxElems.begin() + i);
					MarkDirty(); ImGui::TreePop(); ImGui::PopID(); return;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		// ── Capsule ───────────────────────────────────────────
		for (int32 i = 0; i < (int32)Setup->AggregateGeom.CapsuleElems.size(); ++i)
		{
			FKCapsuleElem& E = Setup->AggregateGeom.CapsuleElems[i];
			ImGui::PushID(i + 200);
			const bool bSel = (PhysicsTabState.SelectedShapeType == EShapeType::Capsule && PhysicsTabState.SelectedShapeElemIndex == i);
			if (bSel) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.7f, 0.55f, 0.f, 1.f));
			const bool bOpen = ImGui::TreeNodeEx("Capsule", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow);
			if (bSel) ImGui::PopStyleColor();
			if (ImGui::IsItemClicked())
			{
				PhysicsTabState.SelectedShapeType      = EShapeType::Capsule;
				PhysicsTabState.SelectedShapeElemIndex = i;
				UpdatePhysicsShapeGizmo();
			}
			if (bOpen)
			{
				if (ImGui::DragFloat3("Center", &E.Center.X, 0.1f)) { MarkDirty(); UpdatePhysicsShapeGizmo(); }
				if (ImGui::DragFloat("Radius",      &E.Radius,     0.1f, 0.1f, 500.f)) MarkDirty();
				if (ImGui::DragFloat("Half Height", &E.HalfHeight, 0.1f, 0.1f, 500.f)) MarkDirty();
				if (ImGui::SmallButton("Remove"))
				{
					if (bSel) { PhysicsTabState.SelectedShapeType = EShapeType::None; PhysicsTabState.SelectedShapeElemIndex = -1; UpdatePhysicsShapeGizmo(); }
					Setup->AggregateGeom.CapsuleElems.erase(Setup->AggregateGeom.CapsuleElems.begin() + i);
					MarkDirty(); ImGui::TreePop(); ImGui::PopID(); return;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		ImGui::Spacing();
		if (ImGui::Button("+ Sphere"))  { Setup->AggregateGeom.SphereElems.push_back({});  MarkDirty(); }
		ImGui::SameLine();
		if (ImGui::Button("+ Box"))     { Setup->AggregateGeom.BoxElems.push_back({});     MarkDirty(); }
		ImGui::SameLine();
		if (ImGui::Button("+ Capsule")) { Setup->AggregateGeom.CapsuleElems.push_back({}); MarkDirty(); }
	}
}

void FMeshEditorWidget::RenderConstraintDetails(UPhysicsConstraintSetup* Constraint)
{
	if (!Constraint) return;

	ImGui::Text("%s -> %s", Constraint->ParentBoneName.c_str(), Constraint->ChildBoneName.c_str());
	ImGui::Separator();

	if (ImGui::CollapsingHeader("Constraint Frame", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::DragFloat3("Anchor Pos",      &Constraint->ParentAnchorPos.X, 0.1f))               MarkDirty();
		if (ImGui::DragFloat4("Anchor Rot XYZW", &Constraint->ParentAnchorRot.X, 0.01f, -1.f, 1.f))  MarkDirty();
	}

	if (ImGui::CollapsingHeader("Angular Limits", ImGuiTreeNodeFlags_DefaultOpen))
	{
		static const char* Modes[] = { "Locked", "Limited", "Free" };
		auto RenderAxis = [&](const char* Label, EConstraintMotion& Motion, float& Angle)
		{
			int32 Idx = (int32)Motion;
			if (ImGui::Combo(Label, &Idx, Modes, 3)) { Motion = (EConstraintMotion)Idx; MarkDirty(); }
			if (Motion == EConstraintMotion::Limited)
			{
				FString AngleLabel = FString("  Limit##") + Label;
				if (ImGui::SliderFloat(AngleLabel.c_str(), &Angle, 0.f, 180.f, "%.1f deg")) MarkDirty();
			}
		};
		RenderAxis("Twist (X)",  Constraint->TwistMotion,  Constraint->TwistLimitAngle);
		RenderAxis("Swing1 (Y)", Constraint->Swing1Motion, Constraint->Swing1LimitAngle);
		RenderAxis("Swing2 (Z)", Constraint->Swing2Motion, Constraint->Swing2LimitAngle);
	}

	if (ImGui::CollapsingHeader("Linear Limits"))
	{
		if (ImGui::Checkbox("Lock Linear Motion", &Constraint->bLockLinearMotion)) MarkDirty();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Physics 셰이프 기즈모
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::UpdatePhysicsShapeGizmo()
{
    UGizmoComponent* Gizmo = ViewportClient.GetGizmo();
    if (!Gizmo) return;

    using EShapeType = FPhysicsEditTabState::EShapeType;
    UPhysicsAsset* PA = PhysicsTabState.PhysicsAsset;

    if (!PA
        || PhysicsTabState.SelectedBodySetupIndex < 0
        || PhysicsTabState.SelectedShapeType      == EShapeType::None
        || PhysicsTabState.SelectedShapeElemIndex < 0)
    {
        PhysicsTabState.ShapeGizmoTarget.Unbind();
        Gizmo->Deactivate();
        return;
    }

    UBodySetup* BS = PA->GetBodySetupsMutable()[PhysicsTabState.SelectedBodySetupIndex];
    FVector* CenterPtr = nullptr;

    switch (PhysicsTabState.SelectedShapeType)
    {
    case EShapeType::Sphere:
        if (PhysicsTabState.SelectedShapeElemIndex < (int32)BS->AggregateGeom.SphereElems.size())
            CenterPtr = &BS->AggregateGeom.SphereElems[PhysicsTabState.SelectedShapeElemIndex].Center;
        break;
    case EShapeType::Box:
        if (PhysicsTabState.SelectedShapeElemIndex < (int32)BS->AggregateGeom.BoxElems.size())
            CenterPtr = &BS->AggregateGeom.BoxElems[PhysicsTabState.SelectedShapeElemIndex].Center;
        break;
    case EShapeType::Capsule:
        if (PhysicsTabState.SelectedShapeElemIndex < (int32)BS->AggregateGeom.CapsuleElems.size())
            CenterPtr = &BS->AggregateGeom.CapsuleElems[PhysicsTabState.SelectedShapeElemIndex].Center;
        break;
    default: break;
    }

    if (CenterPtr)
    {
        // 본 이름으로 인덱스 찾아 월드 트랜스폼 가져오기
        FVector BoneWorldPos  = FVector(0,0,0);
        FQuat   BoneWorldQuat = FQuat::Identity;
        if (USkeletalMeshComponent* MeshComp = ViewportClient.GetPreviewMeshComponent())
        {
            const FSkeletalMesh* MeshAsset = MeshComp->GetSkeletalMesh()
                ? MeshComp->GetSkeletalMesh()->GetSkeletalMeshAsset() : nullptr;
            if (MeshAsset)
            {
                for (int32 i = 0; i < (int32)MeshAsset->Bones.size(); ++i)
                {
                    if (MeshAsset->Bones[i].Name == BS->BoneName)
                    {
                        BoneWorldPos  = MeshComp->GetBoneLocationByIndex(i);
                        BoneWorldQuat = MeshComp->GetBoneQuatByIndex(i);
                        break;
                    }
                }
            }
        }

        PhysicsTabState.ShapeGizmoTarget.Bind(CenterPtr, ViewportClient.GetPreviewWorld(), BoneWorldPos, BoneWorldQuat);
        Gizmo->SetTarget(&PhysicsTabState.ShapeGizmoTarget);
    }
    else
    {
        PhysicsTabState.ShapeGizmoTarget.Unbind();
        Gizmo->Deactivate();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Physics 콜리전 셰이프 오버레이
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float kPhPi  = 3.14159265f;
static constexpr float kPhPi2 = kPhPi * 2.f;

bool FMeshEditorWidget::WorldToScreenPhysics(
    const FMatrix& VP, FVector World, ImVec2 VPMin, ImVec2 VPSize, ImVec2& Out) const
{
    float W = World.X * VP.M[0][3] + World.Y * VP.M[1][3]
            + World.Z * VP.M[2][3] + VP.M[3][3];
    if (W < 0.001f) return false;

    FVector NDC = VP.TransformPositionWithW(World);
    if (NDC.X < -1.f || NDC.X > 1.f || NDC.Y < -1.f || NDC.Y > 1.f) return false;

    Out.x = VPMin.x + (NDC.X *  0.5f + 0.5f) * VPSize.x;
    Out.y = VPMin.y + (NDC.Y * -0.5f + 0.5f) * VPSize.y;
    return true;
}

void FMeshEditorWidget::DrawWireSpherePh(
    ImDrawList* DL, FVector C, float R,
    unsigned int Col, const FMatrix& VP, ImVec2 VPMin, ImVec2 VPSize) const
{
    constexpr int32 Segs = 24;
    const float Step = kPhPi2 / Segs;
    for (int32 i = 0; i < Segs; ++i)
    {
        const float A0 = i * Step, A1 = (i + 1) * Step;
        ImVec2 S0, S1;
        if (WorldToScreenPhysics(VP, FVector(C.X + R*cosf(A0), C.Y + R*sinf(A0), C.Z), VPMin, VPSize, S0) &&
            WorldToScreenPhysics(VP, FVector(C.X + R*cosf(A1), C.Y + R*sinf(A1), C.Z), VPMin, VPSize, S1))
            DL->AddLine(S0, S1, Col, 1.5f);
        if (WorldToScreenPhysics(VP, FVector(C.X + R*cosf(A0), C.Y, C.Z + R*sinf(A0)), VPMin, VPSize, S0) &&
            WorldToScreenPhysics(VP, FVector(C.X + R*cosf(A1), C.Y, C.Z + R*sinf(A1)), VPMin, VPSize, S1))
            DL->AddLine(S0, S1, Col, 1.5f);
        if (WorldToScreenPhysics(VP, FVector(C.X, C.Y + R*cosf(A0), C.Z + R*sinf(A0)), VPMin, VPSize, S0) &&
            WorldToScreenPhysics(VP, FVector(C.X, C.Y + R*cosf(A1), C.Z + R*sinf(A1)), VPMin, VPSize, S1))
            DL->AddLine(S0, S1, Col, 1.5f);
    }
}

void FMeshEditorWidget::DrawWireBoxPh(
    ImDrawList* DL, FVector C, FQuat Rot,
    float HX, float HY, float HZ,
    unsigned int Col, const FMatrix& VP, ImVec2 VPMin, ImVec2 VPSize) const
{
    const FVector Local[8] = {
        {-HX,-HY,-HZ},{HX,-HY,-HZ},{HX,HY,-HZ},{-HX,HY,-HZ},
        {-HX,-HY, HZ},{HX,-HY, HZ},{HX,HY, HZ},{-HX,HY, HZ},
    };
    ImVec2 S[8]; bool V[8];
    for (int32 i = 0; i < 8; ++i)
        V[i] = WorldToScreenPhysics(VP, C + Rot.RotateVector(Local[i]), VPMin, VPSize, S[i]);
    static constexpr int32 E[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (const auto& e : E)
        if (V[e[0]] && V[e[1]]) DL->AddLine(S[e[0]], S[e[1]], Col, 1.5f);
}

void FMeshEditorWidget::DrawWireCapsulePh(
    ImDrawList* DL, FVector C, FQuat Rot,
    float Radius, float HalfH,
    unsigned int Col, const FMatrix& VP, ImVec2 VPMin, ImVec2 VPSize) const
{
    const FVector Up      = Rot.RotateVector(FVector(0,0,1));
    const FVector Right   = Rot.RotateVector(FVector(1,0,0));
    const FVector Forward = Rot.RotateVector(FVector(0,1,0));
    const FVector TopC = C + Up * HalfH, BotC = C - Up * HalfH;
    const float   Step = kPhPi2 / 24;

    for (int32 Ring = 0; Ring < 2; ++Ring)
    {
        const FVector O = Ring ? BotC : TopC;
        for (int32 i = 0; i < 24; ++i)
        {
            const float A0 = i*Step, A1=(i+1)*Step;
            ImVec2 S0,S1;
            if (WorldToScreenPhysics(VP, O+Right*(Radius*cosf(A0))+Forward*(Radius*sinf(A0)), VPMin,VPSize,S0) &&
                WorldToScreenPhysics(VP, O+Right*(Radius*cosf(A1))+Forward*(Radius*sinf(A1)), VPMin,VPSize,S1))
                DL->AddLine(S0,S1,Col,1.5f);
        }
    }
    for (int32 i = 0; i < 4; ++i)
    {
        const float A = i*(kPhPi/2.f);
        const FVector Off = Right*(Radius*cosf(A))+Forward*(Radius*sinf(A));
        ImVec2 S0,S1;
        if (WorldToScreenPhysics(VP,TopC+Off,VPMin,VPSize,S0) && WorldToScreenPhysics(VP,BotC+Off,VPMin,VPSize,S1))
            DL->AddLine(S0,S1,Col,1.5f);
    }
    const float HStep = (kPhPi/2.f)/10;
    for (int32 Hemi = 0; Hemi < 2; ++Hemi)
    {
        const FVector O = Hemi ? BotC : TopC;
        const float Sign = Hemi ? -1.f : 1.f;
        for (int32 d = 0; d < 4; ++d)
        {
            const FVector Dir = Right*cosf(d*kPhPi/2.f)+Forward*sinf(d*kPhPi/2.f);
            for (int32 i = 0; i < 10; ++i)
            {
                const float A0=i*HStep, A1=(i+1)*HStep;
                ImVec2 S0,S1;
                if (WorldToScreenPhysics(VP, O+Dir*(Radius*cosf(A0))+Up*(Sign*Radius*sinf(A0)), VPMin,VPSize,S0) &&
                    WorldToScreenPhysics(VP, O+Dir*(Radius*cosf(A1))+Up*(Sign*Radius*sinf(A1)), VPMin,VPSize,S1))
                    DL->AddLine(S0,S1,Col,1.5f);
            }
        }
    }
}

void FMeshEditorWidget::DrawPhysicsShapeOverlays(ImDrawList* DL, ImVec2 VPMin, ImVec2 VPSize) const
{
    UPhysicsAsset* PA = PhysicsTabState.PhysicsAsset;
    if (!PA) return;

    FMinimalViewInfo POV;
    if (!ViewportClient.GetCameraView(POV)) return;
    const FMatrix VP = POV.CalculateViewProjectionMatrix();

    using EShapeType = FPhysicsEditTabState::EShapeType;
    const auto& Setups = PA->GetBodySetups();

    USkeletalMeshComponent* MeshComp = ViewportClient.GetPreviewMeshComponent();
    const FSkeletalMesh* MeshAsset = MeshComp && MeshComp->GetSkeletalMesh()
        ? MeshComp->GetSkeletalMesh()->GetSkeletalMeshAsset() : nullptr;

    for (int32 Idx = 0; Idx < (int32)Setups.size(); ++Idx)
    {
        UBodySetup* BS = Setups[Idx];
        if (!BS) continue;
        const bool bBodySel = (Idx == PhysicsTabState.SelectedBodySetupIndex);

        // 본 월드 트랜스폼 조회
        FVector BoneWorldPos  = FVector(0,0,0);
        FQuat   BoneWorldQuat = FQuat::Identity;
        if (MeshAsset && MeshComp)
        {
            for (int32 i = 0; i < (int32)MeshAsset->Bones.size(); ++i)
            {
                if (MeshAsset->Bones[i].Name == BS->BoneName)
                {
                    BoneWorldPos  = MeshComp->GetBoneLocationByIndex(i);
                    BoneWorldQuat = MeshComp->GetBoneQuatByIndex(i);
                    break;
                }
            }
        }

        for (int32 Si = 0; Si < (int32)BS->AggregateGeom.SphereElems.size(); ++Si)
        {
            const FKSphereElem& E = BS->AggregateGeom.SphereElems[Si];
            const bool bSel = bBodySel && PhysicsTabState.SelectedShapeType == EShapeType::Sphere && PhysicsTabState.SelectedShapeElemIndex == Si;
            const unsigned int Col = bSel ? IM_COL32(255,220,0,255) : bBodySel ? IM_COL32(0,220,100,200) : IM_COL32(0,180,80,120);
            const FVector WorldCenter = BoneWorldPos + BoneWorldQuat.RotateVector(E.Center);
            DrawWireSpherePh(DL, WorldCenter, E.Radius, Col, VP, VPMin, VPSize);
        }
        for (int32 Bi = 0; Bi < (int32)BS->AggregateGeom.BoxElems.size(); ++Bi)
        {
            const FKBoxElem& E = BS->AggregateGeom.BoxElems[Bi];
            const bool bSel = bBodySel && PhysicsTabState.SelectedShapeType == EShapeType::Box && PhysicsTabState.SelectedShapeElemIndex == Bi;
            const unsigned int Col = bSel ? IM_COL32(255,220,0,255) : bBodySel ? IM_COL32(0,220,100,200) : IM_COL32(0,180,80,120);
            const FVector WorldCenter = BoneWorldPos + BoneWorldQuat.RotateVector(E.Center);
            const FQuat   WorldRot    = BoneWorldQuat * E.Rotation;
            DrawWireBoxPh(DL, WorldCenter, WorldRot, E.HalfX, E.HalfY, E.HalfZ, Col, VP, VPMin, VPSize);
        }
        for (int32 Ci = 0; Ci < (int32)BS->AggregateGeom.CapsuleElems.size(); ++Ci)
        {
            const FKCapsuleElem& E = BS->AggregateGeom.CapsuleElems[Ci];
            const bool bSel = bBodySel && PhysicsTabState.SelectedShapeType == EShapeType::Capsule && PhysicsTabState.SelectedShapeElemIndex == Ci;
            const unsigned int Col = bSel ? IM_COL32(255,220,0,255) : bBodySel ? IM_COL32(0,220,100,200) : IM_COL32(0,180,80,120);
            const FVector WorldCenter = BoneWorldPos + BoneWorldQuat.RotateVector(E.Center);
            const FQuat   WorldRot    = BoneWorldQuat * E.Rotation;
            DrawWireCapsulePh(DL, WorldCenter, WorldRot, E.Radius, E.HalfHeight, Col, VP, VPMin, VPSize);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Physics 저장
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::SavePhysicsAsset()
{
    UPhysicsAsset* PhysicsAsset = PhysicsTabState.PhysicsAsset;
    if (!PhysicsAsset) return;

    USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
    if (!SkeletalMesh) return;

    const FString MeshPath = SkeletalMesh->GetAssetPathFileName();
    if (MeshPath.empty() || MeshPath == "None")
    {
        UE_LOG("PhysicsAsset save failed: mesh has no path.");
        return;
    }

    const FString PhysicsPath = FPaths::MakeProjectRelative(MakePhysicsAssetPath(MeshPath));
    PhysicsAsset->AssetPathFileName = PhysicsPath;

    // 저장 디렉토리 자동 생성
    {
        std::filesystem::path FullPath =
            std::filesystem::path(FPaths::RootDir()) / FPaths::ToWide(PhysicsPath);
        FPaths::CreateDir(FullPath.parent_path().wstring());
    }

    FWindowsBinWriter Writer(PhysicsPath);
    if (!Writer.IsValid())
    {
        UE_LOG("PhysicsAsset save failed: could not open file. Path=%s", PhysicsPath.c_str());
        return;
    }

    FAssetPackageHeader Header;
    Header.Type = static_cast<uint32>(EAssetPackageType::PhysicsAsset);

    FAssetImportMetadata Metadata; // PhysicsAsset은 별도 소스 파일 없음

    Writer << Header;
    Writer << Metadata;
    PhysicsAsset->Serialize(Writer);

    if (!Writer.IsValid())
    {
        UE_LOG("PhysicsAsset save failed: write error. Path=%s", PhysicsPath.c_str());
        return;
    }

    // 저장 성공 → 메시에 포인터 연결 (이후 Close 시 delete 안 함)
    SkeletalMesh->PhysicsAsset = PhysicsAsset;

    ClearDirty();
}
