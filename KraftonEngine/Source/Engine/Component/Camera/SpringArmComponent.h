#pragma once

#include "Component/SceneComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "Math/Vector.h"
#include "Math/Quat.h"

#include "Source/Engine/Component/Camera/SpringArmComponent.generated.h"
// ============================================================
// USpringArmComponent — 부착 액터 뒤를 부드럽게 따라가는 카메라 부착점.
//
// 사용 패턴: Pawn(Owner) → SpringArm(자식) → Camera(SpringArm 의 자식).
// SpringArm 의 World 는 매 Tick 에 부모의 World 를 따라 갱신되되, lag 옵션이
// 켜져 있으면 부드러운 보간으로 따라온다. Camera 컴포넌트는 SpringArm 의
// World 를 자동 상속하므로 별도 후크 없이 부드럽게 끌려오는 효과가 난다.
//
// 차량/플레이어 뒤를 따라오는 3인칭 카메라, 흔들림 있는 카메라 마운트 등에 사용.
// 충돌 인지(raycast) 는 별도 PR 에서 추가 — 현재는 lag 만 처리.
// UE: USpringArmComponent (간소화)
// ============================================================
UCLASS()
class USpringArmComponent : public USceneComponent
{
public:
	GENERATED_BODY()
	USpringArmComponent() = default;
	~USpringArmComponent() override = default;

	void BeginPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void SetRelativeRotation(const FRotator& NewRotation) override;
	void SetRelativeRotation(const FQuat& NewRotation) override;
	// ─── 튜닝 파라미터 ─────────────────────────────────────────────
	// arm 길이 — 부착점에서 카메라까지의 거리 (Local -X 방향).
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Target Arm Length", Min=0.0f, Max=100000.0f, Speed=1.0f)
	float TargetArmLength = 300.0f;

	// arm 끝점(카메라 위치) 에 추가되는 offset (Lagged 회전 기준 적용).
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Socket Offset", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f)
	FVector SocketOffset = FVector(0.0f, 0.0f, 0.0f);

	// 부착점 자체에 추가되는 offset (Lagged 회전 기준 적용). 보통 머리 위/어깨 높이.
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Target Offset", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f)
	FVector TargetOffset = FVector(0.0f, 0.0f, 0.0f);

	// Lag 옵션 — 끄면 부모를 즉시 따라감 (lag 없는 부착).
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Enable Camera Lag")
	bool bEnableCameraLag = false;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Enable Rotation Lag")
	bool bEnableCameraRotationLag = false;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Camera Lag Speed", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float CameraLagSpeed = 10.0f;          // 클수록 빠르게 따라옴
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Camera Rotation Lag Speed", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float CameraRotationLagSpeed = 10.0f;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Camera Lag Max Distance", Min=0.0f, Max=100000.0f, Speed=1.0f)
	float CameraLagMaxDistance = 0.0f;     // 0 = 무제한

	// Collision 옵션 — 활성화 시 부착점 → ArmEnd 사이로 ray 를 쏴서 첫 충돌점까지만 arm
	// 길이를 단축. 카메라가 벽/지형 너머로 빠지는 현상 방지. Owner Pawn 은 ignore.
	// (본 엔진은 sphere sweep 미지원이라 단일 ray + ProbeSize 안전 거리로 근사한다.)
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Do Collision Test")
	bool bDoCollisionTest = false;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Probe Channel", Enum=ECollisionChannel)
	ECollisionChannel ProbeChannel = ECollisionChannel::WorldStatic;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Probe Size", Min=0.0f, Max=100.0f, Speed=0.01f)
	float ProbeSize = 0.12f;               // hit 지점에서 ProbeSize 만큼 안쪽에 정지

	// Bone-follow 옵션 — 켜면 ParentComponent 대신 owner 의 SkeletalMesh 본을 따라간다.
	// 래그돌/애니메이션은 본 스키닝(SetBoneLocalTransforms)만 갱신하고 메시 컴포넌트 트랜스폼은
	// 캡슐(root)에 묶인 채라, 흐물거리는 바디를 카메라로 추적하려면 본을 직접 따라가야 한다
	// (메시에 reparent 만으론 메시 컴포넌트 트랜스폼=캡슐 이라 효과 없음).
	// 위치는 본을 따르고 회전은 bUsePawnControlRotation 이 그대로 우선한다 (3인칭 death-cam 패턴).
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Use Bone Target")
	bool bUseBoneTarget = false;
	// 따라갈 본 이름. 비우면 root 본(index 0) 사용 — 래그돌 root body 추적과 동일.
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Target Bone Name")
	FString TargetBoneName;

	// Control rotation 사용 옵션 (UE 패턴). true 면 부모 (capsule) 의 world rotation 대신
	// owner APawn 의 ControlRotation 을 desired rotation 으로 사용 — mouse look 이 capsule
	// 회전 안 건드리고 카메라만 움직이는 ThirdPerson 패턴.
	// bInheritPitch/Yaw/Roll : true 면 그 axis = ControlRotation, false 면 그 axis = 부모(capsule/본) 회전.
	// ※ 넘어질 때 카메라가 도는 게 싫으면 Inherit Roll 을 켜라 — roll = ControlRotation.Roll(=0) 로 고정돼
	//    부모가 굴러도 카메라가 배럴롤 하지 않는다 (셋 다 켜면 회전이 부모와 완전 분리됨).
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Use Pawn Control Rotation")
	bool bUsePawnControlRotation = true;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Inherit Pitch")
	bool bInheritPitch           = true;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Inherit Yaw")
	bool bInheritYaw             = true;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Inherit Roll")
	bool bInheritRoll            = false;

private:
	// bUseBoneTarget 일 때 따라갈 본의 world 위치/회전을 채운다.
	// 실패 시 false 반환 — 호출측이 ParentComponent 로 fallback.
	bool ResolveBoneWorldTransform(FVector& OutLoc, FQuat& OutRot) const;

	// 매 Tick 에 갱신되는 보간 상태 — 부착점 (parent + TargetOffset) 위치/회전.
	FVector LaggedAttachLoc = FVector(0.0f, 0.0f, 0.0f);
	FQuat LaggedAttachRot;
	FQuat DesiredLocalRotation = FQuat::Identity;
	bool bHasPreviousState = false;
	bool bApplyingComputedTransform = false;
};
