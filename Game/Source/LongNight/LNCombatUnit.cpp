#include "LNCombatUnit.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"

ALNCombatUnit::ALNCombatUnit()
{
    PrimaryActorTick.bCanEverTick=true;
    Body = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Body"));
    Body->InitCapsuleSize(30.f, 65.f); RootComponent = Body;
    Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Body->SetCollisionObjectType(ECC_Pawn);
    Body->SetCollisionResponseToAllChannels(ECR_Ignore);
    Body->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
    Visual->SetupAttachment(Body); Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Visual->SetStaticMesh(Cylinder.Object);
    Visual->SetRelativeScale3D(FVector(.6f,.6f,1.3f));
    CharacterMesh=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh"));
    CharacterMesh->SetupAttachment(Body); CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CharacterMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    CharacterMesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    WeaponMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
    WeaponMesh->SetupAttachment(CharacterMesh); WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SeatRing=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SeatRing"));
    SeatRing->SetupAttachment(Body); SeatRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SeatRing->SetRelativeLocation(FVector(0,0,-60)); SeatRing->SetCastShadow(false);
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
    Label->SetupAttachment(Body); Label->SetRelativeLocation(FVector(0,0,105));
    Label->SetWorldSize(30.f); Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetRelativeRotation(FRotator(60.f,90.f,0.f));
}
void ALNCombatUnit::Configure(bool Enemy, const FLNCharacterRow& Row, int32 InSeat, FLinearColor Color,FName ConfigId)
{
    bEnemy=Enemy; Stats=Row; Seat=InSeat; Health=Row.MaxHealth; Station=GetActorLocation();VisualConfigId=ConfigId;
    LastVisualLocation=GetActorLocation();
    if(auto* Mat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Unit.M_Unit")))
    {
        auto* Dynamic=UMaterialInstanceDynamic::Create(Mat,this);
        Dynamic->SetVectorParameterValue(TEXT("Tint"),Color); Visual->SetMaterial(0,Dynamic);
    }
    ApplyVisualDefinition(); UpdateLabel(); Label->SetVisibility(false);
}
void ALNCombatUnit::TakeHit(float Amount,double Now)
{
    if(!IsAlive()) return;
    Health=FMath::Max(0.f,Health-Amount);
    const double PresentationNow=GetWorld()->GetTimeSeconds();
    HitFlashUntil=PresentationNow+.16;
    if(Health<=0.f)
    {
        ReviveAt=Now+8; Visual->SetVisibility(false); DownVisualAt=PresentationNow;
        PlayVisualAnimation(VisualDefinition.Down.LoadSynchronous(),false);
        VisualActionUntil=PresentationNow+8.; WeaponMesh->SetVisibility(false); SeatRing->SetVisibility(false);
    }
    else if(PresentationNow>=VisualActionUntil)
    {PlayVisualAnimation(VisualDefinition.Hit.LoadSynchronous(),false,.25f);VisualActionUntil=PresentationNow+.25;}
    UpdateLabel();
}
void ALNCombatUnit::Restore()
{
    Health=Stats.MaxHealth;
    RootUntil=MarkUntil=ReviveAt=0; Visual->SetVisibility(!HasCharacterVisual()); CharacterMesh->SetVisibility(true);
    WeaponMesh->SetVisibility(WeaponMesh->GetStaticMesh()!=nullptr); SeatRing->SetVisibility(true);
    SetActorLocation(Station,false);LastVisualLocation=Station;
    PlayVisualAnimation(VisualDefinition.Revive.LoadSynchronous(),false,.7f);
    VisualActionUntil=GetWorld()->GetTimeSeconds()+.7; UpdateLabel();
}
void ALNCombatUnit::MoveTowards(const FVector& Target,float Speed,float DeltaSeconds)
{
    FVector Delta=Target-GetActorLocation(); Delta.Z=0;
    const FVector Step=Delta.GetClampedToMaxSize(Speed*DeltaSeconds);
    FHitResult Hit;
    AddActorWorldOffset(Step,true,&Hit);
    if(Hit.bBlockingHit)
    {
        const FVector Slide=FVector::VectorPlaneProject(Step*(1.f-Hit.Time),Hit.Normal);
        AddActorWorldOffset(Slide,true);
    }
}
bool ALNCombatUnit::CanSee(const FVector& Point) const
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(LNLineOfSight),false,this);
    FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Point,ECC_Visibility,Query);
}
void ALNCombatUnit::UpdateLabel()
{
    // ASCII IDs in world-space; Chinese names and skill details are in runtime UMG.
    Label->SetText(FText::FromString(FString::Printf(TEXT("%s %d"),bEnemy?TEXT("ENEMY"):*FString::Printf(TEXT("S%d"),Seat+1),FMath::CeilToInt(Health))));
    Label->SetTextRenderColor(IsAlive()?(bEnemy?FColor(255,145,110):FColor(170,240,235)):FColor(100,100,110));
}

void ALNCombatUnit::ApplyVisualDefinition()
{
    const FName Id=!VisualConfigId.IsNone()?VisualConfigId:Stats.PersonId==TEXT("rover")?FName(TEXT("rover_spectro")):Stats.PersonId;
    if(auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_Visuals.DT_Visuals")))
    {
        if(const auto* Row=Table->FindRow<FLNVisualRow>(Id,TEXT("VisualConfiguration")))
        {
            VisualDefinition=*Row;
            if(USkeletalMesh* Mesh=Row->Mesh.LoadSynchronous())
            {
                CharacterMesh->SetSkeletalMeshAsset(Mesh);
                CharacterMesh->SetRelativeLocation(Row->MeshOffset);
                CharacterMesh->SetRelativeScale3D(FVector(Row->MeshScale));
                FacingYaw=bEnemy?0.f:180.f;
                CharacterMesh->SetRelativeRotation(FRotator(0,FacingYaw+Row->MeshYaw,0));
                Visual->SetVisibility(false);
                const TSoftObjectPtr<UAnimSequence> Clips[]={Row->Idle,Row->Move,Row->Attack,Row->Skill,Row->SkillTwo,Row->Dodge,Row->Hit,Row->Down,Row->Revive};
                for(const auto& Reference:Clips)if(auto* Clip=Reference.LoadSynchronous())AnimationCache.Add(Clip);
                for(int32 Index=0;Index<CharacterMesh->GetNumMaterials();++Index)
                {if(auto* M=CharacterMesh->CreateDynamicMaterialInstance(Index))CharacterMaterials.Add(M);}
                PlayVisualAnimation(Row->Idle.LoadSynchronous(),true);
                if(UStaticMesh* Weapon=Row->Weapon.LoadSynchronous())
                {
                    if(CharacterMesh->DoesSocketExist(Row->WeaponSocket))
                    {
                        WeaponMesh->SetStaticMesh(Weapon);
                        WeaponMesh->AttachToComponent(CharacterMesh,FAttachmentTransformRules::SnapToTargetNotIncludingScale,Row->WeaponSocket);
                        WeaponMesh->SetRelativeLocation(Row->WeaponOffset);WeaponMesh->SetRelativeRotation(Row->WeaponRotation);
                        WeaponMesh->SetRelativeScale3D(FVector(Row->WeaponScale));
                    }
                    else UE_LOG(LogTemp,Warning,TEXT("LN visual weapon socket missing: %s / %s"),*Id.ToString(),*Row->WeaponSocket.ToString());
                }
                UE_LOG(LogTemp,Display,TEXT("LN_VISUAL_READY id=%s mesh=%s animation=%s"),*Id.ToString(),*Mesh->GetPathName(),*GetNameSafe(CurrentAnimation));
            }
        }
    }
    SeatRing->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/M11/Presentation/SM_Ring.SM_Ring")));
    SeatRing->SetRelativeScale3D(FVector(bEnemy?1.f:1.25f,bEnemy?1.f:1.25f,.8f));
    if(auto* M=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/M11/FX/M_Glow.M_Glow")))
    {
        RingMaterial=UMaterialInstanceDynamic::Create(M,this);SeatRing->SetMaterial(0,RingMaterial);
        RingMaterial->SetVectorParameterValue(TEXT("Tint"),bEnemy?FLinearColor(1.f,.15f,.035f):FLinearColor(.07f,.65f,.8f));
        RingMaterial->SetScalarParameterValue(TEXT("Intensity"),bEnemy?.5f:1.4f);
    }
}
void ALNCombatUnit::PlayVisualAnimation(UAnimSequence* Animation,bool Loop,float PresentationSeconds)
{
    if(!Animation||!HasCharacterVisual())return;
    if(CurrentAnimation==Animation&&Loop)return;
    CurrentAnimation=Animation; CharacterMesh->PlayAnimation(Animation,Loop);
    CharacterMesh->SetPlayRate(PresentationSeconds>0.f?Animation->GetPlayLength()/PresentationSeconds:1.f);
}
bool ALNCombatUnit::HasCharacterVisual()const{return CharacterMesh&&CharacterMesh->GetSkeletalMeshAsset()!=nullptr;}
FString ALNCombatUnit::GetVisualDiagnostics()const
{
    if(!HasCharacterVisual())return TEXT("fallback");
    return FString::Printf(TEXT("mesh=%s; bounds=%s; component_scale=%s; root_scale=%s; root_location=%s; anim=%s; visible=%d"),
        *GetNameSafe(CharacterMesh->GetSkeletalMeshAsset()),*CharacterMesh->Bounds.BoxExtent.ToString(),
        *CharacterMesh->GetComponentScale().ToString(),*CharacterMesh->GetBoneTransform(0).GetScale3D().ToString(),
        *CharacterMesh->GetBoneTransform(0).GetLocation().ToString(),*GetNameSafe(CurrentAnimation),CharacterMesh->IsVisible());
}
void ALNCombatUnit::PlayCombatAction(int32 SkillIndex,FVector Target,float PresentationSeconds)
{
    const FVector Direction=Target-GetActorLocation();
    if(!Direction.IsNearlyZero())FacingYaw=Direction.Rotation().Yaw;
    CharacterMesh->SetRelativeRotation(FRotator(0,FacingYaw+VisualDefinition.MeshYaw,0));
    const float Window=PresentationSeconds>0.f?PresentationSeconds:(SkillIndex?.65f:.45f);
    const auto& Clip=SkillIndex==3?VisualDefinition.Dodge:SkillIndex==2&&!VisualDefinition.SkillTwo.IsNull()?VisualDefinition.SkillTwo:SkillIndex?VisualDefinition.Skill:VisualDefinition.Attack;
    PlayVisualAnimation(Clip.LoadSynchronous(),false,Window);
    VisualActionUntil=GetWorld()->GetTimeSeconds()+Window;
}
void ALNCombatUnit::SetConductor(bool Active)
{
    bIsConductor=Active;
    if(RingMaterial&&!bEnemy)
    {RingMaterial->SetVectorParameterValue(TEXT("Tint"),Active?FLinearColor(1.f,.62f,.10f):FLinearColor(.07f,.65f,.8f));}
}
void ALNCombatUnit::SetDebugVisible(bool Visible){Label->SetVisibility(Visible);}
void ALNCombatUnit::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Now=GetWorld()->GetTimeSeconds();
    const FVector Motion=GetActorLocation()-LastVisualLocation;LastVisualLocation=GetActorLocation();
    if(IsAlive()&&Now>=VisualActionUntil)
    {
        const bool Moving=Motion.SizeSquared2D()>FMath::Square(.1f);
        if(Moving)FacingYaw=Motion.Rotation().Yaw;
        CharacterMesh->SetRelativeRotation(FRotator(0,FacingYaw+VisualDefinition.MeshYaw,0));
        PlayVisualAnimation((Moving?VisualDefinition.Move:VisualDefinition.Idle).LoadSynchronous(),true);
    }
    if(!IsAlive()&&bEnemy&&Now-DownVisualAt>1.5)CharacterMesh->SetVisibility(false);
    for(UMaterialInstanceDynamic* Material:CharacterMaterials)Material->SetScalarParameterValue(TEXT("HitFlash"),Now<HitFlashUntil?1.f:0.f);
    if(RingMaterial)RingMaterial->SetScalarParameterValue(TEXT("Intensity"),bIsConductor?2.2f+.5f*FMath::Sin(Now*4.):bEnemy?.5f:1.1f);
}
