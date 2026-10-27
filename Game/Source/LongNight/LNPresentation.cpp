#include "LNPresentation.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

namespace
{
FLinearColor EffectTint(FName Source)
{
    const FString S=Source.ToString();
    if(S.Contains(TEXT("sanhua"))||S.Contains(TEXT("baizhi"))||S.Contains(TEXT("youhu")))return FLinearColor(.24f,.72f,1.f);
    if(S.Contains(TEXT("camellya")))return FLinearColor(.95f,.18f,.46f);
    if(S.Contains(TEXT("shock"))||S.Contains(TEXT("yuanwu"))||S.Contains(TEXT("buling"))||S.Contains(TEXT("yinlin"))||S.Contains(TEXT("xiangliyao")))return FLinearColor(.52f,.32f,1.f);
    if(S.Contains(TEXT("reflect"))||S.Contains(TEXT("aalto"))||S.Contains(TEXT("verina")))return FLinearColor(.14f,.95f,.66f);
    if(S.Contains(TEXT("beam"))||S.Contains(TEXT("jinhsi"))||S.Contains(TEXT("lumi"))||S.Contains(TEXT("yangyang")))return FLinearColor(.12f,.8f,1.f);
    return FLinearColor(1.f,.46f,.08f);
}
}

ALNVisualEffect::ALNVisualEffect()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
}
UStaticMeshComponent* ALNVisualEffect::AddMesh(const TCHAR* Path,FVector Location,FVector Scale,FLinearColor Color)
{
    auto* Mesh=NewObject<UStaticMeshComponent>(this);Mesh->SetupAttachment(RootComponent);
    Mesh->SetCollisionProfileName(TEXT("NoCollision"));Mesh->SetCastShadow(false);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Path));Mesh->RegisterComponent();
    Mesh->SetWorldLocation(Location);Mesh->SetWorldScale3D(Scale);
    if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/M11/FX/M_Effect.M_Effect")))
    {
        auto* Material=UMaterialInstanceDynamic::Create(Base,this);Material->SetVectorParameterValue(TEXT("Tint"),Color);
        Material->SetScalarParameterValue(TEXT("Intensity"),3.f);Mesh->SetMaterial(0,Material);Materials.Add(Material);
    }
    Parts.Add(Mesh);InitialLocations.Add(Location);InitialScales.Add(Scale);return Mesh;
}
void ALNVisualEffect::Burst(FVector Center,float Radius,FLinearColor Color)
{
    Kind=0;Duration=.55f;SetLifeSpan(Duration);Center.Z+=16;
    AddMesh(TEXT("/Game/M11/Presentation/SM_Ring.SM_Ring"),Center,FVector(Radius/50.f,Radius/50.f,.8f),Color);
    for(int32 i=0;i<10;++i)
    {
        const float Angle=i*2*PI/10;
        const FVector P=Center+FVector(FMath::Cos(Angle),FMath::Sin(Angle),.1f)*Radius*.45f;
        AddMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"),P,FVector(.08f,.08f,.25f),Color);
    }
}
void ALNVisualEffect::Beam(FVector From,FVector To,float Width,FLinearColor Color,float Seconds)
{
    Kind=1;Duration=Seconds;SetLifeSpan(Duration);
    FHitResult Hit;
    if(GetWorld()->LineTraceSingleByChannel(Hit,From,To,ECC_Visibility))To=Hit.ImpactPoint;
    const FVector Delta=To-From;
    auto* M=AddMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),(From+To)*.5,FVector(Width/100.f,Width/100.f,Delta.Size()/100.f),Color);
    M->SetWorldRotation(FRotationMatrix::MakeFromZ(Delta.GetSafeNormal()).Rotator());
    if(Width>50.f)
    {
        if(Materials.Num())Materials[0]->SetScalarParameterValue(TEXT("Intensity"),.6f);
        auto* Core=AddMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),(From+To)*.5,FVector(Width*.13f/100.f,Width*.13f/100.f,Delta.Size()/100.f),Color);
        Core->SetWorldRotation(M->GetComponentRotation());
    }
}
void ALNVisualEffect::Burn(FVector Center,float Radius,float Seconds,bool Cross)
{
    Kind=2;Duration=Seconds;SetLifeSpan(Duration);Center.Z+=12;
    AddMesh(TEXT("/Game/M11/Presentation/SM_Ring.SM_Ring"),Center,FVector(Radius/50.f,Radius/50.f,.5f),FLinearColor(1.f,.17f,.02f));
    for(int32 i=0;i<12;++i)
    {
        const float Angle=i*2*PI/12;
        const FVector P=Center+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Radius*.74f;
        AddMesh(TEXT("/Engine/BasicShapes/Cone.Cone"),P+FVector(0,0,40),FVector(.25f,.25f,.8f),FLinearColor(1.f,.3f,.04f));
    }
    if(Cross)
    {
        AddMesh(TEXT("/Engine/BasicShapes/Cube.Cube"),Center,FVector(Radius/50.f,2,.05f),FLinearColor(1.f,.45f,.05f));
        AddMesh(TEXT("/Engine/BasicShapes/Cube.Cube"),Center,FVector(2,Radius/50.f,.05f),FLinearColor(1.f,.45f,.05f));
    }
}
void ALNVisualEffect::SetStartDelay(float Seconds)
{const float Delay=FMath::Max(0.f,Seconds);Age=-Delay;SetActorHiddenInGame(Delay>0);SetLifeSpan(Duration+Delay);}
void ALNVisualEffect::Impact(FVector Center,float Radius,FName Source,int32 Shape,FVector Direction,float Width)
{
    if(Shape==1)Beam(Center,Center+Direction*Radius,Width,EffectTint(Source),.45f);
    else if(Shape==2)Field(Center,Radius,.55f,Shape,Direction,Width,Source);
    else Burst(Center,Radius,EffectTint(Source));
}
void ALNVisualEffect::Field(FVector Center,float Radius,float Seconds,int32 Shape,FVector Direction,float Width,FName Source)
{
    Kind=5;Duration=FMath::Max(.1f,Seconds);SetLifeSpan(Duration);Center.Z=FMath::Max(12.,Center.Z-48);const FLinearColor Color=EffectTint(Source);
    if(Shape==1)
    {
        const FVector Mid=Center+Direction*Radius*.5;
        auto* Strip=AddMesh(TEXT("/Engine/BasicShapes/Cube.Cube"),Mid,FVector(Radius/100,Width/100,.025f),Color);Strip->SetWorldRotation(Direction.Rotation());
        for(int32 Side:{-1,1}){const FVector Normal(-Direction.Y,Direction.X,0);auto* Edge=AddMesh(TEXT("/Engine/BasicShapes/Cube.Cube"),Mid+Normal*Side*Width*.5,FVector(Radius/100,.035f,.045f),Color);Edge->SetWorldRotation(Direction.Rotation());}
    }
    else if(Shape==2)
    {
        AddMesh(TEXT("/Engine/BasicShapes/Cube.Cube"),Center,FVector(Radius/50,Width/100,.035f),Color);
        AddMesh(TEXT("/Engine/BasicShapes/Cube.Cube"),Center,FVector(Width/100,Radius/50,.035f),Color);
    }
    else
    {
        AddMesh(TEXT("/Game/M11/Presentation/SM_Ring.SM_Ring"),Center,FVector(Radius/50,Radius/50,.35f),Color);
        for(int32 I=0;I<8;++I){const float Angle=2*PI*I/8;const FVector P=Center+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Radius*.82f;auto* M=AddMesh(TEXT("/Engine/BasicShapes/Cube.Cube"),P,FVector(.5f,.06f,.035f),Color);M->SetWorldRotation(FRotator(0,FMath::RadiansToDegrees(Angle)+90,0));}
    }
}
void ALNVisualEffect::PulseField(FVector Center,float Radius,FLinearColor Color,int32 Pulses,float Interval)
{
    Kind=3;PulseCount=FMath::Clamp(Pulses,1,5);PulseInterval=FMath::Max(.1f,Interval);Duration=(PulseCount-1)*PulseInterval+.65f;SetLifeSpan(Duration);Center.Z=15;
    for(int32 I=0;I<PulseCount;++I)AddMesh(TEXT("/Game/M11/Presentation/SM_Ring.SM_Ring"),Center+FVector(0,0,I*.5f),FVector(Radius/50,Radius/50,.7f),Color);
}
void ALNVisualEffect::Shield(FVector Center,float Radius,float Seconds,FLinearColor Color)
{
    Kind=4;Duration=FMath::Max(.1f,Seconds);SetLifeSpan(Duration);Center.Z=10;
    AddMesh(TEXT("/Game/M11/Presentation/SM_Ring.SM_Ring"),Center,FVector(Radius/50,Radius/50,.8f),Color);
    AddMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"),Center+FVector(0,0,35),FVector(Radius/50,Radius/50,Radius*.4f/50),Color);
    for(int32 I=0;I<3;++I){auto* Arc=AddMesh(TEXT("/Game/M11/Presentation/SM_Ring.SM_Ring"),Center,FVector(Radius/50,Radius*.4f/50,.32f),Color);Arc->SetWorldRotation(FRotator(0,I*60.f,90));}
}
void ALNVisualEffect::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);Age+=DeltaSeconds;
    if(Age<0)return;SetActorHiddenInGame(false);
    const float T=FMath::Clamp(Age/Duration,0.f,1.f);
    for(int32 i=0;i<Parts.Num();++i)
    {
        if(Kind==0)
        {
            if(i==0)Parts[i]->SetWorldScale3D(InitialScales[i]*(.3f+.7f*T));
            else{Parts[i]->SetWorldLocation(InitialLocations[i]+FVector(0,0,120*T));Parts[i]->SetWorldScale3D(InitialScales[i]*(1-T));}
        }
        else if(Kind==2&&i>0&&i<13)Parts[i]->SetWorldScale3D(InitialScales[i]*FVector(1,1,.65f+.35f*FMath::Sin(Age*13+i)));
        else if(Kind==3)
        {
            const float Local=(Age-i*PulseInterval)/.65f;Parts[i]->SetVisibility(Local>=0&&Local<=1);
            Parts[i]->SetWorldScale3D(InitialScales[i]*FVector(FMath::Clamp(Local,0.f,1.f),FMath::Clamp(Local,0.f,1.f),1));
        }
    }
    for(int32 i=0;i<Materials.Num();++i)
    {
        float Opacity=Kind==2?FMath::Min(1.f,(1-T)*4.f)*.5f:(1-T)*(Kind==1&&Materials.Num()>1&&i==0?.12f:.8f);
        if(Kind==3){const float Local=(Age-i*PulseInterval)/.65f;Opacity=Local<0||Local>1?0:(1-Local)*.72f;}
        if(Kind==4)Opacity=FMath::Min(1.f,(Duration-Age)/.5f)*(i==1?.028f:i==0?.55f:.11f);
        if(Kind==5)Opacity=FMath::Min(1.f,(Duration-Age)/.4f)*(.19f+.045f*FMath::Sin(Age*3));
        Materials[i]->SetScalarParameterValue(TEXT("Opacity"),FMath::Max(0.f,Opacity));
    }
}

ALNLanternVisual::ALNLanternVisual()
{
    PrimaryActorTick.bCanEverTick=true;
    Device=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Device"));RootComponent=Device;
    Device->SetCollisionProfileName(TEXT("NoCollision"));
    Ring=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EnergyRing"));Ring->SetupAttachment(Device);
    Ring->SetCollisionProfileName(TEXT("NoCollision"));Ring->SetCastShadow(false);
    Ring->SetRelativeLocation(FVector(0,0,18));Ring->SetRelativeScale3D(FVector(2.6f,2.6f,.6f));
    Light=CreateDefaultSubobject<UPointLightComponent>(TEXT("CoreLight"));Light->SetupAttachment(Device);
    Light->SetRelativeLocation(FVector(0,0,185));Light->SetAttenuationRadius(1100);Light->SetCastShadows(false);
    Light->SetIntensityUnits(ELightUnits::Lumens);Light->SetSourceRadius(30.f);
}
void ALNLanternVisual::SetModule(int32 Type,float Yaw)
{
    Type=FMath::Clamp(Type,0,3);Device->SetWorldRotation(FRotator(0,Type==1?Yaw:0,0));if(ModuleType==Type)return;ModuleType=Type;
    const TCHAR* Paths[]={TEXT("/Game/M11/Presentation/SM_LampFlame.SM_LampFlame"),TEXT("/Game/M11/Presentation/SM_LampBeam.SM_LampBeam"),TEXT("/Game/Campaign/Presentation/SM_LampShock.SM_LampShock"),TEXT("/Game/Campaign/Presentation/SM_LampReflect.SM_LampReflect")};
    Device->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Paths[Type]));if(!Device->GetStaticMesh())UE_LOG(LogTemp,Warning,TEXT("Campaign lamp model missing: %s"),Paths[Type]);GlowMaterials.Empty();
    if(Device->GetStaticMesh())for(int32 i=0;i<Device->GetNumMaterials();++i)
    {
        const FString Name=Device->GetStaticMesh()->GetStaticMaterials()[i].MaterialSlotName.ToString();
        if(Name.Contains(TEXT("Glow")))if(auto* D=Device->CreateDynamicMaterialInstance(i))GlowMaterials.Add(D);
    }
    Ring->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/M11/Presentation/SM_Ring.SM_Ring")));
    if(auto* M=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/M11/FX/M_Glow.M_Glow")))
    {RingMaterial=UMaterialInstanceDynamic::Create(M,this);Ring->SetMaterial(0,RingMaterial);}
}
void ALNLanternVisual::EndPlay(const EEndPlayReason::Type Reason)
{if(ActiveField.IsValid())ActiveField->Destroy();Super::EndPlay(Reason);}
void ALNLanternVisual::UpdateState(float Energy,bool Lit,bool Fault,bool Preparation)
{EnergyValue=Energy;bLit=Lit;bFault=Fault;bPreparation=Preparation;if((Fault||!Lit)&&ActiveField.IsValid()){ActiveField->Destroy();ActiveField.Reset();}}
FLinearColor ALNLanternVisual::ModuleTint(int32 Type)
{const FLinearColor Colors[]={FLinearColor(1,.58f,.10f),FLinearColor(.08f,.8f,1),FLinearColor(.52f,.32f,1),FLinearColor(.12f,.95f,.65f)};return Colors[FMath::Clamp(Type,0,3)];}
void ALNLanternVisual::Trigger(float ProtectionSeconds)
{
    Pulse=1.f;if(ActiveField.IsValid())ActiveField->Destroy();
    if(ModuleType==2){auto* FX=GetWorld()->SpawnActor<ALNVisualEffect>();FX->PulseField(GetActorLocation(),1000,ModuleTint(2));ActiveField=FX;}
    if(ModuleType==3){auto* FX=GetWorld()->SpawnActor<ALNVisualEffect>();FX->Shield(GetActorLocation(),800,ProtectionSeconds,ModuleTint(3));ActiveField=FX;}
}
void ALNLanternVisual::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);Pulse=FMath::Max(0.f,Pulse-DeltaSeconds*1.7f);
    const float Time=GetWorld()->GetTimeSeconds();
    const FLinearColor Tint=bFault?FLinearColor(.8f,.12f,.03f):ModuleTint(ModuleType);
    const float Intensity=bFault?.12f+.08f*FMath::Abs(FMath::Sin(Time*4)):bLit?2.6f+Pulse*4.f:bPreparation?1.45f:.2f+EnergyValue;
    for(UMaterialInstanceDynamic* M:GlowMaterials){M->SetVectorParameterValue(TEXT("Tint"),Tint);M->SetScalarParameterValue(TEXT("Intensity"),Intensity);}
    if(RingMaterial){RingMaterial->SetVectorParameterValue(TEXT("Tint"),Tint);RingMaterial->SetScalarParameterValue(TEXT("Intensity"),.25f+EnergyValue*1.8f+Pulse*4.f);}
    Light->SetLightColor(Tint);Light->SetIntensity(bFault?0.f:bLit?1200.f+Pulse*1600.f:60.f);
}

ALNCampaignTaskVisual::ALNCampaignTaskVisual()
{
    PrimaryActorTick.bCanEverTick=false;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("TaskRoot"));
    Device=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Device"));Device->SetupAttachment(RootComponent);Device->SetCollisionProfileName(TEXT("NoCollision"));
    Zone=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TaskZone"));Zone->SetupAttachment(RootComponent);Zone->SetCollisionProfileName(TEXT("NoCollision"));Zone->SetCastShadow(false);Zone->SetRelativeLocation(FVector(0,0,8));Zone->SetRelativeScale3D(FVector(6,6,.3f));
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("Progress"));Label->SetupAttachment(RootComponent);Label->SetRelativeLocation(FVector(0,0,230));Label->SetRelativeRotation(FRotator(60,90,0));Label->SetHorizontalAlignment(EHTA_Center);Label->SetWorldSize(45);Label->SetTextRenderColor(FColor(170,235,230));
}
void ALNCampaignTaskVisual::Configure(bool Escort)
{
    for(UStaticMeshComponent* Pip:ProgressPips)if(Pip)Pip->DestroyComponent();ProgressPips.Empty();PipMaterials.Empty();
    bEscort=Escort;Device->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Escort?TEXT("/Game/Campaign/Presentation/SM_EscortCart.SM_EscortCart"):TEXT("/Game/Campaign/Presentation/SM_TaskTerminal.SM_TaskTerminal")));
    Zone->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/M11/Presentation/SM_Ring.SM_Ring")));
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/M11/FX/M_Glow.M_Glow"));if(Base){ZoneMaterial=UMaterialInstanceDynamic::Create(Base,this);Zone->SetMaterial(0,ZoneMaterial);}
    for(int32 I=0;I<12;++I)
    {
        const float Angle=2*PI*I/12;auto* Pip=NewObject<UStaticMeshComponent>(this);Pip->SetupAttachment(RootComponent);Pip->SetCollisionProfileName(TEXT("NoCollision"));Pip->SetCastShadow(false);Pip->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Pip->RegisterComponent();Pip->SetRelativeLocation(FVector(FMath::Cos(Angle)*135,FMath::Sin(Angle)*135,12));Pip->SetRelativeScale3D(FVector(.28f,.08f,.04f));Pip->SetRelativeRotation(FRotator(0,FMath::RadiansToDegrees(Angle)+90,0));ProgressPips.Add(Pip);
        if(Base){auto* M=UMaterialInstanceDynamic::Create(Base,this);Pip->SetMaterial(0,M);PipMaterials.Add(M);}
    }
}
void ALNCampaignTaskVisual::UpdateProgress(FVector Position,float Fraction,bool Blocked,bool AwaitingLight,bool Complete)
{
    if(bPlaced&&bEscort){const FVector Motion=Position-GetActorLocation();if(Motion.SizeSquared2D()>1)Device->SetRelativeRotation(FRotator(0,Motion.Rotation().Yaw,0));}
    SetActorLocation(Position);bPlaced=true;Fraction=FMath::Clamp(Fraction,0.f,1.f);
    const FLinearColor Color=Complete?FLinearColor(.15f,.9f,.55f):Blocked?FLinearColor(1,.22f,.13f):AwaitingLight?FLinearColor(1,.62f,.16f):FLinearColor(.13f,.75f,.95f);
    if(ZoneMaterial){ZoneMaterial->SetVectorParameterValue(TEXT("Tint"),Color);ZoneMaterial->SetScalarParameterValue(TEXT("Intensity"),Complete?.45f:.9f);}
    for(int32 I=0;I<PipMaterials.Num();++I){PipMaterials[I]->SetVectorParameterValue(TEXT("Tint"),Color);PipMaterials[I]->SetScalarParameterValue(TEXT("Intensity"),(I+.5f)/12<=Fraction?2.5f:.12f);}
    Label->SetText(FText::FromString(FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Fraction*100))));Label->SetTextRenderColor(Color.ToFColor(true));
}
