#pragma once

#include "Physics/Cloth/IClothScene.h"

#include <memory>

namespace nv
{
namespace cloth
{
class Cloth;
class Fabric;
class Factory;
class Solver;
}
}

struct FNvClothFabricRecord;
struct FNvClothInstanceRecord;

class FNvClothScene : public IClothScene
{
public:
	FNvClothScene();
	~FNvClothScene() override;

	bool Initialize(EClothBackend Backend = EClothBackend::CPU) override;
	void Shutdown() override;

	FClothFabricHandle CreateClothFabric(const FClothFabricDesc& Desc) override;
	void DestroyClothFabric(FClothFabricHandle Fabric) override;

	FClothInstance* CreateClothInstance(const FClothInstanceDesc& Desc) override;
	FClothInstance* CreateGridCloth(const FClothGridDesc& Desc, FClothFabricHandle* OutFabric = nullptr) override;
	void DestroyClothInstance(FClothInstance* Instance) override;

	bool SetClothParticles(FClothInstance* Instance, const TArray<FClothParticle>& Particles) override;
	bool SetPinnedParticlePositions(FClothInstance* Instance, const TArray<FClothPinnedParticle>& Pins, bool bResetPreviousParticles = true) override;
	bool SetClothSettings(FClothInstance* Instance, const FClothSettings& Settings) override;
	bool SetClothConstraints(FClothInstance* Instance, const FClothConstraintDesc& Constraints) override;
	bool SetClothCollisionSpheres(FClothInstance* Instance, const TArray<FClothCollisionSphere>& Spheres) override;
	bool SetClothCollisionCapsules(FClothInstance* Instance, const TArray<FClothCollisionCapsule>& Capsules) override;
	bool SetClothCollision(FClothInstance* Instance, const FClothCollisionDesc& Collision) override;
	bool SetClothWorldMatrix(FClothInstance* Instance, const FMatrix& WorldMatrix) override;

	void RegisterShapeCollider(UShapeComponent* ShapeComponent) override;
	void UnregisterShapeCollider(UShapeComponent* ShapeComponent) override;

	void SimulateCloth(float DeltaTime) override;
	bool GetClothParticlePositions(const FClothInstance* Instance, TArray<FVector>& OutPositions) const override;
	bool GetClothRenderData(const FClothInstance* Instance, FClothRenderData& OutRenderData) const override;
	void GetClothStats(FClothStats& OutStats) const override;
	void GetClothStats(const FClothInstance* Instance, FClothStats& OutStats) const override;
	void ExtractClothDebugLines(
		const FClothInstance* Instance,
		TArray<FPhysicsDebugLine>& OutLines,
		const FClothDebugDrawOptions& Options) const override;

private:
	FNvClothFabricRecord* FindFabricRecord(FClothFabricHandle Handle);
	const FNvClothFabricRecord* FindFabricRecord(FClothFabricHandle Handle) const;
	FNvClothInstanceRecord* FindInstanceRecord(FClothInstance* Instance);
	const FNvClothInstanceRecord* FindInstanceRecord(const FClothInstance* Instance) const;

	void ApplyClothSettings(FNvClothInstanceRecord& Record);
	bool ApplyClothConstraints(FNvClothInstanceRecord& Record, const FClothConstraintDesc& Constraints);
	bool ApplyClothCollision(FNvClothInstanceRecord& Record, const FClothCollisionDesc& Collision);
	bool BuildCollisionFromRegisteredShapes(FNvClothInstanceRecord& Record, FClothCollisionDesc& OutCollision) const;

	nv::cloth::Factory* Factory = nullptr;
	nv::cloth::Solver* Solver = nullptr;

	TArray<std::unique_ptr<FNvClothFabricRecord>> Fabrics;
	TArray<std::unique_ptr<FNvClothInstanceRecord>> Instances;
	TArray<UShapeComponent*> ShapeColliders;

	uint64 NextFabricSerial = 1;
	uint64 NextInstanceSerial = 1;
	FClothStats Stats;
};
