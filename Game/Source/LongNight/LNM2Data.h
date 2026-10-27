#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "LNM2Data.generated.h"

USTRUCT(BlueprintType)
struct FLNM2Formation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> Seats;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Module = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float BeamYaw = -90.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Priority = 0;
};

UCLASS()
class LONGNIGHT_API ULNM2SaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() int32 Version = 1;
    UPROPERTY() FName ScenarioId = TEXT("M2_DuanShao");
    UPROPERTY() TArray<FLNM2Formation> Formation;
    UPROPERTY() int32 WaveIndex = 0;
    UPROPERTY() int32 Defense = 100;
    UPROPERTY() int32 CompletedTasks = 0;
    UPROPERTY() bool bCompleted = false;
    UPROPERTY() bool bAssistance = false;
};
