#pragma once

#include "GameFramework/AActor.h"
#include "Physics/Cloth/ClothTypes.h"

#include <memory>

class FNvClothScene;

#include "Source/Game/Cloth/TempClothTestActor.generated.h"

// TEMP CLOTH TEST - DELETE LATER.
// Spawn from the viewport Place Actor menu to verify CPU NvCloth grid simulation.
UCLASS()
class ATempClothTestActor : public AActor
{
public:
	GENERATED_BODY()

	ATempClothTestActor();
	~ATempClothTestActor() override;

	void InitDefaultComponents();
	void BeginPlay() override;
	void EndPlay() override;
	void PostDuplicate() override;

protected:
	void Tick(float DeltaTime) override;

private:
	bool EnsureClothCreated();
	void DestroyCloth();
	FVector GetTestOrigin() const;
	void UpdatePins();
	void DrawClothDebug();
	void LogClothStats(float DeltaTime);

	std::unique_ptr<FNvClothScene> ClothScene;
	FClothFabricHandle FabricHandle;
	FClothInstance* ClothInstance = nullptr;
	FClothRenderData RenderData;
	TArray<FClothPinnedParticle> Pins;

	float ElapsedTime = 0.0f;
	float LogAccumulator = 0.0f;
	bool bInitialized = false;
};
