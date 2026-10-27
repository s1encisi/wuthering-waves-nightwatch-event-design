#include "LNCampaignWidget.h"
#include "LNCampaignGameMode.h"
#include "LNCampaignPawn.h"
#include "LNData.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Camera/CameraComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"

void ULNCampaignWidget::RunBindingChecks()
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("LNCampaignUIChecks")))return;
    FString Report=TEXT("UMG button delegate integration checks; isolated automation profile; no OS mouse or keyboard input.\n");
    int32 Checks=0,Failures=0;
    auto Check=[&](bool Value,const TCHAR* Name){++Checks;if(!Value)++Failures;Report+=FString::Printf(TEXT("%s %s\n"),Value?TEXT("PASS"):TEXT("FAIL"),Name);};
    auto Click=[&](ULNCampaignActionButton* Target,const TCHAR* Name)
    {
        const bool Enabled=Target&&Target->GetIsEnabled();Check(Enabled,Name);
        if(Enabled){Target->OnClicked.Broadcast();RefreshFromGame();}
    };
    auto Find=[&](const TCHAR* Label)
    {
        ULNCampaignActionButton* Found=nullptr;
        WidgetTree->ForEachWidget([&](UWidget* Item){if(auto* B=Cast<ULNCampaignActionButton>(Item))if(auto* T=Cast<UTextBlock>(B->GetContent()))if(T->GetText().ToString()==Label)Found=B;});
        return Found;
    };
    auto* Game=GetGame();Check(Game&&Game->IsInMenu(),TEXT("begins_in_isolated_menu"));
    if(Game)
    {
        RefreshFromGame();
        Check(!Game->IsReviewMode()&&StageButtons[0]->GetIsEnabled()&&!StageButtons[1]->GetIsEnabled(),TEXT("normal_stage_buttons_follow_unlocks"));
        Click(ReviewModeButton,TEXT("review_button_enabled"));
        bool AllUnlocked=Game->IsReviewMode();for(ULNCampaignActionButton* B:StageButtons)AllUnlocked&=B->GetIsEnabled();
        Check(AllUnlocked,TEXT("review_button_unlocks_nine_cards"));
        Click(StageButtons[2],TEXT("third_stage_card_enabled"));
        Check(Game->GetSelectedStage()==2&&Game->CanConfigure()&&Game->GetLampCount()==6&&Game->GetAvailableRosterIds().Num()==26,TEXT("third_stage_opens_six_lamps_and_full_roster"));
        auto* VisualTable=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_Visuals.DT_Visuals"));
        int32 MeshesLoaded=0,ClipsLoaded=0;
        for(FName Id:Game->GetAvailableRosterIds())
        {
            const auto* Row=VisualTable?VisualTable->FindRow<FLNVisualRow>(Id,TEXT("Campaign asset verification")):nullptr;
            auto* Mesh=Row?Row->Mesh.LoadSynchronous():nullptr;
            if(!Mesh)continue;++MeshesLoaded;
            const TSoftObjectPtr<UAnimSequence> Clips[]={Row->Idle,Row->Move,Row->Attack,Row->Skill,Row->SkillTwo,Row->Dodge,Row->Hit,Row->Down,Row->Revive};
            for(const auto& Reference:Clips)if(auto* Clip=Reference.LoadSynchronous();Clip&&Clip->GetSkeleton()==Mesh->GetSkeleton())++ClipsLoaded;
        }
        Check(MeshesLoaded==26,TEXT("all_26_normal_models_load_in_running_build"));
        Check(ClipsLoaded==234,TEXT("all_234_action_clips_load_with_matching_skeletons"));
        for(int32 I=0;I<6;++I)Check(LampFrames[I]->GetVisibility()==ESlateVisibility::Visible,TEXT("six_lamp_widgets_visible"));
        Click(LampButtons[5],TEXT("sixth_lamp_button_enabled"));Check(Game->GetSelectedLamp()==5,TEXT("sixth_lamp_selection_dispatched"));
        Click(Cast<ULNCampaignActionButton>(SeatFrames[1]->GetContent()),TEXT("second_seat_button_enabled"));Check(Game->GetSelectedSeat()==1,TEXT("second_seat_selection_dispatched"));
        const FName Original=Game->GetSeatId(5,1);
        int32 Candidate=0;while(Candidate<Game->GetAvailableRosterIds().Num()&&Game->GetAvailableRosterIds()[Candidate]==Original)++Candidate;
        Click(RosterButtons[Candidate],TEXT("candidate_button_enabled"));Check(Game->GetSeatId(5,1)==Game->GetAvailableRosterIds()[Candidate],TEXT("candidate_button_assigns_selected_seat"));
        Click(Find(TEXT("撤回 [Z]")),TEXT("undo_button_enabled"));Check(Game->GetSeatId(5,1)==Original,TEXT("undo_button_restores_assignment"));
        const int32 Module=Game->GetModule(5);Click(ModuleButton,TEXT("module_button_enabled"));Check(Game->GetModule(5)!=Module,TEXT("module_button_changes_selected_lamp"));
        Click(Find(TEXT("撤回 [Z]")),TEXT("module_undo_enabled"));Check(Game->GetModule(5)==Module,TEXT("module_undo_restores_module"));
        for(int32 I=0;I<4&&Game->GetModule(5)!=1;++I)Click(ModuleButton,TEXT("cycle_toward_beam_enabled"));
        const float Yaw=Game->GetBeamYaw(5);Click(BeamButtons[1],TEXT("beam_right_enabled_for_beam"));Check(!FMath::IsNearlyEqual(Yaw,Game->GetBeamYaw(5)),TEXT("beam_button_rotates_direction"));
        Click(Find(TEXT("撤回 [Z]")),TEXT("beam_undo_enabled"));Check(FMath::IsNearlyEqual(Yaw,Game->GetBeamYaw(5)),TEXT("beam_undo_restores_direction"));
        const FString StrategyLabel=Game->GetPriorityLabel(5);Click(PriorityButton,TEXT("priority_button_enabled"));Check(StrategyLabel!=Game->GetPriorityLabel(5),TEXT("priority_button_changes_strategy"));
        Click(Find(TEXT("保存阵容")),TEXT("save_preset_button_enabled"));const int32 PresetModule=Game->GetModule(5);
        Click(ModuleButton,TEXT("preset_mutation_enabled"));Click(Find(TEXT("载入阵容")),TEXT("load_preset_button_enabled"));Check(Game->GetModule(5)==PresetModule,TEXT("preset_button_restores_module"));
        const int32 Target=SwapTarget;const FName BeforeTarget=Game->GetSeatId(Target,0),BeforeSelected=Game->GetSeatId(5,0);
        Click(SwapButton,TEXT("swap_button_enabled"));Check(Game->GetSeatId(5,0)==BeforeTarget&&Game->GetSeatId(Target,0)==BeforeSelected,TEXT("swap_button_exchanges_teams"));
        Click(Find(TEXT("撤回 [Z]")),TEXT("swap_undo_enabled"));Check(Game->GetSeatId(5,0)==BeforeSelected,TEXT("undo_restores_team_swap"));
        const bool Assistance=Game->IsAssistanceEnabled();Click(AssistanceButton,TEXT("mainline_assistance_enabled"));Check(Game->IsAssistanceEnabled()!=Assistance,TEXT("assistance_button_toggles_mode"));
        Click(Find(TEXT("聚焦灯组")),TEXT("focus_button_enabled"));
        auto* Pawn=Cast<ALNCampaignPawn>(GetOwningPlayerPawn());auto* Camera=Pawn?Pawn->FindComponentByClass<UCameraComponent>():nullptr;
        Check(Camera&&FMath::IsNearlyEqual(Camera->FieldOfView,55.f),TEXT("focus_button_sets_close_camera"));
        Click(Find(TEXT("全景")),TEXT("overview_button_enabled"));Check(Camera&&FMath::IsNearlyEqual(Camera->FieldOfView,90.f),TEXT("overview_button_restores_camera"));
        const int32 Wave=Game->GetWaveIndex();Click(ReviewWaveButton,TEXT("review_wave_button_enabled"));Check(Game->GetWaveIndex()!=Wave,TEXT("review_wave_button_advances_selection"));
        Click(HeaderMenuButton,TEXT("return_menu_button_enabled"));Click(StageButtons[6],TEXT("hazard_card_enabled"));
        Check(!AssistanceButton->GetIsEnabled(),TEXT("hazard_disables_assistance"));
        const FString Variant=Game->GetVariantLabel();Click(VariantButton,TEXT("hazard_variant_button_enabled"));Check(Game->GetVariantLabel()!=Variant,TEXT("variant_button_changes_replay_condition"));
        Click(StartButton,TEXT("battle_start_button_enabled"));Check(!Game->CanConfigure()&&!StartButton->GetIsEnabled()&&ConfigurationPanel->GetVisibility()==ESlateVisibility::Collapsed,TEXT("battle_hides_configuration_and_disables_start"));
        const int32 LockedModule=Game->GetModule(Game->GetSelectedLamp());ModuleButton->OnClicked.Broadcast();RefreshFromGame();
        Check(Game->GetModule(Game->GetSelectedLamp())==LockedModule,TEXT("stale_configuration_callback_cannot_change_active_battle"));
        Click(PauseButton,TEXT("pause_button_enabled"));Check(Game->IsPaused(),TEXT("pause_button_pauses_battle"));
        Click(PauseButton,TEXT("resume_button_enabled"));Check(!Game->IsPaused(),TEXT("resume_button_resumes_battle"));
        Click(HeaderMenuButton,TEXT("battle_return_menu_enabled"));Click(NormalModeButton,TEXT("normal_mode_button_enabled"));Click(StageButtons[0],TEXT("first_stage_button_enabled"));
        Check(Game->HasTutorial()&&TutorialPanel->GetVisibility()==ESlateVisibility::Visible,TEXT("normal_first_entry_shows_embedded_training"));
        Click(StartButton,TEXT("tutorial_continue_button_enabled"));Check(!Game->HasTutorial(),TEXT("continue_button_acknowledges_tutorial"));
        Click(StartButton,TEXT("training_start_button_enabled"));Check(!Game->CanConfigure()&&!Game->CanStartOrContinue(),TEXT("start_button_enters_real_training"));
        Game->ReturnToMenu();
    }
    Report+=FString::Printf(TEXT("CHECKS=%d\nFAILURES=%d\n"),Checks,Failures);
    const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Telemetry/Campaign"));IFileManager::Get().MakeDirectory(*Directory,true);
    const bool Saved=FFileHelper::SaveStringToFile(Report,*(Directory/TEXT("ui-binding-checks.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_UI_CHECKS checks=%d failures=%d saved=%d"),Checks,Failures,Saved);
    FPlatformMisc::RequestExitWithStatus(false,Failures||!Saved?1:0);
}
