#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "LNM2TacticalPawn.generated.h"

UCLASS()
class LONGNIGHT_API ALNM2TacticalPawn : public APawn
{
    GENERATED_BODY()
public:
    ALNM2TacticalPawn();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    void FocusOverview();
    void FocusSelectedLamp();
private:
    UPROPERTY() TObjectPtr<class USpringArmComponent> Arm;
    UPROPERTY() TObjectPtr<class UCameraComponent> Camera;
    float DesiredLength=6500.f;
    void ZoomIn();
    void ZoomOut();
    void SelectFirst();
    void SelectSecond();
    bool CanZoom() const;
};
