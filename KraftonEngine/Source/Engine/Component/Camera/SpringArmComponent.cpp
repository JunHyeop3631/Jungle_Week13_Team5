#include "Component/Camera/SpringArmComponent.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Serialization/Archive.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/World.h"
#include "Component/Primitive/SkinnedMeshComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "Math/Rotator.h"
#include "Math/Transform.h"
#include <algorithm>
#include <cmath>

void USpringArmComponent::BeginPlay()
{
	Super::BeginPlay();
	DesiredLocalRotation = GetRelativeQuat().GetNormalized();
	bHasPreviousState = false;
}

void USpringArmComponent::SetRelativeRotation(const FRotator& NewRotation)
{
	Super::SetRelativeRotation(NewRotation);
	if (!bApplyingComputedTransform)
	{
		DesiredLocalRotation = NewRotation.ToQuaternion().GetNormalized();
		bHasPreviousState = false;
	}
}

void USpringArmComponent::SetRelativeRotation(const FQuat& NewRotation)
{
	Super::SetRelativeRotation(NewRotation);
	if (!bApplyingComputedTransform)
	{
		DesiredLocalRotation = NewRotation.GetNormalized();
		bHasPreviousState = false;
	}
}

void USpringArmComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// SpringArm 은 부모가 있어야 의미가 있음. 부모 없으면 spring 동작은 skip 하고
	// SceneComponent 기본 transform 합성에 맡긴다.
	if (!ParentComponent)
	{
		return;
	}

	// (1) 부모 World transform 추출. 세 개 분리:
	//   - ParentActualRot/ParentWorldLoc: ParentComponent 의 실제 world (아래 (5) relative 환산용 — 불변).
	//   - AttachSourceLoc/Rot: SpringArm 이 "따라갈" 대상. 기본은 ParentComponent, bUseBoneTarget 이면 본.
	//   - DesiredParentRot: 실제 사용할 desired rotation (control rotation 적용 후).
	const FMatrix& ParentWorld = ParentComponent->GetWorldMatrix();
	const FVector ParentWorldLoc = ParentComponent->GetWorldLocation();
	const FQuat   ParentActualRot  = ParentWorld.ToQuat().GetNormalized();

	// 따라갈 대상(attach source). bUseBoneTarget 이면 owner SkeletalMesh 의 본(=래그돌 시 PhysX body
	// write-back 결과)을 추적하고, 실패하면 ParentComponent 로 fallback.
	// ★ (5) relative 환산은 여전히 ParentComponent 기준이어야 하므로 ParentActualRot/Loc 은 절대 덮지 않는다.
	FVector AttachSourceLoc = ParentWorldLoc;
	FQuat   AttachSourceRot = ParentActualRot;
	if (bUseBoneTarget)
	{
		FVector BoneLoc;
		FQuat   BoneRot;
		if (ResolveBoneWorldTransform(BoneLoc, BoneRot))
		{
			AttachSourceLoc = BoneLoc;
			AttachSourceRot = BoneRot;
		}
	}
	FQuat DesiredParentRot = AttachSourceRot;

	// bUsePawnControlRotation — capsule rotation 대신 owner APawn 의 ControlRotation 을
	// (선택된 axis 별로) 사용. mouse look 이 capsule 안 건드리고 카메라만 회전하는 패턴.
	if (bUsePawnControlRotation)
	{
		if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
		{
			FRotator Result = DesiredParentRot.ToRotator();
			const FRotator Ctrl = OwnerPawn->GetControlRotation();
			if (bInheritPitch) Result.Pitch = Ctrl.Pitch;
			if (bInheritYaw)   Result.Yaw   = Ctrl.Yaw;
			if (bInheritRoll)  Result.Roll  = Ctrl.Roll;
			DesiredParentRot = Result.ToQuaternion();
		}
	}

	// (2) Desired attach point — 부모 위치 + desired 회전 기준 TargetOffset 적용.
	const FQuat DesiredArmRot = (DesiredParentRot * DesiredLocalRotation).GetNormalized();
	const FVector DesiredAttachLoc = ParentWorldLoc + DesiredArmRot.RotateVector(TargetOffset);
	const FQuat DesiredAttachRot = DesiredArmRot;

	// (3) Lag 적용 — 첫 Tick 은 desired 로 초기화 (아직 비교할 prev 없음).
	if (!bHasPreviousState)
	{
		LaggedAttachRot = DesiredAttachRot;
		LaggedAttachLoc = DesiredAttachLoc;
		bHasPreviousState = true;
	}
	else
	{
		if (bEnableCameraRotationLag && CameraRotationLagSpeed > 0.0f)
		{
			const float Alpha = std::min(DeltaTime * CameraRotationLagSpeed, 1.0f);
			LaggedAttachRot = FQuat::Slerp(LaggedAttachRot, DesiredAttachRot, Alpha).GetNormalized();
		}
		else
		{
			LaggedAttachRot = DesiredAttachRot;
		}

		if (bEnableCameraLag && CameraLagSpeed > 0.0f)
		{
			const float Alpha = std::min(DeltaTime * CameraLagSpeed, 1.0f);
			FVector NewLoc = LaggedAttachLoc + (DesiredAttachLoc - LaggedAttachLoc) * Alpha;

			// 너무 멀어지면 클램프 — 빠른 텔레포트/리스폰 직후 카메라가 한참 뒤따라오는 현상 방지.
			if (CameraLagMaxDistance > 0.0f)
			{
				const float DistSq = FVector::DistSquared(DesiredAttachLoc, NewLoc);
				const float MaxSq = CameraLagMaxDistance * CameraLagMaxDistance;
				if (DistSq > MaxSq)
				{
					const FVector Diff = DesiredAttachLoc - NewLoc;
					NewLoc = DesiredAttachLoc - Diff.Normalized() * CameraLagMaxDistance;
				}
			}
			LaggedAttachLoc = NewLoc;
		}
		else
		{
			LaggedAttachLoc = DesiredAttachLoc;
		}
	}

	// (4) ArmEnd 계산 — SpringArm 의 World 위치 (자식 카메라가 여기 부착됨).
	//     LaggedAttach 에서 Local -X 방향으로 TargetArmLength 만큼 + SocketOffset.
	const FVector ArmDirWorld = LaggedAttachRot.RotateVector(FVector(-TargetArmLength, 0.0f, 0.0f));
	const FVector SocketWorld = LaggedAttachRot.RotateVector(SocketOffset);
	FVector ArmEndWorld = LaggedAttachLoc + ArmDirWorld + SocketWorld;

	// (4b) Collision test — bDoCollisionTest 가 켜져 있으면 LaggedAttach → ArmEnd 방향으로
	//      raycast. Hit 이 있으면 해당 거리에서 ProbeSize 만큼 안쪽에서 정지해 카메라가
	//      벽 너머로 빠지지 않게 한다. 자기 Owner 액터는 ignore. (UE 의 sphere sweep 은 본
	//      엔진 미지원이라 단일 ray + ProbeSize 안전 거리로 근사.)
	if (bDoCollisionTest)
	{
		AActor* Owner = GetOwner();
		UWorld* World = Owner ? Owner->GetWorld() : nullptr;
		if (World)
		{
			const FVector Diff = ArmEndWorld - LaggedAttachLoc;
			const float Distance = Diff.Length();
			if (Distance > 1e-4f)
			{
				const FVector Dir = Diff / Distance;
				FHitResult Hit;
				if (World->PhysicsRaycast(LaggedAttachLoc, Dir, Distance, Hit, ProbeChannel, Owner))
				{
					const float SafeDist = std::max(Hit.Distance - ProbeSize, 0.0f);
					ArmEndWorld = LaggedAttachLoc + Dir * SafeDist;
				}
			}
		}
	}

	// (5) World transform 을 *Relative* 로 환산해서 RelativeTransform 에 set —
	//     SceneComponent 기본 합성 (Parent 실제 × Relative) 이 우리 의도한 World 를 자식에게 전달.
	//     ★ 반드시 ParentActualRot 의 inverse 사용 (DesiredParentRot 아님). 안 그러면
	//       (Desired)^-1 × Lagged 가 Desired ≈ Lagged 일 때 identity 되어 카메라 가 capsule
	//       회전만 따라감 — control rotation 이 무시되는 버그.
	const FQuat ParentInvRot = ParentActualRot.Inverse();
	const FVector RelLoc = ParentInvRot.RotateVector(ArmEndWorld - ParentWorldLoc);
	const FQuat RelRot = (ParentInvRot * LaggedAttachRot).GetNormalized();

	bApplyingComputedTransform = true;
	SetRelativeLocation(RelLoc);
	SetRelativeRotation(RelRot);
	bApplyingComputedTransform = false;
}

bool USpringArmComponent::ResolveBoneWorldTransform(FVector& OutLoc, FQuat& OutRot) const
{
	// 따라갈 SkinnedMesh 찾기: ParentComponent 가 곧 메시면 그걸, 아니면 owner 액터에서 검색
	// (LuaCharacter 처럼 SpringArm 과 SkeletalMesh 가 capsule 아래 형제인 경우 대응).
	USkinnedMeshComponent* Mesh = Cast<USkinnedMeshComponent>(ParentComponent);
	if (!Mesh)
	{
		if (AActor* OwnerActor = GetOwner())
		{
			Mesh = OwnerActor->GetComponentByClass<USkeletalMeshComponent>();
		}
	}
	if (!Mesh)
	{
		return false;
	}

	// 본 world transform — 런타임엔 SetBoneLocalTransforms 가 채운 live 포즈(래그돌/애니)를 반환.
	// 비우면 root 본(index 0). 래그돌 write-back 이 본에 반영돼 있으므로 곧 root body 추적.
	FTransform BoneWorld;
	const bool bGot = TargetBoneName.empty()
		? Mesh->GetBoneWorldTransformByIndex(0, BoneWorld)
		: Mesh->GetBoneWorldTransformByName(TargetBoneName, BoneWorld);
	if (!bGot)
	{
		return false;
	}

	OutLoc = BoneWorld.Location;
	OutRot = BoneWorld.Rotation.GetNormalized();
	return true;
}
