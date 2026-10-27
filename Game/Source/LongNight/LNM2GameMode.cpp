#include "LNM2GameMode.h"
#include "LNCombatUnit.h"
#include "LNPresentation.h"
#include "LNM2TacticalPawn.h"
#include "LNM2PlayerController.h"
#include "Components/TextRenderComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

ALNM2GameMode::ALNM2GameMode()
{
    PrimaryActorTick.bCanEverTick=true;
    DefaultPawnClass=ALNM2TacticalPawn::StaticClass();
    PlayerControllerClass=ALNM2PlayerController::StaticClass();
}

void ALNM2GameMode::BeginPlay()
{
    Super::BeginPlay();
    bStateChecks=FParse::Param(FCommandLine::Get(),TEXT("LNM2StateChecks"));
    bMeleeTest=FParse::Param(FCommandLine::Get(),TEXT("LNM2MeleeTest"));
    bRenderSmoke=FParse::Param(FCommandLine::Get(),TEXT("LNM2RenderSmoke"));
    bSaveProbe=FParse::Param(FCommandLine::Get(),TEXT("LNM2SaveWrite"))||FParse::Param(FCommandLine::Get(),TEXT("LNM2SaveRead"));
    const bool PreparationSmoke=FParse::Param(FCommandLine::Get(),TEXT("LNM2PreparationSmoke"));
    bSmoke=PreparationSmoke||bMeleeTest||bStateChecks||bRenderSmoke||bSaveProbe||FParse::Param(FCommandLine::Get(),TEXT("LNM2AutoTest"));
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("Telemetry");
    IFileManager::Get().MakeDirectory(*Directory,true);
    LogPath=Directory/FString::Printf(TEXT("m2-%s-%u.tsv"),*FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")),FPlatformProcess::GetCurrentProcessId());
    FFileHelper::SaveStringToFile(TEXT("wave\tsimulation_seconds\tevent\tdetail\n"),*LogPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    bConfigValid=LoadStage();InitializeRoster();SetDefaultFormation();
    if(!bConfigValid){Notice=TEXT("M2关卡配置缺失或无效，请重新生成资产");Log(TEXT("configuration_error"),Notice);if(bSmoke)FinishSmoke();return;}
    if(!bSmoke)LoadPreparation();
    if(bMeleeTest){WaveIndex=1;CompletedTasks=1;Formation[1].Seats={TEXT("sanhua"),TEXT("taoqi"),TEXT("yuanwu")};Formation[0].Module=Formation[1].Module=1;Formation[0].BeamYaw=Formation[1].BeamYaw=0;}
    BuildRuntime();ResetWave();
    if(bLoadedCompleted){Phase=EPhase::Victory;Task.Tick(TaskSeconds,true,false);}
    Log(TEXT("session"),TEXT("version=0.2-M2; scenario=M2_DuanShao; numeric_document=1000/30/90/150; action_accept_cooldown; action=1s; basic_gain=5; skill_gain=10"));
    if(bStateChecks){RunStateChecks();return;}
    if(bSaveProbe){RunSaveProbe(FParse::Param(FCommandLine::Get(),TEXT("LNM2SaveWrite")));return;}
    if(PreparationSmoke)
    {
        FTimerHandle Capture,Quit;
        GetWorldTimerManager().SetTimer(Capture,FTimerDelegate::CreateWeakLambda(this,[this](){RequestEvidence(TEXT("M2-preparation.png"));}),1.f,false);
        GetWorldTimerManager().SetTimer(Quit,FTimerDelegate::CreateWeakLambda(this,[this](){if(GEngine)GEngine->Exec(GetWorld(),TEXT("QUIT"));}),2.5f,false);
        return;
    }
    if(bSmoke)StartOrContinue();
}

bool ALNM2GameMode::LoadStage()
{
    FString Json;const FString Path=FPaths::ProjectContentDir()/TEXT("M2/Data/Stage1.json");
    if(!FFileHelper::LoadFileToString(Json,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),StageConfig)||!StageConfig.IsValid())return false;
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    auto Vectors=[&](const TCHAR* Key,TArray<FVector>& Out)->bool
    {if(!StageConfig->TryGetArrayField(Key,Values))return false;for(const auto& V:*Values){const auto& A=V->AsArray();if(A.Num()!=3)return false;Out.Add(FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber()));}return true;};
    TArray<FVector> LP,TP;
    if(!Vectors(TEXT("lamp_positions_cm"),LP)||LP.Num()!=2||!Vectors(TEXT("task_positions_cm"),TP)||TP.Num()!=2||!Vectors(TEXT("routes_left_cm"),Routes[0])||Routes[0].Num()<2||!Vectors(TEXT("routes_right_cm"),Routes[1])||Routes[1].Num()<2||!Vectors(TEXT("shooter_stations_cm"),ShooterStations)||ShooterStations.IsEmpty()||!Vectors(TEXT("seat_offsets_cm"),SeatOffsets)||SeatOffsets.Num()!=3)return false;
    for(int32 I=0;I<2;++I){LampPositions[I]=LP[I];TaskPositions[I]=TP[I];}
    const TSharedPtr<FJsonObject>* Params=nullptr;if(!StageConfig->TryGetObjectField(TEXT("parameters"),Params))return false;
    const auto& P=*Params;CountdownSeconds=P->GetNumberField(TEXT("wave_countdown_seconds"));TaskSeconds=P->GetNumberField(TEXT("task_required_seconds"));TaskBlockRadius=P->GetNumberField(TEXT("task_enemy_block_radius_cm"));
    Defense=P->GetIntegerField(TEXT("starting_defense"));EnemyCap=P->GetIntegerField(TEXT("mainline_enemy_cap"));InitialEnergy=P->GetNumberField(TEXT("energy_wave_initial"));BasicGain=P->GetNumberField(TEXT("energy_basic_hit"));SkillGain=P->GetNumberField(TEXT("energy_skill_hit"));BeamHeavyBonus=P->GetNumberField(TEXT("beam_heavy_bonus_damage"));SynergyBudget=P->GetNumberField(TEXT("damaging_synergy_budget"));
    const auto& Basic=P->GetArrayField(TEXT("basic_action_seconds"));const auto& Skill=P->GetArrayField(TEXT("skill_action_seconds"));const auto& Cooldowns=P->GetArrayField(TEXT("skill_cooldowns_seconds"));
    if(Basic.Num()!=3||Skill.Num()!=3||Cooldowns.Num()!=2)return false;
    BasicWindup=Basic[0]->AsNumber();SkillWindup=Skill[0]->AsNumber();BasicActionSeconds=BasicWindup+Basic[1]->AsNumber()+Basic[2]->AsNumber();SkillActionSeconds=SkillWindup+Skill[1]->AsNumber()+Skill[2]->AsNumber();SkillOneCooldown=Cooldowns[0]->AsNumber();SkillTwoCooldown=Cooldowns[1]->AsNumber();
    // The shared rule library owns these invariants. Reject mismatched JSON rather than silently ignore it.
    if(P->GetIntegerField(TEXT("energy_capacity"))!=100||P->GetIntegerField(TEXT("energy_basic_cap_per_second"))!=15||P->GetIntegerField(TEXT("energy_skill_cap_per_three_seconds"))!=20||P->GetIntegerField(TEXT("relay_min_interval_seconds"))!=8||P->GetIntegerField(TEXT("energy_clear_field_per_second"))!=20||P->GetIntegerField(TEXT("revive_seconds"))!=8||P->GetIntegerField(TEXT("batch_warning_seconds"))!=5||P->GetIntegerField(TEXT("beam_heavy_bonus_target_cap"))!=1)return false;
    if(CountdownSeconds!=5||TaskSeconds<=0||TaskBlockRadius<=0||InitialEnergy<0||InitialEnergy>100)return false;
    if(!StageConfig->TryGetArrayField(TEXT("lamp_definitions"),Values)||Values->Num()!=2)return false;
    for(const auto& Value:*Values){auto O=Value->AsObject();FLNLampRow Row;Row.RangeCm=O->GetNumberField(TEXT("range_cm"));Row.RadiusCm=O->GetNumberField(TEXT("radius_cm"));Row.WidthCm=O->GetNumberField(TEXT("width_cm"));Row.Damage=O->GetNumberField(TEXT("damage"));Row.Duration=O->GetNumberField(TEXT("duration_seconds"));Row.TickDamage=O->GetNumberField(TEXT("tick_damage"));LampDefinitions.Add(Row);}
    if(!StageConfig->TryGetArrayField(TEXT("waves"),Values)||Values->Num()!=2)return false;
    for(const auto& Value:*Values)if(Value->AsObject()->GetArrayField(TEXT("batches")).IsEmpty())return false;
    Log(TEXT("stage_configuration_loaded"),Path);return true;
}

void ALNM2GameMode::InitializeRoster()
{
    RosterIds={TEXT("rover_spectro"),TEXT("jinhsi"),TEXT("changli"),TEXT("yangyang"),TEXT("chixia"),TEXT("baizhi"),TEXT("sanhua"),TEXT("taoqi"),TEXT("yuanwu")};
    const TCHAR* Names[]={TEXT("漂泊者·衍射"),TEXT("今汐"),TEXT("长离"),TEXT("秧秧"),TEXT("炽霞"),TEXT("白芷"),TEXT("散华"),TEXT("桃祈"),TEXT("渊武")};
    const TCHAR* One[]={TEXT("凝光扫"),TEXT("浮光"),TEXT("炎步"),TEXT("回风"),TEXT("压枪"),TEXT("冻尘"),TEXT("霜棘"),TEXT("御反"),TEXT("楔震")};
    const TCHAR* Two[]={TEXT("破隙"),TEXT("点曜"),TEXT("截翎"),TEXT("风线"),TEXT("燃印"),TEXT("羽袭"),TEXT("碎冰"),TEXT("破阵"),TEXT("断雷")};
    CharacterData=LoadObject<UDataTable>(nullptr,TEXT("/Game/M2/Data/DT_CharactersM2.DT_CharactersM2"));
    for(int32 I=0;I<RosterIds.Num();++I)
    {
        FLNCharacterRow Row;Row.PersonId=I==0?FName(TEXT("rover")):RosterIds[I];Row.DisplayName=Names[I];
        Row.MaxHealth=1000;Row.BasicDamage=30;Row.BasicInterval=1;Row.SkillOneDamage=90;Row.SkillTwoDamage=150;
        Row.bRanged=I==1||I==4||I==5;Row.SkillOne=One[I];Row.SkillTwo=Two[I];
        Row.Source=TEXT("M2: numeric document baseline, combat document action commitment; range class is implementation assumption");
        if(CharacterData)if(const auto* Data=CharacterData->FindRow<FLNCharacterRow>(RosterIds[I],TEXT("M2Roster")))Row=*Data;
        Roster.Add(Row);
    }
}
void ALNM2GameMode::SetDefaultFormation()
{
    Formation.SetNum(2);
    Formation[0].Seats={TEXT("rover_spectro"),TEXT("changli"),TEXT("yangyang")};Formation[0].Module=0;
    Formation[1].Seats={TEXT("jinhsi"),TEXT("sanhua"),TEXT("chixia")};Formation[1].Module=1;
    for(auto& F:Formation){F.BeamYaw=-90;F.Priority=0;}
    if(StageConfig.IsValid())
    {
        const auto& Teams=StageConfig->GetArrayField(TEXT("default_teams"));const auto& Modules=StageConfig->GetArrayField(TEXT("default_modules"));const auto& Yaws=StageConfig->GetArrayField(TEXT("default_beam_yaws_degrees"));
        if(Teams.Num()==2&&Modules.Num()==2&&Yaws.Num()==2)for(int32 L=0;L<2;++L){Formation[L].Seats.Empty();for(const auto& Value:Teams[L]->AsArray())Formation[L].Seats.Add(FName(Value->AsString()));Formation[L].Module=Modules[L]->AsNumber();Formation[L].BeamYaw=Yaws[L]->AsNumber();}
    }
}
int32 ALNM2GameMode::FindRoster(FName Id)const{return RosterIds.IndexOfByKey(Id);}
bool ALNM2GameMode::ValidateFormation(const TArray<FLNM2Formation>& Candidate)const
{
    if(Candidate.Num()!=2)return false;
    std::vector<std::vector<LongNight::CharacterSelection>> Selection;
    for(const auto& F:Candidate)
    {
        if(F.Seats.Num()!=3||F.Module<0||F.Module>1||F.Priority<0||F.Priority>2||!FMath::IsFinite(F.BeamYaw))return false;
        std::vector<LongNight::CharacterSelection> Team;
        for(FName Id:F.Seats){const int32 Index=FindRoster(Id);if(Index==INDEX_NONE)return false;Team.push_back({TCHAR_TO_UTF8(*Roster[Index].PersonId.ToString()),TCHAR_TO_UTF8(*Id.ToString())});}
        Selection.push_back(Team);
    }
    return LongNight::ValidateLineup(Selection).Valid;
}
void ALNM2GameMode::BuildRuntime()
{
    for(int32 I=0;I<2;++I)
    {
        auto* V=GetWorld()->SpawnActor<ALNLanternVisual>(LampPositions[I],FRotator::ZeroRotator);LampVisuals.Add(V);
        V->SetModule(Formation[I].Module,Formation[I].BeamYaw);
    }
}
void ALNM2GameMode::SpawnTeam()
{
    for(ALNCombatUnit* U:Units)if(U)U->Destroy();Units.Empty();Actions.Empty();DodgeReady.Empty();ImmuneUntil.Empty();
    if(!ValidateFormation(Formation)){Notice=TEXT("编队数据无效");return;}
    for(int32 L=0;L<2;++L)for(int32 S=0;S<3;++S)
    {
        const FVector Position=LampPositions[L]+SeatOffsets[S];
        auto* U=GetWorld()->SpawnActor<ALNCombatUnit>(Position,FRotator::ZeroRotator);
        U->Configure(false,Roster[FindRoster(Formation[L].Seats[S])],S,FLinearColor(.08f+.12f*S,.6f,.65f));
        Units.Add(U);Actions.Add({});DodgeReady.Add(0);ImmuneUntil.Add(0);
        if(!U->HasCharacterVisual())
        {
            U->Label->SetText(FText::FromString(Formation[L].Seats[S].ToString()));
            U->Label->SetVisibility(true);
        }
    }
}
void ALNM2GameMode::ResetWave()
{
    UGameplayStatics::SetGamePaused(this,false);bPaused=false;
    for(ALNCombatUnit* E:Enemies)if(E)E->Destroy();Enemies.Empty();EnemyState.Empty();Areas.Empty();
    for(int32 L=0;L<2;++L){Lamps[L]=FLampState{};Lamps[L].Energy=LongNight::EnergyPool({BasicGain,SkillGain});Lamps[L].Energy.Reset(InitialEnergy);Lamps[L].Relay.Reset();}
    Task.Reset(TaskSeconds);SimTime=0;Countdown=CountdownSeconds;Spawned=Killed=Leaked=0;ActionSerial=0;
    Phase=EPhase::Preparation;TaskReason=TEXT("等待任务灯点亮");
    SpawnTeam();
    for(int32 L=0;L<LampVisuals.Num();++L){LampVisuals[L]->SetModule(Formation[L].Module,Formation[L].BeamYaw);LampVisuals[L]->UpdateState(.4f,false,false,true);}
    CheckpointDefense=Defense;CheckpointTasks=CompletedTasks;CheckpointFormation=Formation;bCheckpointAssistance=bAssistance;
}
bool ALNM2GameMode::CanConfigure()const{return bConfigValid&&Phase==EPhase::Preparation;}
bool ALNM2GameMode::IsComplete()const{return Phase==EPhase::Victory;}
bool ALNM2GameMode::IsDefeated()const{return Phase==EPhase::Defeat;}
void ALNM2GameMode::SelectLamp(int32 Index){if(Index>=0&&Index<2)SelectedLamp=Index;}
void ALNM2GameMode::SelectSeat(int32 Index){if(Index>=0&&Index<3)SelectedSeat=Index;}
void ALNM2GameMode::PushUndo(){UndoStack.Add({Formation,bAssistance});if(UndoStack.Num()>30)UndoStack.RemoveAt(0);}
void ALNM2GameMode::ApplyFormation()
{
    SpawnTeam();
    for(int32 L=0;L<LampVisuals.Num();++L)LampVisuals[L]->SetModule(Formation[L].Module,Formation[L].BeamYaw);
    SavePreparation();Log(TEXT("formation"),FString::Printf(TEXT("lamp=%d; seats=%s; module=%d; yaw=%.0f; priority=%d"),SelectedLamp+1,*FString::JoinBy(Formation[SelectedLamp].Seats,TEXT(","),[](FName Id){return Id.ToString();}),Formation[SelectedLamp].Module,Formation[SelectedLamp].BeamYaw,Formation[SelectedLamp].Priority));
}
void ALNM2GameMode::AssignCharacter(int32 Index)
{
    if(!CanConfigure()||!RosterIds.IsValidIndex(Index))return;
    const FName Wanted=RosterIds[Index],Old=Formation[SelectedLamp].Seats[SelectedSeat];if(Wanted==Old)return;
    PushUndo();
    for(auto& F:Formation)for(auto& Id:F.Seats)if(Id==Wanted)Id=Old;
    Formation[SelectedLamp].Seats[SelectedSeat]=Wanted;Notice=TEXT("编队已更新；已上场人物自动交换席位");ApplyFormation();
}
void ALNM2GameMode::CycleModule(){if(CanConfigure()){PushUndo();Formation[SelectedLamp].Module=1-Formation[SelectedLamp].Module;ApplyFormation();}}
void ALNM2GameMode::RotateSeats(){if(CanConfigure()){PushUndo();auto& Seats=Formation[SelectedLamp].Seats;const FName First=Seats[0];Seats.RemoveAt(0);Seats.Add(First);ApplyFormation();}}
void ALNM2GameMode::SwapTeams(){if(CanConfigure()){PushUndo();Swap(Formation[0].Seats,Formation[1].Seats);ApplyFormation();}}
void ALNM2GameMode::RotateBeam(float Degrees){if(CanConfigure()){PushUndo();Formation[SelectedLamp].BeamYaw=FMath::UnwindDegrees(Formation[SelectedLamp].BeamYaw+Degrees);ApplyFormation();}}
void ALNM2GameMode::CyclePriority(){if(CanConfigure()){PushUndo();Formation[SelectedLamp].Priority=(Formation[SelectedLamp].Priority+1)%3;ApplyFormation();}}
void ALNM2GameMode::UndoPreparation(){if(CanConfigure()&&!UndoStack.IsEmpty()){const auto Snapshot=UndoStack.Pop();Formation=Snapshot.Formation;bAssistance=Snapshot.Assistance;ApplyFormation();Notice=TEXT("已撤回上一步配置");}}
void ALNM2GameMode::ToggleAssistance(){if(CanConfigure()){PushUndo();bAssistance=!bAssistance;SavePreparation();Notice=bAssistance?TEXT("辅助模式：突破损失减半"):TEXT("标准模式：使用完整突破损失");Log(TEXT("assistance"),bAssistance?TEXT("enabled"):TEXT("disabled"));}}
void ALNM2GameMode::DebugToggle(){bDebug=!bDebug;}
void ALNM2GameMode::StartOrContinue()
{
    if(Phase==EPhase::Victory){RestartLevel();return;}
    if(Phase==EPhase::Defeat){RetryWave();return;}
    if(Phase==EPhase::WaveComplete)
    {++WaveIndex;UndoStack.Empty();ResetWave();SavePreparation();Notice=TEXT("第二波：东侧射手出现，检查射程与贯阵方向");return;}
    if(!CanConfigure()||!ValidateFormation(Formation))return;
    CheckpointFormation=Formation;CheckpointDefense=Defense;CheckpointTasks=CompletedTasks;bCheckpointAssistance=bAssistance;SavePreparation();UndoStack.Empty();
    Phase=EPhase::Countdown;Countdown=CountdownSeconds;
    std::vector<LongNight::BatchSpec> Batches;const auto& SourceBatches=StageConfig->GetArrayField(TEXT("waves"))[WaveIndex]->AsObject()->GetArrayField(TEXT("batches"));
    for(int32 I=0;I<SourceBatches.Num();++I){const auto B=SourceBatches[I]->AsObject();Batches.push_back({I,B->GetIntegerField(TEXT("count")),B->GetNumberField(TEXT("at_seconds"))+CountdownSeconds});}
    Scheduler.Reset(Batches,EnemyCap);
    for(const auto& E:Scheduler.Tick(0,0))Log(TEXT("batch_warning"),FString::Printf(TEXT("batch=%d; count=%d; appear_combat=%.1f"),E.Id,E.EnemyCount,E.SpawnAt-5));
    Notice=TEXT("来敌预告 5 秒；整备已确认");
}
void ALNM2GameMode::PauseToggle()
{
    if(Phase!=EPhase::Combat&&Phase!=EPhase::Countdown)return;
    bPaused=!bPaused;UGameplayStatics::SetGamePaused(this,bPaused);Log(TEXT("pause"),bPaused?TEXT("true"):TEXT("false"));
}
void ALNM2GameMode::RetryWave()
{
    if(Phase!=EPhase::Defeat)return;
    Formation=CheckpointFormation;Defense=CheckpointDefense;CompletedTasks=CheckpointTasks;bAssistance=bCheckpointAssistance;
    TotalSpawned-=Spawned;TotalKilled-=Killed;TotalLeaked-=Leaked;UndoStack.Empty();ResetWave();SavePreparation();Notice=TEXT("已恢复本波开战前状态，前波成果保留");
}
void ALNM2GameMode::RestartLevel()
{
    if(Phase!=EPhase::Victory&&Phase!=EPhase::Defeat&&Phase!=EPhase::Preparation)return;
    WaveIndex=0;Defense=100;CompletedTasks=0;TotalSpawned=TotalKilled=TotalLeaked=0;SessionTime=0;
    SetDefaultFormation();UndoStack.Empty();ResetWave();SavePreparation();Notice=TEXT("第一关已重新开始");
}

void ALNM2GameMode::Tick(float Delta)
{
    Super::Tick(Delta);
    if(bFinished||bPaused)return;
    for(ALNCombatUnit* U:Units)U->SetDebugVisible(bDebug||!U->HasCharacterVisual());
    for(ALNCombatUnit* U:Enemies)U->SetDebugVisible(bDebug);
    if(bDebug)
    {
        for(int32 L=0;L<2;++L)
        {
            DrawDebugCircle(GetWorld(),LampPositions[L]+FVector(0,0,10),800,48,FColor::Cyan,false,-1,0,2,FVector(1,0,0),FVector(0,1,0),false);
            const FVector Dir=FRotator(0,Formation[L].BeamYaw,0).Vector(),Side=FVector(-Dir.Y,Dir.X,0)*150;
            if(Formation[L].Module==1)for(int32 S:{-1,1})DrawDebugLine(GetWorld(),LampPositions[L]+Side*S+FVector(0,0,20),LampPositions[L]+Dir*2800+Side*S+FVector(0,0,20),FColor::Blue,false,-1,0,3);
        }
        DrawDebugCircle(GetWorld(),GetTaskPosition()+FVector(0,0,10),300,32,FColor::Green,false,-1,0,2,FVector(1,0,0),FVector(0,1,0),false);
    }
    if(Phase==EPhase::WaveComplete&&bSmoke&&!bStateChecks)
    {TransitionDelay+=Delta;if(TransitionDelay>=1){TransitionDelay=0;StartOrContinue();StartOrContinue();}return;}
    if(Phase==EPhase::Countdown)
    {Countdown-=Delta;if(Countdown<=0){Phase=EPhase::Combat;Notice.Empty();Log(TEXT("combat_begin"),GetIntelText());}return;}
    if(Phase!=EPhase::Combat)return;
    SimTime+=Delta;SessionTime+=Delta;
    for(const auto& E:Scheduler.Tick(SimTime+5,LivingEnemies()))
    {
        if(E.Type==LongNight::BatchEventType::SpawnReady)
        {if(SpawnBatch(E.Id))Scheduler.ConfirmSpawn(E.Id);}
        else Log(E.Type==LongNight::BatchEventType::Warning?TEXT("batch_warning"):TEXT("batch_deferred"),FString::Printf(TEXT("batch=%d; count=%d; spawn_at=%.3f"),E.Id,E.EnemyCount,E.SpawnAt-5));
    }
    UpdateCharacters(Delta);UpdateEnemies(Delta);UpdateAreas();UpdateLamps(Delta);UpdateTask(Delta);
    if(Defense<=0){Phase=EPhase::Defeat;Notice=TEXT("防线失守，可回到本波整备重新布阵");Log(TEXT("defeat"),TEXT("defense_zero; retry_checkpoint_available"));}
    else if(LongNight::CanCompleteWave(Scheduler.AllSpawned(),LivingEnemies(),Task.IsComplete()))
    {
        CompletedTasks=WaveIndex+1;Phase=WaveIndex==0?EPhase::WaveComplete:EPhase::Victory;
        Notice=WaveIndex==0?TEXT("第一波完成，进入下一波整备后确认开战"):TEXT("两波来敌处理完成，双灯校线完成");
        Log(TEXT("wave_complete"),FString::Printf(TEXT("spawned=%d; killed=%d; leaked=%d; defense=%d; task=%.3f"),Spawned,Killed,Leaked,Defense,Task.Seconds()));SavePreparation();
        if(bRenderSmoke)RequestEvidence(WaveIndex==0?TEXT("M2-wave1-result.png"):TEXT("M2-victory.png"));
    }
    if(bRenderSmoke&&!bScreenshotTaken&&SimTime>=12)
    {bScreenshotTaken=true;RequestEvidence(TEXT("M2-runtime.png"));}
    if(bSmoke&&!bStateChecks&&!bSaveProbe&&(Phase==EPhase::Victory||Phase==EPhase::Defeat||SimTime>=180))FinishSmoke();
}

bool ALNM2GameMode::SpawnBatch(int32 Batch)
{
    const auto& Batches=StageConfig->GetArrayField(TEXT("waves"))[WaveIndex]->AsObject()->GetArrayField(TEXT("batches"));
    if(!Batches.IsValidIndex(Batch))return false;
    const auto Spec=Batches[Batch]->AsObject();const int32 RouteIndex=Spec->GetIntegerField(TEXT("route_index"));
    if(RouteIndex<0||RouteIndex>1)return false;
    const FString EnemyId=Spec->GetStringField(TEXT("enemy_id"));
    TSharedPtr<FJsonObject> Definition;int32 Type=0;
    for(const auto& Value:StageConfig->GetArrayField(TEXT("enemy_definitions"))){auto D=Value->AsObject();if(D->GetStringField(TEXT("id"))==EnemyId){Definition=D;break;}++Type;}
    if(!Definition.IsValid())return false;
    TArray<ALNCombatUnit*> Created;
    TArray<FEnemyState> CreatedStates;
    for(int32 I=0;I<Spec->GetIntegerField(TEXT("count"));++I)
    {
        FLNCharacterRow Row;Row.PersonId=TEXT("enemy_light");Row.DisplayName=Definition->GetStringField(TEXT("display_name"));Row.MaxHealth=Definition->GetNumberField(TEXT("max_health"));
        const FVector Start=Routes[RouteIndex][0]+(RouteIndex==0?FVector(-(I%3)*100,-(I/3)*120,0):FVector((I%2)*70,-(I/2)*130,0));
        auto* U=GetWorld()->SpawnActor<ALNCombatUnit>(Start,FRotator::ZeroRotator);
        if(!U){for(auto* Failed:Created)Failed->Destroy();Log(TEXT("spawn_failed"),TEXT("batch remains unconfirmed"));return false;}
        U->Configure(true,Row,-1,FLinearColor(.8f,.2f,.08f));
        U->Route=Routes[RouteIndex];U->Route.RemoveAt(0);
        FEnemyState State;State.Unit=U;State.Type=Type;State.Speed=Definition->GetNumberField(TEXT("speed_cm_per_second"));State.Damage=Definition->GetNumberField(TEXT("damage"));State.Interval=Definition->GetNumberField(TEXT("attack_interval_seconds"));State.BreakthroughLoss=Definition->GetIntegerField(TEXT("breakthrough_loss"));
        State.Shooter=Definition->GetStringField(TEXT("role"))==TEXT("ranged");State.ShootingPoint=ShooterStations[I%ShooterStations.Num()];
        Created.Add(U);CreatedStates.Add(State);
    }
    EnemyState.Append(CreatedStates);
    for(auto* U:Created)Enemies.Add(U);Spawned+=Created.Num();TotalSpawned+=Created.Num();
    Log(TEXT("batch_spawn"),FString::Printf(TEXT("batch=%d; count=%d; actually_spawned=%d"),Batch,Created.Num(),Spawned));return true;
}
int32 ALNM2GameMode::LivingEnemies()const{int32 Count=0;for(ALNCombatUnit* E:Enemies)if(E->IsAlive())++Count;return Count;}
bool ALNM2GameMode::Visible(FVector From,FVector To)const{FHitResult Hit;return !GetWorld()->LineTraceSingleByChannel(Hit,From,To,ECC_Visibility);}
ALNM2GameMode::FEnemyState* ALNM2GameMode::FindEnemy(ALNCombatUnit* U){for(auto& E:EnemyState)if(E.Unit==U)return &E;return nullptr;}
const ALNM2GameMode::FEnemyState* ALNM2GameMode::FindEnemy(const ALNCombatUnit* U)const{for(const auto& E:EnemyState)if(E.Unit==U)return &E;return nullptr;}
void ALNM2GameMode::Damage(ALNCombatUnit* Target,float Amount,int32 Lamp,FName Source)
{
    if(!Target||!Target->IsAlive())return;Target->TakeHit(Amount,SimTime);
    if(!Target->IsAlive()){++Killed;++TotalKilled;Log(TEXT("enemy_defeated"),FString::Printf(TEXT("lamp=%d; source=%s; killed=%d"),Lamp+1,*Source.ToString(),Killed));}
}
void ALNM2GameMode::Pull(ALNCombatUnit* U,FVector Toward,float Distance)
{
    if(!U||!U->IsAlive()||U->bHeavy)return;
    FVector Delta=Toward-U->GetActorLocation();Delta.Z=0;
    U->AddActorWorldOffset(Delta.GetClampedToMaxSize(Distance),true);
}

ALNCombatUnit* ALNM2GameMode::FindTarget(int32 Index,int32 Skill)const
{
    if(!Units.IsValidIndex(Index))return nullptr;
    const ALNCombatUnit* U=Units[Index];const int32 L=Index/3;const FName Id=U->Stats.PersonId;
    const bool HeavyOnly=Skill==2&&(Id==TEXT("jinhsi")||Id==TEXT("chixia"));
    float Range=Skill?1400.f:(U->Stats.bRanged?1400.f:800.f);
    if(Skill==1&&(Id==TEXT("rover")||Id==TEXT("yangyang")||Id==TEXT("sanhua")||Id==TEXT("baizhi")||Id==TEXT("taoqi")||Id==TEXT("yuanwu")))Range=600;
    if(Skill==2&&(Id==TEXT("taoqi")||Id==TEXT("sanhua")||Id==TEXT("yuanwu")))Range=600;
    ALNCombatUnit* Best=nullptr;float ScoreBest=-FLT_MAX;
    for(const auto& State:EnemyState)
    {
        auto* E=State.Unit;if(!E->IsAlive()||(HeavyOnly&&!E->bHeavy))continue;
        if(Skill==2&&Id==TEXT("sanhua")){const double* Ice=State.Marks.Find(TEXT("ice"));if(!Ice||*Ice<=SimTime)continue;}
        // Melee acquisition uses the lamp's pursuit boundary. The actor-to-target
        // reach is checked separately before committing an attack; a defender at
        // the rear station must still be able to walk toward a legal forward target.
        if(((Skill!=0||U->Stats.bRanged)&&FVector::Dist2D(U->GetActorLocation(),E->GetActorLocation())>Range)||!U->CanSee(E->GetActorLocation()))continue;
        if(!U->Stats.bRanged&&FVector::Dist2D(E->GetActorLocation(),LampPositions[L])>800)continue;
        // Physical path distance to the exit is the stable priority tie breaker.
        float Remaining=0;FVector Previous=E->GetActorLocation();for(int32 W=E->Waypoint;W<E->Route.Num();++W){Remaining+=FVector::Dist2D(Previous,E->Route[W]);Previous=E->Route[W];}
        float Score=-Remaining;
        const bool WantsShooter=(Formation[L].Priority==2)||(Id==TEXT("chixia")&&Skill==1);
        if(WantsShooter&&State.Shooter)Score+=100000;
        if(Formation[L].Priority==0&&!State.Shooter)Score+=10000;
        if(Formation[L].Priority==1&&E->bHeavy)Score+=100000;
        if(Skill==2&&Id==TEXT("baizhi"))Score=-FVector::DistSquared2D(E->GetActorLocation(),LampPositions[L]);
        if(Score>ScoreBest){ScoreBest=Score;Best=E;}
    }
    return Best;
}

void ALNM2GameMode::UpdateCharacters(float Delta)
{
    for(int32 Index=0;Index<Units.Num();++Index)
    {
        ALNCombatUnit* U=Units[Index];auto& A=Actions[Index];const int32 L=Index/3;
        if(!U->IsAlive())
        {
            A={};if(SimTime>=U->ReviveAt){U->Restore();Log(TEXT("revive"),U->Stats.PersonId.ToString());}continue;
        }
        // Dodge reacts to an already logged attack warning, with a legal swept landing.
        if(SimTime>=DodgeReady[Index]&&!(A.Target&&A.Submitted&&SimTime<A.HitAt+.1))
        {
            bool Threat=false;for(const auto& E:EnemyState)if(E.Unit->IsAlive()&&E.AttackTarget==U&&E.HitAt>SimTime&&E.HitAt-SimTime<=.2){Threat=true;break;}
            if(Threat)
            {
                const FVector Origins[]={FVector(200,0,0),FVector(-200,0,0),FVector(0,200,0),FVector(0,-200,0)};
                for(FVector Offset:Origins)
                {
                    const FVector Destination=U->GetActorLocation()+Offset;
                    if(FVector::Dist2D(Destination,LampPositions[L])>600||!U->CanSee(Destination))continue;
                    FHitResult Probe;FCollisionQueryParams Query(SCENE_QUERY_STAT(M2DodgeLanding),false,U);
                    const FCollisionShape Capsule=FCollisionShape::MakeCapsule(U->Body->GetScaledCapsuleRadius(),U->Body->GetScaledCapsuleHalfHeight()-1.f);
                    if(GetWorld()->SweepSingleByChannel(Probe,U->GetActorLocation(),Destination,FQuat::Identity,ECC_Pawn,Capsule,Query))continue;
                    U->AddActorWorldOffset(Offset,true);
                    DodgeReady[Index]=SimTime+4;ImmuneUntil[Index]=SimTime+.2;
                    if(A.Target&&!A.Submitted)Log(TEXT("action_cancel"),TEXT("dodge before hit; cooldown retained"));
                    A={};U->ActionReady=SimTime+.35;Log(TEXT("dodge"),FString::Printf(TEXT("lamp=%d; person=%s; immunity=.2; cooldown=4"),L+1,*U->Stats.PersonId.ToString()));break;
                }
            }
        }
        if(SimTime<U->RootUntil)
        {if(A.Target&&!A.Submitted){Log(TEXT("action_cancel"),TEXT("crowd_control before hit; cooldown retained"));A={};}continue;}
        if(A.Target)
        {
            if(!A.Submitted&&U->Stats.PersonId==TEXT("changli")&&A.Skill==1&&A.Target->IsAlive()&&FVector::Dist2D(U->GetActorLocation(),A.Target->GetActorLocation())>180)
                U->MoveTowards(A.Target->GetActorLocation(),1200,Delta);
            if(!A.Submitted&&SimTime>=A.HitAt){ResolveAction(Index);A.Submitted=true;}
            if(SimTime<A.EndAt)continue;A={};
        }
        if(SimTime<U->ActionReady)continue;
        if(Lamps[L].WaitStarted>=0&&Lamps[L].Relay.NextSeat()==Index%3)continue;
        int32 Skill=0;ALNCombatUnit* Target=nullptr;
        const FName Person=U->Stats.PersonId;
        const bool Prepare=Lamps[L].Energy.Value()>=80&&Lamps[L].Relay.NextSeat()==Index%3;
        if(Prepare&&Person==TEXT("yangyang")&&SimTime>=U->SkillOneReady){Target=FindTarget(Index,1);if(Target)Skill=1;}
        if(!Target&&SimTime>=U->SkillTwoReady){Target=FindTarget(Index,2);if(Target)Skill=2;}
        if(!Target&&SimTime>=U->SkillOneReady){Target=FindTarget(Index,1);if(Target)Skill=1;}
        if(!Target)
        {
            Target=FindTarget(Index,0);
            if(!Target){U->MoveTowards(U->Station,280,Delta);continue;}
            const float Reach=U->Stats.bRanged?1400:200;
            if(FVector::Dist2D(U->GetActorLocation(),Target->GetActorLocation())>Reach){U->MoveTowards(Target->GetActorLocation(),280,Delta);continue;}
            if(SimTime<U->BasicReady)continue;
        }
        A.Target=Target;A.Skill=Skill;A.Serial=++ActionSerial;A.HitAt=SimTime+(Skill?SkillWindup:BasicWindup);A.EndAt=SimTime+(Skill?SkillActionSeconds:BasicActionSeconds);A.Submitted=false;
        U->ActionReady=A.EndAt;
        if(Skill==2)U->SkillTwoReady=SimTime+SkillTwoCooldown;else if(Skill==1)U->SkillOneReady=SimTime+SkillOneCooldown;else U->BasicReady=SimTime+U->Stats.BasicInterval;
        U->PlayCombatAction(Skill,Target->GetActorLocation(),Skill?SkillActionSeconds:BasicActionSeconds);
        Log(TEXT("action_begin"),FString::Printf(TEXT("lamp=%d; person=%s; action=%llu; skill=%d; hit_at=%.3f; end_at=%.3f"),L+1,*Person.ToString(),A.Serial,Skill,A.HitAt,A.EndAt));
    }
}

void ALNM2GameMode::ResolveAction(int32 Index)
{
    ALNCombatUnit* U=Units[Index];const auto& A=Actions[Index];const int32 L=Index/3;const FName Person=U->Stats.PersonId;
    if(!A.Target||!A.Target->IsAlive()||!U->IsAlive())return;
    const FVector Origin=U->GetActorLocation();const FVector Aim=A.Target->GetActorLocation();const FVector Dir=(Aim-Origin).GetSafeNormal2D();
    const float MaxRange=A.Skill?1400:(U->Stats.bRanged?1400:220);
    if(FVector::Dist2D(Origin,Aim)>MaxRange||!Visible(Origin,Aim)||(!U->Stats.bRanged&&FVector::Dist2D(Aim,LampPositions[L])>800))return;
    if(Person==TEXT("changli")&&A.Skill==1&&FVector::Dist2D(Origin,Aim)>220)return;
    const float BaseAmount=A.Skill==2?U->Stats.SkillTwoDamage:A.Skill==1?U->Stats.SkillOneDamage:U->Stats.BasicDamage;
    const float Amount=A.Skill==1&&Person==TEXT("yuanwu")?BaseAmount/4:BaseAmount;
    TArray<ALNCombatUnit*> Victims;
    const bool Cone=A.Skill==1&&(Person==TEXT("rover")||Person==TEXT("yangyang")||Person==TEXT("baizhi")||Person==TEXT("taoqi"));
    const bool Line=A.Skill==1&&(Person==TEXT("jinhsi")||Person==TEXT("sanhua"));
    if(Cone||Line)
    {
        for(ALNCombatUnit* E:Enemies)
        {
            FVector Offset=E->GetActorLocation()-Origin;Offset.Z=0;const float Along=FVector::DotProduct(Offset,Dir),Side=FVector::CrossProduct(Offset,Dir).Size();
            const bool Within=Cone?(Offset.Size()<=600&&FVector::DotProduct(Offset.GetSafeNormal(),Dir)>=.707f):(Along>=0&&Along<=(Person==TEXT("sanhua")?600:1400)&&Side<=100);
            if(E->IsAlive()&&Within&&Visible(Origin,E->GetActorLocation()))Victims.Add(E);
        }
    }
    else if(A.Skill==2&&Person==TEXT("sanhua"))
    {for(const auto& State:EnemyState){const double* Ice=State.Marks.Find(TEXT("ice"));if(Ice&&*Ice>SimTime&&State.Unit->IsAlive()&&FVector::Dist2D(Origin,State.Unit->GetActorLocation())<=600&&Visible(Origin,State.Unit->GetActorLocation()))Victims.Add(State.Unit);}}
    else Victims.Add(A.Target);
    for(auto* E:Victims)
    {
        Damage(E,Amount,L,Person);auto* State=FindEnemy(E);if(!State)continue;
        if(A.Skill==1&&Person==TEXT("rover")&&!E->bHeavy)E->RootUntil=FMath::Max(E->RootUntil,SimTime+2);
        if(A.Skill==2&&(Person==TEXT("rover")||Person==TEXT("jinhsi")||Person==TEXT("chixia")))State->Marks.Add(Person,SimTime+6);
        if(A.Skill==1&&Person==TEXT("sanhua"))State->Marks.Add(TEXT("ice"),SimTime+6);
        if(A.Skill==1&&Person==TEXT("yangyang"))Pull(E,LampPositions[L]+FVector(0,-200,65),300);
        if((A.Skill==2&&Person==TEXT("yangyang"))||(A.Skill==1&&(Person==TEXT("baizhi")||Person==TEXT("sanhua"))))State->SlowUntil=SimTime+2;
        if(A.Skill==1&&Person==TEXT("chixia")&&State->Shooter){State->SuppressUntil=SimTime+2;State->AttackTarget=nullptr;}
        if(A.Skill==1&&Person==TEXT("taoqi"))Pull(E,E->GetActorLocation()+(E->GetActorLocation()-Origin).GetSafeNormal2D()*300,300);
        if(A.Skill==2&&(Person==TEXT("changli")||Person==TEXT("baizhi")||Person==TEXT("sanhua")||Person==TEXT("taoqi")||Person==TEXT("yuanwu")))
        {if(E->TryInterrupt(SimTime,.5f))State->AttackTarget=nullptr;}
    }
    if(A.Skill==1&&Person==TEXT("sanhua")&&!Victims.IsEmpty())
    {
        FArea Ice;Ice.Lamp=L;Ice.Center=Origin;Ice.Direction=Dir;Ice.Radius=600;Ice.Damage=0;Ice.Remaining=6;Ice.NextTick=SimTime+1;Ice.Source=TEXT("ice_line");Ice.IceLine=true;Areas.Add(Ice);
        auto* Effect=GetWorld()->SpawnActor<ALNVisualEffect>();Effect->Beam(Origin,Origin+Dir*600,200,FLinearColor(.2f,.8f,1.f),6.f);
    }
    if(A.Skill==1&&Person==TEXT("yuanwu")&&!Victims.IsEmpty())
    {FArea Area;Area.Lamp=L;Area.Center=Aim;Area.Radius=300;Area.Damage=U->Stats.SkillOneDamage/4;Area.Remaining=3;Area.NextTick=SimTime+1;Area.Source=TEXT("yuanwu");Areas.Add(Area);}
    if(Victims.IsEmpty())return;
    if(Lamps[L].FirstContact<0)Lamps[L].FirstContact=SimTime;
    const double Added=Lamps[L].Energy.RecordHit(A.Skill?LongNight::HitSource::Skill:LongNight::HitSource::Basic,A.Serial,SimTime);
    Log(TEXT("energy_hit"),FString::Printf(TEXT("lamp=%d; person=%s; action=%llu; skill=%d; victims=%d; delta=%.3f; energy=%.3f"),L+1,*Person.ToString(),A.Serial,A.Skill,Victims.Num(),Added,Lamps[L].Energy.Value()));
    auto* Effect=GetWorld()->SpawnActor<ALNVisualEffect>();Effect->Beam(Origin+FVector(0,0,35),Aim+FVector(0,0,25),A.Skill?12:4,FLinearColor(.25,.8,1),.15);
}

bool ALNM2GameMode::CanLampHit(int32 L,const ALNCombatUnit* E)const
{
    if(!E||!E->IsAlive())return false;FVector Offset=E->GetActorLocation()-LampPositions[L];Offset.Z=0;
    const auto& Definition=LampDefinitions[Formation[L].Module];
    if(Formation[L].Module==0){if(Offset.Size()>Definition.RangeCm)return false;}
    else
    {
        const FVector Dir=FRotator(0,Formation[L].BeamYaw,0).Vector();
        const float Along=FVector::DotProduct(Offset,Dir),Side=FVector::CrossProduct(Offset,Dir).Size();
        if(Along<0||Along>Definition.RangeCm||Side>Definition.WidthCm/2)return false;
    }
    return Visible(LampPositions[L]+FVector(0,0,80),E->GetActorLocation());
}
void ALNM2GameMode::FireLamp(int32 L,int32 Seat)
{
    LampVisuals[L]->Trigger();for(int32 S=0;S<3;++S)Units[L*3+S]->SetConductor(S==Seat);
    const FName Person=Units[L*3+Seat]->Stats.PersonId;
    const auto& Definition=LampDefinitions[Formation[L].Module];
    ALNCombatUnit* Target=nullptr;float ScoreBest=-FLT_MAX;
    for(const auto& State:EnemyState)
    {
        auto* E=State.Unit;if(!CanLampHit(L,E))continue;
        float Score=-FVector::Dist2D(E->GetActorLocation(),FVector(0,1700,65));
        if(Formation[L].Priority==2&&State.Shooter)Score+=100000;
        if(Formation[L].Priority==1&&E->bHeavy)Score+=100000;
        if(Person==TEXT("rover")){const double* Mark=State.Marks.Find(Person);if(Mark&&*Mark>SimTime)Score+=100000;}
        if(Score>ScoreBest){ScoreBest=Score;Target=E;}
    }
    if(!Target)return;
    if(Formation[L].Module==0)
    {
        FVector Center=Target->GetActorLocation();Center.Z=0;
        if(Person==TEXT("yangyang"))for(ALNCombatUnit* E:Enemies)if(E->IsAlive()&&FVector::Dist2D(E->GetActorLocation(),Center)<=Definition.RadiusCm&&Visible(Center+FVector(0,0,65),E->GetActorLocation()))Pull(E,Center+FVector(0,0,65),300);
        for(ALNCombatUnit* E:Enemies)if(E->IsAlive()&&FVector::Dist2D(E->GetActorLocation(),Center)<=Definition.RadiusCm&&Visible(Center+FVector(0,0,65),E->GetActorLocation()))
        {
            Damage(E,Definition.Damage,L,TEXT("lamp_flame"));auto* State=FindEnemy(E);
            if(State&&Person==TEXT("baizhi"))State->SlowUntil=SimTime+2;
            if(State&&Person==TEXT("sanhua")){const double* Ice=State->Marks.Find(TEXT("ice"));if(Ice&&*Ice>SimTime)Damage(E,SynergyBudget,L,TEXT("coop_ice"));}
        }
        FArea Area;Area.Lamp=L;Area.Center=Center;Area.Radius=Definition.RadiusCm;Area.Damage=Definition.TickDamage;Area.Remaining=FMath::RoundToInt(Definition.Duration);Area.NextTick=SimTime+1;Area.Source=TEXT("lamp_flame");Areas.Add(Area);
        if(Person==TEXT("changli")){Area.Cross=true;Area.Damage=SynergyBudget/4;Area.Remaining=4;Area.Source=TEXT("coop_cross");Areas.Add(Area);}
        auto* Effect=GetWorld()->SpawnActor<ALNVisualEffect>();Effect->Burst(Center,Definition.RadiusCm,FLinearColor(1,.35,.02));
        auto* Burn=GetWorld()->SpawnActor<ALNVisualEffect>();Burn->Burn(Center,Definition.RadiusCm,Definition.Duration,Person==TEXT("changli"));
    }
    else
    {
        bool HeavyBonus=false;
        for(ALNCombatUnit* E:Enemies)
        {
            if(!CanLampHit(L,E))continue;auto* State=FindEnemy(E);
            const bool ExtraHeavy=E->bHeavy&&!HeavyBonus;if(ExtraHeavy)HeavyBonus=true;
            Damage(E,Definition.Damage+(ExtraHeavy?BeamHeavyBonus:0),L,TEXT("lamp_beam"));
            if(State)
            {
                const double* Mark=State->Marks.Find(Person);
                bool Coop=(Person==TEXT("rover")||Person==TEXT("jinhsi")||Person==TEXT("chixia"))&&Mark&&*Mark>SimTime;
                if(Person==TEXT("sanhua")){const double* Ice=State->Marks.Find(TEXT("ice"));Coop=Ice&&*Ice>SimTime;}
                if(Person==TEXT("yuanwu"))for(const auto& Area:Areas)if(Area.Source==TEXT("yuanwu")&&FVector::Dist2D(E->GetActorLocation(),Area.Center)<=Area.Radius)Coop=true;
                if(Coop)Damage(E,SynergyBudget,L,TEXT("coop_beam"));
            }
        }
        const FVector Dir=FRotator(0,Formation[L].BeamYaw,0).Vector();
        auto* Effect=GetWorld()->SpawnActor<ALNVisualEffect>();Effect->Beam(LampPositions[L]+FVector(0,0,175),LampPositions[L]+Dir*Definition.RangeCm+FVector(0,0,175),Definition.WidthCm,FLinearColor(.05,.65,1),.55);
    }
}
void ALNM2GameMode::UpdateAreas()
{
    // Refresh living state before persistent damage; last defender can fall this same tick.
    for(int32 L=0;L<2;++L)
    {
        std::array<bool,3> Alive{};for(int32 S=0;S<3;++S)Alive[S]=Units[L*3+S]->IsAlive();Lamps[L].Relay.UpdateTeam(Alive);
    }
    for(auto& Area:Areas)if(Area.Source!=TEXT("yuanwu")&&!Area.IceLine&&!Lamps[Area.Lamp].Relay.IsLit())Area.Remaining=0;
    for(ALNCombatUnit* E:Enemies)
    {
        if(!E->IsAlive())continue;
        TMap<FName,int32> Owners;
        for(int32 I=0;I<Areas.Num();++I)
        {
            const auto& Area=Areas[I];if(Area.Remaining<=0)continue;
            const FVector Offset=E->GetActorLocation()-Area.Center;
            const float Along=FVector::DotProduct(Offset,Area.Direction),Side=FVector::CrossProduct(Offset,Area.Direction).Size();
            const bool InArea=Area.IceLine?(Along>=0&&Along<=Area.Radius&&Side<=100):Area.Cross?((FMath::Abs(Offset.X)<=600&&FMath::Abs(Offset.Y)<=100)||(FMath::Abs(Offset.Y)<=600&&FMath::Abs(Offset.X)<=100)):Offset.SizeSquared2D()<=FMath::Square(Area.Radius);
            if(!InArea||!Visible(Area.Center+FVector(0,0,65),E->GetActorLocation()))continue;
            if(Area.IceLine){if(auto* State=FindEnemy(E)){State->SlowUntil=SimTime+2;State->Marks.Add(TEXT("ice"),SimTime+2);}continue;}
            const int32* Previous=Owners.Find(Area.Source);
            if(!Previous||Areas[*Previous].Damage<Area.Damage)Owners.Add(Area.Source,I);
        }
        for(const auto& Pair:Owners){const auto& Area=Areas[Pair.Value];if(SimTime>=Area.NextTick)Damage(E,Area.Damage,Area.Lamp,Area.Source);}
    }
    // Expire all regions together, after owner selection, including their fourth/final tick.
    for(auto& Area:Areas)if(Area.Remaining>0&&SimTime>=Area.NextTick){--Area.Remaining;Area.NextTick+=1;}
    Areas.RemoveAll([](const FArea& Area){return Area.Remaining<=0;});
}
void ALNM2GameMode::UpdateLamps(float Delta)
{
    for(int32 L=0;L<2;++L)
    {
        auto& Lamp=Lamps[L];std::array<bool,3> Living{},Actionable{};
        for(int32 S=0;S<3;++S){ALNCombatUnit* U=Units[L*3+S];Living[S]=U->IsAlive();Actionable[S]=Living[S]&&SimTime>=U->RootUntil;}
        Lamp.Relay.UpdateTeam(Living);
        bool Target=false;for(ALNCombatUnit* E:Enemies)if(CanLampHit(L,E)){Target=true;break;}
        const bool TaskNeed=WaveIndex==L&&!Task.IsComplete()&&!Lamp.Relay.IsLit();
        if(LivingEnemies()==0&&TaskNeed){const double Gain=Lamp.Energy.AddTaskFallback(Delta,false,false);if(Gain>0)Log(TEXT("task_energy"),FString::Printf(TEXT("lamp=%d; delta=%.3f; energy=%.3f"),L+1,Gain,Lamp.Energy.Value()));}
        int32 Next=Lamp.Relay.NextSeat();for(int32 Offset=0;Offset<3;++Offset)if(Actionable[(Lamp.Relay.NextSeat()+Offset)%3]){Next=(Lamp.Relay.NextSeat()+Offset)%3;break;}
        const bool MayWait=Lamp.Energy.Value()>=100&&SimTime-Lamp.LastRelay>=8&&!Lamp.Relay.IsFaulted(SimTime)&&(Target||TaskNeed);
        bool Waiting=false;
        if(MayWait&&Units[L*3+Next]->ActionReady>SimTime)
        {if(Lamp.WaitStarted<0)Lamp.WaitStarted=SimTime;Waiting=SimTime-Lamp.WaitStarted<1;}
        else Lamp.WaitStarted=-1;
        if(Waiting){Lamp.Reason=TEXT("等待下一主奏当前动作（最多1秒）");}
        else
        {
            const auto Result=Lamp.Relay.TryTrigger(SimTime,Lamp.Energy,Actionable,{Target,false,TaskNeed});
            const TCHAR* Reasons[]={TEXT("已触发灯击"),TEXT("暂停"),TEXT("积累协奏"),TEXT("故障中仍可积能"),TEXT("至少两人可行动"),TEXT("接力间隔至少8秒"),TEXT("满能等待有效目标")};
            Lamp.Reason=Reasons[static_cast<int32>(Result.Reason)];
            if(Result.Triggered)
            {
                Lamp.CurrentSeat=Result.Seat;Lamp.LastRelay=SimTime;Lamp.WaitStarted=-1;Lamp.RelayTimes.Add(SimTime);FireLamp(L,Result.Seat);
                Log(TEXT("relay"),FString::Printf(TEXT("lamp=%d; seat=%d; person=%s; since_contact=%.3f; energy=%.1f"),L+1,Result.Seat+1,*Units[L*3+Result.Seat]->Stats.PersonId.ToString(),Lamp.FirstContact<0?-1:SimTime-Lamp.FirstContact,Lamp.Energy.Value()));
            }
        }
        LampVisuals[L]->UpdateState(Lamp.Energy.Value()/100,Lamp.Relay.IsLit(),Lamp.Relay.IsFaulted(SimTime),false);
    }
}
void ALNM2GameMode::UpdateTask(float Delta)
{
    bool Blocked=false;for(ALNCombatUnit* E:Enemies)if(E->IsAlive()&&FVector::Dist2D(E->GetActorLocation(),TaskPositions[WaveIndex])<=TaskBlockRadius){Blocked=true;break;}
    const bool Lit=Lamps[WaveIndex].Relay.IsLit();Task.Tick(Delta,Lit,Blocked);
    const FString Reason=Task.IsComplete()?TEXT("校线完成"):!Lit?TEXT("等待指定灯点亮"):Blocked?TEXT("作业点3米内有敌人，进度保留"):TEXT("校线进行中");
    if(Reason!=TaskReason){TaskReason=Reason;Log(TEXT("task_state"),FString::Printf(TEXT("lamp=%d; progress=%.3f; reason=%s"),WaveIndex+1,Task.Seconds(),*Reason));}
}

FString ALNM2GameMode::GetPhaseLabel()const
{
    switch(Phase){case EPhase::Preparation:return TEXT("波前整备");case EPhase::Countdown:return FString::Printf(TEXT("来敌预告 %.1f秒"),FMath::Max(0.,Countdown));case EPhase::Combat:return bPaused?TEXT("战斗已暂停"):TEXT("战斗观察");case EPhase::WaveComplete:return TEXT("第一波完成");case EPhase::Victory:return TEXT("断哨留火 · 通关");default:return TEXT("防线失守");}
}
FString ALNM2GameMode::GetStartLabel()const
{return Phase==EPhase::WaveComplete?TEXT("进入下一波整备"):Phase==EPhase::Victory?TEXT("重新开始第一关"):Phase==EPhase::Defeat?TEXT("恢复本波整备"):TEXT("确认编队 · 开始");}
FString ALNM2GameMode::GetStatusText()const
{return FString::Printf(TEXT("第 %d / 2 波  ·  防线 %d  ·  战斗 %.1f秒\n本波出现 %d / %d  ·  击败 %d  ·  突破 %d"),WaveIndex+1,Defense,SimTime,Spawned,WaveIndex==0?10:12,Killed,Leaked);}
FString ALNM2GameMode::GetLampText(int32 L)const
{
    if(L<0||L>1)return FString();const auto& Lamp=Lamps[L];const TCHAR* Priorities[]={TEXT("突破优先"),TEXT("拆灯优先"),TEXT("远程优先")};
    const FString Current=Lamp.CurrentSeat<0?TEXT("待接任"):GetSeatName(L,Lamp.CurrentSeat);
    return FString::Printf(TEXT("灯%d · %s  协奏 %.0f/100\n当前 %s · 下一 %s\n%s · %s\n方向 %.0f° · %s"),L+1,Formation[L].Module==0?TEXT("聚焰"):TEXT("贯阵"),Lamp.Energy.Value(),*Current,*GetSeatName(L,Lamp.Relay.NextSeat()),Lamp.Relay.IsLit()?TEXT("点亮"):TEXT("熄灭"),*Lamp.Reason,Formation[L].BeamYaw,Priorities[Formation[L].Priority]);
}
FName ALNM2GameMode::GetSeatId(int32 L,int32 S)const{return Formation.IsValidIndex(L)&&Formation[L].Seats.IsValidIndex(S)?Formation[L].Seats[S]:NAME_None;}
FString ALNM2GameMode::GetSeatName(int32 L,int32 S)const{const int32 I=FindRoster(GetSeatId(L,S));return Roster.IsValidIndex(I)?Roster[I].DisplayName:TEXT("—");}
float ALNM2GameMode::GetSeatHealthFraction(int32 L,int32 S)const{const int32 I=L*3+S;return Units.IsValidIndex(I)?Units[I]->Health/Units[I]->Stats.MaxHealth:0;}
FString ALNM2GameMode::GetRosterName(int32 I)const{return Roster.IsValidIndex(I)?Roster[I].DisplayName:TEXT("—");}
FString ALNM2GameMode::GetRosterDescription(int32 I)const
{
    if(!Roster.IsValidIndex(I))return FString();
    const TCHAR* Presentation=TEXT("具名练习替身");
    if(auto* Visuals=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_Visuals.DT_Visuals")))
        if(const auto* Row=Visuals->FindRow<FLNVisualRow>(RosterIds[I],TEXT("RosterPresentation")))
            if(!Row->Mesh.IsNull())Presentation=Row->Idle.IsNull()?TEXT("角色模型 · 静态姿态"):TEXT("角色模型");
    return FString::Printf(TEXT("%s / %s\n%s · %s"),*Roster[I].SkillOne,*Roster[I].SkillTwo,Roster[I].bRanged?TEXT("远程普攻"):TEXT("近战普攻"),Presentation);
}
FString ALNM2GameMode::GetTaskText()const{return FString::Printf(TEXT("第%d处校线 · 需要灯%d\n%.1f / 10秒 · 已完成 %d / 2\n%s"),WaveIndex+1,WaveIndex+1,Task.Seconds(),CompletedTasks,*TaskReason);}
FString ALNM2GameMode::GetIntelText()const
{return WaveIndex==0?TEXT("开战：左路审判×6，右路鸣泣×4\n目标：点亮灯1，校线10秒\n共10名；弯口聚焰、直道贯阵\n每批预警5秒；波内观察与暂停"):TEXT("开战：右侧惊蛰射手×4\n10秒：左路审判战士×8\n目标：点亮灯2，校线10秒\n共12名；检查射程和贯阵方向");}
FString ALNM2GameMode::GetEventText()const{return FString::Join(Events,TEXT("\n"));}
float ALNM2GameMode::GetEnergyFraction(int32 L)const{return L>=0&&L<2?Lamps[L].Energy.Value()/100:0;}
float ALNM2GameMode::GetBeamYaw(int32 L)const{return Formation.IsValidIndex(L)?Formation[L].BeamYaw:0;}
int32 ALNM2GameMode::GetModule(int32 L)const{return Formation.IsValidIndex(L)?Formation[L].Module:0;}
int32 ALNM2GameMode::GetCurrentSeat(int32 L)const{return L>=0&&L<2?Lamps[L].CurrentSeat:INDEX_NONE;}
FVector ALNM2GameMode::GetLampPosition(int32 L)const{return L>=0&&L<2?LampPositions[L]:FVector::ZeroVector;}
FVector ALNM2GameMode::GetTaskPosition()const{return TaskPositions[FMath::Clamp(WaveIndex,0,1)];}
void ALNM2GameMode::Log(const FString& Event,const FString& Detail)
{
    const FString Line=FString::Printf(TEXT("%d\t%.3f\t%s\t%s\n"),WaveIndex+1,SimTime,*Event,*Detail.Replace(TEXT("\n"),TEXT("; ")));
    FFileHelper::SaveStringToFile(Line,*LogPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    if(Event!=TEXT("energy_hit")&&Event!=TEXT("task_energy")&&Event!=TEXT("action_begin")&&Event!=TEXT("enemy_warning"))
    {Events.Insert(FString::Printf(TEXT("W%d %.1fs  %s %s"),WaveIndex+1,SimTime,*Event,*Detail),0);if(Events.Num()>6)Events.SetNum(6);}
    UE_LOG(LogTemp,Display,TEXT("LNM2 %s"),*Line.TrimEnd());
}
FString ALNM2GameMode::SaveSlot()const{return bSmoke?TEXT("LongNight_M2_Automation"):TEXT("LongNight_M2");}
bool ALNM2GameMode::SavePreparation()
{
    if(bSmoke&&!bStateChecks&&!bSaveProbe)return true;
    auto* Save=Cast<ULNM2SaveGame>(UGameplayStatics::CreateSaveGameObject(ULNM2SaveGame::StaticClass()));
    const bool Battle=Phase==EPhase::Combat||Phase==EPhase::Countdown||Phase==EPhase::Defeat;
    Save->Formation=Battle?CheckpointFormation:Formation;
    Save->WaveIndex=Phase==EPhase::WaveComplete?WaveIndex+1:WaveIndex;
    Save->Defense=Battle?CheckpointDefense:Defense;Save->CompletedTasks=Battle?CheckpointTasks:CompletedTasks;
    Save->bCompleted=Phase==EPhase::Victory;
    Save->bAssistance=Battle?bCheckpointAssistance:bAssistance;
    const bool Result=UGameplayStatics::SaveGameToSlot(Save,SaveSlot(),0);
    if(!Result){Notice=TEXT("保存失败，请检查磁盘和权限");Log(TEXT("save_error"),SaveSlot());}return Result;
}
bool ALNM2GameMode::LoadPreparation()
{
    if(!UGameplayStatics::DoesSaveGameExist(SaveSlot(),0))return false;
    auto* Save=Cast<ULNM2SaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlot(),0));
    if(!Save||Save->Version!=1||Save->ScenarioId!=TEXT("M2_DuanShao")||!ValidateFormation(Save->Formation)||Save->WaveIndex<0||Save->WaveIndex>1||Save->Defense<=0||Save->Defense>100||Save->CompletedTasks<0||Save->CompletedTasks>2)
    {Notice=TEXT("存档数据无效，保留默认配置");Log(TEXT("save_invalid"),SaveSlot());return false;}
    Formation=Save->Formation;WaveIndex=Save->WaveIndex;Defense=Save->Defense;CompletedTasks=Save->CompletedTasks;
    bAssistance=Save->bAssistance;
    bLoadedCompleted=Save->bCompleted;
    if(Save->bCompleted)Notice=TEXT("已完成记录保留；重新开始可重玩第一关");
    Log(TEXT("save_loaded"),FString::Printf(TEXT("wave=%d; defense=%d; tasks=%d; completed=%d"),WaveIndex+1,Defense,CompletedTasks,Save->bCompleted));return true;
}
void ALNM2GameMode::EndPlay(const EEndPlayReason::Type Reason)
{if(!bSmoke)SavePreparation();Super::EndPlay(Reason);}
void ALNM2GameMode::RequestEvidence(const FString& Name)
{
    const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/Name);IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    FTimerHandle Capture;GetWorldTimerManager().SetTimer(Capture,FTimerDelegate::CreateWeakLambda(this,[Path](){FScreenshotRequest::RequestScreenshot(Path,true,false,false,FIntRect(),true);}),.15f,false);Log(TEXT("screenshot_requested"),Path);
}
void ALNM2GameMode::FinishSmoke()
{
    if(bFinished)return;bFinished=true;
    FString Result=FString::Printf(TEXT("scenario=M2_DuanShao\nresult=%s\nwave=%d\nsim_seconds=%.3f\nspawned=%d\nkilled=%d\nleaked=%d\ndefense=%d\ncompleted_tasks=%d\ncurrent_task_seconds=%.3f\n"),Phase==EPhase::Victory?TEXT("VICTORY"):Phase==EPhase::Defeat?TEXT("DEFEAT"):TEXT("TIMEOUT"),WaveIndex+1,SessionTime,TotalSpawned,TotalKilled,TotalLeaked,Defense,CompletedTasks,Task.Seconds());
    for(int32 L=0;L<2;++L)Result+=FString::Printf(TEXT("lamp_%d_relays=%d\n"),L+1,Lamps[L].RelayTimes.Num());
    FFileHelper::SaveStringToFile(Result,*(FPaths::ProjectSavedDir()/TEXT("Telemetry")/(bMeleeTest?TEXT("m2-melee.txt"):bRenderSmoke?TEXT("m2-render.txt"):TEXT("m2-headless.txt"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    Log(TEXT("smoke_complete"),Result);
    if(bRenderSmoke){FTimerHandle Timer;GetWorldTimerManager().SetTimer(Timer,FTimerDelegate::CreateWeakLambda(this,[this](){if(GEngine)GEngine->Exec(GetWorld(),TEXT("QUIT"));}),1.f,false);}
    else if(GEngine)GEngine->Exec(GetWorld(),TEXT("QUIT"));
}

void ALNM2GameMode::RunSaveProbe(bool Write)
{
    bool Result=false;
    if(Write)
    {
        WaveIndex=1;Defense=70;CompletedTasks=1;bAssistance=true;Formation[0].Module=1;Formation[1].Module=0;Formation[0].BeamYaw=-60;Formation[1].Priority=2;
        Swap(Formation[0].Seats[0],Formation[1].Seats[2]);Result=SavePreparation();
    }
    else
    {
        Result=LoadPreparation()&&bAssistance&&WaveIndex==1&&Defense==70&&CompletedTasks==1&&Formation[0].Module==1&&Formation[1].Module==0&&FMath::IsNearlyEqual(Formation[0].BeamYaw,-60.f)&&Formation[1].Priority==2&&Formation[0].Seats[0]==TEXT("chixia")&&Formation[1].Seats[2]==TEXT("rover_spectro");
    }
    const FString Report=FString::Printf(TEXT("%s cross_process_%s\nFAILURES=%d\n"),Result?TEXT("PASS"):TEXT("FAIL"),Write?TEXT("save_write"):TEXT("save_read"),Result?0:1);
    FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectSavedDir()/TEXT("Telemetry")/(Write?TEXT("m2-save-write.txt"):TEXT("m2-save-read.txt"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    bFinished=true;if(GEngine)GEngine->Exec(GetWorld(),TEXT("QUIT"));
}

void ALNM2GameMode::RunStateChecks()
{
    FString Report;int32 Failures=0;
    auto Check=[&](bool Result,const TCHAR* Name){Report+=FString::Printf(TEXT("%s %s\n"),Result?TEXT("PASS"):TEXT("FAIL"),Name);if(!Result)++Failures;};
    Check(Roster.Num()==9&&Units.Num()==6&&ValidateFormation(Formation),TEXT("nine_candidates_six_unique_units"));
    ToggleAssistance();Check(IsAssistanceEnabled(),TEXT("assistance_enabled_in_preparation"));UndoPreparation();Check(!IsAssistanceEnabled(),TEXT("assistance_undo_restores_previous_setting"));
    Tick(2);Check(SimTime==0&&Lamps[0].Energy.Value()==40&&Lamps[1].Energy.Value()==40,TEXT("preparation_freezes_two_lamps"));
    auto Invalid=Formation;Invalid[1].Seats[0]=Invalid[0].Seats[0];Check(!ValidateFormation(Invalid),TEXT("duplicate_identity_rejected_across_lamps"));
    SelectLamp(0);SelectSeat(0);AssignCharacter(1);
    Check(Formation[0].Seats[0]==TEXT("jinhsi")&&Formation[1].Seats[0]==TEXT("rover_spectro")&&ValidateFormation(Formation),TEXT("roster_assignment_exchanges_existing_person"));
    UndoPreparation();Check(Formation[0].Seats[0]==TEXT("rover_spectro"),TEXT("undo_restores_lineup"));
    CycleModule();RotateBeam(15);CyclePriority();RotateSeats();SwapTeams();
    Check(Formation[0].Module==1&&Formation[0].BeamYaw==-75&&Formation[0].Priority==1&&ValidateFormation(Formation),TEXT("preparation_module_direction_priority_seats_team_exchange"));
    const auto Saved=Formation;Check(SavePreparation(),TEXT("preparation_save_success"));SetDefaultFormation();Check(LoadPreparation()&&Formation[0].Seats==Saved[0].Seats&&Formation[0].Module==Saved[0].Module,TEXT("stable_id_save_load"));
    SetDefaultFormation();ResetWave();StartOrContinue();const auto Locked=Formation;
    CycleModule();RotateSeats();AssignCharacter(5);RotateBeam(30);SwapTeams();ToggleAssistance();
    Check(!IsAssistanceEnabled(),TEXT("combat_configuration_lock_includes_assistance"));
    Check(Formation[0].Seats==Locked[0].Seats&&Formation[0].Module==Locked[0].Module&&Formation[0].BeamYaw==Locked[0].BeamYaw,TEXT("countdown_rejects_configuration"));
    Tick(1);const double BeforeCountdown=Countdown;PauseToggle();Tick(2);Check(bPaused&&Countdown==BeforeCountdown,TEXT("pause_freezes_countdown"));PauseToggle();Tick(5.1);Tick(.01);
    Check(Spawned==10&&Scheduler.AllSpawned(),TEXT("wave1_actual_batch_has_ten_enemies"));
    const double BeforeTime=SimTime;PauseToggle();Tick(2);Check(SimTime==BeforeTime,TEXT("pause_freezes_combat"));PauseToggle();Tick(.01);Check(SimTime>BeforeTime,TEXT("resume_advances_combat"));
    const double EnergyBefore=Lamps[1].Energy.Value();Lamps[0].Energy.Add(10);Check(Lamps[1].Energy.Value()==EnergyBefore,TEXT("independent_lamp_energy"));
    if(!Enemies.IsEmpty())
    {
        ALNCombatUnit* E=Enemies[0];const FVector Original=E->GetActorLocation();E->SetActorLocation(LampPositions[0]+FVector(0,-700,65));
        Check(FVector::Dist2D(Units[0]->GetActorLocation(),E->GetActorLocation())>800&&FindTarget(0,0)==E,TEXT("melee_acquires_legal_home_target_beyond_station_distance"));
        E->SetActorLocation(Original);
        Lamps[0].Energy.Reset(100);Lamps[0].Relay.TryTrigger(SimTime,Lamps[0].Energy,{true,true,true},{true,false,false});
        FArea A;A.Lamp=0;A.Center=E->GetActorLocation();A.NextTick=SimTime;A.Remaining=1;A.Source=TEXT("state_area");A.Damage=100;
        Areas={A,A};const float BeforeArea=E->Health;UpdateAreas();
        Check(FMath::IsNearlyEqual(BeforeArea-E->Health,100.f)&&Areas.IsEmpty(),TEXT("overlap_final_tick_only_one_damage"));
        A.Remaining=1;A.NextTick=SimTime;Areas={A};const float BeforeDark=E->Health;
        for(int32 S=0;S<3;++S)Units[S]->Health=0;UpdateAreas();
        Check(E->Health==BeforeDark&&!Lamps[0].Relay.IsLit()&&Areas.IsEmpty(),TEXT("whole_team_down_cancels_burn_same_tick"));
        for(int32 S=0;S<3;++S)Units[S]->Restore();
        A.IceLine=true;A.Source=TEXT("ice_line");A.Remaining=6;A.NextTick=SimTime+1;A.Direction=FVector(1,0,0);Areas={A};UpdateAreas();
        Check(FindEnemy(E)->SlowUntil>SimTime,TEXT("ice_line_affects_later_entrants_without_new_hit"));Areas.Empty();
    }
    for(ALNCombatUnit* E:Enemies)Damage(E,100000,0,TEXT("state_test"));Tick(.01);Check(Phase==EPhase::Combat,TEXT("unfinished_task_blocks_wave_completion"));
    Task.Tick(10,true,false);Defense=73;Tick(.01);Check(Phase==EPhase::WaveComplete&&CompletedTasks==1,TEXT("wave1_three_completion_gates"));
    StartOrContinue();Check(WaveIndex==1&&Defense==73&&CompletedTasks==1&&Task.Seconds()==0&&Lamps[0].Energy.Value()==40,TEXT("wave2_preserves_defense_tasks_resets_energy"));
    StartOrContinue();Tick(5.1);Tick(.01);Check(Spawned==4&&!Scheduler.AllSpawned(),TEXT("wave2_delayed_batch_pending"));
    for(ALNCombatUnit* E:Enemies)Damage(E,100000,0,TEXT("state_test"));Task.Tick(10,true,false);Tick(.01);Check(Phase==EPhase::Combat,TEXT("unspawned_batch_blocks_victory"));
    Defense=0;Tick(.01);Check(Phase==EPhase::Defeat,TEXT("zero_defense_fails"));RetryWave();
    Check(CanConfigure()&&WaveIndex==1&&Defense==73&&CompletedTasks==1&&Task.Seconds()==0,TEXT("retry_restores_current_wave_and_previous_progress"));
    StartOrContinue();Tick(5.1);Tick(.01);Tick(5.1);Tick(5.1);
    Check(Spawned==12&&Scheduler.AllSpawned(),TEXT("wave2_four_shooters_eight_rushers"));
    for(ALNCombatUnit* E:Enemies)Damage(E,100000,0,TEXT("state_test"));Task.Tick(10,true,false);Tick(.01);
    Check(Phase==EPhase::Victory&&CompletedTasks==2,TEXT("two_wave_final_victory"));
    Check(SavePreparation()&&LoadPreparation()&&bLoadedCompleted&&CompletedTasks==2,TEXT("completed_save_preserves_victory_marker"));
    Report+=FString::Printf(TEXT("FAILURES=%d\n"),Failures);
    FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectSavedDir()/TEXT("Telemetry/m2-state-checks.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("LNM2_STATE_CHECKS\n%s"),*Report);bFinished=true;if(GEngine)GEngine->Exec(GetWorld(),TEXT("QUIT"));
}

void ALNM2GameMode::UpdateEnemies(float Delta)
{
    for(auto& State:EnemyState)
    {
        auto* E=State.Unit;if(!E->IsAlive())continue;
        if(SimTime<E->RootUntil||SimTime<State.SuppressUntil){State.AttackTarget=nullptr;continue;}
        if(State.AttackTarget)
        {
            if(SimTime<State.HitAt)continue;
            auto* U=State.AttackTarget;const int32 Index=Units.IndexOfByKey(U);
            if(U->IsAlive()&&Index!=INDEX_NONE&&SimTime>ImmuneUntil[Index]+1.e-6&&E->CanSee(U->GetActorLocation())&&FVector::Dist2D(E->GetActorLocation(),U->GetActorLocation())<=(State.Shooter?1400:240))
            {U->TakeHit(State.Damage,SimTime);Log(TEXT("enemy_hit"),FString::Printf(TEXT("person=%s; health=%.1f; damage=%.1f"),*U->Stats.PersonId.ToString(),U->Health,State.Damage));}
            State.AttackTarget=nullptr;continue;
        }
        if(State.Shooter&&FVector::Dist2D(E->GetActorLocation(),State.ShootingPoint)>40)
        {E->MoveTowards(State.ShootingPoint,State.Speed*(SimTime<State.SlowUntil?.6f:1.f),Delta);continue;}
        ALNCombatUnit* Near=nullptr;float Dist=State.Shooter?1400:220;
        for(ALNCombatUnit* U:Units)if(U->IsAlive())
        {const float D=FVector::Dist2D(E->GetActorLocation(),U->GetActorLocation());if(D<Dist&&E->CanSee(U->GetActorLocation())){Dist=D;Near=U;}}
        if(Near&&!State.Shooter&&SimTime>=E->NextEngagement){E->EngagementUntil=SimTime+3;E->NextEngagement=SimTime+7;}
        if(Near&&(State.Shooter||SimTime<E->EngagementUntil))
        {
            if(SimTime>=E->BasicReady)
            {
                State.AttackTarget=Near;State.HitAt=SimTime+.4;E->BasicReady=SimTime+State.Interval;
                E->PlayCombatAction(0,Near->GetActorLocation());
                auto* Effect=GetWorld()->SpawnActor<ALNVisualEffect>();Effect->Beam(E->GetActorLocation()+FVector(0,0,40),Near->GetActorLocation()+FVector(0,0,30),3,FLinearColor(1,.2,.03),.4);
                Log(TEXT("enemy_warning"),FString::Printf(TEXT("type=%d; target=%s; hit_at=%.3f"),State.Type,*Near->Stats.PersonId.ToString(),State.HitAt));
            }
            continue;
        }
        if(State.Shooter)continue;
        if(E->Waypoint<E->Route.Num())
        {E->MoveTowards(E->Route[E->Waypoint],State.Speed*(SimTime<State.SlowUntil?.6f:1.f),Delta);if(FVector::Dist2D(E->GetActorLocation(),E->Route[E->Waypoint])<40)++E->Waypoint;}
        else
        {const int32 Loss=bAssistance?State.BreakthroughLoss/2:State.BreakthroughLoss;Defense=FMath::Max(0,Defense-Loss);E->bExited=true;E->SetActorHiddenInGame(true);++Leaked;++TotalLeaked;Log(TEXT("breach"),FString::Printf(TEXT("type=%d; loss=%d; assistance=%d; defense=%d"),State.Type,Loss,bAssistance,Defense));}
    }
}
