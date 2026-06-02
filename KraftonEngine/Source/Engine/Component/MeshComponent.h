#pragma once

#include "Component/PrimitiveComponent.h"
#include "Physics/Cloth/ClothTypes.h"
#include "Physics/PhysicsTypes.h"


#include "Source/Engine/Component/MeshComponent.generated.h"

class IClothScene;
class UMaterialInterface;

UCLASS()
class UMeshComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()
	UMeshComponent() = default;
	~UMeshComponent() override;

	void BeginPlay() override;
	void EndPlay() override;
	void PostEditProperty(const char* PropertyName) override;

	bool IsMeshClothEnabled() const { return bSimulateMeshCloth; }
	bool HasMeshClothInstance() const { return MeshClothInstance && MeshClothInstance->bValid; }
	bool RecreateMeshCloth();
	void DestroyMeshCloth();

	virtual bool GetClothRenderData(FClothRenderData& OutRenderData) const;
	virtual bool GetClothStats(FClothStats& OutStats) const;
	virtual bool ExtractClothDebugLines(TArray<FPhysicsDebugLine>& OutLines, const FClothDebugDrawOptions& Options) const;
	virtual const FVector4& GetClothRenderColor() const { return MeshClothRenderColor; }
	virtual bool ShouldRenderClothTwoSided() const { return bMeshClothTwoSided; }
	virtual UMaterialInterface* GetClothMaterial(int32 ElementIndex) const { (void)ElementIndex; return nullptr; }

	bool UpdateMeshClothWorldAABB() const;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	bool BuildMeshClothDescriptions(FClothFabricDesc& OutFabricDesc, FClothInstanceDesc& OutInstanceDesc) const;
	FClothSettings BuildMeshClothSettings() const;
	bool ApplyMeshClothSettings();
	bool BuildMeshClothPreviewRenderData(FClothRenderData& OutRenderData) const;
	void ResetMeshClothCache() const;

private:
	FClothInstance* MeshClothInstance = nullptr;
	FClothFabricHandle MeshClothFabric;

	mutable FClothRenderData CachedMeshClothRenderData;

	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Simulate Mesh Cloth")
	bool bSimulateMeshCloth = false;

	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Mesh Cloth Pin Mode", Enum=EClothPinMode)
	EClothPinMode MeshClothPinMode = EClothPinMode::TopRow;

	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Mesh Cloth Pin Tolerance")
	float MeshClothPinTolerance = 0.03f;

	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Mesh Cloth Weld Tolerance")
	float MeshClothWeldTolerance = 0.01f;

	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Mesh Cloth Gravity")
	FVector MeshClothGravity = FVector(0.0f, 0.0f, -980.0f);

	UPROPERTY(Edit, Save, Category="Cloth|Mesh", DisplayName="Mesh Cloth Damping")
	float MeshClothDamping = 0.1f;

	UPROPERTY(Edit, Save, Category="Cloth|Mesh|Wind", DisplayName="Mesh Cloth Wind Velocity")
	FVector MeshClothWindVelocity = FVector::ZeroVector;

	UPROPERTY(Edit, Save, Category="Cloth|Mesh|Wind", DisplayName="Mesh Cloth Wind Drag")
	float MeshClothWindDragCoefficient = 0.02f;

	UPROPERTY(Edit, Save, Category="Cloth|Mesh|Wind", DisplayName="Mesh Cloth Wind Lift")
	float MeshClothWindLiftCoefficient = 0.0f;

	UPROPERTY(Edit, Save, Category="Cloth|Mesh|Rendering", DisplayName="Mesh Cloth Color", Type=Color4)
	FVector4 MeshClothRenderColor = FVector4(0.75f, 0.85f, 1.0f, 1.0f);

	UPROPERTY(Edit, Save, Category="Cloth|Mesh|Rendering", DisplayName="Mesh Cloth Two Sided")
	bool bMeshClothTwoSided = true;
};
