#include "StaticMeshEditorWidget.h"

#include "Component/Debug/GizmoComponent.h"
#include "Component/Debug/StaticMeshCollisionDebugComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "Asset/AssetPackage.h"
#include "Core/Logging/Log.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Static/StaticMeshAsset.h"
#include "Physics/Asset/BodySetup.h"
#include "Physics/Asset/PhysicsGeometry.h"
#include "Runtime/Engine.h"
#include "Serialization/WindowsArchive.h"
#include "Settings/EditorSettings.h"
#include "Slate/SlateApplication.h"
#include "UI/Toolbar/ViewportToolbar.h"
#include "Viewport/Viewport.h"

#include <imgui.h>

namespace
{
	FString FormatStaticMeshStatCount(size_t Value)
	{
		FString Result = std::to_string(Value);
		for (int32 InsertPos = static_cast<int32>(Result.length()) - 3; InsertPos > 0; InsertPos -= 3)
		{
			Result.insert(static_cast<size_t>(InsertPos), ",");
		}
		return Result;
	}
}

static uint32 GNextStaticMeshEditorInstanceId = 0;

FStaticMeshEditorWidget::FStaticMeshEditorWidget()
	: InstanceId(GNextStaticMeshEditorInstanceId++)
{
	const FString Id = std::to_string(InstanceId);
	PreviewWorldHandle = FName("StaticMeshEditorPreview_" + Id);
	WindowIdSuffix = "###StaticMeshEditor_" + Id;
}

bool FStaticMeshEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<UStaticMesh>();
}

bool FStaticMeshEditorWidget::IsEditingObject(UObject* Object) const
{
	if (FAssetEditorWidget::IsEditingObject(Object))
	{
		return true;
	}

	const UStaticMesh* CurrentMesh = Cast<UStaticMesh>(EditedObject);
	const UStaticMesh* RequestedMesh = Cast<UStaticMesh>(Object);
	if (!IsOpen() || !CurrentMesh || !RequestedMesh)
	{
		return false;
	}

	const FString& CurrentPath = CurrentMesh->GetAssetPathFileName();
	return !CurrentPath.empty()
		&& CurrentPath != "None"
		&& CurrentPath == RequestedMesh->GetAssetPathFileName();
}

void FStaticMeshEditorWidget::Open(UObject* Object)
{
	FAssetEditorWidget::Open(Object);

	FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
	WorldContext.World->SetWorldType(EWorldType::EditorPreview);
	WorldContext.World->InitWorld();

	AActor* Actor = WorldContext.World->SpawnActor<AActor>();
	UStaticMeshComponent* PreviewComp = nullptr;
	if (UStaticMesh* Mesh = Cast<UStaticMesh>(EditedObject))
	{
		PreviewComp = Actor->AddComponent<UStaticMeshComponent>();
		PreviewComp->SetStaticMesh(Mesh);
		Actor->SetRootComponent(PreviewComp);
	}
	Actor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));

	const FBoundingBox Bounds = PreviewComp
		? PreviewComp->GetWorldBoundingBox()
		: FBoundingBox(FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f));

	const FVector Extent = Bounds.GetExtent();
	const float FloorZ = Bounds.Min.Z - 0.02f;
	const float FloorScale = (std::max)(Extent.X, Extent.Y) * 10.0f;

	ADirectionalLightActor* LightActor = WorldContext.World->SpawnActor<ADirectionalLightActor>();
	LightActor->InitDefaultComponents();
	LightActor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
	UDirectionalLightComponent* LightComp = LightActor->GetComponentByClass<UDirectionalLightComponent>();
	LightComp->SetShadowBias(0.0f);
	LightComp->PushToScene();

	AStaticMeshActor* FloorActor = WorldContext.World->SpawnActor<AStaticMeshActor>();
	FloorActor->InitDefaultComponents("Content/Data/BasicShape/Cube.OBJ");
	FloorActor->SetActorLocation(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, FloorZ));
	FloorActor->SetActorScale(FVector(FloorScale, FloorScale, 0.02f));

	ImVec2 ViewportSize = ImGui::GetContentRegionAvail();

	if (UStaticMesh* Mesh = Cast<UStaticMesh>(EditedObject))
	{
		CollisionDebugComponent = Actor->AddComponent<UStaticMeshCollisionDebugComponent>();
		CollisionDebugComponent->SetStaticMesh(Mesh);
		ViewportClient.SetCollisionDebugComponent(CollisionDebugComponent);
		LoadShapesFromMesh(Mesh);
	}

	ViewportClient.Initialize(GEngine->GetRenderer().GetFD3DDevice().GetDevice(), static_cast<uint32>(ViewportSize.x), static_cast<uint32>(ViewportSize.y));
	ViewportClient.SetPreviewWorld(WorldContext.World);
	ViewportClient.SetPreviewActor(Actor);
	ViewportClient.SetPreviewMeshComponent(Actor->GetComponentByClass<UStaticMeshComponent>());
	ViewportClient.CreateGizmo();
	ViewportClient.ResetCameraToPreviewBounds();

	SelectedShapeIndex = -1;

	WorldContext.World->SetEditorPOVProvider(&ViewportClient);

	FSlateApplication::Get().RegisterViewport(&ViewportClient);
}

void FStaticMeshEditorWidget::Close()
{
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

	ShapeGizmoTarget.Unbind();
	LocalShapes.clear();
	SelectedShapeIndex    = -1;
	CollisionDebugComponent = nullptr;

	FSlateApplication::Get().UnregisterViewport(&ViewportClient);
	ViewportClient.Release();
}

void FStaticMeshEditorWidget::Tick(float DeltaTime)
{
	if (ViewportClient.IsRenderable())
	{
		ViewportClient.Tick(DeltaTime);
	}

	// 기즈모 드래그가 끝난 프레임에 geometry 갱신 + dirty 마킹
	const bool bIsHolding = ViewportClient.IsGizmoHolding();
	if (bWasGizmoHolding && !bIsHolding)
	{
		if (UStaticMesh* Mesh = Cast<UStaticMesh>(EditedObject))
			FlushShapesToMesh(Mesh);
		ViewportClient.RefreshCollisionDebug();
		MarkDirty();
	}
	// 드래그 중에도 매 프레임 geometry 갱신 (부드러운 시각화)
	else if (bIsHolding)
	{
		if (UGizmoComponent* Gizmo = ViewportClient.GetGizmo())
		{
			if (Gizmo->GetTarget() == &ShapeGizmoTarget)
			{
				if (CollisionDebugComponent)
					CollisionDebugComponent->MarkGeometryDirty();
			}
		}
	}
	bWasGizmoHolding = bIsHolding;
}

void FStaticMeshEditorWidget::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (IsOpen())
	{
		OutClients.push_back(const_cast<FStaticMeshEditorViewportClient*>(&ViewportClient));
	}
}

void FStaticMeshEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	static float DetailsWidth = 300.0f;
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(EditedObject);

	bool bWindowOpen = true;
	FString VisibleTitle = "Static Mesh Editor";
	const FString AssetPath = StaticMesh ? StaticMesh->GetAssetPathFileName() : FString();
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

	ImGui::BeginGroup();
	{
		float AvailableWidth = ImGui::GetContentRegionAvail().x - DetailsWidth - ImGui::GetStyle().ItemSpacing.x;
		ImVec2 Size = ImVec2(AvailableWidth, ImGui::GetContentRegionAvail().y);

		ImVec2 ViewportPos = ImGui::GetCursorScreenPos();
		ViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y);

		FViewport* VP = ViewportClient.GetViewport();
		if (VP && Size.x > 0 && Size.y > 0)
		{
			VP->RequestResize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));

			if (VP->GetSRV())
			{
				ImGui::Image((ImTextureID)VP->GetSRV(), Size);
				// ImGui 인지 hover 를 입력 소유권 중재에 보고.
				FSlateApplication::Get().SetViewportImGuiHovered(&ViewportClient, ImGui::IsItemHovered());
			}

			constexpr float ToolbarHeight = 28.0f;

			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			DrawList->AddRectFilled(ViewportPos,
				ImVec2(ViewportPos.x + Size.x, ViewportPos.y + ToolbarHeight),
				IM_COL32(40, 40, 40, 255));

			FViewportToolbarContext Context;
			Context.Renderer = &GEngine->GetRenderer();
			Context.Settings = &FEditorSettings::Get().MeshEditorViewportSettings;
			Context.RenderOptions = &ViewportClient.GetRenderOptions();
			Context.ToolbarLeft = ViewportPos.x;
			Context.ToolbarTop = ViewportPos.y;
			Context.ToolbarWidth = Size.x;
			Context.bReservePlayStopSpace = false;
			Context.bShowAddActor = false;
			Context.bShowGizmoControls = false;

			FViewportToolbar::Render(Context);
			RenderMeshStatsOverlay(DrawList, ViewportPos);
		}
	}
	ImGui::EndGroup();

	ImGui::SameLine();

	ImGui::BeginChild("Details", ImVec2(DetailsWidth, 0), true);
	ImGui::Text("Static Mesh Details");
	ImGui::Separator();
	RenderDetailsPanel(StaticMesh ? StaticMesh->GetStaticMeshAsset() : nullptr);
	ImGui::Spacing();
	RenderPhysicsPanel(StaticMesh);
	ImGui::Spacing();
	RenderCollisionPanel(StaticMesh);
	ImGui::EndChild();

	ImGui::End();

	if (!bWindowOpen)
	{
		Close();
	}
}

void FStaticMeshEditorWidget::RenderMeshStatsOverlay(ImDrawList* DrawList, const ImVec2& ViewportPos) const
{
	if (!DrawList || !EditedObject)
	{
		return;
	}

	size_t VertexCount = 0;
	size_t TriangleCount = 0;

	if (const UStaticMesh* StaticMesh = Cast<UStaticMesh>(EditedObject))
	{
		if (const FStaticMesh* Asset = StaticMesh->GetStaticMeshAsset())
		{
			VertexCount = Asset->Vertices.size();
			TriangleCount = Asset->Indices.size() / 3;
		}
	}

	const FString Text =
		"Triangles: " + FormatStaticMeshStatCount(TriangleCount) + "\n" +
		"Vertices: " + FormatStaticMeshStatCount(VertexCount);

	const ImVec2 TextPos(ViewportPos.x + 8.0f, ViewportPos.y + 36.0f);
	DrawList->AddText(ImVec2(TextPos.x + 1.0f, TextPos.y + 1.0f), IM_COL32(0, 0, 0, 220), Text.c_str());
	DrawList->AddText(TextPos, IM_COL32(235, 238, 242, 255), Text.c_str());
}

void FStaticMeshEditorWidget::RenderDetailsPanel(FStaticMesh* Asset) const
{
	if (!Asset)
	{
		ImGui::TextDisabled("No static mesh data.");
		return;
	}

	ImGui::Text("Vertices: %s", FormatStaticMeshStatCount(Asset->Vertices.size()).c_str());
	ImGui::Text("Indices: %s", FormatStaticMeshStatCount(Asset->Indices.size()).c_str());
	ImGui::Text("Triangles: %s", FormatStaticMeshStatCount(Asset->Indices.size() / 3).c_str());
	ImGui::Text("Sections: %s", FormatStaticMeshStatCount(Asset->Sections.size()).c_str());
}

void FStaticMeshEditorWidget::RenderCollisionPanel(UStaticMesh* Mesh)
{
	// ── 툴바 ─────────────────────────────────────────────────────
	if (IsDirty()) ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f), "●");
	else           ImGui::TextDisabled("●");
	ImGui::SameLine();
	if (ImGui::SmallButton("Save")) SaveStaticMesh(Mesh);
	ImGui::SameLine();
	ImGui::TextDisabled("Collision");
	ImGui::Separator();

	if (!Mesh) { ImGui::TextDisabled("No mesh loaded."); return; }

	// ── Add 버튼 ─────────────────────────────────────────────────
	const float BW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	auto AddBtn = [&](const char* Label, bool (UStaticMesh::*Fn)()) -> bool
	{
		if (ImGui::Button(Label, ImVec2(BW, 0)))
		{
			if ((Mesh->*Fn)())
			{
				LoadShapesFromMesh(Mesh);
				SelectedShapeIndex = static_cast<int32>(LocalShapes.size()) - 1;
				UpdateShapeGizmo(Mesh);
				ViewportClient.RefreshCollisionDebug();
				MarkDirty();
				return true;
			}
		}
		return false;
	};

	AddBtn("Add Box",     &UStaticMesh::AddDefaultBoxCollisionFromBounds);
	ImGui::SameLine();
	AddBtn("Add Sphere",  &UStaticMesh::AddDefaultSphereCollisionFromBounds);
	AddBtn("Add Capsule", &UStaticMesh::AddDefaultCapsuleCollisionFromBounds);
	if (ImGui::Button("Use Triangle Mesh Collision", ImVec2(-1.0f, 0)))
	{
		if (Mesh->GenerateTriangleMeshCollision())
		{
			LocalShapes.clear();
			SelectedShapeIndex = -1;
			UpdateShapeGizmo(Mesh);
			ViewportClient.RefreshCollisionDebug();
			MarkDirty();
		}
	}

	if (ImGui::Button("Clear All", ImVec2(-1.0f, 0)))
	{
		Mesh->ClearCollisionShapes();
		LocalShapes.clear();
		SelectedShapeIndex = -1;
		UpdateShapeGizmo(Mesh);
		ViewportClient.RefreshCollisionDebug();
		MarkDirty();
	}

	// ── Shape 목록 + 선택 ─────────────────────────────────────────
	ImGui::Spacing();

	if (LocalShapes.empty()) { ImGui::TextDisabled("No collision shapes."); return; }

	ImGui::Text("Shapes (%d):", static_cast<int>(LocalShapes.size()));

	for (int32 i = 0; i < static_cast<int32>(LocalShapes.size()); ++i)
	{
		FStaticMeshCollisionShape& S = LocalShapes[i];
		ImGui::PushID(i);

		const bool bSelected = (i == SelectedShapeIndex);

		// 라벨 생성
		char Label[64];
		switch (S.ShapeType)
		{
		case EStaticMeshCollisionShapeType::Box:
			snprintf(Label, sizeof(Label), "[%d] Box", i); break;
		case EStaticMeshCollisionShapeType::Sphere:
			snprintf(Label, sizeof(Label), "[%d] Sphere", i); break;
		case EStaticMeshCollisionShapeType::Capsule:
			snprintf(Label, sizeof(Label), "[%d] Capsule", i); break;
		case EStaticMeshCollisionShapeType::Convex:
			snprintf(Label, sizeof(Label), "[%d] Convex", i); break;
		default:
			snprintf(Label, sizeof(Label), "[%d] Unknown", i); break;
		}

		if (ImGui::Selectable(Label, bSelected))
		{
			SelectedShapeIndex = i;
			UpdateShapeGizmo(Mesh);
		}

		// 선택된 shape 인라인 편집
		if (bSelected)
		{
			ImGui::Indent(8.0f);

			bool bChanged = false;

			// Center
			float Center[3] = { S.Center.X, S.Center.Y, S.Center.Z };
			if (ImGui::DragFloat3("Center", Center, 0.01f))
			{
				S.Center = FVector(Center[0], Center[1], Center[2]);
				bChanged = true;
			}

			// 타입별 치수
			switch (S.ShapeType)
			{
			case EStaticMeshCollisionShapeType::Box:
			{
				float HE[3] = { S.HalfExtent.X, S.HalfExtent.Y, S.HalfExtent.Z };
				if (ImGui::DragFloat3("Half Extent", HE, 0.01f, 0.01f, 500.f))
				{
					S.HalfExtent = FVector(HE[0], HE[1], HE[2]);
					bChanged = true;
				}
				break;
			}
			case EStaticMeshCollisionShapeType::Sphere:
				if (ImGui::DragFloat("Radius", &S.Radius, 0.01f, 0.01f, 500.f)) bChanged = true;
				break;
			case EStaticMeshCollisionShapeType::Capsule:
				if (ImGui::DragFloat("Radius",      &S.Radius,     0.01f, 0.01f, 500.f)) bChanged = true;
				if (ImGui::DragFloat("Half Height",  &S.HalfHeight, 0.01f, 0.01f, 500.f)) bChanged = true;
				break;
			default: break;
			}

			if (bChanged)
			{
				FlushShapesToMesh(Mesh);
				UpdateShapeGizmo(Mesh);
				if (CollisionDebugComponent) CollisionDebugComponent->MarkGeometryDirty();
				MarkDirty();
			}

			// 현재 기즈모 모드 표시 (Space로 전환)
			if (UGizmoComponent* Gizmo = ViewportClient.GetGizmo())
			{
				ImGui::Spacing();
				const char* ModeName = "Translate";
				switch (Gizmo->GetMode())
				{
				case EGizmoMode::Rotate: ModeName = "Rotate"; break;
				case EGizmoMode::Scale:  ModeName = "Scale";  break;
				default: break;
				}
				ImGui::TextDisabled("Gizmo: %s  [Space]", ModeName);
			}

			// 삭제 버튼
			ImGui::Spacing();
			if (ImGui::SmallButton("Delete Shape"))
			{
				LocalShapes.erase(LocalShapes.begin() + i);
				FlushShapesToMesh(Mesh);
				SelectedShapeIndex = -1;
				UpdateShapeGizmo(Mesh);
				ViewportClient.RefreshCollisionDebug();
				MarkDirty();
				ImGui::Unindent(8.0f);
				ImGui::PopID();
				break;
			}

			ImGui::Unindent(8.0f);
		}

		ImGui::PopID();
	}
}

void FStaticMeshEditorWidget::UpdateShapeGizmo(UStaticMesh* Mesh)
{
	UGizmoComponent* Gizmo = ViewportClient.GetGizmo();
	if (!Gizmo) return;

	if (SelectedShapeIndex >= 0 && SelectedShapeIndex < static_cast<int32>(LocalShapes.size()))
	{
		ShapeGizmoTarget.Bind(&LocalShapes[SelectedShapeIndex], ViewportClient.GetPreviewWorld());
		Gizmo->SetTarget(&ShapeGizmoTarget);
		ViewportClient.ApplyTransformSettingsToGizmo();
	}
	else
	{
		ShapeGizmoTarget.Unbind();
		Gizmo->Deactivate();
	}
}

void FStaticMeshEditorWidget::LoadShapesFromMesh(UStaticMesh* Mesh)
{
	LocalShapes.clear();
	if (!Mesh) return;
	UBodySetup* Body = Mesh->GetBodySetup();
	if (!Body) return;

	for (const FKBoxElem& E : Body->AggregateGeom.BoxElems)
	{
		FStaticMeshCollisionShape S;
		S.ShapeType  = EStaticMeshCollisionShapeType::Box;
		S.Center     = E.Center;
		S.Rotation   = E.Rotation;
		S.HalfExtent = FVector(E.HalfX, E.HalfY, E.HalfZ);
		LocalShapes.push_back(S);
	}
	for (const FKSphereElem& E : Body->AggregateGeom.SphereElems)
	{
		FStaticMeshCollisionShape S;
		S.ShapeType = EStaticMeshCollisionShapeType::Sphere;
		S.Center    = E.Center;
		S.Radius    = E.Radius;
		LocalShapes.push_back(S);
	}
	for (const FKCapsuleElem& E : Body->AggregateGeom.CapsuleElems)
	{
		FStaticMeshCollisionShape S;
		S.ShapeType  = EStaticMeshCollisionShapeType::Capsule;
		S.Center     = E.Center;
		S.Rotation   = E.Rotation;
		S.Radius     = E.Radius;
		S.HalfHeight = E.HalfHeight;
		LocalShapes.push_back(S);
	}
}

void FStaticMeshEditorWidget::FlushShapesToMesh(UStaticMesh* Mesh)
{
	if (!Mesh) return;
	UBodySetup* Body = Mesh->GetOrCreateBodySetup();

	Body->AggregateGeom.BoxElems.clear();
	Body->AggregateGeom.SphereElems.clear();
	Body->AggregateGeom.CapsuleElems.clear();

	for (const FStaticMeshCollisionShape& S : LocalShapes)
	{
		switch (S.ShapeType)
		{
		case EStaticMeshCollisionShapeType::Box:
		{
			FKBoxElem E;
			E.Center   = S.Center;
			E.Rotation = S.Rotation;
			E.HalfX    = S.HalfExtent.X;
			E.HalfY    = S.HalfExtent.Y;
			E.HalfZ    = S.HalfExtent.Z;
			Body->AggregateGeom.BoxElems.push_back(E);
			break;
		}
		case EStaticMeshCollisionShapeType::Sphere:
		{
			FKSphereElem E;
			E.Center = S.Center;
			E.Radius = S.Radius;
			Body->AggregateGeom.SphereElems.push_back(E);
			break;
		}
		case EStaticMeshCollisionShapeType::Capsule:
		{
			FKCapsuleElem E;
			E.Center     = S.Center;
			E.Rotation   = S.Rotation;
			E.Radius     = S.Radius;
			E.HalfHeight = S.HalfHeight;
			Body->AggregateGeom.CapsuleElems.push_back(E);
			break;
		}
		default: break;
		}
	}
}

void FStaticMeshEditorWidget::RenderPhysicsPanel(UStaticMesh* Mesh)
{
	ImGui::TextDisabled("Physical Material");
	ImGui::Separator();

	if (!Mesh) { ImGui::TextDisabled("No mesh loaded."); return; }

	UBodySetup* Body = Mesh->GetBodySetup();
	if (!Body) { ImGui::TextDisabled("No body setup."); return; }

	bool bChanged = false;
	if (ImGui::DragFloat("Friction",    &Body->Friction,    0.001f, 0.0f, 1.0f, "%.3f")) bChanged = true;
	if (ImGui::DragFloat("Restitution", &Body->Restitution, 0.001f, 0.0f, 1.0f, "%.3f")) bChanged = true;

	if (bChanged) MarkDirty();
}

void FStaticMeshEditorWidget::SaveStaticMesh(UStaticMesh* Mesh)
{
	if (!Mesh) return;

	const FString& Path = Mesh->GetAssetPathFileName();
	if (Path.empty() || Path == "None")
	{
		UE_LOG("[StaticMeshEditor] Save failed: mesh has no asset path.");
		return;
	}

	const FString ProjectRelPath = FPaths::MakeProjectRelative(Path);

	FWindowsBinWriter Writer(ProjectRelPath);
	if (!Writer.IsValid())
	{
		UE_LOG("[StaticMeshEditor] Save failed: could not open file. Path=%s", ProjectRelPath.c_str());
		return;
	}

	FAssetPackageHeader Header;
	Header.Type = static_cast<uint32>(EAssetPackageType::StaticMesh);

	FAssetImportMetadata Metadata;
	if (const FStaticMesh* Asset = Mesh->GetStaticMeshAsset())
		Metadata.SourcePath = Asset->PathFileName;

	Writer << Header;
	Writer << Metadata;
	Mesh->Serialize(Writer);

	if (Writer.IsValid())
	{
		UE_LOG("[StaticMeshEditor] Saved: %s", ProjectRelPath.c_str());
		ClearDirty();
	}
	else
	{
		UE_LOG("[StaticMeshEditor] Save failed (write error): %s", ProjectRelPath.c_str());
	}
}
