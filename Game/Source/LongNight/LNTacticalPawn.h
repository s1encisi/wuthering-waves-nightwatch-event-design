#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "LNTacticalPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;

/** Free tactical camera; the playable defenders remain under simulation control. */
UCLASS()
class LONGNIGHT_API ALNTacticalPawn : public APawn
{
    GENERATED_BODY()

public:
    ALNTacticalPawn();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    void FocusOrigin();
    void FocusLamp();
    /** Exact M1 framing for honest before / after evidence. Bound to F9. */
    void FocusComparison();
    void FocusCharacterEvidence(FVector Target);
    void FocusBattle();

protected:
    UPROPERTY(VisibleAnywhere, Category="Camera") TObjectPtr<USpringArmComponent> SpringArm;
    UPROPERTY(VisibleAnywhere, Category="Camera") TObjectPtr<UCameraComponent> Camera;

private:
    float DesiredArmLength = 5600.f;
    bool CanZoom() const;
    void ZoomIn();
    void ZoomOut();
};
