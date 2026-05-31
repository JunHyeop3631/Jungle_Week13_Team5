#pragma once

#include "Component/Primitive/SkinnedMeshComponent.h"
#include "Animation/AnimationMode.h"
#include "Object/Ptr/SubclassOf.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/Cloth/ClothTypes.h"
#include "Physics/PhysicsTypes.h"

#include "Source/Engine/Component/Primitive/SkeletalMeshComponent.generated.h"

class UAnimInstance;
class UAnimSingleNodeInstance;
class UAnimSequenceBase;
class UClass;
class IClothScene;
class IPhysicsScene;
class UPhysicsAsset;

struct FSkeletalClothParticleAttachment
{
    uint32 ParticleIndex = 0;
    FString BoneName;
    int32 BoneIndex = -1;
    FVector LocalPosition = FVector::ZeroVector;
    float MaxDistance = 0.0f;
    bool bPinned = true;
    bool bMotionConstrained = true;
};

struct FSkeletalClothBindingDesc
{
    TArray<FSkeletalClothParticleAttachment> Attachments;
    FTransform ClothLocalTransform;
    UPhysicsAsset* CollisionPhysicsAsset = nullptr;

    bool bUsePhysicsAssetCollision = true;
    bool bUpdatePinnedParticles = true;
    bool bUpdateMotionConstraints = true;
    bool bAutoSimulate = true;
    bool bResetPreviousPinnedParticlesEveryFrame = false;

    float MotionConstraintStiffness = 0.8f;
    float CollisionRadiusScale = 1.0f;
    uint32 MaxCollisionSpheres = 32;
};

// SkeletalMesh 전용 render proxy만 제공하는 얇은 wrapper.
// Skinning/bone/material/bounds 상태는 모두 USkinnedMeshComponent가 소유한다.
UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	GENERATED_BODY()
	USkeletalMeshComponent() = default;
	~USkeletalMeshComponent() override;

    // Render access 섹션: SceneProxy
    FPrimitiveSceneProxy* CreateSceneProxy() override;

    // Mesh 가 바뀌면 AnimInstance 도 새 SkeletalMesh 기준으로 재구성해야 하므로 override.
    void SetSkeletalMesh(USkeletalMesh* InMesh) override;

    // SingleNode 재생 편의 API.
    void PlayAnimation(UAnimSequenceBase* NewAnimToPlay, bool bLooping);
    void StopAnimation();

    // Animation 섹션: Mode 에 따라 AnimInstance 의 생성/파기를 컴포넌트가 책임진다.
    //   - None              : AnimInstance 미생성. BoneEdit 만 적용.
    //   - AnimationSingleNode: UAnimSingleNodeInstance 자동 생성, AnimationData 로 구동.
    //   - AnimationCustom   : AnimInstanceClass 가 가리키는 자식 클래스를 FObjectFactory 로 인스턴스화.
    void SetAnimationMode(EAnimationMode InMode);
    EAnimationMode GetAnimationMode() const { return AnimationMode; }

    // SingleNode 모드용 헬퍼. Custom 모드에선 무시 (자체 인스턴스가 자체 시퀀스를 관리).
    void SetAnimation(UAnimSequenceBase* InAsset);
    bool CanUseAnimation(UAnimSequenceBase* InAsset) const;
    UAnimSequenceBase* GetAnimation() const { return AnimationData.AnimToPlay.Get(); }
    void SetPlayRate(float InRate);
    void SetLooping(bool bInLoop);
    void SetPlaying(bool bInPlay);
    const FSingleAnimationPlayData& GetAnimationData() const { return AnimationData; }

    // Custom 모드용. 클래스 변경 시 다음 InitializeAnimation 에서 재인스턴스화.
    // 슬롯은 TSubclassOf<UAnimInstance> — 잘못된 클래스 대입은 nullptr 로 흡수.
    void SetAnimInstanceClass(UClass* InClass);
    UClass* GetAnimInstanceClass() const { return AnimInstanceClass.Get(); }

    // 외부에서 직접 만든 인스턴스 주입 (테스트 / 특수 케이스). Mode 와 무관하게 즉시 교체.
    void SetAnimInstance(UAnimInstance* InInstance);
    UAnimInstance* GetAnimInstance() const { return AnimInstance; }

    // SingleNode 모드에서 현재 자동 생성된 노드를 반환한다. NodeName 은 현재 단일 노드 구조에서는 무시한다.
    UAnimSingleNodeInstance* GetAnimNodeInstance(FName NodeName) const;

    // Mode/Class/SkeletalMesh 변경 후 일관성 재정렬. SetSkeletalMesh override 안에서 자동 호출됨.
    void InitializeAnimation();
    void ClearAnimInstance();

    // Editor / 직렬화 통합.
    void GetEditableProperties(TArray<FPropertyValue>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;
    void Serialize(FArchive& Ar) override;

    // PhysicsAsset/Ragdoll scene instancing. Bodies are indexed by skeleton bone index
    // so ragdoll write-back can cheaply map physics poses back to bones.
    bool InstantiatePhysicsAssetBodies(IPhysicsScene& Scene);
    bool InstantiatePhysicsAssetBodies(IPhysicsScene& Scene, UPhysicsAsset* PhysicsAsset);
    void DestroyPhysicsAssetBodies();
    FBodyInstance* GetBodyInstanceByBoneIndex(int32 BoneIndex) const;
    FBodyInstance* GetBodyInstanceByBoneName(const FString& BoneName) const;
    const TArray<FBodyInstance*>& GetPhysicsBodies() const { return Bodies; }
    const TArray<FConstraintInstance*>& GetPhysicsConstraints() const { return Constraints; }

    // Skeletal cloth bridge. The cloth instance itself is owned by IClothScene; this component
    // only supplies animated pins, motion constraints, and PhysicsAsset capsule collision.
    bool BindSkeletalCloth(IClothScene& ClothScene, FClothInstance* ClothInstance, const FSkeletalClothBindingDesc& Desc);
    void ClearSkeletalClothBinding();
    bool IsSkeletalClothBound() const { return bSkeletalClothBound; }
    bool TickSkeletalCloth(float DeltaTime);
    bool BuildSkeletalClothPinnedParticles(TArray<FClothPinnedParticle>& OutPins, FClothConstraintDesc* OutConstraints = nullptr) const;
    bool BuildSkeletalClothCollision(FClothCollisionDesc& OutCollision) const;
    bool GetSkeletalClothRenderData(FClothRenderData& OutRenderData) const;
    const TArray<FVector>& GetSkeletalClothParticlePositions() const { return CachedSkeletalClothParticlePositions; }
    bool GetSkeletalClothStats(FClothStats& OutStats) const;
    bool ExtractSkeletalClothDebugLines(TArray<FPhysicsDebugLine>& OutLines, const FClothDebugDrawOptions& Options) const;

	void CreateRagdoll();

	// 시뮬레이션 결과 → 본 local pose. 본 인덱스 오름차순 = parent-first 보장에 의존한다.
	// 액티브 ragdoll 확장 시 drive target push 단계를 이 함수 앞/뒤에 추가하도록 별도 분리.
	// 에디터 프리뷰는 PreviewWorld 가 BeginPlay 미호출이라 TickComponent 가 디스패치되지 않으므로,
	// 위젯이 Simulate 직후 이 함수를 직접 호출해 write-back 을 트리거한다.
	void ApplyPhysicsToBones();

	// Passive ragdoll write-back 진입 상태. true 이면 TickComponent 가 AnimInstance 평가를 건너뛰고
	// PhysX body 트랜스폼을 본 local pose 로 변환해 메시에 푸시한다.
	bool IsSimulatingPhysics() const { return bSimulatingPhysics; }

protected:
    // 매 프레임 AnimInstance 평가 → 결과 포즈를 SetBoneLocalTransforms 로 푸시.
    // 이 경로가 CPU skinning 과 bounds dirty 를 한 번에 처리한다.
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

    bool EvaluateAnimInstance(float DeltaTime);

private:
    void LoadAnimationFromPath();
    bool ResolveSkeletalClothAttachment(FSkeletalClothParticleAttachment& Attachment) const;
    FMatrix GetSkeletalClothWorldMatrix() const;

protected:
    // Animation 런타임 상태.
    UPROPERTY(Edit, Save, Category="Animation", DisplayName="Animation Mode", Enum=EAnimationMode)
    EAnimationMode             AnimationMode = EAnimationMode::None;
    UPROPERTY(Edit, Save, Category="Animation", DisplayName="Animation Data", Type=Struct)
    FSingleAnimationPlayData   AnimationData;
    UPROPERTY(Edit, Save, Category="Animation", DisplayName="Anim Instance Class", Type=ClassRef, AllowedClass=UAnimInstance)
    TSubclassOf<UAnimInstance> AnimInstanceClass;
    UPROPERTY(Save, Instanced, Category="Animation", DisplayName="Anim Instance", Type=ObjectRef, AllowedClass=UAnimInstance)
    UAnimInstance*             AnimInstance  = nullptr;

	TArray<FBodyInstance*> Bodies;
	TArray<FConstraintInstance*> Constraints;
    IPhysicsScene* PhysicsSceneOwner = nullptr;

    FSkeletalClothBindingDesc SkeletalClothBinding;
    IClothScene* SkeletalClothSceneOwner = nullptr;
    FClothInstance* SkeletalClothInstance = nullptr;
    FClothRenderData CachedSkeletalClothRenderData;
    TArray<FVector> CachedSkeletalClothParticlePositions;
    bool bSkeletalClothBound = false;
    bool bResetSkeletalClothPinsNextTick = false;

    // Passive ragdoll on/off. CreateRagdoll 이 true 로 켜고, 이후 TickComponent 가 ApplyPhysicsToBones 경로로 갈린다.
    bool bSimulatingPhysics = false;
};
