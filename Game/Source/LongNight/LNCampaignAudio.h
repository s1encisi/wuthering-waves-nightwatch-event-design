#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LNCampaignAudio.generated.h"

/** Presentation-only event listener; no timers, combat state writes or damage callbacks. */
UCLASS(ClassGroup=(LongNight),meta=(BlueprintSpawnableComponent))
class LONGNIGHT_API ULNCampaignAudioComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    ULNCampaignAudioComponent();
    UFUNCTION(BlueprintCallable) void HandleEvent(const FString& Event,const FString& Detail);
    UFUNCTION(BlueprintCallable) void PlayUi(bool Back=false);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mix",meta=(ClampMin="0",ClampMax="1")) float MasterVolume=.65f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mix",meta=(ClampMin="0",ClampMax="1")) float UiVolume=.45f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mix",meta=(ClampMin="0",ClampMax="1")) float CombatVolume=.55f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mix",meta=(ClampMin="0",ClampMax="1")) float NotificationVolume=.65f;
private:
    UPROPERTY() TMap<FName,TObjectPtr<class USoundBase>> Sounds;
    UPROPERTY() TObjectPtr<class USoundAttenuation> Attenuation;
    TMap<FName,double> LastPlayed;
    TSet<FName> MissingReported;
    double MixWindow=-1;
    int32 WindowVoices=0;
    bool bIsolatedCheck=false;
    void Play(FName Key,float CategoryVolume,float Gain,float MinimumInterval,const FVector* Location=nullptr);
    int32 ReadLamp(const FString& Detail)const;
};
