#include "LNCampaignGameMode.h"
#include "LNCampaignBattle.h"
#include "LNCampaignPawn.h"
#include "LNCampaignPlayerController.h"
#include "LNCampaignAudio.h"
#include "LNCombatUnit.h"
#include "LNData.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "TimerManager.h"

namespace CampaignUI
{
    FString S(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,const FString& Fallback=FString()){FString Value;return O.IsValid()&&O->TryGetStringField(Key,Value)?Value:Fallback;}
    double N(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,double Fallback=0){double Value;return O.IsValid()&&O->TryGetNumberField(Key,Value)?Value:Fallback;}
    TArray<TSharedPtr<FJsonValue>> A(const TSharedPtr<FJsonObject>& O,const TCHAR* Key){const TArray<TSharedPtr<FJsonValue>>* Value=nullptr;return O.IsValid()&&O->TryGetArrayField(Key,Value)?*Value:TArray<TSharedPtr<FJsonValue>>{};}
    TSharedPtr<FJsonObject> Clone(const TSharedPtr<FJsonObject>& O){FString Text;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));TSharedPtr<FJsonObject> Copy;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Copy);return Copy;}
    bool MatchesEventFields(const TSharedPtr<FJsonObject>& Tutorial,const FString& Detail)
    {
        const TSharedPtr<FJsonObject>* Expected=nullptr;if(!Tutorial->TryGetObjectField(TEXT("event_fields"),Expected))return true;
        TMap<FString,FString> Actual;TArray<FString> Fields;Detail.ParseIntoArray(Fields,TEXT(";"),true);
        for(const FString& Field:Fields){FString Key,Value;if(Field.Split(TEXT("="),&Key,&Value))Actual.Add(Key.TrimStartAndEnd(),Value.TrimStartAndEnd());}
        for(const auto& Pair:(*Expected)->Values){const FString* Value=Actual.Find(FString(Pair.Key.ToView()));if(!Value||*Value!=Pair.Value->AsString())return false;}return true;
    }
    const TCHAR* ModuleName(int32 M){const TCHAR* Names[]={TEXT("聚焰"),TEXT("贯阵"),TEXT("震域"),TEXT("返照")};return Names[FMath::Clamp(M,0,3)];}
}
using namespace CampaignUI;

ALNCampaignGameMode::ALNCampaignGameMode()
{
    PrimaryActorTick.bCanEverTick=true;DefaultPawnClass=ALNCampaignPawn::StaticClass();PlayerControllerClass=ALNCampaignPlayerController::StaticClass();
    SoundFeedback=CreateDefaultSubobject<ULNCampaignAudioComponent>(TEXT("SoundFeedback"));
}
ALNCampaignGameMode::~ALNCampaignGameMode()=default;
void ALNCampaignGameMode::BeginPlay()
{
    Super::BeginPlay();
    const TCHAR* Cmd=FCommandLine::Get();bAutoAll=FParse::Param(Cmd,TEXT("LNCampaignAll"));
    const bool ManualAutomation=FParse::Param(Cmd,TEXT("LNCampaignManual"))||FParse::Param(Cmd,TEXT("LNCampaignUIChecks"))||FParse::Param(Cmd,TEXT("LNCampaignSceneChecks"));
    bDemo=FParse::Param(Cmd,TEXT("LNCampaignDemo"));bEvidenceRecord=FParse::Param(Cmd,TEXT("LNCampaignRecord"));
#if !WITH_EDITOR
    if(bEvidenceRecord)
    {
        Phase=EPhase::Error;bFinished=true;bAutomation=true;
        UE_LOG(LogTemp,Error,TEXT("Native recording requires UnrealEditor -game on this Windows setup. No capture or profile was created."));
        FPlatformMisc::RequestExitWithStatus(false,2,TEXT("Packaged native recording is unsupported"));return;
    }
#endif
    bEvidenceStress=FParse::Param(Cmd,TEXT("LNCampaignStress"));
    bEvidence=FParse::Param(Cmd,TEXT("LNCampaignEvidence"))||bDemo||bEvidenceRecord||bEvidenceStress;
    bRender=FParse::Param(Cmd,TEXT("LNCampaignRender"));
    AutoMode=FParse::Param(Cmd,TEXT("LNCampaignStateChecks"))?TEXT("state"):FParse::Param(Cmd,TEXT("LNCampaignSaveWrite"))?TEXT("save_write"):FParse::Param(Cmd,TEXT("LNCampaignSaveRead"))?TEXT("save_read"):FParse::Param(Cmd,TEXT("LNCampaignPreparation"))?TEXT("preparation"):bAutoAll?TEXT("all"):bRender?TEXT("render"):TEXT("play");
    if(bEvidence&&AutoMode==TEXT("play"))AutoMode=bDemo?TEXT("demo"):TEXT("evidence");
    if(bEvidenceStress)AutoMode=TEXT("stress");
    if(ManualAutomation)AutoMode=TEXT("manual");
    bAutomation=bAutoAll||bRender||bEvidence||AutoMode!=TEXT("play")||FParse::Param(Cmd,TEXT("LNCampaignAuto"))||FParse::Param(Cmd,TEXT("LNCampaignTraining"));
    bAutoPlay=bAutomation&&!ManualAutomation&&AutoMode!=TEXT("state")&&AutoMode!=TEXT("save_write")&&AutoMode!=TEXT("save_read")&&AutoMode!=TEXT("preparation");
    FParse::Value(Cmd,TEXT("LNCampaignStage="),AutoStage);
    bReview=FParse::Param(Cmd,TEXT("LNReview"))||(bAutomation&&!bAutoAll&&!ManualAutomation&&!FParse::Param(Cmd,TEXT("LNCampaignTraining")));
    const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Telemetry/Campaign"));IFileManager::Get().MakeDirectory(*Directory,true);
    LogPath=Directory/FString::Printf(TEXT("campaign-%s-%u.tsv"),*FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")),FPlatformProcess::GetCurrentProcessId());
    FFileHelper::SaveStringToFile(TEXT("stage\twave\tsim_seconds\tevent\tdetail\n"),*LogPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if(!LoadCampaign()){Phase=EPhase::Error;Notice=TEXT("九关配置缺失或校验失败，请查看日志");Log(TEXT("configuration_error"),Notice);if(bAutomation)FinishAutomation();return;}
    LoadProfile();
    if(bAutomation&&FParse::Value(Cmd,TEXT("LNFormationPlan="),AutomationPlanId))
    {
        FString PlanText;TSharedPtr<FJsonObject> Plans;const TSharedPtr<FJsonObject>* Selected=nullptr;
        if(!FFileHelper::LoadFileToString(PlanText,*(FPaths::ProjectContentDir()/TEXT("Campaign/Data/ValidationPlans.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PlanText),Plans)||!Plans.IsValid()||!Plans->TryGetObjectField(AutomationPlanId,Selected))
        {Phase=EPhase::Error;Notice=TEXT("指定的验证编队方案不存在");FinishAutomation();return;}
        AutomationPlan=*Selected;
    }
    if(AutoMode==TEXT("state")){RunStateChecks();return;}
    if(AutoMode==TEXT("save_write")||AutoMode==TEXT("save_read")){RunSaveProbe(AutoMode==TEXT("save_write"));return;}
    Log(TEXT("session"),FString::Printf(TEXT("version=0.3-campaign; profile=%s; stages=%d; characters=%d; automation=%d"),bReview?TEXT("review"):TEXT("normal"),StageDefinitions.Num(),Characters.Num(),bAutomation));
    if(bAutomation&&!ManualAutomation)
    {
        SelectStage(FMath::Clamp(AutoStage<0?0:AutoStage,0,GetStageCount()-1));
        int32 RequestedVariant=0,RequestedWave=0;FParse::Value(Cmd,TEXT("LNVariant="),RequestedVariant);FParse::Value(Cmd,TEXT("LNWave="),RequestedWave);
        for(int32 I=0;I<RequestedVariant;++I)CycleVariant();
        if(bReview&&RequestedWave>0){WaveIndex=FMath::Clamp(RequestedWave,0,GetWaveCount()-1);PrepareWave();}
        if(AutoMode==TEXT("preparation")){FTimerHandle T;GetWorldTimerManager().SetTimer(T,FTimerDelegate::CreateWeakLambda(this,[this](){Screenshot(TEXT("Campaign-preparation.png"));FTimerHandle Q;GetWorldTimerManager().SetTimer(Q,FTimerDelegate::CreateWeakLambda(this,[this](){FinishAutomation();}),1.0f,false);}),1.0f,false);}
    }
    BeginEvidence();
}

bool ALNCampaignGameMode::LoadCampaign()
{
    FString Text;const FString Path=FPaths::ProjectContentDir()/TEXT("Campaign/Data/Campaign.json");
    if(!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Campaign)||!Campaign.IsValid())return false;
    for(const auto& V:A(Campaign,TEXT("stages")))StageDefinitions.Add(V->AsObject());
    for(const auto& V:A(Campaign,TEXT("characters"))){auto O=V->AsObject();FName Id(*S(O,TEXT("id")));if(Id.IsNone()||Characters.Contains(Id))return false;Characters.Add(Id,O);}
    int32 Waves=0,Enemies=0;
    for(const auto& D:StageDefinitions){for(const auto& W:A(D,TEXT("waves"))){++Waves;for(const auto& B:A(W->AsObject(),TEXT("batches")))Enemies+=N(B->AsObject(),TEXT("count"));}}
    Log(TEXT("configuration_loaded"),FString::Printf(TEXT("stages=%d; waves=%d; enemies=%d; characters=%d"),StageDefinitions.Num(),Waves,Enemies,Characters.Num()));
    return StageDefinitions.Num()==9&&Characters.Num()==26&&Waves==29&&Enemies==610;
}
FString ALNCampaignGameMode::Slot()const{return bAutomation?TEXT("LongNight_Campaign_Automation_v1"):bReview?TEXT("LongNight_Campaign_Review_v1"):TEXT("LongNight_Campaign_Normal_v1");}
void ALNCampaignGameMode::LoadProfile()
{
    Profile=nullptr;
    if((!bAutomation||AutoMode==TEXT("save_read"))&&UGameplayStatics::DoesSaveGameExist(Slot(),0))Profile=Cast<ULNCampaignSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot(),0));
    if(Profile&&(Profile->SchemaVersion!=1||Profile->Astrites<0||Profile->Astrites>600||Profile->ShellCredits<0||Profile->ShellCredits>300000)){Profile=nullptr;Notice=TEXT("旧存档不适用当前活动版本，使用新档案");}
    if(!Profile){Profile=Cast<ULNCampaignSaveGame>(UGameplayStatics::CreateSaveGameObject(ULNCampaignSaveGame::StaticClass()));Profile->PlayerId=FGuid::NewGuid().ToString();Profile->ActivityStartUtc=FDateTime::UtcNow().GetTicks();}
    Profile->ProfileKind=bReview?TEXT("review"):TEXT("normal");
}
bool ALNCampaignGameMode::SaveProfile()
{
    if(!Profile)return false;
    const bool Saved=UGameplayStatics::SaveGameToSlot(Profile,Slot(),0);
    if(!Saved){Notice=TEXT("保存失败，进度尚未写入磁盘");Log(TEXT("save_failed"),Slot());}return Saved;
}
FName ALNCampaignGameMode::StageId()const{return Stage.IsValid()?FName(*S(Stage,TEXT("id"))):NAME_None;}
FName ALNCampaignGameMode::ProgressId()const{return Variant==0?StageId():FName(*(StageId().ToString()+TEXT("_variant")+FString::FromInt(Variant)));}
bool ALNCampaignGameMode::IsInMenu()const{return Phase==EPhase::Menu;}
int32 ALNCampaignGameMode::GetStageCount()const{return StageDefinitions.Num();}
FString ALNCampaignGameMode::GetStageName(int32 Index)const{return StageDefinitions.IsValidIndex(Index)?S(StageDefinitions[Index],TEXT("name")):FString();}
bool ALNCampaignGameMode::IsStageUnlocked(int32 Index)const
{
    if(!StageDefinitions.IsValidIndex(Index)||!Profile)return false;
    if(bReview||Index==0)return true;
    const FName Required(*S(StageDefinitions[Index<6?Index-1:5],TEXT("id")));
    return Profile->CompletedStages.Contains(Required);
}
void ALNCampaignGameMode::SetReviewMode(bool Review)
{
    if(!IsInMenu()||!SaveProfile())return;bReview=Review;LoadProfile();Notice=bReview?TEXT("评审档：自由选关，进度与正常档独立"):TEXT("正常档：按主线顺序推进与领取奖励");
}
void ALNCampaignGameMode::ReturnToMenu()
{
    if(bPaused){bPaused=false;UGameplayStatics::SetGamePaused(this,false);}
    bTutorialPaused=false;bTutorialReplay=false;TutorialText.Empty();TutorialQueue.Empty();
    if(Stage.IsValid())
    {
        if(Phase==EPhase::Battle||Phase==EPhase::Training||Phase==EPhase::Defeat)Profile->Stages.Add(ProgressId(),Checkpoint);
        else if(Phase!=EPhase::Menu&&Phase!=EPhase::Error)SnapshotProgress();
        if(!SaveProfile())return;
    }
    if(Battle)Battle->Shutdown();Battle.Reset();
    for(AActor* Actor:Scenery)if(IsValid(Actor))Actor->Destroy();Scenery.Empty();
    Phase=EPhase::Menu;Notice=TEXT("选择关卡；评审档可直接查看后段机制");
}
void ALNCampaignGameMode::SelectStage(int32 Index)
{
    if(!(IsInMenu()||CanConfigure()||IsComplete()||IsDefeated())||!IsStageUnlocked(Index))return;
    if(!bReview&&FDateTime::UtcNow().GetTicks()-Profile->ActivityStartUtc>FTimespan::FromDays(21).GetTicks()){Notice=TEXT("本地活动周期已结束，可切换评审档查看内容");return;}
    if(Battle)Battle->Shutdown();Battle.Reset();Formation.Empty();
    StageIndex=Index;Stage=Clone(StageDefinitions[Index]);WaveIndex=0;Variant=0;SelectedLamp=SelectedSeat=0;Defense=100;bAssistance=false;
    StageSpawned=StageKilled=StageLeaked=0;SessionTime=0;bScreenshotTaken=false;WaveReports.Empty();UndoStack.Empty();TutorialQueue.Empty();TutorialText.Empty();RecapText.Empty();
    AvailableRoster.Empty();for(const auto& V:A(Stage,TEXT("available_characters")))AvailableRoster.Add(FName(V->AsString()));
    Phase=EPhase::Preparation;LoadRecommendedFormation();
    if(const auto* Saved=Profile->Stages.Find(StageId()))
    {
        if(!Saved->Completed&&Saved->WaveIndex>=0&&Saved->WaveIndex<GetWaveCount()&&Saved->Defense>0&&Saved->Defense<=100&&ValidateFormation(Saved->Formation))
        {WaveIndex=Saved->WaveIndex;Defense=Saved->Defense;Formation=Saved->Formation;bAssistance=Saved->Assistance&&Index<6;}
    }
    if(!BuildStage())
    {
        Phase=EPhase::Error;Notice=TEXT("场景资源创建失败，请查看日志后重试。");Log(TEXT("scene_build_failed"),Notice);
        if(bAutomation)FinishAutomation();return;
    }
    PrepareWave();
    if(!bReview&&Index==0&&WaveIndex==0&&!Profile->TutorialSeen.Contains(TEXT("t01_training")))
    {Phase=EPhase::TrainingReady;SetTutorial(TEXT("t01_training"),TEXT("先看一次单灯接力：漂泊者、今汐、长离真实积能并驱动聚焰。完成后进入双灯配队。"));}
    Log(TEXT("stage_selected"),FString::Printf(TEXT("stage=%s; wave=%d; mode=%s"),*StageId().ToString(),WaveIndex+1,bReview?TEXT("review"):TEXT("normal")));
    if(auto* PC=GetWorld()->GetFirstPlayerController())if(auto* P=Cast<ALNCampaignPawn>(PC->GetPawn()))P->FocusOverview();
}
int32 ALNCampaignGameMode::GetWaveCount()const{return A(Stage,TEXT("waves")).Num();}
void ALNCampaignGameMode::LoadRecommendedFormation()
{
    if(!CanConfigure())return;
    if(!Formation.IsEmpty())PushUndo();Formation.Empty();
    for(const auto& V:A(Stage,TEXT("lamps"))){auto O=V->AsObject();FLNCampaignFormation F;F.Module=N(O,TEXT("module"));F.BeamYaw=N(O,TEXT("yaw_degrees"));F.Priority=N(O,TEXT("priority"));for(const auto& Id:A(O,TEXT("team")))F.Seats.Add(FName(Id->AsString()));Formation.Add(F);}
    if(Battle)ApplyFormation();
}
bool ALNCampaignGameMode::ValidateFormation(const TArray<FLNCampaignFormation>& Value)const
{
    if(Value.Num()!=A(Stage,TEXT("lamps")).Num())return false;
    TSet<FName> People;TArray<int32> Modules;for(const auto& V:A(Stage,TEXT("available_modules")))Modules.Add(V->AsNumber());
    for(const auto& F:Value)
    {
        if(F.Seats.Num()!=3||!Modules.Contains(F.Module)||F.Priority<0||F.Priority>2||!FMath::IsFinite(F.BeamYaw))return false;
        for(FName Id:F.Seats){auto D=Characters.FindRef(Id);if(!D.IsValid()||!AvailableRoster.Contains(Id))return false;FName Person(*S(D,TEXT("person_id")));if(Person.IsNone()||People.Contains(Person))return false;People.Add(Person);}
    }return true;
}
void ALNCampaignGameMode::PrepareWave()
{
    if(Battle)Battle->Shutdown();Battle=MakeUnique<FLNCampaignBattle>();
    float Scale=1,Offset=0;FParse::Value(FCommandLine::Get(),TEXT("LNEnemyHealthScale="),Scale);FParse::Value(FCommandLine::Get(),TEXT("LNBatchOffset="),Offset);Battle->SetHealthScale(Scale);Battle->SetBatchTimeOffset(Offset);
    Battle->OnEvent=[this](const FString& E,const FString& D){OnBattleEvent(E,D);};
    if(!ValidateFormation(Formation)||!Battle->Initialize(GetWorld(),Campaign,Stage,Formation,WaveIndex,Defense,bAssistance))
    {Phase=EPhase::Error;Notice=TEXT("关卡配置无法运行：")+Battle->GetLastError();Log(TEXT("runtime_error"),Notice);return;}
    Phase=EPhase::Preparation;Checkpoint.Formation=Formation;Checkpoint.WaveIndex=WaveIndex;Checkpoint.Defense=Defense;Checkpoint.Assistance=bAssistance;
    if(bReview||StageIndex!=0||WaveIndex!=0||Profile->TutorialSeen.Contains(TEXT("t01_training")))ShowPreparationTutorial();
}
void ALNCampaignGameMode::PushUndo(){FLNCampaignProgress P;P.Formation=Formation;P.Assistance=bAssistance;UndoStack.Add(P);if(UndoStack.Num()>32)UndoStack.RemoveAt(0);}
void ALNCampaignGameMode::SnapshotProgress(){FLNCampaignProgress P;P.WaveIndex=Phase==EPhase::WaveComplete?WaveIndex+1:WaveIndex;P.Defense=Defense;P.Formation=Formation;P.Assistance=bAssistance;P.Completed=Phase==EPhase::Victory;Profile->Stages.Add(ProgressId(),P);}
void ALNCampaignGameMode::ApplyFormation(){PrepareWave();SnapshotProgress();SaveProfile();Log(TEXT("formation_changed"),GetLampText(SelectedLamp));}
bool ALNCampaignGameMode::CanConfigure()const{return Phase==EPhase::Preparation&&Stage.IsValid();}
bool ALNCampaignGameMode::IsDefeated()const{return Phase==EPhase::Defeat;}
bool ALNCampaignGameMode::IsComplete()const{return Phase==EPhase::Victory;}
void ALNCampaignGameMode::SelectLamp(int32 I){if(Formation.IsValidIndex(I)){SelectedLamp=I;SelectedSeat=0;}}
void ALNCampaignGameMode::SelectSeat(int32 I){if(I>=0&&I<3)SelectedSeat=I;}
void ALNCampaignGameMode::AssignCharacter(int32 I)
{
    if(!CanConfigure()||!AvailableRoster.IsValidIndex(I)||!Formation.IsValidIndex(SelectedLamp))return;
    const FName NewId=AvailableRoster[I];const FName OldId=Formation[SelectedLamp].Seats[SelectedSeat];if(NewId==OldId)return;
    const FString Person=S(Characters.FindRef(NewId),TEXT("person_id"));PushUndo();
    for(int32 L=0;L<Formation.Num();++L)for(int32 T=0;T<3;++T)if(S(Characters.FindRef(Formation[L].Seats[T]),TEXT("person_id"))==Person){Formation[L].Seats[T]=OldId;}
    Formation[SelectedLamp].Seats[SelectedSeat]=NewId;
    if(!ValidateFormation(Formation)){Formation=UndoStack.Pop().Formation;Notice=TEXT("编队需每灯三人且全场人物唯一");return;}ApplyFormation();
}
void ALNCampaignGameMode::CycleModule(){if(!CanConfigure())return;PushUndo();const auto Pool=A(Stage,TEXT("available_modules"));int32 Index=0;for(int32 I=0;I<Pool.Num();++I)if(Pool[I]->AsNumber()==Formation[SelectedLamp].Module)Index=I;Formation[SelectedLamp].Module=Pool[(Index+1)%Pool.Num()]->AsNumber();ApplyFormation();}
void ALNCampaignGameMode::RotateSeats(){if(!CanConfigure())return;PushUndo();auto& Seats=Formation[SelectedLamp].Seats;FName First=Seats[0];Seats.RemoveAt(0);Seats.Add(First);ApplyFormation();}
void ALNCampaignGameMode::SwapTeams(int32 I){if(!CanConfigure()||!Formation.IsValidIndex(I)||I==SelectedLamp)return;PushUndo();Swap(Formation[I],Formation[SelectedLamp]);ApplyFormation();}
void ALNCampaignGameMode::RotateBeam(float D){if(CanConfigure()&&Formation[SelectedLamp].Module==1){PushUndo();Formation[SelectedLamp].BeamYaw=FMath::UnwindDegrees(Formation[SelectedLamp].BeamYaw+D);ApplyFormation();}}
void ALNCampaignGameMode::CyclePriority(){if(CanConfigure()){PushUndo();Formation[SelectedLamp].Priority=(Formation[SelectedLamp].Priority+1)%3;ApplyFormation();}}
void ALNCampaignGameMode::UndoPreparation(){if(CanConfigure()&&!UndoStack.IsEmpty()){auto P=UndoStack.Pop();Formation=P.Formation;bAssistance=P.Assistance;ApplyFormation();Notice=TEXT("已撤回上一步整备修改");}}
void ALNCampaignGameMode::ToggleAssistance(){if(CanConfigure()&&StageIndex<6){PushUndo();bAssistance=!bAssistance;ApplyFormation();}}
void ALNCampaignGameMode::SaveFormationPreset(){if(CanConfigure()){FLNCampaignProgress P;P.Formation=Formation;P.Assistance=bAssistance;Profile->Presets.Add(StageId(),P);if(SaveProfile())Notice=TEXT("已保存本关阵容预设");}}
void ALNCampaignGameMode::LoadFormationPreset(){if(CanConfigure())if(auto* P=Profile->Presets.Find(StageId()))if(ValidateFormation(P->Formation)){PushUndo();Formation=P->Formation;bAssistance=P->Assistance&&StageIndex<6;ApplyFormation();Notice=TEXT("已载入本关阵容预设");}}
void ALNCampaignGameMode::CycleReviewWave(){if(bReview&&CanConfigure()){WaveIndex=(WaveIndex+1)%GetWaveCount();Defense=100;StageSpawned=StageKilled=StageLeaked=0;PrepareWave();}}
FString ALNCampaignGameMode::GetVariantLabel()const{auto Values=A(StageDefinitions.IsValidIndex(StageIndex)?StageDefinitions[StageIndex]:nullptr,TEXT("variants"));return Variant>0&&Values.IsValidIndex(Variant-1)?S(Values[Variant-1]->AsObject(),TEXT("name")):TEXT("原关卡");}
void ALNCampaignGameMode::CycleVariant()
{
    if(!CanConfigure()||(!bReview&&!Profile->CompletedStages.Contains(StageId())))return;
    const auto Variants=A(StageDefinitions[StageIndex],TEXT("variants"));if(Variants.IsEmpty())return;
    SnapshotProgress();if(!SaveProfile())return;
    Variant=(Variant+1)%(Variants.Num()+1);Stage=Clone(StageDefinitions[StageIndex]);
    if(Variant)
    {
        auto V=Variants[Variant-1]->AsObject();const TSharedPtr<FJsonObject>* Map=nullptr;
        for(const auto& W:A(Stage,TEXT("waves")))
        {
            for(const auto& T:A(W->AsObject(),TEXT("tasks")))
            {
                auto Task=T->AsObject();
                if(V->TryGetObjectField(TEXT("task_lamp_remap"),Map)){auto Lamps=A(Task,TEXT("lamp_ids"));for(auto& L:Lamps){FString M;if((*Map)->TryGetStringField(L->AsString(),M))L=MakeShared<FJsonValueString>(M);}Task->SetArrayField(TEXT("lamp_ids"),Lamps);}
                if(V->TryGetObjectField(TEXT("task_point_remap"),Map)){FString P;if((*Map)->TryGetStringField(S(Task,TEXT("point_id")),P)){Task->SetStringField(TEXT("point_id"),P);auto Points=Stage->GetObjectField(TEXT("task_points"));auto OldTo=A(Task,TEXT("to_m"));Task->SetArrayField(TEXT("from_m"),A(Points,*P));if(S(Task,TEXT("kind"))!=TEXT("escort"))Task->SetArrayField(TEXT("to_m"),A(Points,*P));}}
                double Mirror=0;if(S(Task,TEXT("kind"))==TEXT("escort")&&V->TryGetNumberField(TEXT("mirror_escort_x_m"),Mirror))for(const TCHAR* Key:{TEXT("from_m"),TEXT("to_m")}){auto Position=A(Task,Key);if(Position.Num()>=2){Position[0]=MakeShared<FJsonValueNumber>(2*Mirror-Position[0]->AsNumber());Task->SetArrayField(Key,Position);}}
            }
            for(const auto& F:A(W->AsObject(),TEXT("faults")))if(V->TryGetObjectField(TEXT("fault_lamp_remap"),Map)){FString L;if((*Map)->TryGetStringField(S(F->AsObject(),TEXT("lamp_id")),L))F->AsObject()->SetStringField(TEXT("lamp_id"),L);}
        }
    }
    WaveIndex=0;Defense=100;StageSpawned=StageKilled=StageLeaked=0;WaveReports.Empty();UndoStack.Empty();
    if(const auto* Saved=Profile->Stages.Find(ProgressId()))if(!Saved->Completed&&Saved->WaveIndex>=0&&Saved->WaveIndex<GetWaveCount()&&Saved->Defense>0&&Saved->Defense<=100&&ValidateFormation(Saved->Formation))
    {WaveIndex=Saved->WaveIndex;Defense=Saved->Defense;Formation=Saved->Formation;}
    PrepareWave();Notice=GetVariantLabel();
}

void ALNCampaignGameMode::StartOrContinue()
{
    if(HasTutorial()){AcknowledgeTutorial();return;}
    if(Phase==EPhase::TrainingReady)
    {
        SelectedLamp=SelectedSeat=0;
        Battle->Shutdown();Battle=MakeUnique<FLNCampaignBattle>();Battle->OnEvent=[this](const FString& E,const FString& D){OnBattleEvent(E,D);};
        if(!Battle->Initialize(GetWorld(),Campaign,Stage,Formation,0,100,false,true)){Phase=EPhase::Error;return;}Phase=EPhase::Training;Battle->BeginWave();return;
    }
    if(Phase==EPhase::Defeat){RetryWave();return;}
    if(Phase==EPhase::Victory){ReturnToMenu();return;}
    if(Phase==EPhase::WaveComplete){++WaveIndex;UndoStack.Empty();PrepareWave();SnapshotProgress();SaveProfile();return;}
    if(!CanConfigure()||!ValidateFormation(Formation))return;
    Checkpoint.Formation=Formation;Checkpoint.WaveIndex=WaveIndex;Checkpoint.Defense=Defense;Checkpoint.Assistance=bAssistance;Checkpoint.Completed=false;
    Profile->Stages.Add(ProgressId(),Checkpoint);if(!SaveProfile())return;UndoStack.Empty();Notice.Empty();Phase=EPhase::Battle;Battle->BeginWave();
}
bool ALNCampaignGameMode::CanStartOrContinue()const{return HasTutorial()||CanConfigure()||Phase==EPhase::TrainingReady||Phase==EPhase::WaveComplete||IsComplete()||IsDefeated();}
FString ALNCampaignGameMode::GetStartLabel()const{return HasTutorial()?TEXT("继续说明"):Phase==EPhase::TrainingReady?TEXT("开始接力演示"):Phase==EPhase::WaveComplete?TEXT("下一波整备"):IsComplete()?TEXT("返回选关"):IsDefeated()?TEXT("恢复本波整备"):TEXT("确认并开战");}
void ALNCampaignGameMode::PauseToggle(){if(Phase!=EPhase::Battle&&Phase!=EPhase::Training)return;bPaused=!bPaused;UGameplayStatics::SetGamePaused(this,bPaused);Log(TEXT("pause"),bPaused?TEXT("1"):TEXT("0"));}
void ALNCampaignGameMode::RetryWave(){if(!IsDefeated())return;Defense=Checkpoint.Defense;WaveIndex=Checkpoint.WaveIndex;Formation=Checkpoint.Formation;bAssistance=Checkpoint.Assistance;PrepareWave();Notice=TEXT("已恢复本波开战前配置，前波成果保留");}
void ALNCampaignGameMode::DebugToggle(){bDebug=!bDebug;if(Battle)Battle->SetDebugVisible(bDebug);}
void ALNCampaignGameMode::SetTutorial(const FString& Key,const FString& Text)
{
    if(!Profile||(!bTutorialReplay&&(bReview||Profile->TutorialSeen.Contains(FName(*Key)))))return;
    if(CurrentTutorial==FName(*Key)&&!TutorialText.IsEmpty())return;
    if(HasTutorial()){for(const auto& P:TutorialQueue)if(P.Key==FName(*Key))return;TutorialQueue.Add({FName(*Key),Text});return;}
    CurrentTutorial=FName(*Key);TutorialText=Text;
    bool PauseOnShow=true;
    for(const auto& V:A(Campaign,TEXT("tutorials"))){auto T=V->AsObject();if(S(T,TEXT("id"))==Key)T->TryGetBoolField(TEXT("pause_on_show"),PauseOnShow);}
    if(PauseOnShow&&!bAutomation&&(Phase==EPhase::Battle||Phase==EPhase::Training)&&!bPaused){bPaused=true;bTutorialPaused=true;UGameplayStatics::SetGamePaused(this,true);}
    Log(TEXT("tutorial_show"),Key);
}
void ALNCampaignGameMode::AcknowledgeTutorial()
{
    if(!HasTutorial())return;if(!bTutorialReplay&&!(CurrentTutorial==TEXT("t01_training")&&Phase==EPhase::TrainingReady))Profile->TutorialSeen.AddUnique(CurrentTutorial);Log(bTutorialReplay?TEXT("tutorial_replay_ack"):TEXT("tutorial_ack"),CurrentTutorial.ToString());TutorialText.Empty();CurrentTutorial=NAME_None;if(!bTutorialReplay)SaveProfile();
    if(bTutorialPaused){bPaused=false;bTutorialPaused=false;UGameplayStatics::SetGamePaused(this,false);}
    if(!TutorialQueue.IsEmpty()){auto Next=TutorialQueue[0];TutorialQueue.RemoveAt(0);SetTutorial(Next.Key.ToString(),Next.Value);}else bTutorialReplay=false;
}
void ALNCampaignGameMode::ReplayTutorials()
{
    if(HasTutorial()||!(CanConfigure()||Phase==EPhase::WaveComplete||Phase==EPhase::Victory))return;
    bTutorialReplay=true;TutorialQueue.Empty();
    for(const auto& V:A(Campaign,TEXT("tutorials"))){auto T=V->AsObject();if(S(T,TEXT("stage"))==StageId().ToString())SetTutorial(S(T,TEXT("id")),TEXT("教学回看 · ")+S(T,TEXT("title"))+TEXT("\n")+S(T,TEXT("text")));}
    if(!HasTutorial()){bTutorialReplay=false;Notice=TEXT("本关沿用主线教学，可返回对应主线回看");}
}
void ALNCampaignGameMode::ShowPreparationTutorial()
{
    for(const auto& V:A(Campaign,TEXT("tutorials"))){auto T=V->AsObject();if(S(T,TEXT("stage"))==StageId().ToString()&&N(T,TEXT("wave"))==WaveIndex&&S(T,TEXT("phase"))==TEXT("preparation"))SetTutorial(S(T,TEXT("id")),S(T,TEXT("title"))+TEXT("\n")+S(T,TEXT("text")));}
}
void ALNCampaignGameMode::OnBattleEvent(const FString& Event,const FString& Detail)
{
    Log(Event,Detail);
    if(SoundFeedback)SoundFeedback->HandleEvent(Event,Detail);
    if(Phase==EPhase::Training)return;
    for(const auto& V:A(Campaign,TEXT("tutorials"))){auto T=V->AsObject();const FString P=S(T,TEXT("phase"));if(S(T,TEXT("stage"))==StageId().ToString()&&N(T,TEXT("wave"))==WaveIndex&&(P==TEXT("combat")||P==TEXT("complete"))&&S(T,TEXT("event"))==Event&&MatchesEventFields(T,Detail))SetTutorial(S(T,TEXT("id")),S(T,TEXT("title"))+TEXT("\n")+S(T,TEXT("text")));}
}
void ALNCampaignGameMode::Tick(float Delta)
{
    Super::Tick(Delta);TickEvidence(Delta);if(bFinished||bPaused)return;
    if(bAutomation&&AutoMode!=TEXT("manual")&&Phase==EPhase::Error){FinishAutomation();return;}
    if(bAutoPlay&&HasTutorial()){if(!bDemo||(AutoDelay+=Delta)>=3){AutoDelay=0;AcknowledgeTutorial();}return;}
    if(bAutoPlay&&Phase==EPhase::Victory)
    {
        if(bDemo&&(AutoDelay+=Delta)<4)return;AutoDelay=0;
        ClaimRewards();
        Log(TEXT("reward_retry_probe"),TEXT("repeat_claim_after_real_victory=1"));ClaimRewards();
        if(bAutoAll&&StageIndex+1<GetStageCount()){WriteAutomationReport();const int32 Next=StageIndex+1;ReturnToMenu();SelectStage(Next);}
        else FinishAutomation();
        return;
    }
    if(bAutoPlay&&(CanConfigure()||Phase==EPhase::TrainingReady||Phase==EPhase::WaveComplete)){AutoDelay+=Delta;if(AutoDelay>=(bDemo?4:.25)){AutoDelay=0;if(CanConfigure())ApplyAutomationPlan();if(Phase!=EPhase::Error)StartOrContinue();}return;}
    if((Phase!=EPhase::Battle&&Phase!=EPhase::Training)||!Battle)return;
    Battle->Tick(Delta);SessionTime+=Delta;
    if(Phase==EPhase::Training&&Battle->IsTrainingComplete())
    {Profile->TutorialSeen.AddUnique(TEXT("t01_training"));Log(TEXT("tutorial_complete"),TEXT("t01_training; real_relay_and_lamp_hit=1"));SaveProfile();bTrainingDone=true;PrepareWave();SetTutorial(TEXT("t01_deploy"),TEXT("现在组成两支三人队伍。每个人物全场唯一，整备确认后才会开始正式第一波。"));return;}
    if(Phase==EPhase::Training)return;
    Defense=Battle->GetDefense();
    if(bRender&&!bScreenshotTaken&&Battle->GetTime()>12){bScreenshotTaken=true;Screenshot(TEXT("Campaign-runtime.png"));}
    if(Battle->IsDefeated()){Phase=EPhase::Defeat;RecapText=Battle->GetSummary();Notice=TEXT("防线失守，可回本波整备调整");}
    else if(Battle->IsComplete())
    {
        StageSpawned+=Battle->GetSpawned();StageKilled+=Battle->GetKilled();StageLeaked+=Battle->GetLeaked();RecapText=Battle->GetSummary();
        auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("stage"),StageId().ToString());R->SetNumberField(TEXT("wave"),WaveIndex+1);R->SetNumberField(TEXT("seconds"),Battle->GetTime());R->SetNumberField(TEXT("spawned"),Battle->GetSpawned());R->SetNumberField(TEXT("killed"),Battle->GetKilled());R->SetNumberField(TEXT("leaked"),Battle->GetLeaked());R->SetNumberField(TEXT("defense"),Defense);R->SetNumberField(TEXT("tasks"),Battle->GetTaskCount());R->SetNumberField(TEXT("task_fraction"),Battle->GetTaskFraction());R->SetNumberField(TEXT("planned_enemies"),Battle->GetPlannedEnemies());R->SetNumberField(TEXT("living_enemies"),Battle->GetLivingEnemies());R->SetNumberField(TEXT("completed_batches"),Battle->GetCompletedBatches());R->SetBoolField(TEXT("all_batches_spawned"),Battle->AllBatchesSpawned());R->SetBoolField(TEXT("complete"),true);WaveReports.Add(MakeShared<FJsonValueObject>(R));
        if(WaveIndex+1==GetWaveCount())CompleteStage();
        else{Phase=EPhase::WaveComplete;FLNCampaignProgress P;P.WaveIndex=WaveIndex+1;P.Defense=Defense;P.Formation=Formation;P.Assistance=bAssistance;Profile->Stages.Add(ProgressId(),P);SaveProfile();Notice=TEXT("本波完成，下一波可重新布阵");}
    }
    if(bAutoPlay&&(Phase==EPhase::Error||Phase==EPhase::Defeat||Battle->GetTime()>600)){FinishAutomation();return;}
}
void ALNCampaignGameMode::CompleteStage()
{
    Phase=EPhase::Victory;Profile->CompletedStages.AddUnique(StageId());SnapshotProgress();if(SaveProfile())Notice=TEXT("本关敌人与任务均已完成，可领取首通奖励");
    OnBattleEvent(TEXT("stage_complete"),FString::Printf(TEXT("spawned=%d; killed=%d; leaked=%d; defense=%d"),StageSpawned,StageKilled,StageLeaked,Defense));
    if(bRender)Screenshot(TEXT("Campaign-victory.png"));
}

int32 ALNCampaignGameMode::GetLampCount()const{return Phase==EPhase::Training&&Battle?Battle->GetLampCount():Formation.Num();}
FName ALNCampaignGameMode::GetSeatId(int32 L,int32 T)const{if(Phase==EPhase::Training&&Battle)if(auto* U=Battle->GetUnit(L,T))return U->Stats.PersonId==TEXT("rover")?FName(TEXT("rover_spectro")):U->Stats.PersonId;return Formation.IsValidIndex(L)&&Formation[L].Seats.IsValidIndex(T)?Formation[L].Seats[T]:NAME_None;}
FString ALNCampaignGameMode::GetSeatName(int32 L,int32 T)const{return S(Characters.FindRef(GetSeatId(L,T)),TEXT("name"),TEXT("—"));}
float ALNCampaignGameMode::GetSeatHealthFraction(int32 L,int32 T)const{auto* U=Battle?Battle->GetUnit(L,T):nullptr;return U&&U->Stats.MaxHealth>0?U->Health/U->Stats.MaxHealth:1;}
int32 ALNCampaignGameMode::GetCurrentSeat(int32 L)const{return Battle?Battle->GetCurrentSeat(L):INDEX_NONE;}
float ALNCampaignGameMode::GetEnergyFraction(int32 L)const{return Battle?Battle->GetEnergyFraction(L):.4f;}
int32 ALNCampaignGameMode::GetModule(int32 L)const{return Formation.IsValidIndex(L)?Formation[L].Module:0;}
float ALNCampaignGameMode::GetBeamYaw(int32 L)const{return Formation.IsValidIndex(L)?Formation[L].BeamYaw:0;}
FVector ALNCampaignGameMode::GetLampPosition(int32 L)const{return Battle?Battle->GetLampPosition(L):GetCameraCenter();}
FString ALNCampaignGameMode::GetLampText(int32 L)const
{
    if(!Formation.IsValidIndex(L)||!Battle)return FString();const int32 C=Battle->GetCurrentSeat(L);const int32 Next=Battle->GetNextSeat(L);
    const TCHAR* PriorityLabels[]={TEXT("优先突破"),TEXT("优先拆灯"),TEXT("优先远程")};
    return FString::Printf(TEXT("%s · 协奏 %.0f/100 · %s\n当前 %s / 下一 %s\n%s · 方位 %.0f° · %s"),ModuleName(Formation[L].Module),Battle->GetEnergyFraction(L)*100,Battle->IsLampLit(L)?TEXT("点亮"):TEXT("未点亮"),C<0?TEXT("待接任"):*GetSeatName(L,C),*GetSeatName(L,Next),*Battle->GetLampReason(L),Formation[L].BeamYaw,PriorityLabels[FMath::Clamp(Formation[L].Priority,0,2)]);
}
FString ALNCampaignGameMode::GetRosterName(int32 I)const{return AvailableRoster.IsValidIndex(I)?S(Characters.FindRef(AvailableRoster[I]),TEXT("name")):FString();}
FString ALNCampaignGameMode::GetRosterDescription(int32 I)const
{
    if(!AvailableRoster.IsValidIndex(I))return FString();auto D=Characters.FindRef(AvailableRoster[I]);bool Ranged=false;D->TryGetBoolField(TEXT("ranged"),Ranged);
    FString Text=S(D,TEXT("name"))+(Ranged?TEXT(" · 远程驻守"):TEXT(" · 近战迎击"));
    Text+=TEXT("\n战技一：")+S(D,TEXT("skill1_name"))+TEXT(" · ")+S(D,TEXT("skill1_description"));
    Text+=TEXT("\n战技二：")+S(D,TEXT("skill2_name"))+TEXT(" · ")+S(D,TEXT("skill2_description"));
    auto Coop=D->GetObjectField(TEXT("coop"));
    for(const auto& V:A(D,TEXT("modules"))){const int32 Module=V->AsNumber();const TSharedPtr<FJsonObject>* Effect=nullptr;if(Coop->TryGetObjectField(FString::FromInt(Module),Effect))Text+=FString(TEXT("\n"))+ModuleName(Module)+TEXT("协同：")+S(*Effect,TEXT("description"));}
    return Text;
}
FString ALNCampaignGameMode::GetRosterFocusDescription(int32 I)const
{
    if(!AvailableRoster.IsValidIndex(I))return FString();auto D=Characters.FindRef(AvailableRoster[I]);const int32 Module=GetModule(SelectedLamp);
    const TSharedPtr<FJsonObject>* Effect=nullptr;auto Coop=D->GetObjectField(TEXT("coop"));
    return S(D,TEXT("skill1_name"))+TEXT(" / ")+S(D,TEXT("skill2_name"))+TEXT("\n")+ModuleName(Module)+TEXT("：")+(Coop->TryGetObjectField(FString::FromInt(Module),Effect)?S(*Effect,TEXT("description")):TEXT("基础灯击"));
}
FString ALNCampaignGameMode::GetSeatRole(int32 L,int32 Seat)const{bool Ranged=false;auto D=Characters.FindRef(GetSeatId(L,Seat));if(D)D->TryGetBoolField(TEXT("ranged"),Ranged);return Ranged?TEXT("远程\n驻守"):TEXT("近战\n迎击");}
FString ALNCampaignGameMode::GetPriorityLabel(int32 L)const{const TCHAR* Names[]={TEXT("突破优先"),TEXT("拆灯优先"),TEXT("远程优先")};return Formation.IsValidIndex(L)?Names[FMath::Clamp(Formation[L].Priority,0,2)]:TEXT("");}
FString ALNCampaignGameMode::GetLampSummary(int32 L)const
{
    if(!Battle||!Formation.IsValidIndex(L))return FString();auto Short=[](FString Name){return Name.Replace(TEXT("·衍射"),TEXT("")).Replace(TEXT("·玄翎"),TEXT(""));};
    const int32 Current=Battle->GetCurrentSeat(L),Next=Battle->GetNextSeat(L);
    return (Current<0?TEXT("待接力"):Short(GetSeatName(L,Current)))+TEXT(" → ")+Short(GetSeatName(L,Next))+TEXT("\n")+(CanConfigure()?TEXT("整备完成后开战"):Battle->GetLampReason(L));
}
void ALNCampaignGameMode::PlayUiCue(bool Back){if(SoundFeedback)SoundFeedback->PlayUi(Back);}
int32 ALNCampaignGameMode::GetDefense()const{return Defense;}
float ALNCampaignGameMode::GetTaskFraction()const{return Battle?Battle->GetTaskFraction():0;}
FString ALNCampaignGameMode::GetTaskText()const{return Battle?Battle->GetTaskText():TEXT("选择关卡查看任务");}
FString ALNCampaignGameMode::GetPhaseLabel()const
{
    switch(Phase){case EPhase::Menu:return TEXT("活动选关");case EPhase::Preparation:return TEXT("波前整备");case EPhase::TrainingReady:return TEXT("入关教学");case EPhase::Training:return TEXT("协奏演示");case EPhase::Battle:return bPaused?TEXT("已暂停"):Battle&&Battle->IsCountdown()?FString::Printf(TEXT("预告 %.1f秒"),Battle->GetCountdown()):TEXT("战斗观察");case EPhase::WaveComplete:return TEXT("本波完成");case EPhase::Victory:return TEXT("关卡完成");case EPhase::Defeat:return TEXT("防线失守");default:return TEXT("配置错误");}
}
FString ALNCampaignGameMode::GetStatusText()const{return (bEvidenceStress?FString(TEXT("隔离压力测试 · ")):bDemo?FString(TEXT("自动演示 · ")):FString())+(Battle?FString::Printf(TEXT("第%d/%d波 · 出现%d/%d · 击败%d · 突破%d · %.1f秒"),WaveIndex+1,GetWaveCount(),Battle->GetSpawned(),Battle->GetPlannedEnemies(),Battle->GetKilled(),Battle->GetLeaked(),Battle->GetTime()):FString(TEXT("六关主线与三关险境，共29波")));}
FString ALNCampaignGameMode::GetIntelText()const
{
    auto Waves=A(Stage,TEXT("waves"));if(!Waves.IsValidIndex(WaveIndex))return FString();auto W=Waves[WaveIndex]->AsObject();FString Text=S(W,TEXT("source_spawn_text"))+TEXT("\n")+S(W,TEXT("source_goal"));
    for(const auto& F:A(W,TEXT("faults")))Text+=FString::Printf(TEXT("\n%.0f秒 %s 故障%.0f秒（提前%.0f秒预警）"),N(F->AsObject(),TEXT("time")),*S(F->AsObject(),TEXT("lamp_id")),N(F->AsObject(),TEXT("seconds")),N(F->AsObject(),TEXT("warning_seconds")));
    Text+=TEXT("\n")+S(Stage,TEXT("between_wave_guidance"));
    return Text;
}
FString ALNCampaignGameMode::GetEventText()const{return FString::Join(Events,TEXT("\n"));}
bool ALNCampaignGameMode::CanClaimRewards()const
{
    if(!Profile)return false;for(const auto& V:A(Campaign,TEXT("rewards"))){auto R=V->AsObject();if(Profile->RewardClaims.Contains(FName(*S(R,TEXT("id")))))continue;bool Met=S(R,TEXT("kind"))==TEXT("all_main_complete");if(Met){for(const auto& Id:A(R,TEXT("stage_ids")))Met&=Profile->CompletedStages.Contains(FName(Id->AsString()));}else Met=Profile->CompletedStages.Contains(FName(*S(R,TEXT("stage_id"))));if(Met)return true;}return false;
}
void ALNCampaignGameMode::ClaimRewards()
{
    if(!Profile)return;const auto Before=Profile->RewardClaims;const int32 ABefore=Profile->Astrites,SBefore=Profile->ShellCredits;
    for(const auto& V:A(Campaign,TEXT("rewards"))){auto R=V->AsObject();FName Id(*S(R,TEXT("id")));if(Profile->RewardClaims.Contains(Id))continue;bool Met=S(R,TEXT("kind"))==TEXT("all_main_complete");if(Met){for(const auto& Key:A(R,TEXT("stage_ids")))Met&=Profile->CompletedStages.Contains(FName(Key->AsString()));}else Met=Profile->CompletedStages.Contains(FName(*S(R,TEXT("stage_id"))));if(Met){Profile->RewardClaims.Add(Id);Profile->Astrites+=N(R,TEXT("astrite"));Profile->ShellCredits+=N(R,TEXT("shell_credits"));}}
    if(!SaveProfile()){Profile->RewardClaims=Before;Profile->Astrites=ABefore;Profile->ShellCredits=SBefore;return;}
    TArray<FString> Claimed;for(FName Id:Profile->RewardClaims)if(!Before.Contains(Id))Claimed.Add(Id.ToString());
    Notice=FString::Printf(TEXT("本地奖励到账：星声 +%d · 贝币 +%d"),Profile->Astrites-ABefore,Profile->ShellCredits-SBefore);
    Log(TEXT("reward_claim"),FString::Printf(TEXT("astrite_delta=%d; shell_delta=%d; conditions=%s; balance_astrite=%d; balance_shell=%d; profile=%s"),Profile->Astrites-ABefore,Profile->ShellCredits-SBefore,*FString::Join(Claimed,TEXT(",")),Profile->Astrites,Profile->ShellCredits,bReview?TEXT("review"):TEXT("normal")));
}
FString ALNCampaignGameMode::GetRewardSummary()const
{
    if(!Profile)return FString();const double Day=(FDateTime::UtcNow().GetTicks()-Profile->ActivityStartUtc)/double(FTimespan::FromDays(1).GetTicks());
    return FString::Printf(TEXT("%s · 本地演示资源\n已领星声 %d / 600 · 贝币 %d / 300000\n首通条件 %d / 10 · 活动第 %.0f / 21 天"),bReview?TEXT("独立评审档"):TEXT("正常活动档"),Profile->Astrites,Profile->ShellCredits,Profile->RewardClaims.Num(),bReview?1:FMath::FloorToDouble(Day)+1);
}
FVector ALNCampaignGameMode::GetCameraCenter()const{return FVector(5000,-2100,0);}

bool ALNCampaignGameMode::BuildStage()
{
    for(AActor* Actor:Scenery)if(IsValid(Actor))Actor->Destroy();Scenery.Empty();
    UStaticMesh* SceneCube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(!SceneCube){UE_LOG(LogTemp,Error,TEXT("Campaign scene cube could not be loaded"));return false;}
    bool SceneReady=true;
    auto Shape=[&](FVector P,FVector Size,const TCHAR* Material,bool Blocking=false,float Yaw=0.f)->AStaticMeshActor*
    {
        auto* Actor=GetWorld()->SpawnActor<AStaticMeshActor>(P,FRotator(0,Yaw,0));
        if(!Actor){SceneReady=false;UE_LOG(LogTemp,Error,TEXT("Campaign scenery actor could not be created"));return nullptr;}
        auto* C=Actor->GetStaticMeshComponent();
        // These actors are constructed after a menu selection, when the world
        // has already begun play. Static mobility rejects runtime mesh changes.
        // Query collision for warehouses remains WorldStatic below.
        C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(SceneCube);
        if(C->GetStaticMesh()!=SceneCube)
        {SceneReady=false;UE_LOG(LogTemp,Error,TEXT("Campaign scenery mesh assignment failed: %s"),*Actor->GetName());Actor->Destroy();return nullptr;}
        // AStaticMeshActor defaults to its mesh's collision profile; explicitly opting out
        // keeps decorative road meshes from reverting to BlockAll during body recreation.
        C->SetCollisionProfileName(Blocking?TEXT("BlockAll"):TEXT("NoCollision"));C->SetCollisionEnabled(Blocking?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);C->SetCollisionResponseToAllChannels(ECR_Ignore);
        if(Blocking){C->SetCollisionObjectType(ECC_WorldStatic);C->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);C->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);}C->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,Material));Actor->SetActorScale3D(Size/100);Scenery.Add(Actor);return Actor;
    };
    const TCHAR* Road=TEXT("/Game/M11/Materials/MI_Road.MI_Road");const TCHAR* Edge=TEXT("/Game/M11/Materials/MI_RouteEdge.MI_RouteEdge");
    Shape(FVector(5000,-12000,-40),FVector(200000,200000,60),TEXT("/Game/M11/Materials/MI_Concrete.MI_Concrete"));
    Shape(FVector(5000,-3000,-35),FVector(11000,7600,60),TEXT("/Game/M11/Materials/MI_Concrete.MI_Concrete"));
    for(const auto& E:A(Stage,TEXT("entries")))
    {
        auto Route=A(E->AsObject(),TEXT("route_m"));for(int32 I=1;I<Route.Num();++I){FVector From=FLNCampaignBattle::ToWorld(Route[I-1]->AsArray(),1),To=FLNCampaignBattle::ToWorld(Route[I]->AsArray(),1),D=To-From;FVector Normal(-D.Y,D.X,0);Normal.Normalize();Shape((From+To)*.5,FVector(D.Size2D(),600,2),Road,false,D.Rotation().Yaw);for(int32 Side:{-1,1})Shape((From+To)*.5+Normal*Side*306,FVector(D.Size2D(),8,3),Edge,false,D.Rotation().Yaw);}
        FVector P=FLNCampaignBattle::ToWorld(A(E->AsObject(),TEXT("position_m")),120);auto* Label=GetWorld()->SpawnActor<ATextRenderActor>(P,FRotator(55,90,0));Label->GetTextRender()->SetText(FText::FromString(S(E->AsObject(),TEXT("id"))));Label->GetTextRender()->SetWorldSize(100);Scenery.Add(Label);
    }
    for(const auto& V:A(Stage,TEXT("obstacles_m")))
    {
        auto Pair=V->AsArray();if(Pair.Num()<2)continue;
        auto P=FLNCampaignBattle::ToWorld(Pair[0]->AsArray()),Q=FLNCampaignBattle::ToWorld(Pair[1]->AsArray());
        FVector Center=(P+Q)*.5;Center.Z=220;FVector Size(FMath::Abs(Q.X-P.X),FMath::Abs(Q.Y-P.Y),440);
        Shape(Center,Size,TEXT("/Game/M11/Materials/MI_Warehouse.MI_Warehouse"),true);
        Shape(Center+FVector(0,0,235),Size+FVector(35,35,-410),TEXT("/Game/M11/Materials/MI_Roof.MI_Roof"));
        // Surface details have no collision and leave the authored blocker unchanged.
        const float Front=Center.Y+Size.Y*.5f+2;
        Shape(FVector(Center.X,Front,140),FVector(FMath::Min(260.f,Size.X*.5f),8,280),TEXT("/Game/M11/Materials/MI_Shutter.MI_Shutter"));
        Shape(FVector(Center.X,Front+6,305),FVector(FMath::Min(200.f,Size.X*.4f),9,18),TEXT("/Game/M11/Materials/MI_Amber.MI_Amber"));
        for(int32 Side:{-1,1})Shape(FVector(Center.X+Side*(Size.X*.5f-18),Front,220),FVector(22,12,440),TEXT("/Game/M11/Materials/MI_DarkMetal.MI_DarkMetal"));
    }
    FVector Exit=FLNCampaignBattle::ToWorld(A(Stage,TEXT("exit_m")),5);Shape(Exit,FVector(650,25,5),TEXT("/Game/M11/Materials/MI_Cyan.MI_Cyan"));
    return SceneReady;
}
void ALNCampaignGameMode::Log(const FString& E,const FString& D)
{
    FString Clean=D.Replace(TEXT("\r"),TEXT(" ")).Replace(TEXT("\n"),TEXT("; ")).Replace(TEXT("\t"),TEXT(" "));const double Time=Battle?Battle->GetTime():0;
    FString Line=FString::Printf(TEXT("%s\t%d\t%.6f\t%s\t%s\n"),*StageId().ToString(),WaveIndex+1,Time,*E,*Clean);
    if(!LogPath.IsEmpty())FFileHelper::SaveStringToFile(Line,*LogPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    Events.Add(FString::Printf(TEXT("%.1f %s %s"),Time,*E,*Clean));if(Events.Num()>22)Events.RemoveAt(0);UE_LOG(LogTemp,Display,TEXT("LNCAMPAIGN %s"),*Line.TrimEnd());
}
void ALNCampaignGameMode::Screenshot(const FString& Name)
{
    FTimerHandle T;GetWorldTimerManager().SetTimer(T,FTimerDelegate::CreateWeakLambda(this,[this,Name](){const FString P=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/Name);IFileManager::Get().MakeDirectory(*FPaths::GetPath(P),true);FScreenshotRequest::RequestScreenshot(P,true,false,false,FIntRect(),true);}),.15f,false);
}
void ALNCampaignGameMode::FinishAutomation()
{
    if(bFinished)return;bFinished=true;
    WriteAutomationReport();
    FinishEvidence();
    if(bEvidenceRecord){ScheduleEvidenceExit();return;}
    FTimerHandle Q;GetWorldTimerManager().SetTimer(Q,FTimerDelegate::CreateWeakLambda(this,[this](){if(GEngine)GEngine->Exec(GetWorld(),TEXT("QUIT"));}),bRender?1.f:.05f,false);
}
void ALNCampaignGameMode::WriteAutomationReport()
{
    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("stage"),StageId().ToString());R->SetStringField(TEXT("mode"),AutoMode);R->SetNumberField(TEXT("variant"),Variant);R->SetStringField(TEXT("result"),Phase==EPhase::Victory?TEXT("VICTORY"):Phase==EPhase::Defeat?TEXT("DEFEAT"):AutoMode==TEXT("preparation")?TEXT("PREPARATION"):TEXT("ERROR_OR_TIMEOUT"));R->SetNumberField(TEXT("defense"),Defense);R->SetNumberField(TEXT("spawned"),StageSpawned);R->SetNumberField(TEXT("killed"),StageKilled);R->SetNumberField(TEXT("leaked"),StageLeaked);R->SetNumberField(TEXT("session_seconds"),SessionTime);R->SetArrayField(TEXT("waves"),WaveReports);R->SetStringField(TEXT("events"),LogPath);
    if(Profile){R->SetNumberField(TEXT("completed_stages"),Profile->CompletedStages.Num());R->SetNumberField(TEXT("astrites"),Profile->Astrites);R->SetNumberField(TEXT("shell_credits"),Profile->ShellCredits);}
    R->SetBoolField(TEXT("assistance"),bAssistance);R->SetStringField(TEXT("profile_kind"),bReview?TEXT("review"):TEXT("normal"));R->SetStringField(TEXT("config_version"),S(Campaign,TEXT("version")));
    R->SetStringField(TEXT("formation_plan"),AutomationPlanId);
    float HealthScale=1,BatchOffset=0;FParse::Value(FCommandLine::Get(),TEXT("LNEnemyHealthScale="),HealthScale);FParse::Value(FCommandLine::Get(),TEXT("LNBatchOffset="),BatchOffset);
    R->SetNumberField(TEXT("enemy_health_scale"),HealthScale);R->SetNumberField(TEXT("batch_time_offset"),BatchOffset);
    if(Battle&&(Phase==EPhase::Battle||Phase==EPhase::Defeat))
    {
        auto Partial=MakeShared<FJsonObject>();Partial->SetNumberField(TEXT("wave"),WaveIndex+1);Partial->SetNumberField(TEXT("seconds"),Battle->GetTime());Partial->SetNumberField(TEXT("spawned"),Battle->GetSpawned());Partial->SetNumberField(TEXT("killed"),Battle->GetKilled());Partial->SetNumberField(TEXT("leaked"),Battle->GetLeaked());Partial->SetNumberField(TEXT("living_enemies"),Battle->GetLivingEnemies());Partial->SetNumberField(TEXT("task_fraction"),Battle->GetTaskFraction());R->SetObjectField(TEXT("incomplete_wave"),Partial);
        R->SetNumberField(TEXT("spawned"),StageSpawned+Battle->GetSpawned());R->SetNumberField(TEXT("killed"),StageKilled+Battle->GetKilled());R->SetNumberField(TEXT("leaked"),StageLeaked+Battle->GetLeaked());
    }
    FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));const FString Suffix=AutomationPlanId.IsEmpty()?FString():TEXT("-")+AutomationPlanId;const FString P=FPaths::ProjectSavedDir()/TEXT("Telemetry/Campaign")/FString::Printf(TEXT("%s-%s-v%d%s.json"),*StageId().ToString(),*AutoMode,Variant,*Suffix);FFileHelper::SaveStringToFile(Text,*P,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);Log(TEXT("run_complete"),P);
}
void ALNCampaignGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
    FinishEvidence();
    if(Profile&&!bAutomation){if(Phase==EPhase::Battle||Phase==EPhase::Training||Phase==EPhase::Defeat)Profile->Stages.Add(ProgressId(),Checkpoint);else if(Stage.IsValid()&&Phase!=EPhase::Menu&&Phase!=EPhase::Error)SnapshotProgress();SaveProfile();}
    if(Battle){Battle->OnEvent={};Battle->Shutdown();}Super::EndPlay(Reason);
}
