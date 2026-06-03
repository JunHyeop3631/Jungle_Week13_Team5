#pragma once

#include "Component/MeshComponent.h"
#include "Physics/Cloth/ClothTypes.h"
#include "Physics/PhysicsTypes.h"
#include "Render/Types/VertexTypes.h"
#include "Object/Ptr/SoftObjectPtr.h"

#include "Source/Engine/Component/Primitive/ClothComponent.generated.h"

class FPrimitiveSceneProxy;
class UMaterialInterface;

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
	void PostDuplicate() override;
	void PostEditProperty(const char* PropertyName) override;
	void UpdateWorldAABB() const override;

	FMeshDataView GetMeshDataView() const override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	void SetMaterial(UMaterialInterface* InMaterial);
	UMaterialInterface* GetMaterial() const { return Material; }

	bool RecreateCloth();
	void DestroyCloth();

	bool GetClothRenderData(FClothRenderData& OutRenderData) const;
	bool GetClothStats(FClothStats& OutStats) const;
	bool ExtractClothDebugLines(TArray<FPhysicsDebugLine>& OutLines, const FClothDebugDrawOptions& Options) const;

	const TMeshData<FVertex>& GetCachedMeshData() const;
	const FVector4& GetRenderColor() const { return RenderColor; }
	bool ShouldRenderTwoSided() const { return bRenderTwoSided; }
	const FVector4& GetClothRenderColor() const { return RenderColor; }
	bool ShouldRenderClothTwoSided() const { return bRenderTwoSided; }

private:
	FClothGridDesc BuildGridDesc() const;
	FClothSettings BuildClothSettings() const;
	bool ApplyClothSettings();
	bool UpdatePinnedParticles(bool bResetPreviousParticles);
	void BuildPreviewRenderData() const;
	void UpdateCachedMeshDataFromRenderData(const FClothRenderData& RenderData) const;
	void MarkClothRenderDataDirty();

private:
	FClothInstance* ClothInstance = nullptr;
	FClothFabricHandle ClothFabric;
	UMaterialInterface* Material = nullptr;

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

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Pin Mode", Enum=EClothPinMode)
	EClothPinMode PinMode = EClothPinMode::TopRow;

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Origin")
	FVector ClothOrigin = FVector(0.0f, 0.0f, 200.0f);

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Gravity")
	FVector Gravity = FVector(0.0f, 0.0f, -980.0f);

	UPROPERTY(Edit, Save, Category="Cloth", DisplayName="Damping")
	float Damping = 0.1f;

	UPROPERTY(Edit, Save, Category="Cloth|Solver", DisplayName="Solver Frequency", Min=1.0f, Max=1000.0f, Speed=1.0f)
	float SolverFrequency = 120.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Solver", DisplayName="Stiffness Frequency", Min=1.0f, Max=1000.0f, Speed=1.0f)
	float StiffnessFrequency = 60.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Solver", DisplayName="Continuous Collision")
	bool bContinuousCollision = false;

	UPROPERTY(Edit, Save, Category="Cloth|Constraint", DisplayName="Phase Stiffness", Min=0.0f, Max=1.0f, Speed=0.01f)
	float PhaseStiffness = 1.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Constraint", DisplayName="Tether Scale", Min=0.0f, Max=10.0f, Speed=0.01f)
	float TetherScale = 1.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Constraint", DisplayName="Tether Stiffness", Min=0.0f, Max=1.0f, Speed=0.01f)
	float TetherStiffness = 1.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Collision", DisplayName="Friction", Min=0.0f, Max=1.0f, Speed=0.01f)
	float Friction = 0.0f;

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

	UPROPERTY(Edit, Save, Category="Materials", DisplayName="Material", AssetType="Material")
	FSoftObjectPtr MaterialSlot = "None";
};
