#include "LNCampaignPawn.h"
#include "LNCampaignGameMode.h"
#include "LNCampaignPlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
ALNCampaignPawn::ALNCampaignPawn()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Arm=CreateDefaultSubobject<USpringArmComponent>(TEXT("Arm"));Arm->SetupAttachment(RootComponent);
    Arm->bDoCollisionTest=false;Arm->SetRelativeRotation(FRotator(-35,-90,0));Arm->TargetArmLength=7322;
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));Camera->SetupAttachment(Arm,USpringArmComponent::SocketName);Camera->FieldOfView=90;
}
void ALNCampaignPawn::BeginPlay(){Super::BeginPlay();FocusOverview();}
void ALNCampaignPawn::Tick(float Delta)
{
    Super::Tick(Delta);
    Arm->TargetArmLength=FMath::FInterpTo(Arm->TargetArmLength,DesiredHeight/FMath::Sin(FMath::DegreesToRadians(35.f)),Delta,10);
    if(auto* PC=Cast<APlayerController>(GetController()))
    {
        float X=(PC->IsInputKeyDown(EKeys::D)?1.f:0.f)-(PC->IsInputKeyDown(EKeys::A)?1.f:0.f);
        float Y=(PC->IsInputKeyDown(EKeys::S)?1.f:0.f)-(PC->IsInputKeyDown(EKeys::W)?1.f:0.f);
        FVector P=GetActorLocation()+FVector(X,Y,0).GetClampedToMaxSize(1)*Delta*2000;
        P.X=FMath::Clamp(P.X,-2000.f,12000.f);P.Y=FMath::Clamp(P.Y,-9000.f,3000.f);SetActorLocation(P);
    }
}
void ALNCampaignPawn::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindKey(EKeys::Home,IE_Pressed,this,&ALNCampaignPawn::FocusOverview).bExecuteWhenPaused=true;
    Input->BindKey(EKeys::One,IE_Pressed,this,&ALNCampaignPawn::Lamp1).bExecuteWhenPaused=true;
    Input->BindKey(EKeys::Two,IE_Pressed,this,&ALNCampaignPawn::Lamp2).bExecuteWhenPaused=true;
    Input->BindKey(EKeys::Three,IE_Pressed,this,&ALNCampaignPawn::Lamp3).bExecuteWhenPaused=true;
    Input->BindKey(EKeys::Four,IE_Pressed,this,&ALNCampaignPawn::Lamp4).bExecuteWhenPaused=true;
    Input->BindKey(EKeys::Five,IE_Pressed,this,&ALNCampaignPawn::Lamp5).bExecuteWhenPaused=true;
    Input->BindKey(EKeys::Six,IE_Pressed,this,&ALNCampaignPawn::Lamp6).bExecuteWhenPaused=true;
    Input->BindKey(EKeys::MouseScrollUp,IE_Pressed,this,&ALNCampaignPawn::ZoomIn).bExecuteWhenPaused=true;
    Input->BindKey(EKeys::MouseScrollDown,IE_Pressed,this,&ALNCampaignPawn::ZoomOut).bExecuteWhenPaused=true;
}
bool ALNCampaignPawn::CanZoom()const{const auto* P=Cast<ALNCampaignPlayerController>(GetController());return !P||!P->IsPointerOverHud();}
void ALNCampaignPawn::ZoomIn(){if(CanZoom()){DesiredHeight=4200;Camera->SetFieldOfView(90);}}
void ALNCampaignPawn::ZoomOut(){if(CanZoom()){DesiredHeight=7000;Camera->SetFieldOfView(90);}}
void ALNCampaignPawn::FocusOverview(){if(auto* G=GetWorld()->GetAuthGameMode<ALNCampaignGameMode>())SetActorLocation(G->GetCameraCenter());else SetActorLocation(FVector(5400,-2800,0));DesiredHeight=4200;Arm->TargetArmLength=7322;Camera->SetFieldOfView(90);}
void ALNCampaignPawn::FocusSelectedLamp(){if(auto* G=GetWorld()->GetAuthGameMode<ALNCampaignGameMode>()){SetActorLocation(G->GetLampPosition(G->GetSelectedLamp())+FVector(0,150,0));DesiredHeight=1400;Camera->SetFieldOfView(55);}}
void ALNCampaignPawn::FocusLamp(int32 Index){if(auto* G=GetWorld()->GetAuthGameMode<ALNCampaignGameMode>()){if(Index>=G->GetLampCount())return;G->SelectLamp(Index);FocusSelectedLamp();}}
void ALNCampaignPawn::Lamp1(){FocusLamp(0);}void ALNCampaignPawn::Lamp2(){FocusLamp(1);}void ALNCampaignPawn::Lamp3(){FocusLamp(2);}void ALNCampaignPawn::Lamp4(){FocusLamp(3);}void ALNCampaignPawn::Lamp5(){FocusLamp(4);}void ALNCampaignPawn::Lamp6(){FocusLamp(5);}
