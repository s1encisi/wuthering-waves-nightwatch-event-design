#include "LNTacticalPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "LNPlayerController.h"

ALNTacticalPawn::ALNTacticalPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("CameraRoot")));
    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("TacticalArm"));
    SpringArm->SetupAttachment(RootComponent);
    SpringArm->TargetArmLength = DesiredArmLength;
    SpringArm->SetRelativeRotation(FRotator(-55.f, -90.f, 0.f));
    SpringArm->bDoCollisionTest = false;
    SpringArm->bUsePawnControlRotation = false;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("TacticalCamera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
    Camera->FieldOfView = 60.f;
    Camera->bUsePawnControlRotation = false;
}

void ALNTacticalPawn::BeginPlay()
{
    Super::BeginPlay();
    FocusBattle();
}

void ALNTacticalPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, DesiredArmLength, DeltaSeconds, 12.f);
    if (const APlayerController* Player = Cast<APlayerController>(GetController()))
    {
        // Poll held keys so losing UI focus cannot leave a movement axis stuck on.
        const float Forward = (Player->IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (Player->IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
        const float Right = (Player->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (Player->IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
        FVector GroundForward = SpringArm->GetForwardVector();
        GroundForward.Z = 0.f;
        GroundForward.Normalize();
        const FVector GroundRight = SpringArm->GetRightVector();
        const FVector Direction = (GroundForward * Forward + GroundRight * Right).GetClampedToMaxSize(1.f);
        FVector Location = GetActorLocation() + Direction * 1250.f * (DesiredArmLength / 5600.f) * DeltaSeconds;
        Location.X = FMath::Clamp(Location.X, -6000.f, 6000.f);
        Location.Y = FMath::Clamp(Location.Y, -6000.f, 6000.f);
        Location.Z = 0.f;
        SetActorLocation(Location);
    }
}

void ALNTacticalPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    PlayerInputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ALNTacticalPawn::ZoomIn).bExecuteWhenPaused = true;
    PlayerInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ALNTacticalPawn::ZoomOut).bExecuteWhenPaused = true;
    PlayerInputComponent->BindKey(EKeys::Home, IE_Pressed, this, &ALNTacticalPawn::FocusOrigin).bExecuteWhenPaused = true;
    PlayerInputComponent->BindKey(EKeys::One, IE_Pressed, this, &ALNTacticalPawn::FocusLamp).bExecuteWhenPaused = true;
    PlayerInputComponent->BindKey(EKeys::F9, IE_Pressed, this, &ALNTacticalPawn::FocusComparison).bExecuteWhenPaused = true;
}

bool ALNTacticalPawn::CanZoom() const
{
    const ALNPlayerController* Player = Cast<ALNPlayerController>(GetController());
    return !Player || !Player->IsPointerOverHud();
}

void ALNTacticalPawn::ZoomIn()
{
    if (CanZoom()) DesiredArmLength = FMath::Clamp(DesiredArmLength - 350.f, 1800.f, 9000.f);
}

void ALNTacticalPawn::ZoomOut()
{
    if (CanZoom()) DesiredArmLength = FMath::Clamp(DesiredArmLength + 350.f, 1800.f, 9000.f);
}

void ALNTacticalPawn::FocusOrigin()
{
    SpringArm->TargetOffset=FVector::ZeroVector;
    SetActorLocation(FVector(-1000.f, -450.f, 0.f));
    SpringArm->SetRelativeRotation(FRotator(-55.f, -90.f, 0.f));
    DesiredArmLength = 5600.f;
}

void ALNTacticalPawn::FocusBattle()
{
    SetActorLocation(FVector(-1100.f,-550.f,0.f));
    SpringArm->TargetOffset=FVector::ZeroVector;
    SpringArm->SetRelativeRotation(FRotator(-53.f,-90.f,0.f));
    DesiredArmLength=4200.f;SpringArm->TargetArmLength=DesiredArmLength;
}

void ALNTacticalPawn::FocusLamp()
{
    SpringArm->TargetOffset=FVector::ZeroVector;
    // Frame the real lamp and nearby defenders to the right of the 306 px HUD.
    SetActorLocation(FVector(-1200.f, -570.f, 0.f));
    SpringArm->SetRelativeRotation(FRotator(-48.f, -90.f, 0.f));
    DesiredArmLength = 2400.f;
}

void ALNTacticalPawn::FocusComparison()
{
    SpringArm->TargetOffset=FVector::ZeroVector;
    SetActorLocation(FVector(-1000.f, -450.f, 0.f));
    SpringArm->SetRelativeRotation(FRotator(-60.f, -90.f, 0.f));
    DesiredArmLength = 6000.f;
    SpringArm->TargetArmLength = DesiredArmLength;
}

void ALNTacticalPawn::FocusCharacterEvidence(FVector Target)
{
    Target.Z=0;SetActorLocation(Target);
    SpringArm->TargetOffset=FVector(0,0,85);
    SpringArm->SetRelativeRotation(FRotator(-12,0,0));
    DesiredArmLength=650;SpringArm->TargetArmLength=DesiredArmLength;
}
