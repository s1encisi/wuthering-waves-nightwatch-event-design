#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LNCampaignData.h"
#include "LNCampaignBattle.h"
#include "LNCampaignGameMode.generated.h"

class FJsonObject;
class FLNCampaignBattle;
class ALNCombatUnit;
class ULNCampaignAudioComponent;
class UMovieSceneCapture;

UCLASS()
class LONGNIGHT_API ALNCampaignGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ALNCampaignGameMode();
    virtual ~ALNCampaignGameMode() override;
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool IsInMenu() const;
    bool IsReviewMode() const {return bReview;}
    void SetReviewMode(bool Review);
    void ReturnToMenu();
    int32 GetStageCount() const;
    FString GetStageName(int32 Index) const;
    bool IsStageUnlocked(int32 Index) const;
    void SelectStage(int32 Index);
    int32 GetSelectedStage() const {return StageIndex;}
    int32 GetWaveIndex() const {return WaveIndex;}
    int32 GetWaveCount() const;
    void CycleReviewWave();
    void CycleVariant();
    FString GetVariantLabel() const;
    void LoadRecommendedFormation();
    int32 GetLampCount() const;
    void SelectLamp(int32 Index);
    int32 GetSelectedLamp() const {return SelectedLamp;}
    void SelectSeat(int32 Index);
    int32 GetSelectedSeat() const {return SelectedSeat;}
    FName GetSeatId(int32 Lamp,int32 Seat) const;
    FString GetSeatName(int32 Lamp,int32 Seat) const;
    float GetSeatHealthFraction(int32 Lamp,int32 Seat) const;
    int32 GetCurrentSeat(int32 Lamp) const;
    FString GetLampText(int32 Lamp) const;
    FString GetLampSummary(int32 Lamp) const;
    FString GetPriorityLabel(int32 Lamp) const;
    FString GetSeatRole(int32 Lamp,int32 Seat) const;
    float GetEnergyFraction(int32 Lamp) const;
    int32 GetModule(int32 Lamp) const;
    float GetBeamYaw(int32 Lamp) const;
    FVector GetLampPosition(int32 Lamp) const;
    const TArray<FName>& GetAvailableRosterIds() const {return AvailableRoster;}
    FString GetRosterName(int32 Index) const;
    FString GetRosterDescription(int32 Index) const;
    FString GetRosterFocusDescription(int32 Index) const;
    void PlayUiCue(bool Back=false);
    void AssignCharacter(int32 Index);
    void CycleModule();
    void RotateSeats();
    void SwapTeams(int32 TargetLamp);
    void RotateBeam(float Degrees);
    void CyclePriority();
    void UndoPreparation();
    void SaveFormationPreset();
    void LoadFormationPreset();
    void ToggleAssistance();
    bool IsAssistanceEnabled() const {return bAssistance;}
    FString GetPhaseLabel() const;
    FString GetStatusText() const;
    FString GetIntelText() const;
    FString GetTaskText() const;
    float GetTaskFraction() const;
    int32 GetDefense() const;
    FString GetNotice() const {return Notice;}
    FString GetTutorialText() const {return TutorialText;}
    bool HasTutorial() const {return !TutorialText.IsEmpty();}
    void AcknowledgeTutorial();
    void ReplayTutorials();
    void StartOrContinue();
    FString GetStartLabel() const;
    bool CanStartOrContinue() const;
    void PauseToggle();
    bool IsPaused() const {return bPaused;}
    bool CanConfigure() const;
    bool IsDefeated() const;
    bool IsComplete() const;
    bool IsDebugVisible() const {return bDebug;}
    void RetryWave();
    void DebugToggle();
    FString GetEventText() const;
    FString GetRewardSummary() const;
    bool CanClaimRewards() const;
    void ClaimRewards();
    FString GetRecapText() const {return RecapText;}
    FVector GetCameraCenter() const;
private:
    enum class EPhase { Menu, Preparation, TrainingReady, Training, Battle, WaveComplete, Victory, Defeat, Error };
    EPhase Phase=EPhase::Menu;
    TSharedPtr<FJsonObject> Campaign,Stage,AutomationPlan;
    TArray<TSharedPtr<FJsonObject>> StageDefinitions;
    TMap<FName,TSharedPtr<FJsonObject>> Characters;
    TUniquePtr<FLNCampaignBattle> Battle;
    UPROPERTY() TObjectPtr<ULNCampaignSaveGame> Profile;
    UPROPERTY() TObjectPtr<ULNCampaignAudioComponent> SoundFeedback;
    UPROPERTY() TObjectPtr<UMovieSceneCapture> EvidenceCapture;
    UPROPERTY() TArray<TObjectPtr<AActor>> Scenery;
    UPROPERTY() TArray<FLNCampaignFormation> Formation;
    TArray<FName> AvailableRoster;
    TArray<FLNCampaignProgress> UndoStack;
    FLNCampaignProgress Checkpoint;
    TArray<FString> Events;
    TArray<FName> RewardIds;
    TArray<TPair<FName,FString>> TutorialQueue;
    TArray<TSharedPtr<class FJsonValue>> WaveReports;
    FString Notice,TutorialText,RecapText,LogPath;
    FName CurrentTutorial;
    int32 StageIndex=0,WaveIndex=0,SelectedLamp=0,SelectedSeat=0,Variant=0;
    int32 Defense=100,StageSpawned=0,StageKilled=0,StageLeaked=0;
    double AutoDelay=0,SessionTime=0;
    bool bReview=false,bAssistance=false,bPaused=false,bDebug=false,bAutomation=false,bAutoPlay=false,bRender=false,bFinished=false;
    bool bTrainingDone=false,bScreenshotTaken=false;
    bool bAutoAll=false,bTutorialPaused=false,bTutorialReplay=false;
    int32 AutoStage=-1;
    FString AutoMode,AutomationPlanId,PreparedPlanKey;
    bool bEvidence=false,bDemo=false,bEvidenceRecord=false,bEvidenceClosed=false,bEvidenceScreenshots=false,bEvidenceStress=false;
    bool bEvidenceCaptureFinalized=false;
    double EvidenceStartWall=0,EvidenceLastWall=0,EvidenceSimSeconds=0,EvidenceLimitSeconds=0,EvidenceRecordStartWall=0;
    double EvidenceCaptureFinalizedWall=0;
    int32 EvidenceView=-1,EvidenceFrame=0,EvidenceScreenshotIndex=0,EvidenceFocusLamp=INDEX_NONE;
    int32 EvidencePreviousNeverDisableSubmixes=INDEX_NONE;
    FString EvidenceDirectory,EvidenceFrames;
    bool LoadCampaign();
    void LoadProfile();
    bool SaveProfile();
    FString Slot() const;
    FName StageId() const;
    FName ProgressId() const;
    bool ValidateFormation(const TArray<FLNCampaignFormation>& Value) const;
    bool BuildStage();
    void PrepareWave();
    void PushUndo();
    void ApplyFormation();
    void SnapshotProgress();
    void OnBattleEvent(const FString& Event,const FString& Detail);
    void SetTutorial(const FString& Key,const FString& Text);
    void ShowPreparationTutorial();
    void CompleteStage();
    void Log(const FString& Event,const FString& Detail);
    void FinishAutomation();
    void WriteAutomationReport();
    void RunStateChecks();
    void RunSaveProbe(bool Write);
    void ApplyAutomationPlan();
    void Screenshot(const FString& Name);
    void BeginEvidence();
    void TickEvidence(float Delta);
    void FinishEvidence();
    void ScheduleEvidenceExit();
};
