#include "LNCampaignGameMode.h"
#include "LNCampaignPawn.h"
#include "LNCombatUnit.h"
#include "Engine/Engine.h"
#include "Engine/GameEngine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MovieSceneCapture.h"
#include "MovieSceneCaptureModule.h"
#include "Protocols/VideoCaptureProtocol.h"
#include "AudioMixerBlueprintLibrary.h"
#include "TimerManager.h"

namespace
{
TSharedPtr<FJsonObject> CopyEvidenceJson(const TSharedPtr<FJsonObject>& Source)
{
    FString Text;FJsonSerializer::Serialize(Source.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
    TSharedPtr<FJsonObject> Copy;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Copy);return Copy;
}
bool SaveEvidenceJson(const TSharedPtr<FJsonObject>& Object,const FString& Path)
{
    FString Text;FJsonSerializer::Serialize(Object.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
    return FFileHelper::SaveStringToFile(Text,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}

// Explicit, isolated automation evidence. Only the separately identified stress
// fixture changes in-memory test values; recordings use the normal battle rules.
void ALNCampaignGameMode::BeginEvidence()
{
    if(!bEvidence||!bAutomation)return;
    EvidenceStartWall=EvidenceLastWall=FPlatformTime::Seconds();
    EvidenceDirectory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Evidence/Campaign")/
        FString::Printf(TEXT("%s-%u"),*FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")),FPlatformProcess::GetCurrentProcessId()));
    IFileManager::Get().MakeDirectory(*EvidenceDirectory,true);
    EvidenceFrames=TEXT("frame,wall_seconds,frame_ms,delta_ms,stage,wave,phase,lamps,friendly_seats,friendly_alive,living_enemies,battle_seconds\n");
    EvidenceFrames.Reserve(4*1024*1024);
    FParse::Value(FCommandLine::Get(),TEXT("LNEvidenceSeconds="),EvidenceLimitSeconds);
    bEvidenceScreenshots=FParse::Param(FCommandLine::Get(),TEXT("LNEvidenceScreenshots"));
    if(bEvidenceStress)
    {
        // A separately identified performance fixture, never a played stage.
        // Only deep-copied in-memory data changes; source JSON remains intact.
        if(bDemo||bEvidenceRecord||bAutoAll||Formation.Num()!=6||Stage->GetArrayField(TEXT("entries")).Num()<4||Stage->GetStringField(TEXT("kind"))!=TEXT("hazard"))
        {Phase=EPhase::Error;Notice=TEXT("压力测试要求独立六灯险境场景，不允许录像或全关模式");FinishAutomation();return;}
        const FString OriginalStage=StageId().ToString();
        Campaign=CopyEvidenceJson(Campaign);Stage=CopyEvidenceJson(Stage);
        for(const auto& Character:Campaign->GetArrayField(TEXT("characters")))Character->AsObject()->SetNumberField(TEXT("hp"),1000000);
        Campaign->GetObjectField(TEXT("enemies"))->GetObjectField(TEXT("hunter"))->SetNumberField(TEXT("hp"),1000000);
        Stage->SetStringField(TEXT("id"),TEXT("stress_6lamp_18friendly_32hunter"));Stage->SetStringField(TEXT("name"),TEXT("六灯32敌隔离压力测试"));
        auto Wave=MakeShared<FJsonObject>();Wave->SetStringField(TEXT("id"),TEXT("stress_wave"));Wave->SetStringField(TEXT("name"),TEXT("独立性能夹具"));
        TArray<TSharedPtr<FJsonValue>> Batches;
        for(int32 Index=0;Index<4;++Index)
        {
            auto Batch=MakeShared<FJsonObject>();Batch->SetStringField(TEXT("id"),FString::Printf(TEXT("stress_b%d"),Index+1));
            Batch->SetStringField(TEXT("entry"),Stage->GetArrayField(TEXT("entries"))[Index]->AsObject()->GetStringField(TEXT("id")));
            Batch->SetStringField(TEXT("enemy"),TEXT("hunter"));Batch->SetNumberField(TEXT("time"),0);Batch->SetNumberField(TEXT("count"),8);
            Batches.Add(MakeShared<FJsonValueObject>(Batch));
        }
        Wave->SetArrayField(TEXT("batches"),Batches);Wave->SetArrayField(TEXT("tasks"),{});Wave->SetArrayField(TEXT("faults"),{});Wave->SetNumberField(TEXT("enemy_total"),32);
        Stage->SetArrayField(TEXT("waves"),{MakeShared<FJsonValueObject>(Wave)});Stage->SetNumberField(TEXT("wave_count"),1);Stage->SetNumberField(TEXT("enemy_total"),32);
        WaveIndex=0;AutomationPlan.Reset();AutomationPlanId.Empty();PreparedPlanKey.Empty();TutorialText.Empty();TutorialQueue.Empty();
        if(EvidenceLimitSeconds<=0)EvidenceLimitSeconds=90;
        auto Overrides=MakeShared<FJsonObject>();Overrides->SetStringField(TEXT("source_stage"),OriginalStage);
        Overrides->SetStringField(TEXT("scope"),TEXT("isolated performance fixture; no balance, victory, reward or progression evidence"));
        Overrides->SetNumberField(TEXT("friendly_config_hp"),1000000);Overrides->SetNumberField(TEXT("hunter_base_hp_before_authored_level_scaling"),1000000);
        Overrides->SetNumberField(TEXT("entries"),4);Overrides->SetNumberField(TEXT("hunters_per_entry"),8);
        Overrides->SetStringField(TEXT("removed_from_fixture"),TEXT("original waves, tasks, scheduled faults"));
        Overrides->SetStringField(TEXT("retained"),TEXT("source geometry, teams, modules, attacks, cooldowns, energy, relay, AI, real projectiles, spawn spacing and 32-enemy cap"));
        Overrides->SetBoolField(TEXT("source_data_files_modified"),false);
        if(!SaveEvidenceJson(Overrides,EvidenceDirectory/TEXT("fixture-overrides.json"))||!SaveEvidenceJson(Stage,EvidenceDirectory/TEXT("fixture-stage.json"))||!SaveEvidenceJson(Campaign,EvidenceDirectory/TEXT("fixture-campaign.json")))
        {Phase=EPhase::Error;Notice=TEXT("压力测试输入证据保存失败");FinishAutomation();return;}
        Log(TEXT("test_state_injection"),TEXT("fixture=stress_6lamp_18friendly_32hunter; memory_only=1; friendly_hp=1000000; hunter_base_hp=1000000; batches=4x8hunters_at0; tasks=0; faults=0; not_victory_evidence=1"));
        PrepareWave();
    }
    int32 Lamp=INDEX_NONE;
    if(FParse::Value(FCommandLine::Get(),TEXT("LNEvidenceLamp="),Lamp)&&Lamp>=0&&Lamp<GetLampCount())
    {
        EvidenceFocusLamp=Lamp;
    }
    Log(TEXT("evidence_begin"),FString::Printf(TEXT("path=%s; demo=%d; record=%d; fixed_step=%d; benchmark=%d; limit_seconds=%.2f; simulation_injection=%d"),
        *EvidenceDirectory,bDemo,bEvidenceRecord,FApp::UseFixedTimeStep(),FApp::IsBenchmarking(),EvidenceLimitSeconds,bEvidenceStress));
}

void ALNCampaignGameMode::TickEvidence(float Delta)
{
    if(!bEvidence||EvidenceDirectory.IsEmpty()||bEvidenceClosed||bFinished)return;
    const double Now=FPlatformTime::Seconds(),Wall=Now-EvidenceStartWall,FrameMs=(Now-EvidenceLastWall)*1000;
    EvidenceLastWall=Now;EvidenceSimSeconds+=Delta;
    // Pawn BeginPlay restores the overview after the GameMode begins. Apply the
    // requested evidence close-up once that initialization has completed.
    if(EvidenceFocusLamp!=INDEX_NONE&&EvidenceSimSeconds>=.3)
    {
        if(auto* PC=GetWorld()->GetFirstPlayerController())if(auto* Pawn=Cast<ALNCampaignPawn>(PC->GetPawn()))
        {SelectLamp(EvidenceFocusLamp);Pawn->FocusSelectedLamp();EvidenceFocusLamp=INDEX_NONE;}
    }
    const int32 Living=Battle?Battle->GetLivingEnemies():0;
    int32 FriendlyAlive=0;if(Battle)for(int32 Lamp=0;Lamp<Battle->GetLampCount();++Lamp)for(int32 Seat=0;Seat<3;++Seat)
        if(auto* Unit=Battle->GetUnit(Lamp,Seat))if(Unit->IsAlive())++FriendlyAlive;
    EvidenceFrames+=FString::Printf(TEXT("%d,%.6f,%.6f,%.6f,%s,%d,%d,%d,%d,%d,%d,%.6f\n"),
        EvidenceFrame++,Wall,FrameMs,Delta*1000.,*StageId().ToString(),WaveIndex+1,int32(Phase),GetLampCount(),GetLampCount()*3,FriendlyAlive,Living,Battle?Battle->GetTime():0);

    // Delay until the viewport exists. UMovieSceneCapture uses real-time capture
    // by default; unlike LevelCapture it does not set a fixed simulation step.
    if(bEvidenceRecord&&!EvidenceCapture&&EvidenceSimSeconds>=.25)
    {
        auto* GameEngine=Cast<UGameEngine>(GEngine);
        if(GameEngine&&GameEngine->SceneViewport.IsValid())
        {
            // Editor initialization loads this module automatically. Cooked
            // games must explicitly run StartupModule so OnPreExit owns the
            // active-capture singleton's teardown before UObject/RHI shutdown.
            IMovieSceneCaptureModule::Get();
            UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_CAPTURE_MODULE_READY"));
            EvidenceCapture=NewObject<UMovieSceneCapture>(this);
            EvidenceCapture->OnCaptureFinished().AddWeakLambda(this,[this]()
            {
                bEvidenceCaptureFinalized=true;EvidenceCaptureFinalizedWall=FPlatformTime::Seconds();
                Log(TEXT("recording_finalized"),TEXT("source=OnCaptureFinished; video_protocol_and_writer_finalized=1"));
                UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_CAPTURE_FINALIZED %s"),*EvidenceDirectory);
            });
            auto& Settings=EvidenceCapture->Settings;
            Settings.OutputDirectory.Path=EvidenceDirectory;Settings.OutputFormat=TEXT("LongNight-AutoDemo");
            Settings.bOverwriteExisting=false;Settings.FrameRate=FFrameRate(30,1);
            Settings.bUseCustomFrameRate=true;Settings.CustomFrameRate=FFrameRate(30,1);
            Settings.Resolution=FCaptureResolution(1440,900);Settings.bCinematicEngineScalability=false;
            Settings.bCinematicMode=false;Settings.bShowHUD=true;Settings.bEnableTextureStreaming=true;
            EvidenceCapture->SetImageCaptureProtocolType(UVideoCaptureProtocol::StaticClass());
            EvidenceCapture->Initialize(GameEngine->SceneViewport);
            if(auto* Video=Cast<UVideoCaptureProtocol>(EvidenceCapture->GetImageCaptureProtocol()))
            {Video->bUseCompression=true;Video->CompressionQuality=85;}
            EvidenceCapture->StartCapture();
            // The generic movie capture mutes its image pass; this run records
            // audio concurrently, so restore the app's own audio immediately.
            FApp::SetVolumeMultiplier(1);FApp::SetUnfocusedVolumeMultiplier(1);
            // UE's silent-submix auto-disable returns before RecordingData.Append.
            // Keep the real silent buffers during this recording; otherwise the
            // captured WAV would concatenate only audible portions of the run.
            if(auto* KeepSilence=IConsoleManager::Get().FindConsoleVariable(TEXT("au.NeverDisableSubmixes")))
            {EvidencePreviousNeverDisableSubmixes=KeepSilence->GetInt();KeepSilence->Set(1,ECVF_SetByCode);}
            UAudioMixerBlueprintLibrary::StartRecordingOutput(this,EvidenceLimitSeconds>0?EvidenceLimitSeconds:300.f);
            EvidenceRecordStartWall=Now;
            Log(TEXT("recording_started"),FString::Printf(TEXT("source=live_game_viewport; audio=same_run_master_submix; strategy=real_time; operator=automation; keep_real_silent_buffers=%d"),EvidencePreviousNeverDisableSubmixes!=INDEX_NONE));
        }
    }

    // Camera-only tour; selected lamp does not change the battle simulation.
    if(bDemo&&GetLampCount()>0)
    {
        const int32 View=int32(EvidenceSimSeconds/18.);
        if(View!=EvidenceView)
        {
            EvidenceView=View;
            if(auto* PC=GetWorld()->GetFirstPlayerController())if(auto* Pawn=Cast<ALNCampaignPawn>(PC->GetPawn()))
            {
                if(View%3==0)Pawn->FocusOverview();
                else{SelectLamp((View-View/3-1)%GetLampCount());Pawn->FocusSelectedLamp();}
            }
        }
    }
    if(bEvidenceScreenshots&&EvidenceScreenshotIndex<8&&EvidenceSimSeconds>=2+EvidenceScreenshotIndex*20.)
    {
        const FString Name=FString::Printf(TEXT("CampaignEvidence-%s-%02d-%s.png"),*FPaths::GetCleanFilename(EvidenceDirectory),EvidenceScreenshotIndex++,*StageId().ToString());
        Screenshot(Name);Log(TEXT("evidence_screenshot_requested"),Name);
    }
    if(EvidenceLimitSeconds>0&&EvidenceSimSeconds>=EvidenceLimitSeconds)
    {
        Log(TEXT("evidence_duration_complete"),TEXT("The observation duration ended; this is not a victory assertion."));
        FinishAutomation();
    }
}

void ALNCampaignGameMode::FinishEvidence()
{
    if(!bEvidence||EvidenceDirectory.IsEmpty()||bEvidenceClosed)return;bEvidenceClosed=true;
    const double Now=FPlatformTime::Seconds();
    if(EvidenceCapture)
    {
        EvidenceCapture->FinalizeWhenReady();
        UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("LongNight-AutoDemo"),EvidenceDirectory);
        if(EvidencePreviousNeverDisableSubmixes!=INDEX_NONE)
            if(auto* KeepSilence=IConsoleManager::Get().FindConsoleVariable(TEXT("au.NeverDisableSubmixes")))KeepSilence->Set(EvidencePreviousNeverDisableSubmixes,ECVF_SetByCode);
    }
    const bool Saved=FFileHelper::SaveStringToFile(EvidenceFrames,*(EvidenceDirectory/TEXT("frames.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    auto Report=MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("source"),TEXT("actual_engine_tick_wall_clock"));
    Report->SetStringField(TEXT("runtime_host"),WITH_EDITOR?TEXT("UnrealEditor -game"):TEXT("Windows packaged executable"));
    Report->SetStringField(TEXT("operator"),TEXT("automation; not a human playtest or OS click verification"));
    Report->SetStringField(TEXT("frame_csv"),EvidenceDirectory/TEXT("frames.csv"));Report->SetBoolField(TEXT("frames_saved"),Saved);
    Report->SetStringField(TEXT("telemetry_tsv"),LogPath);Report->SetStringField(TEXT("formation_plan"),AutomationPlanId);
    Report->SetStringField(TEXT("save_slot"),Slot());Report->SetBoolField(TEXT("demo"),bDemo);
    Report->SetBoolField(TEXT("stress_fixture"),bEvidenceStress);
    Report->SetBoolField(TEXT("record_requested"),bEvidenceRecord);Report->SetBoolField(TEXT("record_started"),EvidenceRecordStartWall>0);
    Report->SetBoolField(TEXT("record_kept_real_silent_audio_buffers"),EvidencePreviousNeverDisableSubmixes!=INDEX_NONE);
    Report->SetBoolField(TEXT("fixed_time_step"),FApp::UseFixedTimeStep());Report->SetBoolField(TEXT("benchmark"),FApp::IsBenchmarking());
    Report->SetBoolField(TEXT("nullrhi_requested"),FParse::Param(FCommandLine::Get(),TEXT("nullrhi")));
    Report->SetBoolField(TEXT("render_offscreen"),FParse::Param(FCommandLine::Get(),TEXT("RenderOffscreen")));
    Report->SetNumberField(TEXT("wall_seconds"),Now-EvidenceStartWall);Report->SetNumberField(TEXT("simulation_seconds"),EvidenceSimSeconds);
    Report->SetNumberField(TEXT("record_wall_seconds"),EvidenceRecordStartWall>0?Now-EvidenceRecordStartWall:0);
    Report->SetNumberField(TEXT("warmup_seconds_for_analysis"),5);Report->SetNumberField(TEXT("frames"),EvidenceFrame);
    FIntPoint Size=GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport?GEngine->GameViewport->Viewport->GetSizeXY():FIntPoint::ZeroValue;
    Report->SetNumberField(TEXT("viewport_width"),Size.X);Report->SetNumberField(TEXT("viewport_height"),Size.Y);
    if(auto* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))Report->SetNumberField(TEXT("max_fps_cvar"),Variable->GetFloat());
    if(auto* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))Report->SetNumberField(TEXT("vsync_cvar"),Variable->GetInt());
    Report->SetStringField(TEXT("end_phase"),GetPhaseLabel());Report->SetStringField(TEXT("stage"),StageId().ToString());
    FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
    const bool ReportSaved=FFileHelper::SaveStringToFile(Text,*(EvidenceDirectory/TEXT("run.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_EVIDENCE_%s %s"),Saved&&ReportSaved?TEXT("SAVED"):TEXT("WRITE_FAILED"),*EvidenceDirectory);
}

void ALNCampaignGameMode::ScheduleEvidenceExit()
{
    if(!EvidenceCapture)
    {
        UE_LOG(LogTemp,Error,TEXT("CAMPAIGN_CAPTURE_EXIT_FAILED: recording requested but no capture was initialized"));
        FPlatformMisc::RequestExitWithStatus(false,2,TEXT("Campaign capture was not initialized"));return;
    }
    const double RequestedAt=FPlatformTime::Seconds();
    auto Poll=MakeShared<FTimerHandle>();
    GetWorldTimerManager().SetTimer(*Poll,FTimerDelegate::CreateWeakLambda(this,[this,Poll,RequestedAt]()
    {
        const double Now=FPlatformTime::Seconds(),Waited=Now-RequestedAt;
        // Require actual video finalization, plus the existing three-second
        // allowance for asynchronous WAV export and one second after finalization.
        if(bEvidenceCaptureFinalized&&Waited>=3&&Now-EvidenceCaptureFinalizedWall>=1)
        {
            UWorld* ExitWorld=GetWorld();
            Log(TEXT("recording_exit_ready"),FString::Printf(TEXT("finalized=1; waited_seconds=%.3f; audio_export_grace_seconds=3"),Waited));
            // Clearing the currently executing timer may destroy this lambda's
            // captures. Everything used afterward must already live on the stack.
            GetWorldTimerManager().ClearTimer(*Poll);
            if(GEngine)GEngine->Exec(ExitWorld,TEXT("QUIT"));
            else FPlatformMisc::RequestExitWithStatus(false,0,TEXT("Campaign capture finalized"));
        }
        else if(Waited>=30)
        {
            Log(TEXT("recording_finalize_timeout"),FString::Printf(TEXT("finalized=%d; waited_seconds=%.3f; limit_seconds=30"),bEvidenceCaptureFinalized,Waited));
            UE_LOG(LogTemp,Error,TEXT("CAMPAIGN_CAPTURE_EXIT_FAILED: capture did not finish its finalization/grace period within 30 seconds"));
            GetWorldTimerManager().ClearTimer(*Poll);
            FPlatformMisc::RequestExitWithStatus(false,2,TEXT("Campaign capture finalization timeout"));
        }
    }),.1f,true);
}
