#include "MeshEditorWidget.h"
#include "Physics/Asset/PhysicsAsset.h"
#include "Physics/Asset/BodySetup.h"
#include "Physics/Asset/PhysicsConstraintSetup.h"
#include "Physics/IPhysicsRuntime.h"
#include "Asset/AssetPackage.h"
#include "GameFramework/World.h"
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

// ─────────────────────────────────────────────────────────────────────────────
// MeshEditorWidget 의 Physics 탭 구현부.
// 분량이 커서 MeshEditorWidget.cpp 에서 이 파일로 분리했다.
// (FMeshEditorWidget 의 멤버 함수 정의이며, 선언은 MeshEditorWidget.h 에 있다.)
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
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

	// ── 편집 툴바: 컨스트레인트 타입 변환 + 바디쌍 충돌 토글 ──
	RenderPhysicsToolbar();

	// ── 왼쪽 컬럼: 본 트리/컨스트레인트(위) + 그래프(아래) ──
	ImGui::BeginGroup();
	const float LeftColW = PhysicsTabState.BoneTreeWidth;
	{
		float AvailH = ImGui::GetContentRegionAvail().y;
		PhysicsTabState.GraphHeight = std::min(PhysicsTabState.GraphHeight, std::max(0.f, AvailH - 80.f));
		if (PhysicsTabState.GraphHeight < 0.f) PhysicsTabState.GraphHeight = 0.f;
		float TreeH = AvailH - PhysicsTabState.GraphHeight - 6.f;
		if (TreeH < 50.f) TreeH = 50.f;

	ImGui::BeginChild("##PhysBoneTree", ImVec2(LeftColW, TreeH), true);
	ImGui::Text("Skeleton");
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
	ImGui::InputTextWithHint("##BoneSearch", "Search...", PhysicsTabState.BoneSearchText, sizeof(PhysicsTabState.BoneSearchText));
	ImGui::Separator();

	const bool bFiltering = PhysicsTabState.BoneSearchText[0] != '\0';
	if (Asset && !bFiltering)
	{
		for (int32 i = 0; i < (int32)Asset->Bones.size(); ++i)
		{
			if (Asset->Bones[i].ParentIndex == -1)
			{
				RenderPhysicsBoneTree(Asset, i);
			}
		}
	}
	else if (Asset && bFiltering)
	{
		// 검색 중에는 계층 대신 매칭되는 본만 평면 목록으로 표시한다.
		FString Needle = PhysicsTabState.BoneSearchText;
		for (char& c : Needle) c = (char)tolower((unsigned char)c);
		for (int32 i = 0; i < (int32)Asset->Bones.size(); ++i)
		{
			FString Hay = Asset->Bones[i].Name;
			for (char& c : Hay) c = (char)tolower((unsigned char)c);
			if (Hay.find(Needle) == FString::npos) continue;
			RenderPhysicsBoneTree(Asset, i, /*bFlat=*/true);
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

		// ── 가로 스플리터: 트리 ↔ 그래프 ─────────────────────
		ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
		ImGui::Button("##physGraphSplitter", ImVec2(LeftColW, 4.0f));
		if (ImGui::IsItemActive())
		{
			PhysicsTabState.GraphHeight -= ImGui::GetIO().MouseDelta.y;
			PhysicsTabState.GraphHeight = std::max(0.f,
				std::min(PhysicsTabState.GraphHeight, ImGui::GetContentRegionAvail().y + PhysicsTabState.GraphHeight - 80.f));
		}
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		ImGui::PopStyleColor(3);

		// ── 그래프 패널 ──────────────────────────────────────
		ImGui::BeginChild("##PhysGraph", ImVec2(LeftColW, PhysicsTabState.GraphHeight), true);
		RenderPhysicsGraphPanel();
		ImGui::EndChild();
	}
	ImGui::EndGroup();

	ImGui::SameLine();

	// ── 스플리터: 본 트리 ↔ 뷰포트 ──────────────────────────
	{
		ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
		ImGui::Button("##physTreeSplitter", ImVec2(4.0f, -1.0f));
		if (ImGui::IsItemActive())
		{
			PhysicsTabState.BoneTreeWidth += ImGui::GetIO().MouseDelta.x;
			PhysicsTabState.BoneTreeWidth = std::max(120.0f,
				std::min(PhysicsTabState.BoneTreeWidth, ImGui::GetWindowWidth() - PhysicsTabState.DetailsWidth - 150.0f));
		}
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		ImGui::PopStyleColor(3);
	}

	ImGui::SameLine();

	// ── 중앙: 뷰포트 + 하단 시뮬레이션 트랜스포트 바 ─────────
	ImGui::BeginGroup();
	{
		const float TransportH = 34.f;
		float ViewportWidth  = ImGui::GetContentRegionAvail().x - PhysicsTabState.DetailsWidth - ImGui::GetStyle().ItemSpacing.x * 2.f - 4.f;
		float ViewportHeight = ImGui::GetContentRegionAvail().y - TransportH;
		if (ViewportWidth  < 1.f) ViewportWidth  = 1.f;
		if (ViewportHeight < 1.f) ViewportHeight = 1.f;
		RenderViewportPanel(ImVec2(ViewportWidth, ViewportHeight));
		RenderPhysicsTransportBar(ViewportWidth);
	}
	ImGui::EndGroup();

	ImGui::SameLine();

	// ── 스플리터: 뷰포트 ↔ 디테일 ───────────────────────────
	{
		ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
		ImGui::Button("##physDetailsSplitter", ImVec2(4.0f, -1.0f));
		if (ImGui::IsItemActive())
		{
			// 디테일 패널은 오른쪽 → 드래그 방향과 반대로 폭 조절
			PhysicsTabState.DetailsWidth -= ImGui::GetIO().MouseDelta.x;
			PhysicsTabState.DetailsWidth = std::max(180.0f,
				std::min(PhysicsTabState.DetailsWidth, ImGui::GetWindowWidth() - PhysicsTabState.BoneTreeWidth - 150.0f));
		}
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		ImGui::PopStyleColor(3);
	}

	ImGui::SameLine();

	// ── 오른쪽: 디테일 / 툴 패널 ─────────────────────────────
	ImGui::BeginChild("##PhysDetails", ImVec2(PhysicsTabState.DetailsWidth, 0), true);
	if (ImGui::BeginTabBar("##PhysRightTabs"))
	{
		if (ImGui::BeginTabItem("Details"))
		{
			PhysicsTabState.RightPanelTab = 0;
			RenderPhysicsDetailsPanel();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Tools"))
		{
			PhysicsTabState.RightPanelTab = 1;
			RenderPhysicsToolsPanel();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::EndChild();
}

void FMeshEditorWidget::RenderPhysicsBoneTree(const FSkeletalMesh* Asset, int32 BoneIndex, bool bFlat)
{
	if (!Asset) return;

	const FBone& Bone       = Asset->Bones[BoneIndex];
	UPhysicsAsset* PhysicsAsset = PhysicsTabState.PhysicsAsset;
	const bool bHasBody     = PhysicsAsset && PhysicsAsset->FindBodySetup(Bone.Name) != nullptr;
	const bool bHasParent   = Bone.ParentIndex >= 0;

	// BodySetup 있는 본 → 초록, 없는 본 → 기본색
	if (bHasBody) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.f, 0.4f, 1.f));

	// 검색 평면 모드에서는 자식을 따지지 않고 단일 항목으로만 렌더.
	bool bHasChildren = false;
	if (!bFlat)
	{
		for (int32 i = BoneIndex + 1; i < (int32)Asset->Bones.size(); ++i)
		{
			if (Asset->Bones[i].ParentIndex == BoneIndex) { bHasChildren = true; break; }
		}
	}

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	if (SelectedBoneIndex == BoneIndex) Flags |= ImGuiTreeNodeFlags_Selected;
	if (!bHasChildren)                  Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	// 바디가 있는 본은 앞에 마커를 붙여 한눈에 구분되게 한다.
	const FString Label = (bHasBody ? FString("* ") : FString("  ")) + Bone.Name;

	ImGui::PushID(BoneIndex);
	bool bOpen = ImGui::TreeNodeEx(Label.c_str(), Flags);
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

		// ── Add Constraint to ▶ (바디를 가진 다른 본 중 선택) ──
		// 컨스트레인트는 바디끼리 연결하므로 현재 본에 바디가 있을 때만 의미가 있다.
		if (bHasBody)
		{
			// a 가 b 의 조상인지 (b 에서 부모를 따라 올라가며 a 를 만나면 true)
			auto IsAncestorOf = [&](int32 AncestorIdx, int32 DescIdx) -> bool
			{
				for (int32 p = Asset->Bones[DescIdx].ParentIndex; p >= 0; p = Asset->Bones[p].ParentIndex)
					if (p == AncestorIdx) return true;
				return false;
			};

			// 선택된 타겟 본과 현재 본 사이에 컨스트레인트를 만든다. 조상을 Parent 로.
			auto CreateConstraintWith = [&](int32 OtherIdx)
			{
				const FString& OtherName = Asset->Bones[OtherIdx].Name;
				FString ParentName, ChildName;
				if (IsAncestorOf(OtherIdx, BoneIndex))      { ParentName = OtherName;  ChildName = Bone.Name; }
				else if (IsAncestorOf(BoneIndex, OtherIdx)) { ParentName = Bone.Name;  ChildName = OtherName; }
				else                                        { ParentName = OtherName;  ChildName = Bone.Name; }

				PhysicsAsset->GetOrCreateConstraintSetup(ParentName, ChildName);
				PhysicsTabState.SelectedConstraintIndex = (int32)PhysicsAsset->GetConstraints().size() - 1;
				PhysicsTabState.SelectedBodySetupIndex  = -1;
				MarkDirty();
			};

			if (ImGui::BeginMenu("Add Constraint to"))
			{
				// 바디를 가진 가장 가까운 조상 → 최상단 빠른 항목
				int32 NearestAncestorBody = -1;
				for (int32 p = Bone.ParentIndex; p >= 0; p = Asset->Bones[p].ParentIndex)
				{
					if (PhysicsAsset->FindBodySetup(Asset->Bones[p].Name)) { NearestAncestorBody = p; break; }
				}
				if (NearestAncestorBody >= 0)
				{
					const FString& AncName = Asset->Bones[NearestAncestorBody].Name;
					const bool bExists = PhysicsAsset->FindConstraintSetup(AncName, Bone.Name)
					                  || PhysicsAsset->FindConstraintSetup(Bone.Name, AncName);
					FString Quick = "Parent Body: " + AncName + (bExists ? "  (exists)" : "");
					if (ImGui::MenuItem(Quick.c_str(), nullptr, false, !bExists))
						CreateConstraintWith(NearestAncestorBody);
					ImGui::Separator();
				}

				// 바디를 가진 다른 모든 본
				bool bAny = false;
				for (int32 j = 0; j < (int32)Asset->Bones.size(); ++j)
				{
					if (j == BoneIndex) continue;
					const FString& OtherName = Asset->Bones[j].Name;
					if (!PhysicsAsset->FindBodySetup(OtherName)) continue;
					bAny = true;
					const bool bExists = PhysicsAsset->FindConstraintSetup(OtherName, Bone.Name)
					                  || PhysicsAsset->FindConstraintSetup(Bone.Name, OtherName);
					ImGui::PushID(j);
					FString ItemLabel = OtherName + (bExists ? "  (exists)" : "");
					if (ImGui::MenuItem(ItemLabel.c_str(), nullptr, false, !bExists))
						CreateConstraintWith(j);
					ImGui::PopID();
				}
				if (!bAny) ImGui::TextDisabled("(no other bodies)");
				ImGui::EndMenu();
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
		if (ImGui::Checkbox("Enable Gravity",   &Setup->bEnableGravity))                        MarkDirty();

		{
			static const char* PhysTypes[] = { "Default (Simulated)", "Kinematic" };
			int32 Idx = (int32)Setup->PhysicsType;
			if (ImGui::Combo("Physics Type", &Idx, PhysTypes, 2)) { Setup->PhysicsType = (EBodyPhysicsType)Idx; MarkDirty(); }
		}
		{
			static const char* CollTypes[] = { "No Collision", "Query Only", "Physics Only", "Query and Physics" };
			int32 Idx = (int32)Setup->CollisionEnabled;
			if (ImGui::Combo("Collision Enabled", &Idx, CollTypes, 4)) { Setup->CollisionEnabled = (EBodyCollisionEnabled)Idx; MarkDirty(); }
		}
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
				if (ImGui::DragFloat("Radius",  &E.Radius,   0.01f, 0.01f, 500.f)) MarkDirty();
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
				if (ImGui::DragFloat("Half X",  &E.HalfX, 0.01f, 0.01f, 500.f)) MarkDirty();
				if (ImGui::DragFloat("Half Y",  &E.HalfY, 0.01f, 0.01f, 500.f)) MarkDirty();
				if (ImGui::DragFloat("Half Z",  &E.HalfZ, 0.01f, 0.01f, 500.f)) MarkDirty();
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
				if (ImGui::DragFloat("Radius",      &E.Radius, 0.01f, 0.01f, 500.f)) MarkDirty();
				if (ImGui::DragFloat("Half Height", &E.HalfHeight, 0.01f, 0.01f, 500.f)) MarkDirty();
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
// 편집 툴바 — 선택한 컨스트레인트의 관절 타입 프리셋 변환 + 두 바디 충돌 토글
//   (언리얼 PhAT 상단 툴바의 "볼앤소켓/힌지/프리즈매틱", "콜리전 활성/비활성"에 대응)
// ─────────────────────────────────────────────────────────────────────────────
void FMeshEditorWidget::RenderPhysicsToolbar()
{
	UPhysicsAsset* PA = PhysicsTabState.PhysicsAsset;
	if (!PA) return;

	const auto& Constraints = PA->GetConstraints();
	const bool bHasConstraint =
		PhysicsTabState.SelectedConstraintIndex >= 0 &&
		PhysicsTabState.SelectedConstraintIndex < (int32)Constraints.size();

	UPhysicsConstraintSetup* CS = bHasConstraint ? Constraints[PhysicsTabState.SelectedConstraintIndex] : nullptr;

	// 컨스트레인트 미선택 시 버튼을 비활성화해 무엇에 적용되는지 분명히 한다.
	ImGui::BeginDisabled(CS == nullptr);

	// ── 관절 타입 프리셋 ──────────────────────────────────────
	auto SetMotions = [&](EConstraintMotion Twist, EConstraintMotion Swing1, EConstraintMotion Swing2, bool bLockLinear)
	{
		if (!CS) return;
		CS->TwistMotion  = Twist;
		CS->Swing1Motion = Swing1;
		CS->Swing2Motion = Swing2;
		CS->bLockLinearMotion = bLockLinear;
		MarkDirty();
	};

	if (ImGui::SmallButton("Ball & Socket"))
		SetMotions(EConstraintMotion::Free, EConstraintMotion::Free, EConstraintMotion::Free, true);
	ImGui::SameLine();
	if (ImGui::SmallButton("Hinge"))
		SetMotions(EConstraintMotion::Free, EConstraintMotion::Locked, EConstraintMotion::Locked, true);
	ImGui::SameLine();
	if (ImGui::SmallButton("Prismatic"))
		SetMotions(EConstraintMotion::Locked, EConstraintMotion::Locked, EConstraintMotion::Locked, false);

	// ── 두 바디 충돌 토글 ─────────────────────────────────────
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();

	const bool bDisabledNow = CS ? PA->IsCollisionDisabled(CS->ParentBoneName, CS->ChildBoneName) : false;
	if (bDisabledNow)
	{
		if (ImGui::SmallButton("Enable Collision"))
		{
			PA->SetCollisionDisabled(CS->ParentBoneName, CS->ChildBoneName, false);
			MarkDirty();
		}
	}
	else
	{
		if (ImGui::SmallButton("Disable Collision"))
		{
			PA->SetCollisionDisabled(CS->ParentBoneName, CS->ChildBoneName, true);
			MarkDirty();
		}
	}

	ImGui::EndDisabled();

	if (!CS)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("(select a constraint)");
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 그래프 패널 — 바디를 노드로, 컨스트레인트를 노드 사이 엣지(+다이아몬드 노드)로 표시.
//   스켈레톤 계층 기반 자동 배치. 노드 클릭으로 바디/컨스트레인트 선택, 빈 공간 드래그로 팬.
// ─────────────────────────────────────────────────────────────────────────────
void FMeshEditorWidget::RenderPhysicsGraphPanel()
{
	UPhysicsAsset* PA = PhysicsTabState.PhysicsAsset;
	USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
	const FSkeletalMesh* MeshAsset = (Comp && Comp->GetSkeletalMesh())
		? Comp->GetSkeletalMesh()->GetSkeletalMeshAsset() : nullptr;

	ImGui::TextDisabled("Graph  (drag to pan, wheel to zoom)");
	if (!PA) return;

	ImDrawList* DL  = ImGui::GetWindowDrawList();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	ImVec2 Avail = ImGui::GetContentRegionAvail();
	if (Avail.x < 1.f) Avail.x = 1.f;
	if (Avail.y < 1.f) Avail.y = 1.f;

	// 캔버스(팬용). 먼저 제출하되 AllowOverlap 을 줘서, 위에 그린 노드 버튼들이
	// hover/클릭을 받을 수 있게 한다. (없으면 캔버스가 hover 를 선점해 노드 클릭이 막힘)
	ImGui::SetNextItemAllowOverlap();
	ImGui::InvisibleButton("##graphCanvas", Avail);
	const bool bCanvasActive = ImGui::IsItemActive();
	if (bCanvasActive)
	{
		PhysicsTabState.GraphPanX += ImGui::GetIO().MouseDelta.x;
		PhysicsTabState.GraphPanY += ImGui::GetIO().MouseDelta.y;
	}
	// 휠 줌 (커서 아래 지점을 고정한 채 확대/축소)
	if (ImGui::IsItemHovered())
	{
		const float Wheel = ImGui::GetIO().MouseWheel;
		if (Wheel != 0.f)
		{
			const float Old = PhysicsTabState.GraphZoom;
			float New = Old * (Wheel > 0.f ? 1.1f : 1.f / 1.1f);
			if (New < 0.4f) New = 0.4f;
			if (New > 3.0f) New = 3.0f;
			if (New != Old)
			{
				const ImVec2 M = ImGui::GetMousePos();
				const float Lx = (M.x - Origin.x - PhysicsTabState.GraphPanX) / Old;
				const float Ly = (M.y - Origin.y - PhysicsTabState.GraphPanY) / Old;
				PhysicsTabState.GraphPanX = M.x - Origin.x - Lx * New;
				PhysicsTabState.GraphPanY = M.y - Origin.y - Ly * New;
				PhysicsTabState.GraphZoom = New;
			}
		}
	}

	DL->PushClipRect(Origin, ImVec2(Origin.x + Avail.x, Origin.y + Avail.y), true);

	const float  PanX = PhysicsTabState.GraphPanX;
	const float  PanY = PhysicsTabState.GraphPanY;
	const float  Z     = PhysicsTabState.GraphZoom;
	const float  FontSz = ImGui::GetFontSize() * Z;
	ImFont* const Font = ImGui::GetFont();

	auto& Bodies = PA->GetBodySetupsMutable();
	const int32 NB = (int32)Bodies.size();
	const auto& Cons = PA->GetConstraints();

	auto FindBodyIdx = [&](const FString& Name) -> int32
	{
		for (int32 b = 0; b < NB; ++b) if (Bodies[b] && Bodies[b]->BoneName == Name) return b;
		return -1;
	};
	auto SyncBoneSelection = [&](const FString& BoneName)
	{
		if (!MeshAsset) return;
		for (int32 bi = 0; bi < (int32)MeshAsset->Bones.size(); ++bi)
			if (MeshAsset->Bones[bi].Name == BoneName) { SelectedBoneIndex = bi; break; }
	};

	// ── 노드 그리기 헬퍼 (다중 라인 박스 + 클릭/호버) ─────────
	auto DrawNode = [&](ImVec2 Ctr, ImVec2 Size, ImU32 Fill, bool bSel,
	                    const char* L0, const char* L1, const char* L2, int32 PushId) -> bool
	{
		const ImVec2 TL(Ctr.x - Size.x * 0.5f, Ctr.y - Size.y * 0.5f);
		const ImVec2 BR(Ctr.x + Size.x * 0.5f, Ctr.y + Size.y * 0.5f);
		DL->AddRectFilled(TL, BR, Fill, 4.f);
		DL->AddRect(TL, BR, bSel ? IM_COL32(235, 170, 40, 255) : IM_COL32(40, 40, 40, 200), 4.f, 0, bSel ? 2.5f : 1.2f);

		const char* Lines[3] = { L0, L1, L2 };
		int32 NLines = 0; for (int32 k = 0; k < 3; ++k) if (Lines[k] && Lines[k][0]) ++NLines;
		float TY = Ctr.y - (NLines * FontSz) * 0.5f;
		const ImU32 TextCol = IM_COL32(20, 24, 16, 255);
		for (int32 k = 0; k < 3; ++k)
		{
			if (!Lines[k] || !Lines[k][0]) continue;
			const ImVec2 TS = Font->CalcTextSizeA(FontSz, 1e30f, 0.f, Lines[k]);
			DL->AddText(Font, FontSz, ImVec2(Ctr.x - TS.x * 0.5f, TY), TextCol, Lines[k]);
			TY += FontSz;
		}

		ImGui::SetCursorScreenPos(TL);
		ImGui::PushID(PushId);
		ImGui::InvisibleButton("##n", Size);
		const bool bClicked = ImGui::IsItemClicked();
		if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		ImGui::PopID();
		return bClicked;
	};

	const ImU32 BodyFill = IM_COL32(190, 214, 152, 255); // 연녹
	const ImU32 ConFill  = IM_COL32(205, 196, 140, 255); // 카키
	const ImVec2 NodeSize(116.f * Z, (ImGui::GetTextLineHeight() * 3.f + 10.f) * Z);

	// ── 포커스 대상 결정 ─────────────────────────────────────
	// 바디가 선택돼 있으면 그 바디 기준, 없으면 안내만 표시 (언리얼처럼 선택 관련만 노출).
	int32 FocusBody = PhysicsTabState.SelectedBodySetupIndex;
	if (FocusBody < 0 && PhysicsTabState.SelectedConstraintIndex >= 0
	    && PhysicsTabState.SelectedConstraintIndex < (int32)Cons.size())
	{
		// 컨스트레인트만 선택된 경우 그 부모 바디를 포커스로
		FocusBody = FindBodyIdx(Cons[PhysicsTabState.SelectedConstraintIndex]->ParentBoneName);
	}

	if (FocusBody < 0 || FocusBody >= NB || !Bodies[FocusBody])
	{
		DL->AddText(ImVec2(Origin.x + 10.f, Origin.y + 10.f), IM_COL32(150, 150, 150, 255),
		            "Select a body to see its connections.");
		DL->PopClipRect();
		return;
	}

	const FString FocusBone = Bodies[FocusBody]->BoneName;

	// 포커스 바디와 연결된 컨스트레인트 수집
	struct FRel { int32 ConIdx; FString OtherBone; int32 OtherBody; };
	TArray<FRel> Rels;
	for (int32 i = 0; i < (int32)Cons.size(); ++i)
	{
		UPhysicsConstraintSetup* CS = Cons[i];
		if (!CS) continue;
		if (CS->ParentBoneName == FocusBone)      Rels.push_back({ i, CS->ChildBoneName,  FindBodyIdx(CS->ChildBoneName) });
		else if (CS->ChildBoneName == FocusBone)  Rels.push_back({ i, CS->ParentBoneName, FindBodyIdx(CS->ParentBoneName) });
	}

	// ── 레이아웃 (3열: 포커스 | 컨스트레인트 | 상대 바디) ────
	const float Col0 = Origin.x + PanX + 30.f * Z + NodeSize.x * 0.5f;
	const float Col1 = Col0 + 150.f * Z;
	const float Col2 = Col1 + 150.f * Z;
	const float RowH = NodeSize.y + 16.f * Z;
	const int32 N = (int32)Rels.size();
	const float StartY = Origin.y + PanY + 30.f * Z + NodeSize.y * 0.5f;
	const float FocusY = StartY + (N > 0 ? (N - 1) * 0.5f * RowH : 0.f);

	// 엣지 먼저
	for (int32 k = 0; k < N; ++k)
	{
		const float RY = StartY + k * RowH;
		const ImVec2 PF(Col0, FocusY), PC(Col1, RY), PO(Col2, RY);
		DL->AddLine(PF, PC, IM_COL32(150, 150, 150, 255), 2.f);
		DL->AddLine(PC, PO, IM_COL32(150, 150, 150, 255), 2.f);
	}

	// 포커스 바디 노드
	{
		char L2[32]; snprintf(L2, sizeof(L2), "%d Shapes", Bodies[FocusBody]->AggregateGeom.GetTotalPrimCount());
		if (DrawNode(ImVec2(Col0, FocusY), NodeSize, BodyFill, true, "Body", FocusBone.c_str(), L2, 1))
		{
			PhysicsTabState.SelectedBodySetupIndex  = FocusBody;
			PhysicsTabState.SelectedConstraintIndex = -1;
			SyncBoneSelection(FocusBone);
		}
	}

	// 컨스트레인트 노드 + 상대 바디 노드
	for (int32 k = 0; k < N; ++k)
	{
		const FRel& R = Rels[k];
		const float RY = StartY + k * RowH;
		UPhysicsConstraintSetup* CS = Cons[R.ConIdx];

		char CLbl[160]; snprintf(CLbl, sizeof(CLbl), "%s : %s", CS->ParentBoneName.c_str(), CS->ChildBoneName.c_str());
		const bool bConSel = (PhysicsTabState.SelectedConstraintIndex == R.ConIdx);
		if (DrawNode(ImVec2(Col1, RY), NodeSize, ConFill, bConSel, "Constraint", CLbl, "", 200000 + R.ConIdx))
		{
			PhysicsTabState.SelectedConstraintIndex = R.ConIdx;
			PhysicsTabState.SelectedBodySetupIndex  = -1;
		}

		if (R.OtherBody >= 0 && R.OtherBody < NB && Bodies[R.OtherBody])
		{
			char L2[32]; snprintf(L2, sizeof(L2), "%d Shapes", Bodies[R.OtherBody]->AggregateGeom.GetTotalPrimCount());
			const bool bSel = (PhysicsTabState.SelectedBodySetupIndex == R.OtherBody);
			if (DrawNode(ImVec2(Col2, RY), NodeSize, BodyFill, bSel, "Body", R.OtherBone.c_str(), L2, 300000 + R.OtherBody))
			{
				PhysicsTabState.SelectedBodySetupIndex  = R.OtherBody;
				PhysicsTabState.SelectedConstraintIndex = -1;
				SyncBoneSelection(R.OtherBone);
			}
		}
		else
		{
			// 상대 본에 바디가 없으면 회색 비활성 노드로 표기
			DrawNode(ImVec2(Col2, RY), NodeSize, IM_COL32(120, 120, 120, 255), false, "(no body)", R.OtherBone.c_str(), "", 400000 + k);
		}
	}

	if (N == 0)
		DL->AddText(ImVec2(Col0 - NodeSize.x * 0.5f, FocusY + NodeSize.y * 0.5f + 8.f),
		            IM_COL32(150, 150, 150, 255), "No constraints on this body.");

	DL->PopClipRect();
}

// ─────────────────────────────────────────────────────────────────────────────
// 바디 자동 생성 툴 (언리얼 PhAT 의 "바디 생성" 패널에 대응)
//   - 여기서는 설정 UI 와 "Generate" 버튼만 제공한다.
//   - 실제 생성 로직(GeneratePhysicsBodies)은 아직 stub 이며, 담당자가 채워넣는다.
// ─────────────────────────────────────────────────────────────────────────────
void FMeshEditorWidget::RenderPhysicsToolsPanel()
{
	auto& S = PhysicsTabState.BodyCreation;

	if (ImGui::CollapsingHeader("Body Creation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::DragFloat("Min Bone Size", &S.MinBoneSize, 0.5f, 0.f, 1000.f);

		{
			static const char* PrimItems[] = { "Sphere", "Box", "Capsule" };
			// EShapeType: None=0, Sphere=1, Box=2, Capsule=3  →  콤보 인덱스로 매핑
			int32 PrimIdx = (S.PrimitiveType == FPhysicsEditTabState::EShapeType::Sphere) ? 0
			              : (S.PrimitiveType == FPhysicsEditTabState::EShapeType::Box)    ? 1 : 2;
			if (ImGui::Combo("Primitive Type", &PrimIdx, PrimItems, 3))
			{
				S.PrimitiveType = (PrimIdx == 0) ? FPhysicsEditTabState::EShapeType::Sphere
				                : (PrimIdx == 1) ? FPhysicsEditTabState::EShapeType::Box
				                                 : FPhysicsEditTabState::EShapeType::Capsule;
			}
		}

		ImGui::Checkbox("Orient Along Bone",        &S.bOrientAlongBone);
		ImGui::Checkbox("Walk Past Small Bones",    &S.bWalkPastSmallBones);
		ImGui::Checkbox("Create Body For All Bones", &S.bCreateBodyForAllBones);
		ImGui::Checkbox("Disable Collision By Default", &S.bDisableCollisionByDefault);
		ImGui::DragInt("LOD Index", &S.LodIndex, 1.f, 0, 8);
	}

	if (ImGui::CollapsingHeader("Constraint Creation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Create Constraints", &S.bCreateConstraints);
		if (S.bCreateConstraints)
		{
			static const char* Modes[] = { "Locked", "Limited", "Free" };
			ImGui::Combo("Angular Constraint Mode", &S.AngularConstraintMode, Modes, 3);
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::Button("Re-generate Bodies", ImVec2(ImGui::GetContentRegionAvail().x, 0.f)))
	{
		GeneratePhysicsBodies();
	}
}

void FMeshEditorWidget::GeneratePhysicsBodies()
{
	// TODO(physics): 스켈레톤 전체를 순회하며 PhysicsTabState.BodyCreation 설정에 따라
	//   - 각 본에 BodySetup + 셰이프(AggregateGeom) 생성
	//   - bCreateConstraints 시 부모-자식 컨스트레인트 생성
	//   - bDisableCollisionByDefault 시 인접 바디쌍 충돌 비활성화
	// 를 수행한다. 현재는 UI hook 만 열어둔 stub 상태.
	UE_LOG("[Physics] GeneratePhysicsBodies() is not implemented yet (UI stub).");
}

// ─────────────────────────────────────────────────────────────────────────────
// Physics 시뮬레이션 (래그돌 미리보기)
//   하단 트랜스포트 바(RenderPhysicsTransportBar)의 버튼이 호출하는 진입점.
//   실제 시뮬레이션 로직(PhysX 런타임 생성/바디·조인트 구성/스텝/포즈 복원)은
//   시뮬레이션 담당자가 구현한다. 현재는 UI hook 만 열어둔 stub 상태.
// ─────────────────────────────────────────────────────────────────────────────
// 진단 사슬: World가 PhysicsRuntime 생성·관리 → PhysicsAsset 확보 → InstantiatePhysicsAssetBodies
//   → CreateRagdoll(bSimulatingPhysics=true) → Simulate(dt) → ApplyPhysicsToBones write-back.
// 본 진입점은 진단 사슬의 마지막 두 화살표(Simulate / write-back trigger)를 잇는 역할만 한다.
// Phyics 코어(PhysXRuntime)/SkeletalMeshComponent 의 공개 API를 호출만 한다.
void FMeshEditorWidget::StartPhysicsSimulation()
{
	if (bSimulating)
	{
		UE_LOG("[Physics] StartPhysicsSimulation skipped: already simulating.");
		return;
	}

	UWorld* PreviewWorld = ViewportClient.GetPreviewWorld();
	USkeletalMeshComponent* MeshComp = ViewportClient.GetPreviewMeshComponent();
	if (!PreviewWorld || !MeshComp)
	{
		UE_LOG("[Physics] StartPhysicsSimulation skipped: missing PreviewWorld(%p) or PreviewMeshComponent(%p).",
			(void*)PreviewWorld, (void*)MeshComp);
		return;
	}

	IPhysicsRuntime* Runtime = PreviewWorld->GetPhysicsRuntime();
	if (!Runtime)
	{
		UE_LOG("[Physics] StartPhysicsSimulation skipped: PreviewWorld has no IPhysicsRuntime.");
		return;
	}

	// 에디터가 들고 있는(메모리상 편집 중) PhysicsAsset 을 명시적으로 넘긴다 — Mesh->PhysicsAsset 이
	// 아직 null 인 경우(런타임 자동 로드 경로 부재)에도 진단된 stub 진입점에서 동작하도록.
	UPhysicsAsset* PA = PhysicsTabState.PhysicsAsset;
	if (!PA)
	{
		UE_LOG("[Physics] StartPhysicsSimulation skipped: no editing PhysicsAsset.");
		return;
	}

	const bool bInstantiated = MeshComp->InstantiatePhysicsAssetBodies(*Runtime, PA);
	if (!bInstantiated)
	{
		// InstantiatePhysicsAssetBodies 내부 early-return 사유(bone 못 찾음, AggregateGeom empty 등)는
		// 이미 UE_LOG 로 남아 있다. 여기서는 진입점이 실패했음을 명시.
		UE_LOG("[Physics] StartPhysicsSimulation: InstantiatePhysicsAssetBodies returned false (no valid bodies created).");
		return;
	}

	MeshComp->CreateRagdoll();
	if (!MeshComp->IsSimulatingPhysics())
	{
		// CreateRagdoll 가드(PhysicsRuntimeOwner / Bodies.empty()) 에 막힌 경우 — 이론상 위 instantiate 가
		// true 였다면 통과하지만 방어적으로 체크하고 정리한다.
		UE_LOG("[Physics] StartPhysicsSimulation: CreateRagdoll did not enter simulating state. Reverting.");
		MeshComp->DestroyPhysicsAssetBodies();
		return;
	}

	bSimulating = true;
	bSimPaused  = false;
	UE_LOG("[Physics] Simulation started. BodyCount=%d, ConstraintCount=%d",
		(int32)MeshComp->GetPhysicsBodies().size(),
		(int32)MeshComp->GetPhysicsConstraints().size());
}

void FMeshEditorWidget::StopPhysicsSimulation()
{
	if (!bSimulating)
	{
		return;
	}

	bSimulating = false;
	bSimPaused  = false;

	USkeletalMeshComponent* MeshComp = ViewportClient.GetPreviewMeshComponent();
	if (MeshComp)
	{
		if (!MeshComp->GetPhysicsBodies().empty())
		{
			MeshComp->DestroyPhysicsAssetBodies();
		}
		if (MeshComp->IsSimulatingPhysics())
		{
			// USkeletalMeshComponent::bSimulatingPhysics 는 CreateRagdoll 에서만 true 로 토글되고
			// 다시 false 로 되돌리는 공개 API 가 없다(진단 결과 — 신규 setter 신설은 이번 사이클 범위 밖).
			// 결과적으로 같은 컴포넌트로 다시 Start 를 누르면 ApplyPhysicsToBones 경로에 머무른다.
			UE_LOG("[Physics] StopPhysicsSimulation: USkeletalMeshComponent has no public API to clear bSimulatingPhysics; component remains in ragdoll mode until destruction.");
		}
	}

	UE_LOG("[Physics] Simulation stopped.");
}

void FMeshEditorWidget::TickPhysicsSimulation(float DeltaTime)
{
	if (!bSimulating || bSimPaused)
	{
		return;
	}

	UWorld* PreviewWorld = ViewportClient.GetPreviewWorld();
	USkeletalMeshComponent* MeshComp = ViewportClient.GetPreviewMeshComponent();
	if (!PreviewWorld || !MeshComp)
	{
		return;
	}

	IPhysicsRuntime* Runtime = PreviewWorld->GetPhysicsRuntime();
	if (!Runtime)
	{
		return;
	}

	const float SimDt = DeltaTime * (std::max)(0.0f, SimSpeed);
	if (SimDt <= 0.0f)
	{
		return;
	}

	// UWorld::Tick 의 `bHasBegunPlay && PhysicsRuntime` 가드 때문에 에디터 PreviewWorld
	// (InitWorld 만 호출, BeginPlay 미호출)에서는 World::Tick 안에서 Runtime->Simulate 가 돌지 않는다.
	// 또한 PreviewWorld->Tick 의 TickManager 경로도 HasActorBegunPlay() 가드에 막혀 PreviewMeshComponent
	// ::TickComponent 를 디스패치하지 못한다. 따라서 Runtime 스텝과 write-back 을 모두 여기서 직접 돌린다:
	// Simulate(fetchResults 포함) 직후 같은 틱에서 ApplyPhysicsToBones 를 호출해 body pose 를 본에 푸시한다.
	Runtime->Simulate(SimDt);
	MeshComp->ApplyPhysicsToBones();
}

void FMeshEditorWidget::RenderPhysicsTransportBar(float Width)
{
	if (Width < 1.f) Width = 1.f;
	ImGui::BeginChild("##PhysTransport", ImVec2(Width, 30), false);

	if (!bSimulating)
	{
		if (ImGui::Button("Simulate"))
			StartPhysicsSimulation();
	}
	else
	{
		if (ImGui::Button(bSimPaused ? "Resume" : "Pause"))
			bSimPaused = !bSimPaused;
		ImGui::SameLine();
		if (ImGui::Button("Stop"))
			StopPhysicsSimulation();
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.3f, 1.f, 0.4f, 1.f), bSimPaused ? "Paused" : "Simulating");
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(140.f);
	ImGui::SliderFloat("Speed", &SimSpeed, 0.05f, 2.0f, "x%.2f");

	ImGui::EndChild();
}

// ─────────────────────────────────────────────────────────────────────────────
// Physics 셰이프 기즈모
// ─────────────────────────────────────────────────────────────────────────────

// 하나뿐인 뷰포트 기즈모를 선택 상태에 맞춰 reconcile 한다 (매 프레임 호출됨):
//   1) 셰이프 선택 → 셰이프 기즈모   2) 컨스트레인트 선택 → anchor 기즈모   3) 없음 → 비활성
void FMeshEditorWidget::UpdatePhysicsShapeGizmo()
{
    UGizmoComponent* Gizmo = ViewportClient.GetGizmo();
    if (!Gizmo) return;

    using EShapeType = FPhysicsEditTabState::EShapeType;
    UPhysicsAsset* PA = PhysicsTabState.PhysicsAsset;

    USkeletalMeshComponent* MeshComp = ViewportClient.GetPreviewMeshComponent();
    const FSkeletalMesh* MeshAsset = (MeshComp && MeshComp->GetSkeletalMesh())
        ? MeshComp->GetSkeletalMesh()->GetSkeletalMeshAsset() : nullptr;
    UWorld* PreviewWorld = ViewportClient.GetPreviewWorld();

    // 본 이름 → 월드 트랜스폼 (못 찾으면 identity)
    auto GetBoneWorld = [&](const FString& BoneName, FVector& OutPos, FQuat& OutQuat)
    {
        OutPos  = FVector(0, 0, 0);
        OutQuat = FQuat::Identity;
        if (!MeshComp || !MeshAsset) return;
        for (int32 i = 0; i < (int32)MeshAsset->Bones.size(); ++i)
        {
            if (MeshAsset->Bones[i].Name == BoneName)
            {
                OutPos  = MeshComp->GetBoneLocationByIndex(i);
                OutQuat = MeshComp->GetBoneQuatByIndex(i);
                return;
            }
        }
    };

    // ── 1) 셰이프 선택 시: 셰이프 기즈모 ──────────────────────
    if (PA
        && PhysicsTabState.SelectedBodySetupIndex >= 0
        && PhysicsTabState.SelectedBodySetupIndex < (int32)PA->GetBodySetups().size()
        && PhysicsTabState.SelectedShapeType      != EShapeType::None
        && PhysicsTabState.SelectedShapeElemIndex >= 0)
    {
        UBodySetup* BS = PA->GetBodySetupsMutable()[PhysicsTabState.SelectedBodySetupIndex];
        const int32 ElemIdx = PhysicsTabState.SelectedShapeElemIndex;

        FKSphereElem*  SphereElem  = nullptr;
        FKBoxElem*     BoxElem     = nullptr;
        FKCapsuleElem* CapsuleElem = nullptr;
        switch (PhysicsTabState.SelectedShapeType)
        {
        case EShapeType::Sphere:
            if (ElemIdx < (int32)BS->AggregateGeom.SphereElems.size())
                SphereElem = &BS->AggregateGeom.SphereElems[ElemIdx];
            break;
        case EShapeType::Box:
            if (ElemIdx < (int32)BS->AggregateGeom.BoxElems.size())
                BoxElem = &BS->AggregateGeom.BoxElems[ElemIdx];
            break;
        case EShapeType::Capsule:
            if (ElemIdx < (int32)BS->AggregateGeom.CapsuleElems.size())
                CapsuleElem = &BS->AggregateGeom.CapsuleElems[ElemIdx];
            break;
        default: break;
        }

        if (SphereElem || BoxElem || CapsuleElem)
        {
            FVector BoneWorldPos; FQuat BoneWorldQuat;
            GetBoneWorld(BS->BoneName, BoneWorldPos, BoneWorldQuat);
            if (SphereElem)
                PhysicsTabState.ShapeGizmoTarget.BindSphere(SphereElem, PreviewWorld, BoneWorldPos, BoneWorldQuat);
            else if (BoxElem)
                PhysicsTabState.ShapeGizmoTarget.BindBox(BoxElem, PreviewWorld, BoneWorldPos, BoneWorldQuat);
            else
                PhysicsTabState.ShapeGizmoTarget.BindCapsule(CapsuleElem, PreviewWorld, BoneWorldPos, BoneWorldQuat);
            PhysicsTabState.ConstraintGizmoTarget.Unbind();
            Gizmo->SetTarget(&PhysicsTabState.ShapeGizmoTarget);
            return;
        }
    }

    // ── 2) 컨스트레인트 선택 시: anchor 기즈모 ────────────────
    //   anchor 는 부모 본 로컬 기준 (런타임 SkeletalMeshComponent 규약과 일치).
    if (PA
        && PhysicsTabState.SelectedConstraintIndex >= 0
        && PhysicsTabState.SelectedConstraintIndex < (int32)PA->GetConstraints().size())
    {
        UPhysicsConstraintSetup* CS = PA->GetConstraintsMutable()[PhysicsTabState.SelectedConstraintIndex];
        if (CS)
        {
            FVector ParentPos; FQuat ParentQuat;
            GetBoneWorld(CS->ParentBoneName, ParentPos, ParentQuat);
            PhysicsTabState.ConstraintGizmoTarget.Bind(
                &CS->ParentAnchorPos, &CS->ParentAnchorRot, PreviewWorld, ParentPos, ParentQuat);
            PhysicsTabState.ShapeGizmoTarget.Unbind();
            Gizmo->SetTarget(&PhysicsTabState.ConstraintGizmoTarget);
            return;
        }
    }

    // ── 3) 선택 없음 ─────────────────────────────────────────
    PhysicsTabState.ShapeGizmoTarget.Unbind();
    PhysicsTabState.ConstraintGizmoTarget.Unbind();
    Gizmo->Deactivate();
}

// ─────────────────────────────────────────────────────────────────────────────
// 뷰포트 레이 피킹 — 클릭한 셰이프(바디)/컨스트레인트 선택
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	float PhDot(const FVector& A, const FVector& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }

	// 레이(정규화 D) vs 구. 가장 가까운 t>=0 반환.
	bool PhRaySphere(const FVector& O, const FVector& D, const FVector& C, float R, float& OutT)
	{
		const FVector L(O.X - C.X, O.Y - C.Y, O.Z - C.Z);
		const float B = PhDot(L, D);
		const float Cc = PhDot(L, L) - R * R;
		const float Disc = B * B - Cc;
		if (Disc < 0.f) return false;
		const float S = sqrtf(Disc);
		float T = -B - S;
		if (T < 0.f) T = -B + S;
		if (T < 0.f) return false;
		OutT = T;
		return true;
	}

	// 레이 vs OBB(중심 C, 회전 Q, 반치수 H). 슬랩 방식.
	bool PhRayOBB(const FVector& O, const FVector& D, const FVector& C, const FQuat& Q, const FVector& H, float& OutT)
	{
		const FQuat QI = Q.Inverse();
		const FVector LO = QI.RotateVector(FVector(O.X - C.X, O.Y - C.Y, O.Z - C.Z));
		const FVector LD = QI.RotateVector(D);
		float TMin = -1e30f, TMax = 1e30f;
		const float Lo[3] = { LO.X, LO.Y, LO.Z };
		const float Di[3] = { LD.X, LD.Y, LD.Z };
		const float He[3] = { H.X, H.Y, H.Z };
		for (int32 a = 0; a < 3; ++a)
		{
			if (fabsf(Di[a]) < 1e-6f)
			{
				if (Lo[a] < -He[a] || Lo[a] > He[a]) return false;
			}
			else
			{
				float T1 = (-He[a] - Lo[a]) / Di[a];
				float T2 = ( He[a] - Lo[a]) / Di[a];
				if (T1 > T2) { float Tmp = T1; T1 = T2; T2 = Tmp; }
				if (T1 > TMin) TMin = T1;
				if (T2 < TMax) TMax = T2;
				if (TMin > TMax) return false;
			}
		}
		float T = TMin >= 0.f ? TMin : TMax;
		if (T < 0.f) return false;
		OutT = T;
		return true;
	}

	// 레이 vs 캡슐(세그먼트 A-B, 반지름 R). 세그먼트를 따라 구를 샘플링해 근사.
	bool PhRayCapsule(const FVector& O, const FVector& D, const FVector& A, const FVector& B, float R, float& OutT)
	{
		constexpr int32 Samples = 7;
		bool bHit = false; float Best = 1e30f;
		for (int32 i = 0; i < Samples; ++i)
		{
			const float U = (float)i / (float)(Samples - 1);
			const FVector C(A.X + (B.X - A.X) * U, A.Y + (B.Y - A.Y) * U, A.Z + (B.Z - A.Z) * U);
			float T;
			if (PhRaySphere(O, D, C, R, T) && T < Best) { Best = T; bHit = true; }
		}
		if (bHit) OutT = Best;
		return bHit;
	}
}

void FMeshEditorWidget::PickPhysicsAtScreen(float LocalX, float LocalY, float VpW, float VpH)
{
	UPhysicsAsset* PA = PhysicsTabState.PhysicsAsset;
	USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
	const FSkeletalMesh* MeshAsset = (Comp && Comp->GetSkeletalMesh())
		? Comp->GetSkeletalMesh()->GetSkeletalMeshAsset() : nullptr;
	if (!PA || !Comp || !MeshAsset || VpW < 1.f || VpH < 1.f) return;

	FMinimalViewInfo POV;
	if (!ViewportClient.GetCameraView(POV)) return;
	const FRay Ray = POV.DeprojectScreenToWorld(LocalX, LocalY, VpW, VpH);
	FVector O = Ray.Origin;
	FVector D = Ray.Direction;
	const float DLen = sqrtf(D.X * D.X + D.Y * D.Y + D.Z * D.Z);
	if (DLen < 1e-6f) return;
	D = FVector(D.X / DLen, D.Y / DLen, D.Z / DLen);

	using EShapeType = FPhysicsEditTabState::EShapeType;

	// ── 컨스트레인트 우선 (셰이프보다 먼저) ──
	// 시각화한 스윙 콘 / 트위스트 호의 화면 투영 영역(삼각형 팬) 안을 클릭하면 선택.
	// 관절 핸들 점 근처도 포함. 겹치면 관절이 클릭에 가장 가까운 것을 택한다.
	{
		const FMatrix VPm = POV.CalculateViewProjectionMatrix();
		const float D2R = 3.14159265f / 180.f;
		const ImVec2 Click(LocalX, LocalY);

		auto ProjL = [&](const FVector& W, ImVec2& Out) -> bool
		{ return WorldToScreenPhysics(VPm, W, ImVec2(0.f, 0.f), ImVec2(VpW, VpH), Out); };
		auto Sign = [](const ImVec2& A, const ImVec2& B, const ImVec2& C) -> float
		{ return (A.x - C.x) * (B.y - C.y) - (B.x - C.x) * (A.y - C.y); };
		auto InTri = [&](const ImVec2& P, const ImVec2& A, const ImVec2& B, const ImVec2& C) -> bool
		{
			const float D1 = Sign(P, A, B), D2 = Sign(P, B, C), D3 = Sign(P, C, A);
			const bool bNeg = (D1 < 0.f) || (D2 < 0.f) || (D3 < 0.f);
			const bool bPos = (D1 > 0.f) || (D2 > 0.f) || (D3 > 0.f);
			return !(bNeg && bPos);
		};

		float BestScore = 1e30f;
		int32 BestC = -1;
		const auto& Cons = PA->GetConstraints();
		for (int32 i = 0; i < (int32)Cons.size(); ++i)
		{
			UPhysicsConstraintSetup* CS = Cons[i];
			if (!CS) continue;
			const int32 Pi = Comp->FindBoneIndex(CS->ParentBoneName);
			if (Pi < 0) continue;

			// 관절 프레임 = 부모 본 월드 ⊗ anchor (런타임/기즈모 규약과 일치)
			const FVector PPos   = Comp->GetBoneLocationByIndex(Pi);
			const FQuat   PQ     = Comp->GetBoneQuatByIndex(Pi);
			const FVector Origin = PPos + PQ.RotateVector(CS->ParentAnchorPos);
			const FQuat   Q      = PQ * CS->ParentAnchorRot;
			const FVector AxisX  = Q.RotateVector(FVector(1.f, 0.f, 0.f));
			const FVector AxisY  = Q.RotateVector(FVector(0.f, 1.f, 0.f));
			const FVector AxisZ  = Q.RotateVector(FVector(0.f, 0.f, 1.f));

			float R = 0.f;
			if (UBodySetup* BS = PA->FindBodySetup(CS->ChildBoneName))
			{
				const FKAggregateGeom& G = BS->AggregateGeom;
				if (!G.CapsuleElems.empty()) R = G.CapsuleElems[0].HalfHeight + G.CapsuleElems[0].Radius;
				else if (!G.SphereElems.empty()) R = G.SphereElems[0].Radius * 2.f;
				else if (!G.BoxElems.empty())    R = G.BoxElems[0].HalfZ * 2.f;
			}
			if (R <= 0.f) R = 5.f;
			R *= 1.1f * (2.f / 3.f);

			ImVec2 S0;
			if (!ProjL(Origin, S0)) continue;
			const float Dpx = sqrtf((S0.x - LocalX) * (S0.x - LocalX) + (S0.y - LocalY) * (S0.y - LocalY));
			bool bHit = (Dpx < 10.f); // 관절 점 근처

			// 스윙 콘 팬
			if (!bHit && (CS->Swing1Motion != EConstraintMotion::Locked || CS->Swing2Motion != EConstraintMotion::Locked))
			{
				const float S1 = (CS->Swing1Motion == EConstraintMotion::Free ? 90.f : CS->Swing1LimitAngle) * D2R;
				const float S2 = (CS->Swing2Motion == EConstraintMotion::Free ? 90.f : CS->Swing2LimitAngle) * D2R;
				constexpr int32 N = 24;
				ImVec2 Prev; bool bHavePrev = false;
				for (int32 k = 0; k <= N && !bHit; ++k)
				{
					const float Th = (float)k / (float)N * 2.f * 3.14159265f;
					const float Ay = sinf(S1) * cosf(Th);
					const float Az = sinf(S2) * sinf(Th);
					const float Ax = sqrtf((std::max)(0.f, 1.f - Ay * Ay - Az * Az));
					const FVector Dir = AxisX * Ax + AxisY * Ay + AxisZ * Az;
					ImVec2 P;
					if (ProjL(Origin + Dir * R, P))
					{
						if (bHavePrev && InTri(Click, S0, Prev, P)) bHit = true;
						Prev = P; bHavePrev = true;
					}
					else bHavePrev = false;
				}
			}
			// 트위스트 호 팬 (Y-Z 평면)
			if (!bHit && CS->TwistMotion == EConstraintMotion::Limited)
			{
				const float Lim = CS->TwistLimitAngle * D2R;
				constexpr int32 N = 24;
				ImVec2 Prev; bool bHavePrev = false;
				for (int32 k = 0; k <= N && !bHit; ++k)
				{
					const float A = -Lim + (2.f * Lim) * ((float)k / (float)N);
					const FVector Dir = AxisY * cosf(A) + AxisZ * sinf(A);
					ImVec2 P;
					if (ProjL(Origin + Dir * (R * 0.6f), P))
					{
						if (bHavePrev && InTri(Click, S0, Prev, P)) bHit = true;
						Prev = P; bHavePrev = true;
					}
					else bHavePrev = false;
				}
			}

			if (bHit && Dpx < BestScore) { BestScore = Dpx; BestC = i; }
		}

		if (BestC >= 0)
		{
			PhysicsTabState.SelectedConstraintIndex = BestC;
			PhysicsTabState.SelectedBodySetupIndex  = -1;
			PhysicsTabState.SelectedShapeType       = EShapeType::None;
			PhysicsTabState.SelectedShapeElemIndex  = -1;
			UpdatePhysicsShapeGizmo(); // 선택된 컨스트레인트의 anchor 기즈모로 전환
			return;
		}
	}

	float      BestT    = 1e30f;
	int32      BestBody = -1;
	int32      BestBone = -1;
	EShapeType BestType = EShapeType::None;
	int32      BestElem = -1;

	const auto& Bodies = PA->GetBodySetups();
	for (int32 b = 0; b < (int32)Bodies.size(); ++b)
	{
		UBodySetup* BS = Bodies[b];
		if (!BS) continue;
		const int32 Bi = Comp->FindBoneIndex(BS->BoneName);
		if (Bi < 0) continue;
		const FVector BP = Comp->GetBoneLocationByIndex(Bi);
		const FQuat   BQ = Comp->GetBoneQuatByIndex(Bi);

		for (int32 i = 0; i < (int32)BS->AggregateGeom.SphereElems.size(); ++i)
		{
			const FKSphereElem& E = BS->AggregateGeom.SphereElems[i];
			const FVector C = BP + BQ.RotateVector(E.Center);
			float T;
			if (PhRaySphere(O, D, C, E.Radius, T) && T < BestT)
			{ BestT = T; BestBody = b; BestBone = Bi; BestType = EShapeType::Sphere; BestElem = i; }
		}
		for (int32 i = 0; i < (int32)BS->AggregateGeom.BoxElems.size(); ++i)
		{
			const FKBoxElem& E = BS->AggregateGeom.BoxElems[i];
			const FVector C = BP + BQ.RotateVector(E.Center);
			const FQuat   Q = BQ * E.Rotation;
			float T;
			if (PhRayOBB(O, D, C, Q, FVector(E.HalfX, E.HalfY, E.HalfZ), T) && T < BestT)
			{ BestT = T; BestBody = b; BestBone = Bi; BestType = EShapeType::Box; BestElem = i; }
		}
		for (int32 i = 0; i < (int32)BS->AggregateGeom.CapsuleElems.size(); ++i)
		{
			const FKCapsuleElem& E = BS->AggregateGeom.CapsuleElems[i];
			const FVector C  = BP + BQ.RotateVector(E.Center);
			const FQuat   Q  = BQ * E.Rotation;
			const FVector Up = Q.RotateVector(FVector(0.f, 0.f, 1.f));
			const FVector A(C.X - Up.X * E.HalfHeight, C.Y - Up.Y * E.HalfHeight, C.Z - Up.Z * E.HalfHeight);
			const FVector Bm(C.X + Up.X * E.HalfHeight, C.Y + Up.Y * E.HalfHeight, C.Z + Up.Z * E.HalfHeight);
			float T;
			if (PhRayCapsule(O, D, A, Bm, E.Radius, T) && T < BestT)
			{ BestT = T; BestBody = b; BestBone = Bi; BestType = EShapeType::Capsule; BestElem = i; }
		}
	}

	if (BestBody >= 0)
	{
		PhysicsTabState.SelectedBodySetupIndex  = BestBody;
		PhysicsTabState.SelectedConstraintIndex = -1;
		PhysicsTabState.SelectedShapeType       = BestType;
		PhysicsTabState.SelectedShapeElemIndex  = BestElem;
		SelectedBoneIndex                       = BestBone;
		UpdatePhysicsShapeGizmo();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Physics 통계 오버레이 (언리얼 PhAT 좌상단 통계 대응)
// ─────────────────────────────────────────────────────────────────────────────
void FMeshEditorWidget::DrawPhysicsStatsOverlay(ImDrawList* DL, ImVec2 VPMin) const
{
    UPhysicsAsset* PA = PhysicsTabState.PhysicsAsset;
    if (!PA || !DL) return;

    const auto& Bodies = PA->GetBodySetups();
    int32 NumBodies   = 0;
    int32 NumSpheres  = 0;
    int32 NumBoxes    = 0;
    int32 NumCapsules = 0;
    for (UBodySetup* BS : Bodies)
    {
        if (!BS) continue;
        ++NumBodies;
        NumSpheres  += (int32)BS->AggregateGeom.SphereElems.size();
        NumBoxes    += (int32)BS->AggregateGeom.BoxElems.size();
        NumCapsules += (int32)BS->AggregateGeom.CapsuleElems.size();
    }
    const int32 NumPrimitives  = NumSpheres + NumBoxes + NumCapsules;
    const int32 NumConstraints = (int32)PA->GetConstraints().size();
    const int32 NumDisabled    = PA->GetNumDisabledCollisionPairs();

    char Line0[128], Line1[160], Line2[96], Line3[96];
    snprintf(Line0, sizeof(Line0), "%d Bodies", NumBodies);
    snprintf(Line1, sizeof(Line1), "%d Primitives (%d Spheres, %d Boxes, %d Capsules)",
             NumPrimitives, NumSpheres, NumBoxes, NumCapsules);
    snprintf(Line2, sizeof(Line2), "%d Constraints", NumConstraints);
    snprintf(Line3, sizeof(Line3), "%d Disabled Collision Pairs", NumDisabled);

    const ImU32 Col    = IM_COL32(235, 235, 235, 255);
    const ImU32 Shadow = IM_COL32(0, 0, 0, 200);
    const float X = VPMin.x + 8.f;
    float Y = VPMin.y + 34.f;   // 상단 툴바(28px) 아래
    const char* Lines[] = { Line0, Line1, Line2, Line3 };
    for (const char* L : Lines)
    {
        DL->AddText(ImVec2(X + 1.f, Y + 1.f), Shadow, L);
        DL->AddText(ImVec2(X, Y), Col, L);
        Y += ImGui::GetTextLineHeight() + 2.f;
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

void FMeshEditorWidget::DrawPhysicsShapeOverlays(ImDrawList* DL, ImVec2 VPMin, ImVec2 VPSize) const
{
    UPhysicsAsset* PA = PhysicsTabState.PhysicsAsset;
    if (!PA) return;

    FMinimalViewInfo POV;
    if (!ViewportClient.GetCameraView(POV)) return;
    const FMatrix VP = POV.CalculateViewProjectionMatrix();

    // 셰이프 와이어는 UPhysicsShapeDebugComponent(프록시) 가 3D NoDepth 라인으로 그린다.
    // 여기서는 선택된 컨스트레인트의 각 한계만 스크린 공간 오버레이로 시각화한다.
    DrawConstraintLimitsOverlay(DL, VP, VPMin, VPSize);
}

// ─────────────────────────────────────────────────────────────────────────────
// 컨스트레인트 각 한계 시각화 — 트위스트 호 + 스윙 콘 (선택된 컨스트레인트만)
//   조인트 프레임 = 부모 본 월드 ⊗ ParentAnchor (런타임 SkeletalMeshComponent 규약 / anchor 기즈모와 일치).
//   PhysX D6 규약: Twist=X축, Swing1=Y축, Swing2=Z축.
// ─────────────────────────────────────────────────────────────────────────────
void FMeshEditorWidget::DrawConstraintLimitsOverlay(ImDrawList* DL, const FMatrix& VP, ImVec2 VPMin, ImVec2 VPSize) const
{
    UPhysicsAsset* PA = PhysicsTabState.PhysicsAsset;
    if (!PA) return;

    USkeletalMeshComponent* MeshComp = ViewportClient.GetPreviewMeshComponent();
    const FSkeletalMesh* MeshAsset = MeshComp && MeshComp->GetSkeletalMesh()
        ? MeshComp->GetSkeletalMesh()->GetSkeletalMeshAsset() : nullptr;
    if (!MeshComp || !MeshAsset) return;

    auto Proj = [&](const FVector& W, ImVec2& Out) -> bool
    { return WorldToScreenPhysics(VP, W, VPMin, VPSize, Out); };

    const float D2R = 3.14159265f / 180.f;

    // 하나의 컨스트레인트 한계를 그린다. bSel 이면 밝게, 아니면 흐리게.
    auto DrawOne = [&](UPhysicsConstraintSetup* CS, bool bSel)
    {
        if (!CS) return;
        const int32 Pi = MeshComp->FindBoneIndex(CS->ParentBoneName);
        if (Pi < 0) return;

        // 관절 프레임 = 부모 본 월드 ⊗ anchor (런타임/기즈모 규약과 일치)
        const FVector PPos   = MeshComp->GetBoneLocationByIndex(Pi);
        const FQuat   PQ     = MeshComp->GetBoneQuatByIndex(Pi);
        const FVector Origin = PPos + PQ.RotateVector(CS->ParentAnchorPos);
        const FQuat   Q      = PQ * CS->ParentAnchorRot;
        const FVector AxisX  = Q.RotateVector(FVector(1.f, 0.f, 0.f)); // twist
        const FVector AxisY  = Q.RotateVector(FVector(0.f, 1.f, 0.f)); // swing1
        const FVector AxisZ  = Q.RotateVector(FVector(0.f, 0.f, 1.f)); // swing2

        // 시각화 반경: 자식 바디의 첫 프리미티브 크기 기준 (2/3 축소).
        float R = 0.f;
        if (UBodySetup* BS = PA->FindBodySetup(CS->ChildBoneName))
        {
            const FKAggregateGeom& G = BS->AggregateGeom;
            if (!G.CapsuleElems.empty()) R = G.CapsuleElems[0].HalfHeight + G.CapsuleElems[0].Radius;
            else if (!G.SphereElems.empty()) R = G.SphereElems[0].Radius * 2.f;
            else if (!G.BoxElems.empty())    R = G.BoxElems[0].HalfZ * 2.f;
        }
        if (R <= 0.f) R = 5.f;
        R *= 1.1f * (2.f / 3.f);

        const int32 AArc = bSel ? 255 : 90; // 라인 알파
        const int32 ACone = bSel ? 200 : 70;

        // 관절 핸들 점 (클릭 타겟) — 선택 시 주황, 평소엔 흰색
        {
            ImVec2 H;
            if (Proj(Origin, H))
            {
                const ImU32 HC = bSel ? IM_COL32(255, 200, 40, 255) : IM_COL32(240, 240, 240, 220);
                DL->AddCircleFilled(H, bSel ? 5.f : 4.f, HC);
                DL->AddCircle(H, bSel ? 5.f : 4.f, IM_COL32(20, 20, 20, 255), 0, 1.5f);
            }
        }

        // 축 라인 (트위스트축=빨강)
        {
            ImVec2 A, B;
            if (Proj(Origin, A) && Proj(Origin + AxisX * R, B))
                DL->AddLine(A, B, IM_COL32(230, 60, 60, AArc), bSel ? 2.f : 1.2f);
        }

        // 스윙 콘 (타원뿔, 청록)
        if (CS->Swing1Motion != EConstraintMotion::Locked || CS->Swing2Motion != EConstraintMotion::Locked)
        {
            const float S1 = (CS->Swing1Motion == EConstraintMotion::Free ? 90.f : CS->Swing1LimitAngle) * D2R;
            const float S2 = (CS->Swing2Motion == EConstraintMotion::Free ? 90.f : CS->Swing2LimitAngle) * D2R;
            const ImU32 Col = IM_COL32(60, 200, 220, ACone);
            constexpr int32 N = 32;
            ImVec2 Prev; bool bHavePrev = false; ImVec2 ApexS; const bool bApex = Proj(Origin, ApexS);
            for (int32 i = 0; i <= N; ++i)
            {
                const float Th = (float)i / (float)N * 2.f * 3.14159265f;
                const float Ay = sinf(S1) * cosf(Th);
                const float Az = sinf(S2) * sinf(Th);
                const float Ax = sqrtf((std::max)(0.f, 1.f - Ay * Ay - Az * Az));
                const FVector Dir = AxisX * Ax + AxisY * Ay + AxisZ * Az;
                ImVec2 P;
                if (Proj(Origin + Dir * R, P))
                {
                    if (bHavePrev) DL->AddLine(Prev, P, Col, 1.5f);
                    if (bApex && bSel && (i % 8 == 0)) DL->AddLine(ApexS, P, IM_COL32(60, 200, 220, 110), 1.f);
                    Prev = P; bHavePrev = true;
                }
                else bHavePrev = false;
            }
        }

        // 트위스트 호 (Y-Z 평면, 초록)
        if (CS->TwistMotion == EConstraintMotion::Limited)
        {
            const float Lim = CS->TwistLimitAngle * D2R;
            const ImU32 Col = IM_COL32(120, 230, 90, AArc);
            constexpr int32 N = 24;
            ImVec2 Prev; bool bHavePrev = false;
            for (int32 i = 0; i <= N; ++i)
            {
                const float A = -Lim + (2.f * Lim) * ((float)i / (float)N);
                const FVector Dir = AxisY * cosf(A) + AxisZ * sinf(A);
                ImVec2 P;
                if (Proj(Origin + Dir * (R * 0.6f), P))
                {
                    if (bHavePrev) DL->AddLine(Prev, P, Col, 1.5f);
                    Prev = P; bHavePrev = true;
                }
                else bHavePrev = false;
            }
            ImVec2 O2, E1, E2;
            if (Proj(Origin, O2))
            {
                if (Proj(Origin + (AxisY * cosf(-Lim) + AxisZ * sinf(-Lim)) * (R * 0.6f), E1)) DL->AddLine(O2, E1, Col, 1.5f);
                if (Proj(Origin + (AxisY * cosf( Lim) + AxisZ * sinf( Lim)) * (R * 0.6f), E2)) DL->AddLine(O2, E2, Col, 1.5f);
            }
        }
    };

    // 모든 컨스트레인트를 항상 표시 (선택된 것은 마지막에 밝게 덧그림)
    const auto& Cons = PA->GetConstraints();
    const int32 Sel = PhysicsTabState.SelectedConstraintIndex;
    for (int32 i = 0; i < (int32)Cons.size(); ++i)
        if (i != Sel) DrawOne(Cons[i], false);
    if (Sel >= 0 && Sel < (int32)Cons.size())
        DrawOne(Cons[Sel], true);
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
