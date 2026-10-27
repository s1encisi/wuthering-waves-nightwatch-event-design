#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "LNCampaignData.generated.h"

USTRUCT(BlueprintType)
struct FLNCampaignFormation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FName> Seats;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Module=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float BeamYaw=90;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Priority=0;
};

USTRUCT()
struct FLNCampaignProgress
{
    GENERATED_BODY()
    UPROPERTY() int32 WaveIndex=0;
    UPROPERTY() int32 Defense=100;
    UPROPERTY() TArray<FLNCampaignFormation> Formation;
    UPROPERTY() bool Assistance=false;
    UPROPERTY() bool Completed=false;
};

UCLASS()
class LONGNIGHT_API ULNCampaignSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() int32 SchemaVersion=1;
    UPROPERTY() FString PlayerId;
    UPROPERTY() FName ProfileKind=TEXT("normal");
    UPROPERTY() int64 ActivityStartUtc=0;
    UPROPERTY() TMap<FName,FLNCampaignProgress> Stages;
    UPROPERTY() TMap<FName,FLNCampaignProgress> Presets;
    UPROPERTY() TArray<FName> CompletedStages;
    UPROPERTY() TArray<FName> TutorialSeen;
    UPROPERTY() TArray<FName> RewardClaims;
    UPROPERTY() int32 Astrites=0;
    UPROPERTY() int32 ShellCredits=0;
};
