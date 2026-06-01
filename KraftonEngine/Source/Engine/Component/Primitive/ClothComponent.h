#pragma once

#include "Component/MeshComponent.h"
#include "Physics/Cloth/ClothTypes.h"
#include "Physics/PhysicsTypes.h"
#include "Render/Types/VertexTypes.h"

#include "Source/Engine/Component/Primitive/ClothComponent.generated.h"

class FPrimitiveSceneProxy;

UCLASS()
class UClothComponent : public UMeshComponent
{
public:
	GENERATED_BODY()

	UClothComponent();
	~UClothComponent() override;

	void BeginPlay() override;
	void EndPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void PostEditProperty(const char* PropertyName) override;
	void UpdateWorldAABB() const override;

	FMeshDataView GetMeshDataView() const override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	bool RecreateCloth();
	void DestroyCloth();

	bool GetClothRenderData(FClothRenderData& OutRenderData) const;
	bool GetClothStats(FClothStats& OutStats) const;
	bool ExtractClothDebugLines(TArray<FPhysicsDebugLine>& OutLines, const FClothDebugDrawOptions& Options) const;

	const TMeshData<FVertex>& GetCachedMeshData() const;
	const FVector4& GetRenderColor() const { return RenderColor; }
	bool ShouldRenderTwoSided() const { return bRenderTwoSided; }

private:
	FClothGridDesc BuildGridDesc() const;
	FClothSettings BuildClothSettings() const;
	bool ApplyClothSettings();
	void BuildPreviewRenderData() const;
	void UpdateCachedMeshDataFromRenderData(const FClothRenderData& RenderData) const;
	void MarkClothRenderDataDirty();

private:
	FClothInstance* ClothInstance = nullptr;
	FClothFabricHandle ClothFabric;

	mutable FClothRenderData CachedRenderData;
	mutable TMeshData<FVertex> CachedMeshData;
	mutable bool bCachedPreviewDirty = true;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Simulate Cloth")
	bool bSimulateCloth = true;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Columns")
	int32 NumColumns = 20;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Rows")
	int32 NumRows = 20;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Spacing")
	float Spacing = 10.0f;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Pin Top Row")
	bool bPinTopRow = true;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Origin")
	FVector ClothOrigin = FVector(0.0f, 0.0f, 200.0f);

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Gravity")
	FVector Gravity = FVector(0.0f, 0.0f, -980.0f);

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Damping")
	float Damping = 0.1f;

	UPROPERTY(Edit, Save, Category="Cloth|Wind", DisplayName="Wind Velocity")
	FVector WindVelocity = FVector::ZeroVector;

	UPROPERTY(Edit, Save, Category="Cloth|Wind", DisplayName="Wind Drag")
	float WindDragCoefficient = 0.02f;

	UPROPERTY(Edit, Save, Category="Cloth|Wind", DisplayName="Wind Lift")
	float WindLiftCoefficient = 0.0f;

	UPROPERTY(Edit, Save, Category="Rendering", DisplayName="Cloth Color", Type=Color4)
	FVector4 RenderColor = FVector4(0.75f, 0.85f, 1.0f, 1.0f);

	UPROPERTY(Edit, Save, Category="Rendering", DisplayName="Two Sided Cloth")
	bool bRenderTwoSided = true;
};
