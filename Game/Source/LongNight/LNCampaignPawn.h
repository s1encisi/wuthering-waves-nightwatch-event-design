#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "LNCampaignPawn.generated.h"
UCLASS()
class LONGNIGHT_API ALNCampaignPawn : public APawn
{
    GENERATED_BODY()
public:
    ALNCampaignPawn();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    void FocusOverview();
    void FocusSelectedLamp();
private:
    UPROPERTY() TObjectPtr<class USpringArmComponent> Arm;
    UPROPERTY() TObjectPtr<class UCameraComponent> Camera;
    float DesiredHeight=4200;
    bool CanZoom() const;
    void ZoomIn();void ZoomOut();
    void Lamp1();void Lamp2();void Lamp3();void Lamp4();void Lamp5();void Lamp6();
    void FocusLamp(int32 Index);
};
