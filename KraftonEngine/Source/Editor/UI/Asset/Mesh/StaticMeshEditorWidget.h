#pragma once

#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Editor/UI/Asset/Mesh/StaticMeshShapeGizmoTarget.h"
#include "Object/FName.h"
#include "Editor/Viewport/Asset/StaticMeshEditorViewportClient.h"

struct FStaticMesh;
struct ImDrawList;
struct ImVec2;
class UStaticMesh;
class UStaticMeshCollisionDebugComponent;

class FStaticMeshEditorWidget : public FAssetEditorWidget
{
public:
	FStaticMeshEditorWidget();

	bool CanEdit(UObject* Object) const override;
	bool IsEditingObject(UObject* Object) const override;

	void Open(UObject* Object) override;
	void Close() override;
	void Tick(float DeltaTime) override;

	void CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const override;

	bool AllowsMultipleInstances() const override { return true; }

	void Render(float DeltaTime) override;

private:
	void RenderMeshStatsOverlay(ImDrawList* DrawList, const ImVec2& ViewportPos) const;
	void RenderDetailsPanel(FStaticMesh* Asset) const;
	void RenderPhysicsPanel(UStaticMesh* Mesh);
	void RenderCollisionPanel(UStaticMesh* Mesh);


	void UpdateShapeGizmo(UStaticMesh* Mesh);
	void SaveStaticMesh(UStaticMesh* Mesh);

	// AggregateGeom ↔ LocalShapes 변환
	void LoadShapesFromMesh(UStaticMesh* Mesh);
	void FlushShapesToMesh(UStaticMesh* Mesh);

private:
	FStaticMeshEditorViewportClient ViewportClient;

	// Collision shape 편집 상태 (에디터 로컬 working copy — 소스 오브 트루스는 AggregateGeom)
	TArray<FStaticMeshCollisionShape> LocalShapes;
	int32 SelectedShapeIndex = -1;
	bool  bWasGizmoHolding   = false;
	FStaticMeshShapeGizmoTarget ShapeGizmoTarget;
	UStaticMeshCollisionDebugComponent* CollisionDebugComponent = nullptr;

	uint32 InstanceId;
	FName PreviewWorldHandle = FName::None;
	FString WindowIdSuffix;
};
