#include "AnimNotifyState_AttackHitWindow.h"

#include "Component/Input/ActionComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Component/Script/LuaScriptComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "Physics/IPhysicsScene.h"
#include "Physics/BodyInstance.h"
#include "Core/Types/EngineTypes.h"
#include "Debug/DrawDebugHelpers.h"
#include "Core/Logging/Log.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"

namespace
{
	int32 FindBoneIndex(USkeletalMeshComponent* MeshComp, const FString& BoneName)
	{
		if (!MeshComp || BoneName.empty()) return -1;

		USkeletalMesh* Mesh = MeshComp->GetSkeletalMesh();
		FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
		if (!Asset) return -1;

		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
		{
			if (Asset->Bones[BoneIndex].Name == BoneName)
			{
				return BoneIndex;
			}
		}

		return -1;
	}

	FVector MakeActorLocalOffset(AActor* Actor, const FVector& LocalOffset)
	{
		if (!Actor) return LocalOffset;

		return Actor->GetActorForward() * LocalOffset.X
			+ Actor->GetActorRight() * LocalOffset.Y
			+ FVector::UpVector * LocalOffset.Z;
	}

	FVector GetHitCenter(USkeletalMeshComponent* MeshComp, AActor* Owner, const FString& BoneName, const FVector& LocalOffset)
	{
		const FVector WorldOffset = MakeActorLocalOffset(Owner, LocalOffset);
		const int32 BoneIndex = FindBoneIndex(MeshComp, BoneName);
		if (BoneIndex >= 0)
		{
			return MeshComp->GetBoneLocationByIndex(BoneIndex) + WorldOffset;
		}

		return Owner ? Owner->GetActorLocation() + WorldOffset : WorldOffset;
	}

	UActionComponent* GetOrCreateActionComponent(AActor* Actor, bool bAutoAdd)
	{
		if (!Actor) return nullptr;

		if (UActionComponent* Existing = Actor->GetComponentByClass<UActionComponent>())
		{
			return Existing;
		}

		return bAutoAdd ? Actor->AddComponent<UActionComponent>() : nullptr;
	}

	void ApplyHitStop(AActor* Actor, float Duration, bool bAutoAddActionComponent)
	{
		if (UActionComponent* Action = GetOrCreateActionComponent(Actor, bAutoAddActionComponent))
		{
			Action->LocalHitStop(Duration);
		}
	}

	FVector ResolveKnockbackDirection(AActor* Attacker, AActor* Target, EAttackKnockbackMode Mode)
	{
		switch (Mode)
		{
		case EAttackKnockbackMode::Up:
			return FVector::UpVector;
		case EAttackKnockbackMode::AwayFromAttacker:
		{
			if (!Attacker || !Target) return FVector::ForwardVector;
			FVector Delta = Target->GetActorLocation() - Attacker->GetActorLocation();
			Delta.Z = 0.0f; // 수평 성분만 — 높낮이 차이로 위/아래로 날아가는 일 방지.
			if (Delta.IsNearlyZero()) return Attacker->GetActorForward();
			return Delta.Normalized();
		}
		case EAttackKnockbackMode::Forward:
		default:
			return Attacker ? Attacker->GetActorForward() : FVector::ForwardVector;
		}
	}

	void ApplyKnockback(AActor* Attacker, AActor* Target, EAttackKnockbackMode Mode,
		float Distance, float Duration, bool bAutoAddActionComponent)
	{
		if (Distance <= 0.0f || !Target) return;

		UActionComponent* Action = GetOrCreateActionComponent(Target, bAutoAddActionComponent);
		if (!Action) return;

		const FVector Dir = ResolveKnockbackDirection(Attacker, Target, Mode);
		Action->Knockback(Dir, Distance, Duration);
	}

	// 히트 대상의 owner 가 skeletal(+PhysicsAsset) 이면 즉시 래그돌 전환 + 발사 임펄스.
	// bone/태그/전용 함수 없이 "피충돌이 skeletal 이면 SetSimulatingPhysics + impulse" 범용 처리.
	void ApplyRagdollLaunch(AActor* Attacker, AActor* Target,
		EAttackKnockbackMode Mode, float LaunchSpeed, float UpBias)
	{
		if (!Target)
		{
			return;
		}

		USkeletalMeshComponent* Mesh = Target->GetComponentByClass<USkeletalMeshComponent>();
		if (!Mesh || !Mesh->HasPhysicsAsset() || Mesh->IsSimulatingPhysics())
		{
			return; // 메시/PhysicsAsset 없음 or 이미 래그돌 → skip (PhysicsAsset 은 사용자가 직접 부여)
		}

		UWorld* World = Mesh->GetWorld();
		IPhysicsScene* Scene = World ? World->GetPhysicsScene() : nullptr;
		if (!Scene)
		{
			return;
		}

		Mesh->SetSimulatingPhysics(true); // 바디 자동 인스턴스화 + Dynamic 전환

		// [임펄스 임시 비활성화] 발사가 너무 강해 잠시 끔 — 래그돌은 그대로 진입하되 발사 속도만 안 준다.
		// (EnterRagdollState 의 진입 관성 RagdollEntryLinearVelocity 만 남아 그 자리에서 쓰러짐)
		// 세기 튜닝은 노티파이 Detail 의 'Ragdoll Launch Speed'/'Ragdoll Up Bias' 권장. 재활성화 시 아래 해제.
		(void)Attacker; (void)Mode; (void)LaunchSpeed; (void)UpBias;
		//const FVector Launch = ResolveKnockbackDirection(Attacker, Target, Mode) * LaunchSpeed
		//	+ FVector::UpVector * UpBias;
		//for (FBodyInstance* Body : Mesh->GetPhysicsBodies())
		//{
		//	if (Body && Body->bValid)
		//	{
		//		Scene->SetBodyLinearVelocity(Body, Launch); // Dynamic 만 적용됨
		//	}
		//}
	}

	UParticleSystemComponent* FindTrailParticleComponent(USkeletalMeshComponent* MeshComp, const FString& TrailActorTag)
	{
		if (!MeshComp)
		{
			return nullptr;
		}

		AActor* Owner = MeshComp->GetOwner();
		if (!Owner)
		{
			return nullptr;
		}

		if (UParticleSystemComponent* OwnerParticle = Owner->GetComponentByClass<UParticleSystemComponent>())
		{
			return OwnerParticle;
		}

		UWorld* World = MeshComp->GetWorld();
		if (!World || TrailActorTag.empty())
		{
			return nullptr;
		}

		const FName TagName(TrailActorTag);
		for (AActor* Actor : World->GetActors())
		{
			if (!Actor || Actor == Owner || !Actor->HasTag(TagName))
			{
				continue;
			}

			if (UParticleSystemComponent* Particle = Actor->GetComponentByClass<UParticleSystemComponent>())
			{
				return Particle;
			}
		}

		return nullptr;
	}
}

void UAnimNotifyState_AttackHitWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* /*Anim*/, float /*TotalDuration*/)
{
	if (!MeshComp)
	{
		return;
	}

	if (bControlTrailParticle)
	{
		if (UParticleSystemComponent* Trail = FindTrailParticleComponent(MeshComp, TrailActorTag))
		{
			if (bResetTrailOnBegin)
			{
				Trail->ResetSystem();
			}

			Trail->Activate();
			Trail->SetEmitterSpawningEnabled(true);
		}
	}

	HitActorsByMesh[MeshComp].clear();
	NoTargetLoggedMeshes.erase(MeshComp);
}

void UAnimNotifyState_AttackHitWindow::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* /*Anim*/, float /*FrameDeltaTime*/)
{
	if (!MeshComp || Radius <= 0.0f)
	{
		return;
	}

	AActor* Owner = MeshComp->GetOwner();
	UWorld* World = MeshComp->GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	TSet<AActor*>& HitActors = HitActorsByMesh[MeshComp];
	const FVector Center = GetHitCenter(MeshComp, Owner, BoneName, LocalOffset);
	//if (bDrawDebugHitWindow)
	//{
	//	DrawDebugSphere(World, Center, Radius, DebugDrawSegments, FColor(255, 220, 0), DebugDrawDuration);
	//}

	bool bSawTargetCandidate = false;

	// PhysX overlap 으로 후보 수집 — query shape 를 가진 컴포넌트만 잡힌다(루즈 AABB 경로 제거).
	// ObjectType: Pawn/WorldDynamic 기본, bHitWorldStatic 이면 WorldStatic 추가.
	uint32 ObjectTypeMask = ObjectTypeBit(ECollisionChannel::Pawn) | ObjectTypeBit(ECollisionChannel::WorldDynamic);
	if (bHitWorldStatic)
	{
		ObjectTypeMask |= ObjectTypeBit(ECollisionChannel::WorldStatic);
	}

	TArray<FOverlapResult> Overlaps;
	World->PhysicsOverlapSphere(Center, Radius, ObjectTypeMask, Overlaps, Owner);

	// 같은 액터가 여러 컴포넌트로 중복 반환될 수 있어 이번 tick 한정 dedup.
	TSet<AActor*> ProcessedActors;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.OverlapActor;
		UPrimitiveComponent* HitComponent = Overlap.OverlapComponent;
		if (!Candidate || Candidate == Owner || !HitComponent)
		{
			continue;
		}
		if (ProcessedActors.find(Candidate) != ProcessedActors.end())
		{
			continue;
		}
		ProcessedActors.insert(Candidate);

		const bool bMatchesTargetActorTag = !TargetActorTag.empty() && Candidate->HasTag(FName(TargetActorTag));
		if (bRequireTargetActorTag)
		{
			if (!bMatchesTargetActorTag)
			{
				continue;
			}
		}
		else if (!TargetActorTag.empty() && !bMatchesTargetActorTag)
		{
			continue;
		}

		bSawTargetCandidate = true;
		if (HitActors.find(Candidate) != HitActors.end())
		{
			continue;
		}

		HitActors.insert(Candidate);
		if (ULuaScriptComponent* LuaScript = Candidate->GetComponentByClass<ULuaScriptComponent>())
		{
			LuaScript->DispatchOverlap(Owner);
		}

		ApplyHitStop(Owner, HitStopDuration, bAutoAddActionComponent);
		ApplyHitStop(Candidate, HitStopDuration, bAutoAddActionComponent);
		if (bApplyKnockback)
		{
			ApplyKnockback(Owner, Candidate, KnockbackMode, KnockbackDistance, KnockbackDuration, bAutoAddActionComponent);
		}
		if (bEnableRagdollOnHit)
		{
			ApplyRagdollLaunch(Owner, Candidate, RagdollLaunchMode, RagdollLaunchSpeed, RagdollUpBias);
		}
		//if (bDrawDebugHitWindow)
		//{
		//	DrawDebugSphere(World, Center, Radius, DebugDrawSegments, FColor(255, 40, 40), DebugDrawDuration);
		//}

		if (bLogHits)
		{
			UE_LOG("[AttackHitWindow] %s hit %s via %s (center=%.1f, %.1f, %.1f radius=%.1f)",
				Owner->GetName().c_str(),
				Candidate->GetName().c_str(),
				HitComponent->GetName().c_str(),
				Center.X,
				Center.Y,
				Center.Z,
				Radius);
		}
	}

	if (bLogMisses && !bSawTargetCandidate && NoTargetLoggedMeshes.find(MeshComp) == NoTargetLoggedMeshes.end())
	{
		NoTargetLoggedMeshes.insert(MeshComp);
		UE_LOG("[AttackHitWindow] no target candidate for %s (RequireTargetTag=%d TargetActorTag=%s)",
			Owner->GetName().c_str(),
			bRequireTargetActorTag ? 1 : 0,
			TargetActorTag.c_str());
	}
}

void UAnimNotifyState_AttackHitWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* /*Anim*/)
{
	if (bControlTrailParticle)
	{
		if (UParticleSystemComponent* Trail = FindTrailParticleComponent(MeshComp, TrailActorTag))
		{
			Trail->SetEmitterSpawningEnabled(false);
		}
	}

	HitActorsByMesh.erase(MeshComp);
	NoTargetLoggedMeshes.erase(MeshComp);
}
