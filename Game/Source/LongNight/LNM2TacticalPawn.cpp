#include "LNM2TacticalPawn.h"
#include "LNM2GameMode.h"
#include "LNM2PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

ALNM2TacticalPawn::ALNM2TacticalPawn()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bTickEvenWhenPaused=true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Arm=CreateDefaultSubobject<USpringArmComponent>(TEXT("Arm"));
    Arm->SetupAttachment(RootComponent);
    Arm->SetRelativeRotation(FRotator(-58,-90,0));
    Arm->TargetArmLength=DesiredLength;
    Arm->bDoCollisionTest=false;
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Arm,USpringArmComponent::SocketName);
    Camera->FieldOfView=60;
}
void ALNM2TacticalPawn::BeginPlay(){Super::BeginPlay();FocusOverview();}
void ALNM2TacticalPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Arm->TargetArmLength=FMath::FInterpTo(Arm->TargetArmLength,DesiredLength,DeltaSeconds,12.f);
    if(const auto* PC=Cast<APlayerController>(GetController()))
    {
        const float X=(PC->IsInputKeyDown(EKeys::D)?1.f:0.f)-(PC->IsInputKeyDown(EKeys::A)?1.f:0.f);
        const float Y=(PC->IsInputKeyDown(EKeys::S)?1.f:0.f)-(PC->IsInputKeyDown(EKeys::W)?1.f:0.f);
        FVector P=GetActorLocation()+FVector(X,Y,0).GetClampedToMaxSize(1)*DeltaSeconds*1250*(DesiredLength/6500);
        P.X=FMath::Clamp(P.X,-5000.f,5000.f);P.Y=FMath::Clamp(P.Y,-5000.f,5000.f);
        SetActorLocation(P);
    }
}
void ALNM2TacticalPawn::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindKey(EKeys::Home,IE_Pressed,this,&ALNM2TacticalPawn::FocusOverview).bExecuteWhenPaused=true;
    Input->BindKey(EKeys::One,IE_Pressed,this,&ALNM2TacticalPawn::SelectFirst).bExecuteWhenPaused=true;
    Input->BindKey(EKeys::Two,IE_Pressed,this,&ALNM2TacticalPawn::SelectSecond).bExecuteWhenPaused=true;
    Input->BindKey(EKeys::MouseScrollUp,IE_Pressed,this,&ALNM2TacticalPawn::ZoomIn).bExecuteWhenPaused=true;
    Input->BindKey(EKeys::MouseScrollDown,IE_Pressed,this,&ALNM2TacticalPawn::ZoomOut).bExecuteWhenPaused=true;
}
bool ALNM2TacticalPawn::CanZoom() const
{
    const auto* PC=Cast<ALNM2PlayerController>(GetController());
    return !PC||!PC->IsPointerOverHud();
}
void ALNM2TacticalPawn::ZoomIn(){if(CanZoom())DesiredLength=FMath::Clamp(DesiredLength-400,2200.f,9000.f);}
void ALNM2TacticalPawn::ZoomOut(){if(CanZoom())DesiredLength=FMath::Clamp(DesiredLength+400,2200.f,9000.f);}
void ALNM2TacticalPawn::FocusOverview()
{
    SetActorLocation(FVector(250,-100,0));Arm->SetRelativeRotation(FRotator(-58,-90,0));
    DesiredLength=6500;Arm->TargetArmLength=DesiredLength;
}
void ALNM2TacticalPawn::FocusSelectedLamp()
{
    if(const auto* GM=GetWorld()->GetAuthGameMode<ALNM2GameMode>())
    {
        SetActorLocation(GM->GetLampPosition(GM->GetSelectedLamp())+FVector(-450,0,0));
        DesiredLength=3600;
    }
}
void ALNM2TacticalPawn::SelectFirst(){if(auto* GM=GetWorld()->GetAuthGameMode<ALNM2GameMode>())GM->SelectLamp(0);FocusSelectedLamp();}
void ALNM2TacticalPawn::SelectSecond(){if(auto* GM=GetWorld()->GetAuthGameMode<ALNM2GameMode>())GM->SelectLamp(1);FocusSelectedLamp();}
