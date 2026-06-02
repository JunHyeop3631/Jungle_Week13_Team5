#pragma once

#include "Core/Types/CoreTypes.h"
#include "Core/Types/EngineTypes.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/Reflection/UStruct.h"

#include "Source/Engine/GameFramework/Camera/CameraTypes.generated.h"

// ============================================================
// EViewTargetBlendFunction — SetViewTargetWithBlend 의 블렌딩 함수
// UE: EViewTargetBlendFunction
// ============================================================
enum class EViewTargetBlendFunction : uint8
{
	VTBlend_Linear,
	VTBlend_Cubic,
	VTBlend_EaseIn,
	VTBlend_EaseOut,
	VTBlend_EaseInOut,
	VTBlend_PreBlended,
};

// ============================================================
// FViewTargetTransitionParams — view target 전환 파라미터
// UE: FViewTargetTransitionParams
// ============================================================
struct FViewTargetTransitionParams
{
	float BlendTime = 0.0f;
	EViewTargetBlendFunction BlendFunction = EViewTargetBlendFunction::VTBlend_Linear;
	float BlendExp = 0.0f;
	bool bLockOutgoing = false;
};

// ============================================================
// ECameraShakePlaySpace — Shake 가 적용되는 좌표 공간
// UE: ECameraShakePlaySpace
// ============================================================
enum class ECameraShakePlaySpace : uint8
{
	CameraLocal,    // 카메라 로컬
	World,          // 월드 좌표
	UserDefined,    // UserPlaySpaceRot 기준
};

// ============================================================
// FCameraShakeUpdateResult — 매 프레임 Shake 결과 (additive)
// UE: FCameraShakeUpdateResult (간소화)
// ============================================================
struct FCameraShakeUpdateResult
{
	FVector Location = FVector(0.0f, 0.0f, 0.0f);   // additive world-space offset
	FRotator Rotation;                              // additive Pitch/Yaw/Roll (degrees)
	float FOV = 0.0f;                               // additive radians (FCameraState/FMinimalViewInfo 단위와 통일)
};

struct FCameraFadeState
{
	bool bEnabled = false;
	float Amount = 0.0f;
	FLinearColor Color = FLinearColor::Black();
	bool bFadeAudio = false;
};

struct FCameraVignetteState
{
	bool bEnabled = false;
	float Intensity = 0.0f;
	float Radius = 0.75f;
	float Softness = 0.35f;
	FLinearColor Color = FLinearColor::Black();
};

USTRUCT()
struct FCameraLetterboxState
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Letterbox", DisplayName="Enabled", Type=Bool)
	bool bEnabled = false;

	UPROPERTY(Edit, Save, Category="Letterbox", DisplayName="Amount", Type=Float, Min=0.0f, Max=1.0f, Speed=0.01f)
	float Amount = 1.0f;

	UPROPERTY(Edit, Save, Category="Letterbox", DisplayName="Thickness", Type=Float, Min=0.0f, Max=0.5f, Speed=0.01f)
	float Thickness = 0.12f;

	UPROPERTY(Edit, Save, Category="Letterbox", DisplayName="Color", Type=Color4)
	FLinearColor Color = FLinearColor::Black();
};

USTRUCT()
struct FCameraDepthOfFieldSettings
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Depth Of Field", DisplayName="Enabled", Type=Bool)
	bool bEnabled = false;

	UPROPERTY(Edit, Save, Category="Depth Of Field", DisplayName="Focus Distance(m)", Type=Float, Min=0.0f, Max=100000.0f, Speed=0.1f)
	float FocusDistance = 500.0f;

	UPROPERTY(Edit, Save, Category="Depth Of Field", DisplayName="Focus Length(mm)", Type=Float, Min=0.0f, Max=100000.0f, Speed=0.1f)
	float FocalLength = 50.0f;
	
	UPROPERTY(Edit, Save, Category = "Depth Of Field", DisplayName = "F-Stop", Type=Float, Min = 0.1f, Max = 64.0f, Speed = 0.1f)
	float FStop = 1.6f;

	UPROPERTY(Edit, Save, Category="Depth Of Field", DisplayName="Max Blur Size", Type=Float, Min=0.0f, Max=64.0f, Speed=0.1f)
	float MaxBlurSize = 6.0f;
};
