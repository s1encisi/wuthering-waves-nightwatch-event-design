#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LNData.h"
#include "LNCombatUnit.generated.h"

UCLASS(Blueprintable)
class LONGNIGHT_API ALNCombatUnit : public AActor
{
    GENERATED_BODY()
public:
    ALNCombatUnit();
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UCapsuleComponent> Body;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UTextRenderComponent> Label;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class USkeletalMeshComponent> CharacterMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UStaticMeshComponent> WeaponMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UStaticMeshComponent> SeatRing;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FLNCharacterRow Stats;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Health = 300.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bEnemy = false;
    bool bHeavy = false;
    int32 Seat = -1;
    FVector Station = FVector::ZeroVector;
    TArray<FVector> Route;
    int32 Waypoint = 0;
    double BasicReady = 0, SkillOneReady = 0, SkillTwoReady = 0, ActionReady = 0;
    double ReviveAt = 0, RootUntil = 0, MarkUntil = 0, LastInterrupt = -100;
    double EngagementUntil = 0, NextEngagement = 0;
    bool bExited = false;
    // Provisional presentation/shape parameters; the PDF does not specify these values.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Provisional") float ConeHalfAngleDegrees = 45.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Provisional") float LineWidthCm = 200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Provisional") float DashSpeedCm = 1200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Provisional") float InterruptSeconds = .5f;
    bool TryInterrupt(double Now, float Duration)
    {
        if(!IsAlive() || (bHeavy && Now-LastInterrupt<8.0)) return false;
        LastInterrupt=Now;
        EngagementUntil=Now;
        NextEngagement=FMath::Max(NextEngagement,Now+Duration);
        BasicReady=FMath::Max(BasicReady,Now+Duration);
        RootUntil=FMath::Max(RootUntil,Now+Duration);
        return true;
    }
    void Configure(bool Enemy, const FLNCharacterRow& Row, int32 InSeat, FLinearColor Color, FName ConfigId=NAME_None);
    bool IsAlive() const { return Health > 0.f && !bExited; }
    void TakeHit(float Amount, double Now);
    void Restore();
    void MoveTowards(const FVector& Target, float Speed, float DeltaSeconds);
    bool CanSee(const FVector& Point) const;
    void UpdateLabel();
    void PlayCombatAction(int32 SkillIndex, FVector Target, float PresentationSeconds = 0.f);
    void SetConductor(bool Active);
    void SetDebugVisible(bool Visible);
    bool HasCharacterVisual() const;
    FString GetVisualDiagnostics() const;
private:
    UPROPERTY() FName VisualConfigId;
    UPROPERTY() FLNVisualRow VisualDefinition;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> CharacterMaterials;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> RingMaterial;
    UPROPERTY() TObjectPtr<class UAnimSequence> CurrentAnimation;
    UPROPERTY() TArray<TObjectPtr<class UAnimSequence>> AnimationCache;
    FVector LastVisualLocation=FVector::ZeroVector;
    double VisualActionUntil=0, HitFlashUntil=0, DownVisualAt=0;
    bool bIsConductor=false;
    float FacingYaw=0;
    void ApplyVisualDefinition();
    void PlayVisualAnimation(class UAnimSequence* Animation, bool Loop, float PresentationSeconds=0.f);
};

