#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "LNM2PlayerController.generated.h"

class ULNM2Widget;

UCLASS()
class LONGNIGHT_API ALNM2PlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    ALNM2PlayerController();
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    bool IsPointerOverHud() const;
private:
    UPROPERTY() TObjectPtr<ULNM2Widget> RuntimeWidget;
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
