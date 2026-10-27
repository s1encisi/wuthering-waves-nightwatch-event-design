#include "LNGameMode.h"
#include "LNCombatUnit.h"
#include "LNTacticalPawn.h"
#include "LNPlayerController.h"
#include "LNPresentation.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ALNGameMode::ALNGameMode()
{
    PrimaryActorTick.bCanEverTick=true;
    DefaultPawnClass=ALNTacticalPawn::StaticClass(); PlayerControllerClass=ALNPlayerController::StaticClass();
    SeatIds={TEXT("rover_spectro"),TEXT("jinhsi"),TEXT("changli")}; BeamYaw=180;
}
void ALNGameMode::BeginPlay()
{
    Super::BeginPlay();
    if(!Characters)Characters=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_Characters.DT_Characters"));
    if(!Lamps)Lamps=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_Lamps.DT_Lamps"));
    bSmoke=FParse::Param(FCommandLine::Get(),TEXT("LNAutoTest"));
    bRenderSmoke=FParse::Param(FCommandLine::Get(),TEXT("LNRenderSmoke"));
    bSmoke|=bRenderSmoke||FParse::Param(FCommandLine::Get(),TEXT("LNStateChecks"));
    if(FParse::Param(FCommandLine::Get(),TEXT("LNNoFault")))bScheduledFault=false;
    if(FParse::Param(FCommandLine::Get(),TEXT("LNBeam")))Module=1;
    FParse::Value(FCommandLine::Get(),TEXT("LNBasicGain="),BasicEnergyPerAction);
    if(FParse::Param(FCommandLine::Get(),TEXT("LNCloseup")))ScreenshotAt=1.5;
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("Telemetry");
    IFileManager::Get().MakeDirectory(*Directory,true);
    LogPath=Directory/FString::Printf(TEXT("lab-%s-%u.tsv"),*FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")),FPlatformProcess::GetCurrentProcessId());
    FFileHelper::SaveStringToFile(TEXT("simulation_seconds\tevent\tdetail\n"),*LogPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    Log(TEXT("session"),TEXT("version=0.1.1-M11; scenario=M1_Lab; engine=5.8.3; combat_parameters=unchanged_from_M1"));
    Log(TEXT("configuration"),FString::Printf(TEXT("basic_gain=%.2f; skill_gain=%.2f; fault=%d; module=%d"),BasicEnergyPerAction,SkillEnergyPerAction,bScheduledFault,Module));
    if(!bSmoke) LoadPreparation();
    BuildArena(); ResetSimulation();
    if(FParse::Param(FCommandLine::Get(),TEXT("LNStateChecks"))){RunStateChecks();return;}
    if(bSmoke) StartOrRetry();
}
AStaticMeshActor* ALNGameMode::Shape(FString Mesh,FVector Location,FVector Scale,FLinearColor Color,bool Blocking)
{
    auto* Actor=GetWorld()->SpawnActor<AStaticMeshActor>(Location,FRotator::ZeroRotator);
    auto* C=Actor->GetStaticMeshComponent(); C->SetMobility(EComponentMobility::Movable);
    C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,*Mesh)); C->SetWorldScale3D(Scale);
    C->SetCollisionEnabled(Blocking?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    if(Blocking){ C->SetCollisionObjectType(ECC_WorldStatic); C->SetCollisionResponseToAllChannels(ECR_Block); }
    if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Unit.M_Unit")))
    { auto* D=UMaterialInstanceDynamic::Create(Material,Actor); D->SetVectorParameterValue(TEXT("Tint"),Color); C->SetMaterial(0,D); }
    Arena.Add(Actor); return Actor;
}
void ALNGameMode::BuildArena()
{
    // Preserve the exact M1 blockers. New authored scenery has cosmetic collision disabled.
    const FString Cube=TEXT("/Engine/BasicShapes/Cube.Cube");
    Shape(Cube,FVector(200,1050,170),FVector(16,6,4),FLinearColor::Black,true)->SetActorHiddenInGame(true);
    Shape(Cube,FVector(-1850,350,120),FVector(4,4,3),FLinearColor::Black,true)->SetActorHiddenInGame(true);
    LanternVisual=GetWorld()->SpawnActor<ALNLanternVisual>(LampPosition,FRotator::ZeroRotator);
    LanternVisual->SetModule(Module,BeamYaw);
}
void ALNGameMode::ResetSimulation()
{
    for(ALNCombatUnit* U:Team) if(U) U->Destroy(); for(ALNCombatUnit* U:Enemies) if(U) U->Destroy();
    Team.Empty(); Enemies.Empty(); Events.Empty(); RelayTimes.Empty();
    SimTime=0; Countdown=5; TaskProgress=0; Defense=100; SpawnedBatches=0; TotalSpawned=TotalKilled=TotalLeaked=0;
    CurrentSeat=INDEX_NONE; ActionSerial=0; FirstContact=-1; bFaultSent=bFaultWarning=false; BurnUntil=0;
    Relay.Reset(); Energy=LongNight::EnergyPool({BasicEnergyPerAction,SkillEnergyPerAction}); Energy.Reset(); RelayWaitStarted=-1; Phase=EPhase::Preparation; bPaused=false; LastTaskReason.Empty();
    TaskReason=TEXT("任务灯未亮");
    if(BurnVisual){BurnVisual->Destroy();BurnVisual=nullptr;}
    if(LanternVisual){LanternVisual->SetModule(Module,BeamYaw);LanternVisual->UpdateState(.4f,false,false,true);}
    bDataValid=Characters && Lamps;
    if(bDataValid)
    {
        std::vector<std::vector<LongNight::CharacterSelection>> Selection(1);
        for(int32 i=0;i<SeatIds.Num();++i)
        {
            auto* Row=Characters->FindRow<FLNCharacterRow>(SeatIds[i],TEXT("CreateTeam"));
            if(!Row) { bDataValid=false; break; }
            Selection[0].push_back({TCHAR_TO_UTF8(*Row->PersonId.ToString()),TCHAR_TO_UTF8(*SeatIds[i].ToString())});
            auto* U=GetWorld()->SpawnActor<ALNCombatUnit>(LampPosition+FVector(120+i*100,220+i*100,65),FRotator::ZeroRotator);
            U->Configure(false,*Row,i,FLinearColor(.1f+.2f*i,.65f,.65f)); Team.Add(U);
        }
        bDataValid &= LongNight::ValidateLineup(Selection).Valid && LampData()!=nullptr;
    }
    BlockReason=bDataValid?TEXT("等待整备确认"):TEXT("数据资产缺失或编队无效；请运行资产生成脚本");
    Log(TEXT("preparation"),FString::Printf(TEXT("valid=%d; module=%d; seats=%s"),bDataValid,Module,*FString::JoinBy(SeatIds,TEXT(","),[](FName Id){return Id.ToString();})));
}
void ALNGameMode::StartOrRetry()
{
    if(Phase==EPhase::Defeat||Phase==EPhase::Victory) { ResetSimulation(); return; }
    if(!CanConfigure()||!bDataValid) return;
    SavePreparation(); Phase=EPhase::Countdown; Countdown=5;
    Log(TEXT("batch_warning"),TEXT("internal lab: 4 light at 0s; 1 heavy+3 light at 10s; 4 light at 20s"));
}
void ALNGameMode::PauseToggle()
{
    if(Phase!=EPhase::Combat&&Phase!=EPhase::Countdown) return;
    bPaused=!bPaused; UGameplayStatics::SetGamePaused(this,bPaused);
    Log(TEXT("pause"),bPaused?TEXT("true"):TEXT("false"));
}
void ALNGameMode::DebugToggle(){ bDebug=!bDebug; }
void ALNGameMode::CycleModule()
{
    if(!CanConfigure()) return;
    Module=(Module+1)%2; SavePreparation(); Log(TEXT("formation"),FString::Printf(TEXT("module=%d"),Module));
    if(LanternVisual)LanternVisual->SetModule(Module,BeamYaw);
}
void ALNGameMode::RotateSeats()
{
    if(!CanConfigure()) return;
    const FName First=SeatIds[0]; SeatIds.RemoveAt(0); SeatIds.Add(First); ResetSimulation(); SavePreparation();
}
void ALNGameMode::ResetLab()
{
    if(!CanConfigure() && Phase!=EPhase::Victory && Phase!=EPhase::Defeat) return;
    SeatIds={TEXT("rover_spectro"),TEXT("jinhsi"),TEXT("changli")}; Module=0; ResetSimulation(); SavePreparation();
}
bool ALNGameMode::CanConfigure() const {return Phase==EPhase::Preparation;}
void ALNGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!bComparisonCameraSet&&(FParse::Param(FCommandLine::Get(),TEXT("LNComparison"))||FParse::Param(FCommandLine::Get(),TEXT("LNCloseup"))))
    {
        if(auto* PC=GetWorld()->GetFirstPlayerController())if(auto* Camera=Cast<ALNTacticalPawn>(PC->GetPawn()))
        {if(FParse::Param(FCommandLine::Get(),TEXT("LNCloseup"))&&Team.Num())Camera->FocusCharacterEvidence(Team[0]->Station);else Camera->FocusComparison();bComparisonCameraSet=true;}
    }
    for(ALNCombatUnit* Unit:Team)Unit->SetDebugVisible(bDebug);
    for(ALNCombatUnit* Unit:Enemies)Unit->SetDebugVisible(bDebug);
    if(bDebug) DrawDebugOverlay();
    if(bPaused) return;
    if(Phase==EPhase::Countdown)
    {
        Countdown-=DeltaSeconds;
        if(Countdown<=0){ Phase=EPhase::Combat; Log(TEXT("combat_begin"),TEXT("M1 lab; no representation of complete level 1")); }
        return;
    }
    if(Phase!=EPhase::Combat) return;
    SimTime+=DeltaSeconds;
    if(SpawnedBatches<3 && SimTime>=SpawnedBatches*10.) { SpawnBatch(SpawnedBatches); ++SpawnedBatches; }
    if(!bFaultWarning && SimTime>=9 && bScheduledFault){bFaultWarning=true;Log(TEXT("fault_warning"),TEXT("L1 scheduled at 12s, duration6s; lab test event"));}
    if(!bFaultSent && SimTime>=12 && bScheduledFault){bFaultSent=true;Relay.StartFault(SimTime,6);BurnUntil=0;if(BurnVisual){BurnVisual->Destroy();BurnVisual=nullptr;}Log(TEXT("fault_begin"),TEXT("L1; scheduled; 6s"));}
    UpdateTeam(DeltaSeconds); UpdateEnemies(DeltaSeconds); UpdateLamp(DeltaSeconds); UpdateTask(DeltaSeconds);
    int32 Alive=0; for(ALNCombatUnit* E:Enemies) if(E->IsAlive()) ++Alive;
    if(Defense<=0){ Phase=EPhase::Defeat;Log(TEXT("defeat"),TEXT("defense_zero; retry restores wave checkpoint")); }
    else if(LongNight::CanCompleteWave(SpawnedBatches==3,Alive,TaskProgress>=10))
    { Phase=EPhase::Victory; bLabCompleted=true; SavePreparation(); Log(TEXT("victory"),TEXT("all 3 batches handled and 10s task complete")); }
    if(bRenderSmoke&&!bScreenshot&&SimTime>=ScreenshotAt)
    {
        bScreenshot=true;
        for(ALNCombatUnit* Unit:Team)Log(TEXT("visual_state"),Unit->Stats.PersonId.ToString()+TEXT("; ")+Unit->GetVisualDiagnostics());
        for(ALNCombatUnit* Unit:Enemies)if(Unit->IsAlive()){Log(TEXT("enemy_visual_state"),Unit->GetVisualDiagnostics());break;}
        const FString ImageName=FParse::Param(FCommandLine::Get(),TEXT("LNCloseup"))?TEXT("M11-characters-closeup.png"):Module==0?TEXT("M11-runtime.png"):TEXT("M11-beam-overview.png");
        const FString ScreenshotFile=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/ImageName);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ScreenshotFile),true);
        FScreenshotRequest::RequestScreenshot(ScreenshotFile,true,false,false,FIntRect(),true);
        Log(TEXT("screenshot_requested"),ScreenshotFile);
    }
    if(bSmoke&&!FParse::Param(FCommandLine::Get(),TEXT("LNStateChecks"))&&(Phase==EPhase::Victory||Phase==EPhase::Defeat||SimTime>=SmokeDeadline)) FinishSmoke();
}
void ALNGameMode::SpawnBatch(int32 Batch)
{
    for(int32 i=0;i<4;++i)
    {
        FLNCharacterRow Row; Row.MaxHealth=EnemyHealth; Row.DisplayName=TEXT("演练轻波");
        const bool Heavy=Batch==1&&i==0;
        if(Heavy){Row.MaxHealth=900;Row.DisplayName=TEXT("演练巨浪");}
        Row.PersonId=Heavy?TEXT("enemy_heavy"):TEXT("enemy_light");
        auto* U=GetWorld()->SpawnActor<ALNCombatUnit>(FVector(-2700-i*90,-1600-i*50,65),FRotator::ZeroRotator);
        U->Configure(true,Row,-1,Heavy?FLinearColor(.7,.3,.6):FLinearColor(.8,.25,.1));
        U->bHeavy=Heavy;
        U->Route={FVector(-1700,-500,65),FVector(1100,-500,65),FVector(2100,200,65)};
        Enemies.Add(U); ++TotalSpawned;
    }
    Log(TEXT("batch_spawn"),FString::Printf(TEXT("batch=%d; count=4; total=%d"),Batch+1,TotalSpawned));
    if(Batch<2) Log(TEXT("batch_intel"),FString::Printf(TEXT("next_at=%.0f"),(Batch+1)*10.));
}
bool ALNGameMode::HasLineOfSight(FVector From,FVector To) const
{
    FHitResult Hit; return !GetWorld()->LineTraceSingleByChannel(Hit,From,To,ECC_Visibility);
}
ALNCombatUnit* ALNGameMode::SelectEnemy(ALNCombatUnit* Unit,bool HeavyOnly) const
{
    ALNCombatUnit* Best=nullptr; float BestScore=-FLT_MAX;
    for(ALNCombatUnit* E:Enemies)
    {
        if(!E->IsAlive()||(HeavyOnly&&!E->bHeavy))continue;
        const float HomeDist=FVector::Dist2D(E->GetActorLocation(),LampPosition);
        if(HomeDist>(Unit->Stats.bRanged?1400.f:800.f)||!Unit->CanSee(E->GetActorLocation()))continue;
        const float Score=E->GetActorLocation().X;
        if(Score>BestScore){BestScore=Score;Best=E;}
    }
    return Best;
}
void ALNGameMode::UpdateTeam(float DeltaSeconds)
{
    // At 80 energy, process the next available conductor first so a ready mark
    // can resolve before another actor fills the pool and the lamp fires.
    int32 PreparedSeat=INDEX_NONE;
    if(Energy.Value()>=80.0 && Team.Num()==3)
    {
        for(int32 Offset=0;Offset<3;++Offset)
        {
            const int32 Seat=(Relay.NextSeat()+Offset)%3;
            if(Team[Seat]->IsAlive() && SimTime>=Team[Seat]->RootUntil)
            {PreparedSeat=Seat;break;}
        }
    }
    for(int32 Visit=0;Visit<Team.Num();++Visit)
    {
        const int32 Index=PreparedSeat==INDEX_NONE?Visit:(PreparedSeat+Visit)%Team.Num();
        ALNCombatUnit* U=Team[Index];
        if(!U->IsAlive())
        {if(SimTime>=U->ReviveAt){U->Restore();Log(TEXT("revive"),U->Stats.DisplayName);}continue;}
        if(SimTime<U->RootUntil || SimTime<U->ActionReady)continue;

        const FName Person=U->Stats.PersonId;
        // M1 route advances monotonically along X: greatest X is nearest exit.
        const auto FindSkillTarget=[&](float Range,bool LightOnly,bool HeavyOnly,bool LimitHome)->ALNCombatUnit*
        {
            ALNCombatUnit* Best=nullptr;
            for(ALNCombatUnit* E:Enemies)
            {
                if(!E->IsAlive() || (LightOnly&&E->bHeavy) || (HeavyOnly&&!E->bHeavy))continue;
                if(FVector::Dist2D(U->GetActorLocation(),E->GetActorLocation())>Range)continue;
                if(LimitHome && FVector::Dist2D(E->GetActorLocation(),LampPosition)>800.f)continue;
                if(!U->CanSee(E->GetActorLocation()))continue;
                if(!Best || E->GetActorLocation().X>Best->GetActorLocation().X)Best=E;
            }
            return Best;
        };

        int32 SkillIndex=0;
        ALNCombatUnit* Target=nullptr;
        const bool PreparingMark=Index==PreparedSeat && (Person==TEXT("rover")||Person==TEXT("jinhsi"));
        if(PreparingMark && SimTime>=U->SkillTwoReady)
        {
            Target=FindSkillTarget(1400.f,false,Person==TEXT("jinhsi"),false);
            if(Target)SkillIndex=2;
        }
        if(!Target && SimTime>=U->SkillTwoReady)
        {
            // Jinhsi only marks heavy enemies. Dismantler preference awaits that enemy role.
            Target=FindSkillTarget(1400.f,false,Person==TEXT("jinhsi"),false);
            if(Target)SkillIndex=2;
        }
        if(!Target && SimTime>=U->SkillOneReady)
        {
            const float Range=Person==TEXT("rover")?600.f:1400.f;
            const bool Dash=Person==TEXT("changli");
            Target=FindSkillTarget(Range,Dash,false,Dash);
            if(Target)SkillIndex=1;
        }
        if(!Target)
        {
            Target=SelectEnemy(U);
            if(!Target){U->MoveTowards(U->Station,280.f,DeltaSeconds);continue;}
            const float Reach=U->Stats.bRanged?1400.f:200.f;
            if(FVector::Dist2D(U->GetActorLocation(),Target->GetActorLocation())>Reach)
            {U->MoveTowards(Target->GetActorLocation(),280.f,DeltaSeconds);continue;}
            if(SimTime<U->BasicReady)continue;
        }

        if(SkillIndex==1 && Person==TEXT("changli"))
        {
            const FVector Offset=Target->GetActorLocation()-U->GetActorLocation();
            const float Distance=Offset.Size2D();
            if(Distance>200.f)
            {
                // Both endpoints are inside the lamp's 8m circle; a swept straight
                // segment stays within that circle and stops at blocking geometry.
                const FVector Direction=FVector(Offset.X,Offset.Y,0).GetSafeNormal();
                const FVector Step=Direction*FMath::Min(Distance-150.f,U->DashSpeedCm*DeltaSeconds);
                FHitResult Hit;
                U->AddActorWorldOffset(Step,true,&Hit);
                if(FVector::Dist2D(U->GetActorLocation(),Target->GetActorLocation())>200.f)continue;
            }
            if(!U->CanSee(Target->GetActorLocation()))continue;
        }

        const FVector Origin=U->GetActorLocation();
        FVector Direction=Target->GetActorLocation()-Origin;Direction.Z=0;Direction.Normalize();
        const float Damage=SkillIndex==2?U->Stats.SkillTwoDamage:SkillIndex==1?U->Stats.SkillOneDamage:U->Stats.BasicDamage;
        int32 HitCount=0;
        const auto ApplyHit=[&](ALNCombatUnit* E)
        {
            if(!E->IsAlive())return;
            E->TakeHit(Damage,SimTime);++HitCount;
            if(SkillIndex==1 && Person==TEXT("rover") && !E->bHeavy)
                E->RootUntil=FMath::Max(E->RootUntil,SimTime+2.0);
            if(SkillIndex==2 && (Person==TEXT("rover")||Person==TEXT("jinhsi")))E->MarkUntil=SimTime+6.0;
            if(SkillIndex==2 && Person==TEXT("changli") && E->TryInterrupt(SimTime,U->InterruptSeconds))
                Log(TEXT("interrupt"),FString::Printf(TEXT("person=changli; heavy=%d; duration=%.2f; heavy_lockout=8s"),E->bHeavy,U->InterruptSeconds));
        };
        if(SkillIndex==1 && Person==TEXT("rover"))
        {
            const float MinDot=FMath::Cos(FMath::DegreesToRadians(U->ConeHalfAngleDegrees));
            for(ALNCombatUnit* E:Enemies)
            {
                FVector Offset=E->GetActorLocation()-Origin;Offset.Z=0;
                if(!E->IsAlive() || Offset.SizeSquared()>FMath::Square(600.f) || !U->CanSee(E->GetActorLocation()))continue;
                if(Offset.IsNearlyZero() || FVector::DotProduct(Offset.GetSafeNormal(),Direction)>=MinDot)ApplyHit(E);
            }
        }
        else if(SkillIndex==1 && Person==TEXT("jinhsi"))
        {
            for(ALNCombatUnit* E:Enemies)
            {
                FVector Offset=E->GetActorLocation()-Origin;Offset.Z=0;
                const float Along=FVector::DotProduct(Offset,Direction),Side=FVector::CrossProduct(Offset,Direction).Size();
                if(E->IsAlive() && Along>=0.f && Along<=1400.f && Side<=U->LineWidthCm*.5f && U->CanSee(E->GetActorLocation()))ApplyHit(E);
            }
        }
        else ApplyHit(Target);
        if(HitCount==0)continue;

        if(FirstContact<0){FirstContact=SimTime;Log(TEXT("first_contact"),U->Stats.DisplayName);}
        if(SkillIndex==2)U->SkillTwoReady=SimTime+10.0;
        else if(SkillIndex==1)U->SkillOneReady=SimTime+6.0;
        else U->BasicReady=SimTime+U->Stats.BasicInterval;
        U->ActionReady=SimTime+.35;
        if(PreparingMark && SkillIndex==2)
            Log(TEXT("relay_prepare"),FString::Printf(TEXT("seat=%d; person=%s; mark=6s; ready_skill_only=1"),U->Seat+1,*Person.ToString()));
        // One action ID and one energy credit regardless of AOE victim count.
        const double Added=Energy.RecordHit(SkillIndex?LongNight::HitSource::Skill:LongNight::HitSource::Basic,++ActionSerial,SimTime);
        Log(TEXT("energy_hit"),FString::Printf(TEXT("L1; seat=%d; person=%s; source=%s; skill=%d; hits=%d; action=%d; delta=%.1f; energy=%.1f"),U->Seat+1,*Person.ToString(),SkillIndex?TEXT("skill"):TEXT("basic"),SkillIndex,HitCount,ActionSerial,Added,Energy.Value()));
        U->PlayCombatAction(SkillIndex,Target->GetActorLocation());
        auto* HitEffect=GetWorld()->SpawnActor<ALNVisualEffect>();
        HitEffect->Beam(Origin+FVector(0,0,45),Target->GetActorLocation()+FVector(0,0,25),SkillIndex?12.f:5.f,SkillIndex?FLinearColor(.2f,.8f,1.f):FLinearColor(1.f,.8f,.3f),.13f);
    }
}
void ALNGameMode::UpdateEnemies(float DeltaSeconds)
{
    for(ALNCombatUnit* E:Enemies)
    {
        if(!E->IsAlive())continue;
        ALNCombatUnit* Near=nullptr; float Distance=200;
        for(ALNCombatUnit* U:Team)if(U->IsAlive())
        {const float D=FVector::Dist2D(E->GetActorLocation(),U->GetActorLocation());if(D<Distance&&E->CanSee(U->GetActorLocation())){Distance=D;Near=U;}}
        if(Near&&SimTime>=E->NextEngagement)
        {E->EngagementUntil=SimTime+3;E->NextEngagement=SimTime+7;}
        if(Near&&SimTime<E->EngagementUntil)
        {
            if(SimTime>=E->BasicReady){Near->TakeHit(EnemyDamage,SimTime);E->BasicReady=SimTime+1.5;E->PlayCombatAction(0,Near->GetActorLocation());}
            continue;
        }
        if(SimTime<E->RootUntil)continue;
        if(E->Waypoint<E->Route.Num())
        {
            E->MoveTowards(E->Route[E->Waypoint],EnemySpeed*(E->bHeavy?.7f:1.f),DeltaSeconds);
            if(FVector::Dist2D(E->GetActorLocation(),E->Route[E->Waypoint])<40)++E->Waypoint;
        }
        else
        {
            Defense=FMath::Max(0,Defense-(E->bHeavy?20:10));E->bExited=true;E->SetActorHiddenInGame(true);++TotalLeaked;
            Log(TEXT("breach"),FString::Printf(TEXT("class=%s; defense=%d"),E->bHeavy?TEXT("heavy"):TEXT("light"),Defense));
        }
    }
}
const FLNLampRow* ALNGameMode::LampData()const
{return Lamps?Lamps->FindRow<FLNLampRow>(Module==0?TEXT("flame"):TEXT("beam"),TEXT("LampData")):nullptr;}
void ALNGameMode::DealArea(FVector Center,float Radius,float Damage)
{
    for(ALNCombatUnit* E:Enemies)if(E->IsAlive()&&FVector::Dist2D(E->GetActorLocation(),Center)<=Radius&&HasLineOfSight(Center+FVector(0,0,65),E->GetActorLocation()))E->TakeHit(Damage,SimTime);
}
bool ALNGameMode::LampCanHit(const ALNCombatUnit* Enemy, const FLNLampRow& Row) const
{
    if(!Enemy || !Enemy->IsAlive()) return false;
    FVector Offset=Enemy->GetActorLocation()-LampPosition; Offset.Z=0;
    if(Module==1)
    {
        const FVector Direction=FRotator(0,BeamYaw,0).Vector();
        const float Along=FVector::DotProduct(Offset,Direction);
        const float Side=FVector::CrossProduct(Offset,Direction).Size();
        if(Along<0 || Along>Row.RangeCm || Side>Row.WidthCm*.5f) return false;
    }
    else if(Offset.SizeSquared()>FMath::Square(Row.RangeCm)) return false;
    return HasLineOfSight(LampPosition+FVector(0,0,80),Enemy->GetActorLocation());
}
void ALNGameMode::FireLamp(int32 Seat)
{
    const auto* Row=LampData(); if(!Row)return;
    if(LanternVisual)LanternVisual->Trigger();
    for(int32 Index=0;Index<Team.Num();++Index)Team[Index]->SetConductor(Index==Seat);
    ALNCombatUnit* Target=nullptr;
    for(ALNCombatUnit* E:Enemies) if(LampCanHit(E,*Row))
    {if(!Target||E->GetActorLocation().X>Target->GetActorLocation().X)Target=E;}
    if(!Target)return; // Task-only ignition still leaves the persistent lamp lit.
    if(Module==0)
    {
        if(Team[Seat]->Stats.PersonId==TEXT("rover"))for(ALNCombatUnit* E:Enemies)if(E->MarkUntil>SimTime&&LampCanHit(E,*Row)){Target=E;break;}
        BurnCenter=Target->GetActorLocation();BurnCenter.Z=0;
        DealArea(BurnCenter,Row->RadiusCm,Row->Damage);BurnUntil=SimTime+Row->Duration;NextBurn=SimTime+1;
        bCrossFire=Team[Seat]->Stats.PersonId==TEXT("changli");
        if(bCrossFire)Log(TEXT("cooperation"),TEXT("changli; cross_fire; duration=4; half_length=600; half_width=100; tick_damage=12 provisional"));
        auto* Burst=GetWorld()->SpawnActor<ALNVisualEffect>();Burst->Burst(BurnCenter,Row->RadiusCm,FLinearColor(1.f,.35f,.025f));
        if(BurnVisual)BurnVisual->Destroy();
        BurnVisual=GetWorld()->SpawnActor<ALNVisualEffect>();BurnVisual->Burn(BurnCenter,Row->RadiusCm,Row->Duration,bCrossFire);
    }
    else
    {
        const FVector Dir=FRotator(0,BeamYaw,0).Vector();
        for(ALNCombatUnit* E:Enemies)
        {
            if(!LampCanHit(E,*Row))continue;
            E->TakeHit(Row->Damage+(E->bHeavy?Row->Damage*.6f:0),SimTime);
            const FName Person=Team[Seat]->Stats.PersonId;
            if(E->MarkUntil>SimTime&&(Person==TEXT("rover")||Person==TEXT("jinhsi"))) E->TakeHit(Person==TEXT("jinhsi")?50:25,SimTime);
        }
        auto* Beam=GetWorld()->SpawnActor<ALNVisualEffect>();
        Beam->Beam(LampPosition+FVector(0,0,175),LampPosition+Dir*Row->RangeCm+FVector(0,0,175),Row->WidthCm,FLinearColor(.06f,.6f,1.f),.55f);
    }
    if(bRenderSmoke&&!bRelayScreenshot)
    {
        bRelayScreenshot=true;
        const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(Module==0?TEXT("M11-flame-trigger.png"):TEXT("M11-beam-trigger.png")));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
        FTimerHandle CaptureTimer;
        GetWorldTimerManager().SetTimer(CaptureTimer,FTimerDelegate::CreateWeakLambda(this,[Path]()
        {FScreenshotRequest::RequestScreenshot(Path,true,false,false,FIntRect(),true);}),.15f,false);
    }
}
void ALNGameMode::UpdateLamp(float DeltaSeconds)
{
    std::array<bool,3> Living={false,false,false}, Actionable={false,false,false};
    for(int i=0;i<Team.Num()&&i<3;++i)
    {
        Living[i]=Team[i]->IsAlive();
        Actionable[i]=Living[i]&&SimTime>=Team[i]->RootUntil;
    }
    Relay.UpdateTeam(Living);
    int32 Alive=0;bool InRange=false;const auto* Row=LampData();
    for(ALNCombatUnit* E:Enemies)if(E->IsAlive())
    {++Alive;if(Row&&LampCanHit(E,*Row))InRange=true;}
    if(Alive==0&&!Relay.IsLit()&&TaskProgress<10)
    {const double Gain=Energy.AddTaskFallback(DeltaSeconds,false,false);if(Gain>0)Log(TEXT("task_energy"),FString::Printf(TEXT("delta=%.3f; energy=%.3f"),Gain,Energy.Value()));}
    bool WaitingForAction=false;
    const bool CanWait=Energy.Value()>=100&&!Relay.IsFaulted(SimTime)&&(RelayTimes.IsEmpty()||SimTime-RelayTimes.Last()>=8.)&&(InRange||(TaskProgress<10&&!Relay.IsLit()));
    if(CanWait)
    {
        for(int32 Offset=0;Offset<3;++Offset)
        {
            const int32 Index=(Relay.NextSeat()+Offset)%3;
            if(Actionable[Index])
            {
                const double Remaining=Team[Index]->ActionReady-SimTime;
                if(Remaining>0){if(RelayWaitStarted<0)RelayWaitStarted=SimTime;WaitingForAction=SimTime-RelayWaitStarted<1.;}
                break;
            }
        }
    }
    else RelayWaitStarted=-1;
    const auto Result=WaitingForAction?LongNight::RelayResult{}:Relay.TryTrigger(SimTime,Energy,Actionable,{InRange,false,TaskProgress<10&&!Relay.IsLit()});
    static const TCHAR* Reasons[]={TEXT("已触发"),TEXT("暂停"),TEXT("协奏不足 100"),TEXT("灯具故障；命中仍可积能"),TEXT("可行动角色少于两人"),TEXT("距上次接力不足 8 秒"),TEXT("无有效目标；保留满额协奏")};
    BlockReason=Reasons[static_cast<int>(Result.Reason)];
    if(WaitingForAction)BlockReason=TEXT("等待下一主奏当前动作完成（最多 1 秒）");
    if(Result.Triggered)
    {
        RelayWaitStarted=-1;CurrentSeat=Result.Seat;RelayTimes.Add(SimTime);FireLamp(Result.Seat);
        Log(TEXT("relay"),FString::Printf(TEXT("L1; seat=%d; person=%s; elapsed_contact=%.3f; energy=%.1f; lit=1"),Result.Seat+1,*Team[Result.Seat]->Stats.PersonId.ToString(),FirstContact>=0?SimTime-FirstContact:-1,Energy.Value()));
    }
    if(LanternVisual)LanternVisual->UpdateState(Energy.Value()/100.,Relay.IsLit(),Relay.IsFaulted(SimTime),false);
    if(SimTime<BurnUntil && SimTime>=NextBurn && Relay.IsLit())
    {
        if(Row)DealArea(BurnCenter,Row->RadiusCm,Row->TickDamage);
        if(bCrossFire)for(ALNCombatUnit* E:Enemies)
        {
            const FVector D=E->GetActorLocation()-BurnCenter;
            const bool InCross=(FMath::Abs(D.X)<=600&&FMath::Abs(D.Y)<=100)||(FMath::Abs(D.Y)<=600&&FMath::Abs(D.X)<=100);
            if(E->IsAlive()&&InCross&&HasLineOfSight(BurnCenter+FVector(0,0,65),E->GetActorLocation()))E->TakeHit(12.f,SimTime);
        }
        NextBurn+=1;
    }
    TotalKilled=0;for(ALNCombatUnit* E:Enemies)if(E->Health<=0)++TotalKilled;
}
void ALNGameMode::UpdateTask(float DeltaSeconds)
{
    bool Blocked=false;
    for(ALNCombatUnit* E:Enemies)if(E->IsAlive()&&FVector::Dist2D(E->GetActorLocation(),TaskPosition)<=300){Blocked=true;break;}
    TaskReason=!Relay.IsLit()?TEXT("任务灯未亮"):Blocked?TEXT("作业点 3 米内有敌人"):TaskProgress>=10?TEXT("已完成"):TEXT("校线进行中");
    if(TaskProgress<10&&Relay.IsLit()&&!Blocked)TaskProgress=FMath::Min(10.,TaskProgress+DeltaSeconds);
    if(TaskReason!=LastTaskReason){Log(TEXT("task_state"),FString::Printf(TEXT("progress=%.3f; reason=%s"),TaskProgress,*TaskReason));LastTaskReason=TaskReason;}
}
void ALNGameMode::DrawDebugOverlay()
{
    DrawDebugCircle(GetWorld(),LampPosition+FVector(0,0,10),800,64,FColor(80,160,190),false,-1,0,2,FVector(1,0,0),FVector(0,1,0),false);
    DrawDebugCircle(GetWorld(),LampPosition+FVector(0,0,12),1400,64,FColor(45,90,100),false,-1,0,1,FVector(1,0,0),FVector(0,1,0),false);
    DrawDebugCircle(GetWorld(),TaskPosition+FVector(0,0,15),300,32,FColor::Green,false,-1,0,3,FVector(1,0,0),FVector(0,1,0),false);
    if(Module==1)
    {const FVector D=FRotator(0,BeamYaw,0).Vector(),S=FVector(-D.Y,D.X,0)*150;for(int s:{-1,1})DrawDebugLine(GetWorld(),LampPosition+S*s+FVector(0,0,20),LampPosition+D*2800+S*s+FVector(0,0,20),FColor::Cyan,false,-1,0,2);}
    if(SimTime<BurnUntil)
    {
        DrawDebugCircle(GetWorld(),BurnCenter+FVector(0,0,20),600,48,FColor::Orange,false,-1,0,3,FVector(1,0,0),FVector(0,1,0),false);
        if(bCrossFire){DrawDebugLine(GetWorld(),BurnCenter+FVector(-600,0,25),BurnCenter+FVector(600,0,25),FColor::Red,false,-1,0,10);DrawDebugLine(GetWorld(),BurnCenter+FVector(0,-600,25),BurnCenter+FVector(0,600,25),FColor::Red,false,-1,0,10);}
    }
}
void ALNGameMode::Log(const FString& Event,const FString& Detail)
{
    const FString Line=FString::Printf(TEXT("%.3f\t%s\t%s\n"),SimTime,*Event,*Detail);
    FFileHelper::SaveStringToFile(Line,*LogPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    if(Event!=TEXT("energy_hit")&&Event!=TEXT("task_energy"))
    {Events.Insert(FString::Printf(TEXT("%05.1fs  %s  %s"),SimTime,*Event,*Detail),0);if(Events.Num()>5)Events.SetNum(5);}
    UE_LOG(LogTemp,Display,TEXT("LN %s"),*Line.TrimEnd());
}
void ALNGameMode::SavePreparation()
{
    const bool StateCheck=FParse::Param(FCommandLine::Get(),TEXT("LNStateChecks"));
    if(bSmoke&&!StateCheck)return;
    const TCHAR* Slot=StateCheck?TEXT("LongNight_Automation"):TEXT("LongNight_M1");
    auto* Save=Cast<ULNSaveGame>(UGameplayStatics::CreateSaveGameObject(ULNSaveGame::StaticClass()));
    Save->Seats=SeatIds;Save->Module=Module;Save->BeamYaw=BeamYaw;Save->bLabCompleted=bLabCompleted;
    if(!UGameplayStatics::SaveGameToSlot(Save,Slot,0))Log(TEXT("save_error"),TEXT("Unable to write preparation slot"));
}
void ALNGameMode::LoadPreparation()
{
    const TCHAR* Slot=FParse::Param(FCommandLine::Get(),TEXT("LNStateChecks"))?TEXT("LongNight_Automation"):TEXT("LongNight_M1");
    if(!UGameplayStatics::DoesSaveGameExist(Slot,0))return;
    auto* Save=Cast<ULNSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
    if(!Save||Save->Version!=1||Save->ScenarioId!=TEXT("M1_Lab")||Save->Seats.Num()!=3||Save->Module<0||Save->Module>1)
    {Log(TEXT("save_invalid"),TEXT("Save rejected; defaults retained"));return;}
    const TSet<FName> Allowed={TEXT("rover_spectro"),TEXT("jinhsi"),TEXT("changli")};TSet<FName> Unique;
    for(FName Id:Save->Seats){if(!Allowed.Contains(Id)||Unique.Contains(Id)){Log(TEXT("save_invalid"),TEXT("Invalid or duplicate character"));return;}Unique.Add(Id);}
    SeatIds=Save->Seats;Module=Save->Module;BeamYaw=Save->BeamYaw;bLabCompleted=Save->bLabCompleted;
    Log(TEXT("save_loaded"),TEXT("pre-wave stable IDs restored"));
}
void ALNGameMode::EndPlay(const EEndPlayReason::Type Reason)
{if(CanConfigure())SavePreparation();Super::EndPlay(Reason);}
FString ALNGameMode::GetStatusText()const
{
    const TCHAR* State=Phase==EPhase::Preparation?TEXT("整备 · 不限时"):Phase==EPhase::Countdown?TEXT("来敌预告"):Phase==EPhase::Combat?TEXT("战斗观察"):Phase==EPhase::Victory?TEXT("演练完成"):TEXT("防线失守");
    return FString::Printf(TEXT("%s%s  |  防线 %d\n计时 %.1f s  ·  已出现 %d / 12\n击败 %d  ·  突破 %d  ·  批次 %d / 3"),State,bPaused?TEXT(" [已暂停]"):TEXT(""),Defense,Phase==EPhase::Countdown?Countdown:SimTime,TotalSpawned,TotalKilled,TotalLeaked,SpawnedBatches);
}
FString ALNGameMode::GetDetailText()const
{
    const FString Current=Team.IsValidIndex(CurrentSeat)?Team[CurrentSeat]->Stats.DisplayName:TEXT("尚未接任");
    const FString Next=Team.IsValidIndex(Relay.NextSeat())?Team[Relay.NextSeat()]->Stats.DisplayName:TEXT("—");
    return FString::Printf(TEXT("灯 1 · %s  |  协奏 %.0f / 100\n当前：%s\n下一席：%s\n状态：%s  |  %s\n故障剩余 %.1fs\n校线 %.1f / 10s · %s\n预告：0 / 10 / 20 秒三批来敌\n测试故障：12—18 秒\n青圈：8m追击 / 14m射程；绿圈：3m任务"),Module==0?TEXT("聚焰"):TEXT("贯阵"),Energy.Value(),*Current,*Next,Relay.IsLit()?TEXT("持续点亮"):TEXT("等待点亮"),*BlockReason,Relay.FaultRemaining(SimTime),TaskProgress,*TaskReason);
}
FString ALNGameMode::GetRosterText()const
{
    FString Result;for(ALNCombatUnit* U:Team)Result+=FString::Printf(TEXT("%d  %s  %.0f / %.0f%s\n"),U->Seat+1,*U->Stats.DisplayName,U->Health,U->Stats.MaxHealth,U->IsAlive()?TEXT(""):TEXT(" [恢复中]"));return Result;
}
FString ALNGameMode::GetEventText()const{return FString::Join(Events,TEXT("\n"));}
float ALNGameMode::GetEnergyFraction()const{return Energy.Value()/100.;}
void ALNGameMode::FinishSmoke()
{
    FString Result=FString::Printf(TEXT("scenario=M1_Lab\nresult=%s\nsim_seconds=%.3f\nspawned=%d\nkilled=%d\nleaked=%d\ndefense=%d\ntask_seconds=%.3f\nrelay_count=%d\nfirst_contact=%.3f\n"),Phase==EPhase::Victory?TEXT("VICTORY"):Phase==EPhase::Defeat?TEXT("DEFEAT"):TEXT("TIMEOUT"),SimTime,TotalSpawned,TotalKilled,TotalLeaked,Defense,TaskProgress,RelayTimes.Num(),FirstContact);
    for(int i=0;i<RelayTimes.Num();++i)Result+=FString::Printf(TEXT("relay_%d=%.3f\n"),i+1,RelayTimes[i]);
    const FString ResultFile=FString::Printf(TEXT("%s-%s-%s.txt"),bRenderSmoke?TEXT("render"):TEXT("headless"),Module==0?TEXT("flame"):TEXT("beam"),bScheduledFault?TEXT("fault"):TEXT("baseline"));
    const FString ResultPath=FPaths::ProjectSavedDir()/TEXT("Telemetry")/ResultFile;
    FFileHelper::SaveStringToFile(Result,*ResultPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    Log(TEXT("smoke_complete"),Result.Replace(TEXT("\n"),TEXT("; ")));
    if(GEngine)GEngine->Exec(GetWorld(),TEXT("QUIT"));
}

void ALNGameMode::RunStateChecks()
{
    FString Report; int32 Failures=0;
    auto Check=[&](bool Condition,const TCHAR* Name){Report+=FString::Printf(TEXT("%s %s\n"),Condition?TEXT("PASS"):TEXT("FAIL"),Name);if(!Condition)++Failures;};
    Check(bDataValid && Team.Num()==3,TEXT("real_assets_and_three_spawned_units"));
    Tick(2.f);Check(SimTime==0&&Energy.Value()==40,TEXT("preparation_freezes_simulation"));
    CycleModule();Check(Module==1,TEXT("module_change_in_preparation"));
    RotateSeats();Check(SeatIds[0]==TEXT("jinhsi"),TEXT("seat_rotation_in_preparation"));
    StartOrRetry();const int32 LockedModule=Module;const FName LockedSeat=SeatIds[0];CycleModule();RotateSeats();
    Check(Module==LockedModule&&SeatIds[0]==LockedSeat,TEXT("countdown_rejects_formation_changes"));
    Tick(1.f);const double BeforeCountdown=Countdown;PauseToggle();Tick(2.f);
    Check(UGameplayStatics::IsGamePaused(this)&&Countdown==BeforeCountdown,TEXT("engine_pause_freezes_countdown"));PauseToggle();
    Tick(5.f);Tick(.1f);const double BeforeTime=SimTime;const double BeforeTask=TaskProgress;
    PauseToggle();Tick(2.f);Check(UGameplayStatics::IsGamePaused(this)&&SimTime==BeforeTime&&TaskProgress==BeforeTask,TEXT("engine_pause_freezes_combat_and_task"));PauseToggle();
    Tick(.1f);Check(SimTime>BeforeTime,TEXT("resume_advances_simulation"));
    const auto SavedSeats=SeatIds;const int32 SavedModule=Module;
    SavePreparation();Module=0;SeatIds={TEXT("rover_spectro"),TEXT("jinhsi"),TEXT("changli")};LoadPreparation();
    Check(SeatIds==SavedSeats&&Module==SavedModule,TEXT("actual_preparation_save_load_restores_stable_ids"));
    Defense=0;Phase=EPhase::Defeat;StartOrRetry();
    Check(CanConfigure()&&Defense==100&&Energy.Value()==40&&TaskProgress==0&&!Relay.IsLit(),TEXT("retry_restores_single_wave_checkpoint"));
    StartOrRetry();Tick(5.1f);Tick(.01f);
    for(ALNCombatUnit* E:Enemies){E->Destroy();}Enemies.Empty();TaskProgress=10;
    Tick(.01f);Check(Phase==EPhase::Combat,TEXT("unspawned_batches_block_victory"));
    SpawnedBatches=3;Tick(.01f);Check(Phase==EPhase::Victory,TEXT("all_batches_clear_and_task_complete_gates_victory"));
    Report+=FString::Printf(TEXT("FAILURES=%d\n"),Failures);
    FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectSavedDir()/TEXT("Telemetry/state-checks.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("LN_STATE_CHECKS\n%s"),*Report);
    if(GEngine)GEngine->Exec(GetWorld(),TEXT("QUIT"));
}



