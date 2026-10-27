#include "LNPlayerController.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "LNGameMode.h"
#include "LNRuntimeWidget.h"

ALNPlayerController::ALNPlayerController()
{
    bShowMouseCursor = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    bShouldPerformFullTickWhenPaused = true;
}

void ALNPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
    RuntimeWidget = CreateWidget<ULNRuntimeWidget>(this, ULNRuntimeWidget::StaticClass());
    if (RuntimeWidget) RuntimeWidget->AddToViewport(10);
    FInputModeGameAndUI InputMode;
    InputMode.SetHideCursorDuringCapture(false);
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(InputMode);
    bShowMouseCursor = true;
    UWidgetBlueprintLibrary::SetFocusToGameViewport();
}

void ALNPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ALNPlayerController::StartOrRetry).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ALNPlayerController::PauseToggle).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ALNPlayerController::PauseToggle).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::F1, IE_Pressed, this, &ALNPlayerController::DebugToggle).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::M, IE_Pressed, this, &ALNPlayerController::CycleModule).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::T, IE_Pressed, this, &ALNPlayerController::RotateSeats).bExecuteWhenPaused = true;
}

bool ALNPlayerController::IsPointerOverHud() const
{
    return RuntimeWidget && RuntimeWidget->IsPointerOverPanel();
}

void ALNPlayerController::RefreshHud()
{
    // Input actions also refresh paused UI immediately, without waiting for a world tick.
    if (RuntimeWidget) RuntimeWidget->RefreshFromGame();
}

void ALNPlayerController::StartOrRetry()
{
    if (ALNGameMode* Game = Cast<ALNGameMode>(GetWorld()->GetAuthGameMode())) Game->StartOrRetry();
    RefreshHud();
}

void ALNPlayerController::PauseToggle()
{
    if (ALNGameMode* Game = Cast<ALNGameMode>(GetWorld()->GetAuthGameMode())) Game->PauseToggle();
    RefreshHud();
}

void ALNPlayerController::DebugToggle()
{
    if (ALNGameMode* Game = Cast<ALNGameMode>(GetWorld()->GetAuthGameMode())) Game->DebugToggle();
    RefreshHud();
}

void ALNPlayerController::CycleModule()
{
    if (ALNGameMode* Game = Cast<ALNGameMode>(GetWorld()->GetAuthGameMode()); Game && Game->CanConfigure()) Game->CycleModule();
    RefreshHud();
}

void ALNPlayerController::RotateSeats()
{
    if (ALNGameMode* Game = Cast<ALNGameMode>(GetWorld()->GetAuthGameMode()); Game && Game->CanConfigure()) Game->RotateSeats();
    RefreshHud();
}
