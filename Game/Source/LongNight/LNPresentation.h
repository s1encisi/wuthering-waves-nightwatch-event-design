#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LNPresentation.generated.h"

/** Cosmetic effects only. No damage, overlap gameplay, energy or animation notifies. */
UCLASS()
class LONGNIGHT_API ALNVisualEffect : public AActor
{
    GENERATED_BODY()
public:
    ALNVisualEffect();
    virtual void Tick(float DeltaSeconds) override;
    void Burst(FVector Center,float Radius,FLinearColor Color);
    void Beam(FVector From,FVector To,float Width,FLinearColor Color,float Seconds=.22f);
    void Burn(FVector Center,float Radius,float Seconds,bool Cross);
    void Field(FVector Center,float Radius,float Seconds,int32 Shape,FVector Direction,float Width,FName Source);
    void PulseField(FVector Center,float Radius,FLinearColor Color,int32 Pulses=3,float Interval=1.f);
    void Shield(FVector Center,float Radius,float Seconds,FLinearColor Color);
    void Impact(FVector Center,float Radius,FName Source,int32 Shape=0,FVector Direction=FVector::ForwardVector,float Width=200.f);
    void SetStartDelay(float Seconds);
private:
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> Parts;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> Materials;
    TArray<FVector> InitialLocations;
    TArray<FVector> InitialScales;
    float Age=0,Duration=.5f;
    int32 Kind=0;
    int32 PulseCount=3;
    float PulseInterval=1.f;
    class UStaticMeshComponent* AddMesh(const TCHAR* Path,FVector Location,FVector Scale,FLinearColor Color);
};

UCLASS(Blueprintable)
class LONGNIGHT_API ALNLanternVisual : public AActor
{
    GENERATED_BODY()
public:
    ALNLanternVisual();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void SetModule(int32 Type,float Yaw);
    void UpdateState(float Energy,bool Lit,bool Fault,bool Preparation);
    void Trigger(float ProtectionSeconds=6.f);
    static FLinearColor ModuleTint(int32 Type);
private:
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Device;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Ring;
    UPROPERTY() TObjectPtr<class UPointLightComponent> Light;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> GlowMaterials;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> RingMaterial;
    float EnergyValue=.4f,Pulse=0;
    int32 ModuleType=-1;
    bool bLit=false,bFault=false,bPreparation=true;
    TWeakObjectPtr<ALNVisualEffect> ActiveField;
};

/** Task progress and escort cart only; movement and completion are supplied by battle state. */
UCLASS()
class LONGNIGHT_API ALNCampaignTaskVisual : public AActor
{
    GENERATED_BODY()
public:
    ALNCampaignTaskVisual();
    void Configure(bool Escort);
    void UpdateProgress(FVector Position,float Fraction,bool Blocked,bool AwaitingLight,bool Complete);
private:
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Device;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Zone;
    UPROPERTY() TObjectPtr<class UTextRenderComponent> Label;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> ProgressPips;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> PipMaterials;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> ZoneMaterial;
    bool bEscort=false,bPlaced=false;
};
