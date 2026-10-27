#include "LNCampaignGameMode.h"
#include "LNCampaignBattle.h"
#include "LNCombatUnit.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "CollisionQueryParams.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
FString TestString(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key)
{FString Value;if(Object)Object->TryGetStringField(Key,Value);return Value;}
TArray<TSharedPtr<FJsonValue>> TestArray(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key)
{const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;return Object&&Object->TryGetArrayField(Key,Values)?*Values:TArray<TSharedPtr<FJsonValue>>{};}
bool SameFormation(const TArray<FLNCampaignFormation>& A,const TArray<FLNCampaignFormation>& B)
{
    if(A.Num()!=B.Num())return false;
    for(int32 I=0;I<A.Num();++I)
        if(A[I].Seats!=B[I].Seats||A[I].Module!=B[I].Module||A[I].Priority!=B[I].Priority||!FMath::IsNearlyEqual(A[I].BeamYaw,B[I].BeamYaw,.001f))return false;
    return true;
}
TArray<FLNCampaignFormation> AuthoredFormation(const TSharedPtr<FJsonObject>& Definition)
{
    TArray<FLNCampaignFormation> Result;
    for(const auto& Value:TestArray(Definition,TEXT("lamps")))
    {
        const auto Lamp=Value->AsObject();FLNCampaignFormation Item;
        Item.Module=Lamp->GetNumberField(TEXT("module"));Item.BeamYaw=Lamp->GetNumberField(TEXT("yaw_degrees"));Item.Priority=Lamp->GetNumberField(TEXT("priority"));
        for(const auto& Id:TestArray(Lamp,TEXT("team")))Item.Seats.Add(FName(Id->AsString()));
        Result.Add(Item);
    }
    return Result;
}
}

void ALNCampaignGameMode::RunStateChecks()
{
    FString Report=TEXT("STATE TESTS: explicit state injection; not evidence of a played victory.\n");
    int32 Failures=0,Checks=0;
    auto Check=[&](bool Value,const TCHAR* Name){++Checks;Report+=FString::Printf(TEXT("%s %s\n"),Value?TEXT("PASS"):TEXT("FAIL"),Name);if(!Value)++Failures;};
    auto Inject=[&](const TCHAR* Detail){Log(TEXT("test_state_injection"),Detail);Report+=FString(TEXT("INJECT "))+Detail+TEXT("\n");};
    auto DrainTutorials=[&](){for(int32 I=0;I<32&&HasTutorial();++I)AcknowledgeTutorial();};
    auto FreshProfile=[&]()
    {
        ReturnToMenu();
        Profile=Cast<ULNCampaignSaveGame>(UGameplayStatics::CreateSaveGameObject(ULNCampaignSaveGame::StaticClass()));
        Profile->PlayerId=TEXT("campaign-state-fixture");Profile->ActivityStartUtc=FDateTime::UtcNow().GetTicks();
        bReview=false;bPaused=false;bTutorialPaused=false;
        Inject(TEXT("new isolated automation profile"));
    };

    // Only calculate production slot names. All I/O below uses unique fixture names.
    const bool OriginalAutomation=bAutomation,OriginalReview=bReview;
    bAutomation=false;bReview=false;const FString NormalSlot=Slot();
    bReview=true;const FString ReviewSlot=Slot();
    bAutomation=true;const FString AutomationSlot=Slot();
    bReview=OriginalReview;bAutomation=OriginalAutomation;
    Check(NormalSlot!=ReviewSlot&&NormalSlot!=AutomationSlot&&ReviewSlot!=AutomationSlot,TEXT("normal_review_automation_slot_names_are_distinct"));
    const FString FixtureSuffix=TEXT("__test_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const TArray<FString> FixtureSlots={NormalSlot+FixtureSuffix,ReviewSlot+FixtureSuffix,AutomationSlot+FixtureSuffix};
    bool SlotWrites=true,SlotReads=true,SlotCleanup=true;
    for(int32 I=0;I<FixtureSlots.Num();++I)
    {
        auto* Fixture=Cast<ULNCampaignSaveGame>(UGameplayStatics::CreateSaveGameObject(ULNCampaignSaveGame::StaticClass()));
        Fixture->PlayerId=FString::Printf(TEXT("isolated-profile-%d"),I);Fixture->Astrites=I+1;
        SlotWrites&=UGameplayStatics::SaveGameToSlot(Fixture,FixtureSlots[I],0);
    }
    for(int32 I=0;I<FixtureSlots.Num();++I)
    {
        auto* Loaded=Cast<ULNCampaignSaveGame>(UGameplayStatics::LoadGameFromSlot(FixtureSlots[I],0));
        SlotReads&=Loaded&&Loaded->PlayerId==FString::Printf(TEXT("isolated-profile-%d"),I)&&Loaded->Astrites==I+1;
        SlotCleanup&=UGameplayStatics::DeleteGameInSlot(FixtureSlots[I],0);
    }
    Check(SlotWrites&&SlotReads,TEXT("three_fixture_profiles_persist_without_cross_contamination"));
    Check(SlotCleanup,TEXT("temporary_profile_fixtures_removed"));

    FreshProfile();
    Check(IsStageUnlocked(0)&&!IsStageUnlocked(1)&&!IsStageUnlocked(6),TEXT("normal_progression_starts_at_first_main"));
    Inject(TEXT("completion flags main_01 then main_06 for unlock predicates"));
    Profile->CompletedStages.AddUnique(TEXT("main_01"));Check(IsStageUnlocked(1)&&!IsStageUnlocked(2),TEXT("main_completion_unlocks_next"));
    Profile->CompletedStages.AddUnique(TEXT("main_06"));Check(IsStageUnlocked(6)&&IsStageUnlocked(7)&&IsStageUnlocked(8),TEXT("main6_unlocks_three_hazards"));
    bReview=true;Check(IsStageUnlocked(5),TEXT("review_can_select_any_stage"));
    const int64 StartUtc=Profile->ActivityStartUtc;
    Inject(TEXT("activity start 22 days ago"));Profile->ActivityStartUtc=FDateTime::UtcNow().GetTicks()-FTimespan::FromDays(22).GetTicks();
    bReview=false;SelectStage(0);Check(IsInMenu(),TEXT("expired_normal_profile_cannot_enter_stage"));
    bReview=true;SelectStage(0);Check(CanConfigure(),TEXT("review_can_enter_after_activity_expiry"));
    Profile->ActivityStartUtc=StartUtc;

    // Query the live map's collision after scaled mesh bodies have been created.
    const auto EntryDefinitions=TestArray(Stage,TEXT("entries"));
    const auto ObstacleDefinitions=TestArray(Stage,TEXT("obstacles_m"));
    if(!EntryDefinitions.IsEmpty()&&!ObstacleDefinitions.IsEmpty())
    {
        const auto Route=TestArray(EntryDefinitions[0]->AsObject(),TEXT("route_m"));
        const auto Rectangle=ObstacleDefinitions[0]->AsArray();
        if(Route.Num()>=2&&Rectangle.Num()==2)
        {
            const FVector From=FLNCampaignBattle::ToWorld(Route[0]->AsArray(),65),To=FLNCampaignBattle::ToWorld(Route[1]->AsArray(),65);
            auto* Probe=GetWorld()->SpawnActor<ALNCombatUnit>(From,FRotator::ZeroRotator);
            FCollisionQueryParams Query(SCENE_QUERY_STAT(CampaignMapRegression),false,Probe);FHitResult RouteHit,WallHit;
            const auto Capsule=FCollisionShape::MakeCapsule(30,64);
            const FVector A=FLNCampaignBattle::ToWorld(Rectangle[0]->AsArray(),65),B=FLNCampaignBattle::ToWorld(Rectangle[1]->AsArray(),65);
            const FVector WallFrom(FMath::Min(A.X,B.X)-100,(A.Y+B.Y)*.5,65),WallTo(FMath::Max(A.X,B.X)+100,(A.Y+B.Y)*.5,65);
            auto DescribePhysics=[&](const TCHAR* When)
            {
                for(AActor* SceneActor:Scenery)if(auto* MeshActor=Cast<AStaticMeshActor>(SceneActor))
                {
                    auto* Component=MeshActor->GetStaticMeshComponent();if(Component->GetCollisionResponseToChannel(ECC_Pawn)!=ECR_Block)continue;
                    auto* Body=Component->GetBodyInstance();const bool ValidBody=Body&&Body->IsValidBodyInstance();FHitResult DirectHit;
                    const bool DirectBlocked=ValidBody&&Component->SweepComponent(DirectHit,WallFrom,WallTo,FQuat::Identity,Capsule,false);
                    Report+=FString::Printf(TEXT("GEOMETRY %s actor=%s registered=%d physics_created=%d async_creating=%d mesh_compiling=%d body_valid=%d profile=%s collision=%d pawn=%d visibility=%d actor_collision=%d location=%s scale=%s body_transform=%s direct_sweep=%d\n"),
                        When,*SceneActor->GetName(),Component->IsRegistered(),Component->IsPhysicsStateCreated(),Component->IsAsyncCreatePhysicsStateRunning(),Component->GetStaticMesh()&&Component->GetStaticMesh()->IsCompiling(),ValidBody,
                        *Component->GetCollisionProfileName().ToString(),int32(Component->GetCollisionEnabled()),int32(Component->GetCollisionResponseToChannel(ECC_Pawn)),int32(Component->GetCollisionResponseToChannel(ECC_Visibility)),SceneActor->GetActorEnableCollision(),
                        *Component->GetComponentLocation().ToString(),*Component->GetComponentScale().ToString(),ValidBody?*Body->GetUnrealWorldTransform().ToString():TEXT("not_created"),DirectBlocked);
                }
            };
            DescribePhysics(TEXT("before_sync"));
#if WITH_EDITOR
            // A newly loaded editor mesh may still be compiling at frame zero;
            // its component intentionally has no physics state until that ends.
            TArray<UStaticMesh*> SceneMeshes;
            for(AActor* SceneActor:Scenery)if(auto* MeshActor=Cast<AStaticMeshActor>(SceneActor))
                if(UStaticMesh* Mesh=MeshActor->GetStaticMeshComponent()->GetStaticMesh())SceneMeshes.AddUnique(Mesh);
            FStaticMeshCompilingManager::Get().FinishCompilation(SceneMeshes);
#endif
            // State tests run synchronously inside BeginPlay, before the first
            // scene tick. Flush only pending body creation, without stepping the
            // simulation or changing production collision profiles/settings.
            if(auto* PhysicsScene=GetWorld()->GetPhysicsScene())
            {PhysicsScene->ProcessDeferredCreatePhysicsState();PhysicsScene->ProcessAsyncPhysicsStateJobs(true);}
            DescribePhysics(TEXT("after_sync"));
            const bool RouteBlocked=GetWorld()->SweepSingleByChannel(RouteHit,From,To,FQuat::Identity,ECC_Pawn,Capsule,Query);
            Check(!RouteBlocked,TEXT("main1_real_capsule_can_traverse_entry_to_first_route_point"));
            const bool WallBlocked=GetWorld()->SweepSingleByChannel(WallHit,WallFrom,WallTo,FQuat::Identity,ECC_Pawn,Capsule,Query);
            FHitResult VisibilityHit;const bool VisibleWall=GetWorld()->LineTraceSingleByChannel(VisibilityHit,WallFrom,WallTo,ECC_Visibility,Query);
            Report+=FString::Printf(TEXT("GEOMETRY world_sweep=%d visibility_trace=%d from=%s to=%s sweep_actor=%s\n"),WallBlocked,VisibleWall,*WallFrom.ToString(),*WallTo.ToString(),WallHit.GetActor()?*WallHit.GetActor()->GetName():TEXT("none"));
            Check(WallBlocked&&WallHit.bBlockingHit,TEXT("main1_real_capsule_is_blocked_by_warehouse_geometry"));
            if(Probe)Probe->Destroy();
        }
        else Check(false,TEXT("main1_geometry_fixture_has_route_and_warehouse"));
    }
    else Check(false,TEXT("main1_geometry_definitions_exist"));

    Check(Formation.Num()==2&&AvailableRoster.Num()==9&&ValidateFormation(Formation),TEXT("stage1_two_lamps_nine_pool"));
    const auto Initial=Formation;
    if(Formation.Num()>=2)
    {
        auto Bad=Formation;Bad[1].Seats[0]=Bad[0].Seats[0];Check(!ValidateFormation(Bad),TEXT("same_person_cannot_occupy_two_lamps"));
        const FName SourceId=Formation[1].Seats[0],OldId=Formation[0].Seats[0];
        SelectLamp(0);SelectSeat(0);AssignCharacter(AvailableRoster.IndexOfByKey(SourceId));
        Check(Formation[0].Seats[0]==SourceId&&Formation[1].Seats[0]==OldId&&ValidateFormation(Formation),TEXT("assigned_character_exchanges_occupied_seats"));
        UndoPreparation();Check(SameFormation(Formation,Initial),TEXT("undo_restores_character_exchange"));
        const int32 ModuleBefore=Formation[0].Module;
        CycleModule();Check(Formation[0].Module!=ModuleBefore&&ValidateFormation(Formation),TEXT("module_cycle_uses_stage_unlocked_pool"));
        const auto AfterModule=Formation;
        if(Formation[0].Module==1)
        {
            RotateBeam(15);Check(!FMath::IsNearlyEqual(Formation[0].BeamYaw,AfterModule[0].BeamYaw),TEXT("beam_rotation_changes_selected_beam"));
            UndoPreparation();Check(SameFormation(Formation,AfterModule),TEXT("undo_restores_beam_direction"));
        }
        else Check(false,TEXT("stage1_second_unlocked_module_is_beam"));
        UndoPreparation();Check(SameFormation(Formation,Initial),TEXT("undo_restores_module"));
        RotateBeam(15);Check(SameFormation(Formation,Initial),TEXT("nonbeam_module_ignores_direction_input"));
        auto ExpectedSwap=Formation;Swap(ExpectedSwap[0],ExpectedSwap[1]);SwapTeams(1);
        Check(SameFormation(Formation,ExpectedSwap),TEXT("team_swap_includes_seats_module_yaw_and_priority"));
        UndoPreparation();Check(SameFormation(Formation,Initial),TEXT("undo_restores_full_team_swap"));
        ToggleAssistance();SaveFormationPreset();ToggleAssistance();CyclePriority();LoadFormationPreset();
        Check(bAssistance&&SameFormation(Formation,Initial),TEXT("preset_restores_assistance_and_complete_formation"));
    }
    const auto BeforeBattle=Formation;StartOrContinue();CycleModule();RotateSeats();AssignCharacter(0);RotateBeam(15);CyclePriority();
    Check(Phase==EPhase::Battle&&SameFormation(Formation,BeforeBattle),TEXT("battle_configuration_locked"));
    PauseToggle();const double BeforeTime=Battle?Battle->GetTime():-1,BeforeCountdown=Battle?Battle->GetCountdown():-1;Tick(1.f);
    Check(bPaused&&Battle&&Battle->GetTime()==BeforeTime&&Battle->GetCountdown()==BeforeCountdown,TEXT("pause_freezes_battle_and_countdown"));PauseToggle();
    ReturnToMenu();SelectStage(1);
    const auto Stage2Initial=Formation;
    Check(Formation.Num()==4&&UndoStack.IsEmpty(),TEXT("new_stage_starts_with_empty_undo_history"));
    UndoPreparation();Check(CanConfigure()&&SameFormation(Formation,Stage2Initial),TEXT("undo_cannot_restore_previous_stage_formation"));

    ReturnToMenu();SelectStage(2);
    FName FormA,FormB;
    for(int32 I=0;I<AvailableRoster.Num()&&FormA.IsNone();++I)for(int32 J=I+1;J<AvailableRoster.Num();++J)
        if(TestString(Characters.FindRef(AvailableRoster[I]),TEXT("person_id"))==TestString(Characters.FindRef(AvailableRoster[J]),TEXT("person_id")))
        {FormA=AvailableRoster[I];FormB=AvailableRoster[J];break;}
    Check(!FormA.IsNone()&&!FormB.IsNone(),TEXT("full_roster_contains_authored_alternate_person_form"));
    if(!FormA.IsNone()&&Formation.Num()>=2)
    {
        SelectLamp(0);SelectSeat(0);AssignCharacter(AvailableRoster.IndexOfByKey(FormA));
        auto Bad=Formation;Bad[1].Seats[0]=FormB;
        Check(!ValidateFormation(Bad),TEXT("alternate_forms_still_share_one_person_identity"));
        AssignCharacter(AvailableRoster.IndexOfByKey(FormB));
        Check(Formation[0].Seats[0]==FormB&&ValidateFormation(Formation),TEXT("selected_person_can_change_to_alternate_form"));
        UndoPreparation();Check(Formation[0].Seats[0]==FormA&&ValidateFormation(Formation),TEXT("undo_restores_original_person_form"));
    }

    FreshProfile();SelectStage(0);
    Check(Phase==EPhase::TrainingReady&&CurrentTutorial==TEXT("t01_training"),TEXT("first_entry_shows_training_before_deployment"));
    bool DeployQueued=false;for(const auto& Item:TutorialQueue)DeployQueued|=Item.Key==TEXT("t01_deploy");
    Check(!DeployQueued&&!Profile->TutorialSeen.Contains(TEXT("t01_deploy")),TEXT("deployment_is_not_shown_or_queued_before_training"));
    AcknowledgeTutorial();Check(Phase==EPhase::TrainingReady&&!HasTutorial()&&!Profile->TutorialSeen.Contains(TEXT("t01_training")),TEXT("acknowledging_intro_does_not_complete_or_skip_training"));
    StartOrContinue();Check(Phase==EPhase::Training&&Battle&&Battle->GetLampCount()==1,TEXT("training_starts_real_single_lamp_service"));
    Inject(TEXT("t01_training completion flag to isolate tutorial transition; not a relay success"));
    Profile->TutorialSeen.AddUnique(TEXT("t01_training"));PrepareWave();
    Check(CanConfigure()&&CurrentTutorial==TEXT("t01_deploy"),TEXT("deployment_tutorial_follows_training_completion"));
    DrainTutorials();PrepareWave();Check(!HasTutorial(),TEXT("acknowledged_preparation_tutorial_is_not_repeated"));
    const auto SeenBeforeReplay=Profile->TutorialSeen;const auto ClaimsBeforeReplay=Profile->RewardClaims;
    const int32 AstritesBeforeReplay=Profile->Astrites,ShellsBeforeReplay=Profile->ShellCredits;
    ReplayTutorials();Check(HasTutorial(),TEXT("preparation_can_replay_current_stage_tutorials"));DrainTutorials();
    Check(Profile->TutorialSeen==SeenBeforeReplay&&Profile->RewardClaims==ClaimsBeforeReplay&&Profile->Astrites==AstritesBeforeReplay&&Profile->ShellCredits==ShellsBeforeReplay,TEXT("tutorial_replay_preserves_seen_flags_and_rewards"));

    ReturnToMenu();Inject(TEXT("main_02 completed for main_03 tutorial fixture"));Profile->CompletedStages.AddUnique(TEXT("main_02"));SelectStage(2);DrainTutorials();StartOrContinue();
    // Normal runs pause tutorials; disable the automation exemption only around
    // dispatch (which performs no save), then restore it before acknowledgement.
    bAutomation=false;OnBattleEvent(TEXT("arrow_intercepted"),TEXT("TEST synthetic event; no arrow was fired"));bAutomation=true;
    Check(CurrentTutorial==TEXT("t03_reflect")&&HasTutorial()&&!bPaused,TEXT("combat_phase_event_matches_and_pause_false_keeps_simulation_running"));
    DrainTutorials();
    Inject(TEXT("SetTutorial with default pause=true while in battle"));bAutomation=false;SetTutorial(TEXT("test_pause_true"),TEXT("Synthetic pause policy fixture"));bAutomation=true;
    Check(bPaused&&bTutorialPaused,TEXT("pause_true_tutorial_pauses_battle"));AcknowledgeTutorial();
    Check(!bPaused&&!bTutorialPaused,TEXT("tutorial_ack_restores_battle_running_state"));
    ReturnToMenu();Inject(TEXT("main_05 completed and main_06 final wave for completion tutorial fixture"));Profile->CompletedStages.AddUnique(TEXT("main_05"));SelectStage(5);DrainTutorials();
    WaveIndex=GetWaveCount()-1;Phase=EPhase::Victory;OnBattleEvent(TEXT("stage_complete"),TEXT("TEST synthetic completion event; not played victory"));
    Check(CurrentTutorial==TEXT("t06_finish"),TEXT("complete_phase_event_shows_final_handover_tutorial"));DrainTutorials();

    FreshProfile();bReview=true;SelectStage(0);
    const auto ResumeFormation=Formation;
    Inject(TEXT("wave 1 complete with defense 73; exercise menu checkpoint transition"));WaveIndex=0;Defense=73;Phase=EPhase::WaveComplete;
    ReturnToMenu();const auto* NextProgress=Profile->Stages.Find(TEXT("main_01"));
    Check(NextProgress&&NextProgress->WaveIndex==1&&NextProgress->Defense==73,TEXT("wave_complete_menu_exit_keeps_next_wave_checkpoint"));
    SelectStage(0);Check(CanConfigure()&&WaveIndex==1&&Defense==73&&SameFormation(Formation,ResumeFormation),TEXT("reenter_resumes_next_wave_with_defense_and_formation"));
    const auto RetryFormation=Formation;const int32 RetryDefense=Defense;
    StartOrContinue();Inject(TEXT("defeat state with depleted defense; exercise checkpoint restore"));Defense=0;Phase=EPhase::Defeat;RetryWave();
    Check(CanConfigure()&&WaveIndex==1&&Defense==RetryDefense&&SameFormation(Formation,RetryFormation),TEXT("defeat_retry_restores_wave_start_checkpoint"));
    Check(Profile->RewardClaims.IsEmpty()&&Profile->Astrites==0&&Profile->ShellCredits==0,TEXT("retry_does_not_grant_rewards"));

    Inject(TEXT("main_01 completion with assistance enabled; reward condition fixture"));bAssistance=true;CompleteStage();ClaimRewards();
    Check(Profile->Astrites==80&&Profile->ShellCredits==40000&&Profile->RewardClaims.Num()==1,TEXT("assisted_first_clear_grants_same_base_reward"));
    CompleteStage();ClaimRewards();Check(Profile->Astrites==80&&Profile->ShellCredits==40000&&Profile->RewardClaims.Num()==1,TEXT("repeat_clear_cannot_duplicate_first_clear_reward"));
    ReturnToMenu();SelectStage(6);Inject(TEXT("hazard_01 base completion for shared variant reward fixture"));CompleteStage();ClaimRewards();
    const int32 BeforeVariantAstrites=Profile->Astrites,BeforeVariantShells=Profile->ShellCredits,BeforeVariantClaims=Profile->RewardClaims.Num();
    ReturnToMenu();SelectStage(6);CycleVariant();Check(Variant==1&&GetVariantLabel()==TEXT("撤离镜像"),TEXT("hazard_variant_available"));
    CyclePriority();const auto VariantFormation=Formation;const FName VariantProgressKey=ProgressId();
    Inject(TEXT("hazard_01 variant wave 1 complete with defense 61; test variant checkpoint restoration"));
    WaveIndex=0;Defense=61;Phase=EPhase::WaveComplete;ReturnToMenu();
    const auto* VariantCheckpoint=Profile->Stages.Find(VariantProgressKey);
    Check(VariantProgressKey!=TEXT("hazard_01")&&VariantCheckpoint&&VariantCheckpoint->WaveIndex==1&&VariantCheckpoint->Defense==61,TEXT("variant_checkpoint_is_saved_separately_from_base_stage"));
    SelectStage(6);CycleVariant();
    Check(CanConfigure()&&Variant==1&&WaveIndex==1&&Defense==61&&SameFormation(Formation,VariantFormation),TEXT("reenter_variant_restores_wave_defense_and_full_formation"));
    Inject(TEXT("hazard_01 variant completion; no real combat"));CompleteStage();ClaimRewards();
    Check(Profile->Astrites==BeforeVariantAstrites&&Profile->ShellCredits==BeforeVariantShells&&Profile->RewardClaims.Num()==BeforeVariantClaims,TEXT("variant_shares_base_first_clear_reward"));
    Inject(TEXT("all nine completion flags for reward budget and idempotency"));
    for(const auto& Definition:StageDefinitions)Profile->CompletedStages.AddUnique(FName(*TestString(Definition,TEXT("id"))));
    ClaimRewards();Check(Profile->Astrites==600&&Profile->ShellCredits==300000&&Profile->RewardClaims.Num()==10,TEXT("all_ten_reward_conditions_match_budget"));
    ClaimRewards();Check(Profile->Astrites==600&&Profile->ShellCredits==300000&&!CanClaimRewards(),TEXT("duplicate_claim_is_idempotent"));
    ReturnToMenu();SelectStage(6);
    FString Invariants;if(Battle){const bool Passed=Battle->RunInvariantChecks(Invariants);Report+=Invariants;Check(Passed,TEXT("battle_invariants"));}else Check(false,TEXT("battle_invariants_service_exists"));

    Report+=FString::Printf(TEXT("CHECKS=%d\nFAILURES=%d\n"),Checks,Failures);
    const bool Wrote=FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectSavedDir()/TEXT("Telemetry/Campaign/state-checks.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("%s"),*Report);if(!Wrote)UE_LOG(LogTemp,Error,TEXT("Could not save campaign state-check report"));
    bFinished=true;if(GEngine)GEngine->Exec(GetWorld(),TEXT("QUIT"));
}

void ALNCampaignGameMode::RunSaveProbe(bool Write)
{
    FString Report=TEXT("CROSS PROCESS SAVE TEST: injected checkpoint, not a played victory.\n");int32 Failures=0;
    auto Check=[&](bool Value,const TCHAR* Name){Report+=FString::Printf(TEXT("%s %s\n"),Value?TEXT("PASS"):TEXT("FAIL"),Name);if(!Value)++Failures;};
    Log(TEXT("test_state_injection"),Write?TEXT("cross-process main_02 wave 2, defense 73, authored formation, assistance fixture"):TEXT("read prior cross-process fixture and enter stage through SelectStage"));
    const auto Expected=AuthoredFormation(StageDefinitions[1]);
    if(Write)
    {
        SelectStage(1);Check(CanConfigure()&&SameFormation(Formation,Expected),TEXT("write_uses_real_authored_main2_formation"));
        Profile->PlayerId=TEXT("campaign-cross-process-fixture-v1");Profile->CompletedStages.AddUnique(TEXT("main_01"));Profile->TutorialSeen.AddUnique(TEXT("t01_training"));
        WaveIndex=1;Defense=73;bAssistance=true;PrepareWave();SnapshotProgress();SaveFormationPreset();ClaimRewards();
        Check(SaveProfile(),TEXT("fixture_saved_to_automation_slot"));
        Check(Profile->Astrites==80&&Profile->ShellCredits==40000,TEXT("write_reward_uses_claim_api"));
    }
    else
    {
        const auto* Saved=Profile->Stages.Find(TEXT("main_02"));
        Check(Profile->PlayerId==TEXT("campaign-cross-process-fixture-v1"),TEXT("read_loads_previous_process_fixture"));
        Check(Saved&&Saved->WaveIndex==1&&Saved->Defense==73&&Saved->Assistance&&SameFormation(Saved->Formation,Expected),TEXT("serialized_checkpoint_contains_valid_full_formation"));
        Check(Profile->CompletedStages.Contains(TEXT("main_01"))&&Profile->RewardClaims.Contains(TEXT("main_01_first_clear"))&&Profile->Astrites==80&&Profile->ShellCredits==40000&&Profile->TutorialSeen.Contains(TEXT("t01_training")),TEXT("serialized_completion_reward_and_tutorial_flags_restored"));
        SelectStage(1);
        Check(CanConfigure()&&WaveIndex==1&&Defense==73&&bAssistance&&SameFormation(Formation,Expected),TEXT("stage_entry_actually_resumes_cross_process_checkpoint"));
        ToggleAssistance();CyclePriority();LoadFormationPreset();
        Check(bAssistance&&SameFormation(Formation,Expected),TEXT("cross_process_preset_restores_assistance_and_all_formation_fields"));
        const int32 BeforeAstrites=Profile->Astrites,BeforeShells=Profile->ShellCredits;ClaimRewards();
        Check(Profile->Astrites==BeforeAstrites&&Profile->ShellCredits==BeforeShells,TEXT("reward_cannot_be_reclaimed_after_process_restart"));
    }
    Report+=FString::Printf(TEXT("FAILURES=%d\n"),Failures);
    const bool Wrote=FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectSavedDir()/TEXT("Telemetry/Campaign")/(AutoMode+TEXT(".txt"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("%s"),*Report);if(!Wrote)UE_LOG(LogTemp,Error,TEXT("Could not save campaign save-probe report"));
    bFinished=true;if(GEngine)GEngine->Exec(GetWorld(),TEXT("QUIT"));
}
