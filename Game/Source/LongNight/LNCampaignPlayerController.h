#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "LNCampaignPlayerController.generated.h"

class ULNCampaignWidget;

UCLASS()
class LONGNIGHT_API ALNCampaignPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    ALNCampaignPlayerController();
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    bool IsPointerOverHud() const;
private:
    UPROPERTY() TObjectPtr<ULNCampaignWidget> RuntimeWidget;
    void RefreshHud();
    void StartOrContinue();
    void PauseToggle();
    void DebugToggle();
    void NextLamp();
    void CycleModule();
    void RotateSeats();
    void BeamLeft();
    void BeamRight();
    void UndoPreparation();
};
