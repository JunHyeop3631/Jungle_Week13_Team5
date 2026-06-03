#pragma once

#include "AnimNotifyState.h"
#include "Core/Types/CollisionTypes.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"

#include "Source/Engine/Animation/Notify/AnimNotifyState_AttackHitWindow.generated.h"

class AActor;
class USkeletalMeshComponent;
class UParticleSystemComponent;

// 히트 시 대상에게 줄 충격 방향. UActionComponent::Knockback 의 Direction 인자로 사용.
UENUM()
enum class EAttackKnockbackMode : uint8
{
	Forward,            // 공격자(attacker) 의 forward 방향 — "앞으로 밀기"
	Up,                 // world up — "위로 띄우기" (launcher)
	AwayFromAttacker,   // attacker→target 의 수평 방향 (다양한 위치에서 자연스럽게 멀어짐)
};

inline const char* GAttackKnockbackModeNames[] = {
	"Forward",
	"Up",
	"AwayFromAttacker",
};
inline constexpr uint32 GAttackKnockbackModeCount = sizeof(GAttackKnockbackModeNames) / sizeof(GAttackKnockbackModeNames[0]);

UCLASS()
class UAnimNotifyState_AttackHitWindow : public UAnimNotifyState
{
public:
	GENERATED_BODY()
	UAnimNotifyState_AttackHitWindow() = default;
	~UAnimNotifyState_AttackHitWindow() override = default;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Bone Name")
	FString BoneName = "Bip001 R Hand";

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Local Offset")
	FVector LocalOffset = FVector(25.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Radius", Min=1.0f, Max=1000.0f, Speed=1.0f)
	float Radius = 60.0f;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Hit Stop Duration", Min=0.0f, Max=1.0f, Speed=0.01f)
	float HitStopDuration = 0.08f;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Apply Knockback")
	bool bApplyKnockback = false;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Knockback Mode", Enum=EAttackKnockbackMode)
	EAttackKnockbackMode KnockbackMode = EAttackKnockbackMode::Forward;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Knockback Distance", Min=0.0f, Max=100.0f, Speed=0.1f)
	float KnockbackDistance = 5.0f;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Knockback Duration", Min=0.0f, Max=2.0f, Speed=0.01f)
	float KnockbackDuration = 0.25f;

	// 히트 대상의 owner 가 skeletal(+PhysicsAsset)이면 즉시 래그돌 전환 + 발사 임펄스. 기본 off → 기존 공격 무변경.
	UPROPERTY(Edit, Save, Category="AttackHitWindow|Ragdoll", DisplayName="Enable Ragdoll On Hit")
	bool bEnableRagdollOnHit = false;

	// 발사 방향 결정(넉백 모드 재사용). 위치기반 = AwayFromAttacker.
	UPROPERTY(Edit, Save, Category="AttackHitWindow|Ragdoll", DisplayName="Ragdoll Launch Mode", Enum=EAttackKnockbackMode)
	EAttackKnockbackMode RagdollLaunchMode = EAttackKnockbackMode::AwayFromAttacker;

	// 수평 발사 속도(m/s, 엔진 m 규약). 기본 5.0 ≈ 약 3m 비행(중력 -9.81 / 래그돌 마찰 감안, 튜닝값).
	UPROPERTY(Edit, Save, Category="AttackHitWindow|Ragdoll", DisplayName="Ragdoll Launch Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
	float RagdollLaunchSpeed = 5.0f;

	// 살짝 띄우는 수직 속도(m/s).
	UPROPERTY(Edit, Save, Category="AttackHitWindow|Ragdoll", DisplayName="Ragdoll Up Bias", Min=0.0f, Max=30.0f, Speed=0.1f)
	float RagdollUpBias = 2.5f;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Draw Debug Hit Window")
	bool bDrawDebugHitWindow = true;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Debug Draw Duration", Min=0.0f, Max=1.0f, Speed=0.01f)
	float DebugDrawDuration = 0.05f;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Debug Draw Segments", Min=4.0f, Max=64.0f, Speed=1.0f)
	int32 DebugDrawSegments = 24;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Draw Debug Target Bounds")
	bool bDrawDebugTargetBounds = true;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Require Query Collision")
	bool bRequireQueryCollision = false;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Hit World Static")
	bool bHitWorldStatic = true;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Auto Add Action Component")
	bool bAutoAddActionComponent = true;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Require Target Actor Tag")
	bool bRequireTargetActorTag = false;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Target Actor Tag")
	FString TargetActorTag = "HitTarget";

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Log Hits")
	bool bLogHits = true;

	UPROPERTY(Edit, Save, Category="AttackHitWindow", DisplayName="Log Misses")
	bool bLogMisses = true;

	UPROPERTY(Edit, Save, Category = "AttackHitWindow|Trail", DisplayName = "Control Trail Particle")
	bool bControlTrailParticle = false;

	UPROPERTY(Edit, Save, Category = "AttackHitWindow|Trail", DisplayName = "Reset Trail On Begin")
	bool bResetTrailOnBegin = true;

	UPROPERTY(Edit, Save, Category = "AttackHitWindow|Trail", DisplayName = "Trail Actor Tag")
	FString TrailActorTag = "";

	void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Anim, float TotalDuration) override;
	void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Anim, float FrameDeltaTime) override;
	void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Anim) override;

private:
	TMap<USkeletalMeshComponent*, TSet<AActor*>> HitActorsByMesh;
	TSet<USkeletalMeshComponent*> NoTargetLoggedMeshes;
};
