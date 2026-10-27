#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "LNPlayerController.generated.h"

class ULNRuntimeWidget;

UCLASS()
class LONGNIGHT_API ALNPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    ALNPlayerController();
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    bool IsPointerOverHud() const;

private:
    UPROPERTY() TObjectPtr<ULNRuntimeWidget> RuntimeWidget;
    void RefreshHud();
    void StartOrRetry();
    void PauseToggle();
    void DebugToggle();
    void CycleModule();
    void RotateSeats();
};
