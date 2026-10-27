#include "LNM2PlayerController.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "LNM2GameMode.h"
#include "LNM2Widget.h"

ALNM2PlayerController::ALNM2PlayerController()
{
    bShowMouseCursor = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    bShouldPerformFullTickWhenPaused = true;
}

void ALNM2PlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
    RuntimeWidget = CreateWidget<ULNM2Widget>(this, ULNM2Widget::StaticClass());
    if (RuntimeWidget) RuntimeWidget->AddToViewport(10);
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    UWidgetBlueprintLibrary::SetFocusToGameViewport();
}

void ALNM2PlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ALNM2PlayerController::StartOrContinue).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ALNM2PlayerController::PauseToggle).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ALNM2PlayerController::PauseToggle).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::F1, IE_Pressed, this, &ALNM2PlayerController::DebugToggle).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ALNM2PlayerController::NextLamp).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::M, IE_Pressed, this, &ALNM2PlayerController::CycleModule).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::T, IE_Pressed, this, &ALNM2PlayerController::RotateSeats).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &ALNM2PlayerController::BeamLeft).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::E, IE_Pressed, this, &ALNM2PlayerController::BeamRight).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Z, IE_Pressed, this, &ALNM2PlayerController::UndoPreparation).bExecuteWhenPaused = true;
}

bool ALNM2PlayerController::IsPointerOverHud() const
{
    return RuntimeWidget && RuntimeWidget->IsPointerOverPanel();
}

void ALNM2PlayerController::RefreshHud()
{
    if (RuntimeWidget) RuntimeWidget->RefreshFromGame();
}

void ALNM2PlayerController::StartOrContinue()
{
    if (auto* Game = Cast<ALNM2GameMode>(GetWorld()->GetAuthGameMode())) Game->StartOrContinue();
    RefreshHud();
}
void ALNM2PlayerController::PauseToggle()
{
    if (auto* Game = Cast<ALNM2GameMode>(GetWorld()->GetAuthGameMode())) Game->PauseToggle();
    RefreshHud();
}
void ALNM2PlayerController::DebugToggle()
{
    if (auto* Game = Cast<ALNM2GameMode>(GetWorld()->GetAuthGameMode())) Game->DebugToggle();
    RefreshHud();
}
void ALNM2PlayerController::NextLamp()
{
    if (auto* Game = Cast<ALNM2GameMode>(GetWorld()->GetAuthGameMode())) Game->SelectLamp(1 - Game->GetSelectedLamp());
    RefreshHud();
}
void ALNM2PlayerController::CycleModule()
{
    if (auto* Game = Cast<ALNM2GameMode>(GetWorld()->GetAuthGameMode()); Game && Game->CanConfigure()) Game->CycleModule();
    RefreshHud();
}
void ALNM2PlayerController::RotateSeats()
{
    if (auto* Game = Cast<ALNM2GameMode>(GetWorld()->GetAuthGameMode()); Game && Game->CanConfigure()) Game->RotateSeats();
    RefreshHud();
}
void ALNM2PlayerController::BeamLeft()
{
    if (auto* Game = Cast<ALNM2GameMode>(GetWorld()->GetAuthGameMode()); Game && Game->CanConfigure()) Game->RotateBeam(-15.f);
    RefreshHud();
}
void ALNM2PlayerController::BeamRight()
{
    if (auto* Game = Cast<ALNM2GameMode>(GetWorld()->GetAuthGameMode()); Game && Game->CanConfigure()) Game->RotateBeam(15.f);
    RefreshHud();
}
void ALNM2PlayerController::UndoPreparation()
{
    if (auto* Game = Cast<ALNM2GameMode>(GetWorld()->GetAuthGameMode()); Game && Game->CanConfigure()) Game->UndoPreparation();
    RefreshHud();
}
