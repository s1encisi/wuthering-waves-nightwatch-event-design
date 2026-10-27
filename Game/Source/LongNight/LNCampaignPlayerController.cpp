#include "LNCampaignPlayerController.h"
#include "LNCampaignGameMode.h"
#include "LNCampaignWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

ALNCampaignPlayerController::ALNCampaignPlayerController()
{
    bShowMouseCursor=true;
    PrimaryActorTick.bTickEvenWhenPaused=true;
    bShouldPerformFullTickWhenPaused=true;
}

void ALNCampaignPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if(!IsLocalController())return;
    RuntimeWidget=CreateWidget<ULNCampaignWidget>(this,ULNCampaignWidget::StaticClass());
    if(RuntimeWidget)RuntimeWidget->AddToViewport(10);
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    UWidgetBlueprintLibrary::SetFocusToGameViewport();
}

void ALNCampaignPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::SpaceBar,IE_Pressed,this,&ALNCampaignPlayerController::StartOrContinue).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::P,IE_Pressed,this,&ALNCampaignPlayerController::PauseToggle).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::Escape,IE_Pressed,this,&ALNCampaignPlayerController::PauseToggle).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::F1,IE_Pressed,this,&ALNCampaignPlayerController::DebugToggle).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::Tab,IE_Pressed,this,&ALNCampaignPlayerController::NextLamp).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::M,IE_Pressed,this,&ALNCampaignPlayerController::CycleModule).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::T,IE_Pressed,this,&ALNCampaignPlayerController::RotateSeats).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::Q,IE_Pressed,this,&ALNCampaignPlayerController::BeamLeft).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::E,IE_Pressed,this,&ALNCampaignPlayerController::BeamRight).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::Z,IE_Pressed,this,&ALNCampaignPlayerController::UndoPreparation).bExecuteWhenPaused=true;
}

bool ALNCampaignPlayerController::IsPointerOverHud() const
{return RuntimeWidget&&RuntimeWidget->IsPointerOverPanel();}
void ALNCampaignPlayerController::RefreshHud(){if(RuntimeWidget)RuntimeWidget->RefreshFromGame();}
void ALNCampaignPlayerController::StartOrContinue()
{
    if(auto* Game=GetWorld()->GetAuthGameMode<ALNCampaignGameMode>())
    {if(Game->HasTutorial())Game->AcknowledgeTutorial();else Game->StartOrContinue();}
    RefreshHud();
}
void ALNCampaignPlayerController::PauseToggle()
{if(auto* Game=GetWorld()->GetAuthGameMode<ALNCampaignGameMode>();Game&&!Game->HasTutorial())Game->PauseToggle();RefreshHud();}
void ALNCampaignPlayerController::DebugToggle()
{if(auto* Game=GetWorld()->GetAuthGameMode<ALNCampaignGameMode>())Game->DebugToggle();RefreshHud();}
void ALNCampaignPlayerController::NextLamp()
{if(auto* Game=GetWorld()->GetAuthGameMode<ALNCampaignGameMode>();Game&&Game->GetLampCount()>0)Game->SelectLamp((Game->GetSelectedLamp()+1)%Game->GetLampCount());RefreshHud();}
void ALNCampaignPlayerController::CycleModule()
{if(auto* Game=GetWorld()->GetAuthGameMode<ALNCampaignGameMode>();Game&&Game->CanConfigure()&&!Game->HasTutorial())Game->CycleModule();RefreshHud();}
void ALNCampaignPlayerController::RotateSeats()
{if(auto* Game=GetWorld()->GetAuthGameMode<ALNCampaignGameMode>();Game&&Game->CanConfigure()&&!Game->HasTutorial())Game->RotateSeats();RefreshHud();}
void ALNCampaignPlayerController::BeamLeft()
{if(auto* Game=GetWorld()->GetAuthGameMode<ALNCampaignGameMode>();Game&&Game->CanConfigure()&&!Game->HasTutorial())Game->RotateBeam(-15);RefreshHud();}
void ALNCampaignPlayerController::BeamRight()
{if(auto* Game=GetWorld()->GetAuthGameMode<ALNCampaignGameMode>();Game&&Game->CanConfigure()&&!Game->HasTutorial())Game->RotateBeam(15);RefreshHud();}
void ALNCampaignPlayerController::UndoPreparation()
{if(auto* Game=GetWorld()->GetAuthGameMode<ALNCampaignGameMode>();Game&&Game->CanConfigure()&&!Game->HasTutorial())Game->UndoPreparation();RefreshHud();}
