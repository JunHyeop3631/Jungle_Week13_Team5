#pragma once

#include "Component/Camera/CameraComponent.h"
#include "Core/Types/EngineTypes.h"
#include "Source/Engine/Component/Camera/CineCameraComponent.generated.h"

UCLASS()
class UCineCameraComponent : public UCameraComponent
{
public:
	GENERATED_BODY()
	UCineCameraComponent() = default;

	void SetLetterboxEnabled(bool bEnabled) { Letterbox.bEnabled = bEnabled; }
	void SetLetterboxAmount(float Amount) { Letterbox.Amount = Amount; }
	void SetLetterboxThickness(float Thickness) { Letterbox.Thickness = Thickness; }
	void SetLetterboxColor(FLinearColor Color) { Letterbox.Color = Color; }

	const FCameraLetterboxState& GetLetterboxSettings() const { return Letterbox; }

private:
	UPROPERTY(Edit, Save, Category="Cinematic", DisplayName="Letterbox", Type=Struct, Struct=FCameraLetterboxState)
	FCameraLetterboxState Letterbox;
};
