#include "LNCampaignAudio.h"
#include "LNCampaignGameMode.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"

ULNCampaignAudioComponent::ULNCampaignAudioComponent(){PrimaryComponentTick.bCanEverTick=false;}
int32 ULNCampaignAudioComponent::ReadLamp(const FString& Detail)const
{
    TArray<FString> Fields;Detail.ParseIntoArray(Fields,TEXT(";"));
    for(FString Field:Fields){Field.TrimStartAndEndInline();if(Field.StartsWith(TEXT("lamp="))){FString Id=Field.Mid(5);if(Id.StartsWith(TEXT("L")))Id.RightChopInline(1);return FCString::Atoi(*Id)-1;}}
    return INDEX_NONE;
}
void ULNCampaignAudioComponent::Play(FName Key,float CategoryVolume,float Gain,float MinimumInterval,const FVector* Location)
{
    if(!GetWorld()||bIsolatedCheck||MasterVolume<=0)return;
    const float Volume=FMath::Clamp(MasterVolume*CategoryVolume*Gain,0.f,1.f);if(Volume<=0)return;
    const double Now=FPlatformTime::Seconds();const double* Previous=LastPlayed.Find(Key);
    if(Previous&&Now-*Previous<MinimumInterval)return;
    if(Now-MixWindow>.12){MixWindow=Now;WindowVoices=0;}if(WindowVoices>=4)return;
    USoundBase* Sound=nullptr;if(auto* Existing=Sounds.Find(Key))Sound=Existing->Get();
    else
    {
        const FString Asset=FString::Printf(TEXT("/Game/Campaign/Audio/S_%s.S_%s"),*Key.ToString(),*Key.ToString());
        Sound=LoadObject<USoundBase>(nullptr,*Asset);Sounds.Add(Key,Sound);
        if(!Sound&&!MissingReported.Contains(Key)){MissingReported.Add(Key);UE_LOG(LogTemp,Warning,TEXT("Campaign audio asset missing: %s; run SetupCampaignPresentation.py"),*Asset);}
    }
    if(!Sound)return;LastPlayed.Add(Key,Now);++WindowVoices;
    if(Location)
    {
        if(!Attenuation){Attenuation=NewObject<USoundAttenuation>(this);auto& S=Attenuation->Attenuation;S.bAttenuate=true;S.bSpatialize=true;S.AttenuationShape=EAttenuationShape::Sphere;S.AttenuationShapeExtents=FVector(12000);S.FalloffDistance=18000;}
        UGameplayStatics::PlaySoundAtLocation(this,Sound,*Location,FRotator::ZeroRotator,Volume,1.f,0.f,Attenuation,nullptr,GetOwner());
    }
    else UGameplayStatics::PlaySound2D(this,Sound,Volume);
}
void ULNCampaignAudioComponent::PlayUi(bool Back){Play(Back?TEXT("UI_Back"):TEXT("UI_Click"),UiVolume,1,.045);}
void ULNCampaignAudioComponent::HandleEvent(const FString& Event,const FString& Detail)
{
    if(Event==TEXT("state_test")){if(Detail.Contains(TEXT("begin;")))bIsolatedCheck=true;else if(Detail.Contains(TEXT("end;")))bIsolatedCheck=false;return;}
    if(bIsolatedCheck)return;
    const auto* Mode=Cast<ALNCampaignGameMode>(GetOwner());const int32 Lamp=ReadLamp(Detail);
    FVector Position;const FVector* At=nullptr;if(Mode&&Lamp>=0&&Lamp<Mode->GetLampCount()){Position=Mode->GetLampPosition(Lamp)+FVector(0,0,120);At=&Position;}
    if(Event==TEXT("relay_committed")&&Mode&&Lamp>=0&&Lamp<Mode->GetLampCount())
    {const FName Keys[]={TEXT("Lamp_Flame"),TEXT("Lamp_Beam"),TEXT("Lamp_Shock"),TEXT("Lamp_Reflect")};Play(Keys[FMath::Clamp(Mode->GetModule(Lamp),0,3)],CombatVolume,1,.18,At);}
    else if(Event==TEXT("wave_started"))Play(TEXT("Wave_Start"),NotificationVolume,.9f,1.5);
    else if(Event==TEXT("batch_warning"))Play(TEXT("Wave_Warning"),NotificationVolume,.6f,.8);
    else if(Event==TEXT("fault_warning"))Play(TEXT("Lamp_Fault"),NotificationVolume,.55f,.8,At);
    else if(Event==TEXT("fault_started"))Play(TEXT("Lamp_Fault"),NotificationVolume,1,.4,At);
    else if(Event==TEXT("lamp_relit"))Play(TEXT("Lamp_Relight"),NotificationVolume,.8f,.3,At);
    else if(Event==TEXT("task_complete"))Play(TEXT("Task_Complete"),NotificationVolume,.8f,.4);
    else if(Event==TEXT("stage_complete"))Play(TEXT("Battle_Victory"),NotificationVolume,1,2);
    else if(Event==TEXT("battle_defeat"))Play(TEXT("Battle_Defeat"),NotificationVolume,1,2);
    else if(Event==TEXT("character_down"))Play(TEXT("Character_Down"),CombatVolume,.55f,.6);
    else if(Event==TEXT("character_revived"))Play(TEXT("Character_Revive"),CombatVolume,.45f,.6);
    else if(Event==TEXT("arrow_intercepted"))Play(TEXT("Arrow_Intercept"),CombatVolume,.45f,.12,At);
}
