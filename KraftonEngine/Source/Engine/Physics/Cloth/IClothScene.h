#pragma once

#include "Physics/Cloth/ClothTypes.h"

class IClothScene
{
public:
	virtual ~IClothScene() = default;

	virtual bool Initialize(EClothBackend Backend = EClothBackend::CPU) = 0;
	virtual void Shutdown() = 0;

	virtual FClothFabricHandle CreateClothFabric(const FClothFabricDesc& Desc) = 0;
	virtual void DestroyClothFabric(FClothFabricHandle Fabric) = 0;

	virtual FClothInstance* CreateClothInstance(const FClothInstanceDesc& Desc) = 0;
	virtual FClothInstance* CreateGridCloth(const FClothGridDesc& Desc, FClothFabricHandle* OutFabric = nullptr) = 0;
	virtual void DestroyClothInstance(FClothInstance* Instance) = 0;

	virtual bool SetClothParticles(FClothInstance* Instance, const TArray<FClothParticle>& Particles) = 0;
	virtual bool SetPinnedParticlePositions(FClothInstance* Instance, const TArray<FClothPinnedParticle>& Pins, bool bResetPreviousParticles = true) = 0;
	virtual bool SetClothSettings(FClothInstance* Instance, const FClothSettings& Settings) = 0;
	virtual bool SetClothConstraints(FClothInstance* Instance, const FClothConstraintDesc& Constraints) = 0;
	virtual bool SetClothCollisionSpheres(FClothInstance* Instance, const TArray<FClothCollisionSphere>& Spheres) = 0;
	virtual bool SetClothCollisionCapsules(FClothInstance* Instance, const TArray<FClothCollisionCapsule>& Capsules) = 0;
	virtual bool SetClothCollision(FClothInstance* Instance, const FClothCollisionDesc& Collision) = 0;

	virtual void SimulateCloth(float DeltaTime) = 0;
	virtual bool GetClothParticlePositions(const FClothInstance* Instance, TArray<FVector>& OutPositions) const = 0;
	virtual bool GetClothRenderData(const FClothInstance* Instance, FClothRenderData& OutRenderData) const = 0;
	virtual void GetClothStats(FClothStats& OutStats) const = 0;
};
