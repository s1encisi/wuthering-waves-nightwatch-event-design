#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "LNCampaignWidget.generated.h"

class ALNCampaignGameMode;
class UBorder;
class UCanvasPanel;
class UImage;
class UProgressBar;
class UScrollBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLNCampaignButtonEvent, int32, Action, int32, Index);

UCLASS()
class LONGNIGHT_API ULNCampaignActionButton : public UButton
{
    GENERATED_BODY()
public:
    ULNCampaignActionButton(const FObjectInitializer& Initializer);
    void SetAction(int32 InAction, int32 InIndex);
    UPROPERTY() FLNCampaignButtonEvent OnAction;
private:
    int32 Action=0;
    int32 Index=0;
    UFUNCTION() void ExecuteAction();
};

/** Native UMG front end for campaign flow; gameplay remains owned by the GameMode. */
UCLASS()
class LONGNIGHT_API ULNCampaignWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void RefreshFromGame();
    bool IsPointerOverPanel() const;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
private:
    UPROPERTY() TArray<TObjectPtr<UBorder>> Panels;
    UPROPERTY() TArray<TObjectPtr<UBorder>> BattlePanels;
    UPROPERTY() TObjectPtr<UBorder> MenuPanel;
    UPROPERTY() TObjectPtr<UBorder> ConfigurationPanel;
    UPROPERTY() TObjectPtr<UBorder> RecapPanel;
    UPROPERTY() TObjectPtr<UBorder> TutorialPanel;
    UPROPERTY() TObjectPtr<UBorder> NoticePanel;
    UPROPERTY() TObjectPtr<UBorder> DebugPanel;
    UPROPERTY() TObjectPtr<UTextBlock> ModeText;
    UPROPERTY() TObjectPtr<UTextBlock> StageText;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseText;
    UPROPERTY() TObjectPtr<UTextBlock> DefenseText;
    UPROPERTY() TObjectPtr<UTextBlock> TaskText;
    UPROPERTY() TObjectPtr<UTextBlock> IntelText;
    UPROPERTY() TObjectPtr<UTextBlock> StatusText;
    UPROPERTY() TObjectPtr<UTextBlock> NoticeText;
    UPROPERTY() TObjectPtr<UTextBlock> TutorialText;
    UPROPERTY() TObjectPtr<UTextBlock> SelectionText;
    UPROPERTY() TObjectPtr<UTextBlock> SelectionDetail;
    UPROPERTY() TObjectPtr<UTextBlock> RosterCaption;
    UPROPERTY() TObjectPtr<UTextBlock> RecapText;
    UPROPERTY() TObjectPtr<UTextBlock> RewardText;
    UPROPERTY() TObjectPtr<UTextBlock> RecapRewardText;
    UPROPERTY() TObjectPtr<UTextBlock> DebugText;
    UPROPERTY() TObjectPtr<UTextBlock> MissionText;
    UPROPERTY() TObjectPtr<UProgressBar> TaskBar;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> NormalModeButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> ReviewModeButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> HeaderMenuButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> StartButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> PauseButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> RetryButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> AssistanceButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> ModuleButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> PriorityButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> SwapTargetButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> SwapButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> ReviewWaveButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> VariantButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> MenuClaimButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> RecapClaimButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> PreparationReplayButton;
    UPROPERTY() TObjectPtr<ULNCampaignActionButton> RecapReplayButton;
    UPROPERTY() TArray<TObjectPtr<ULNCampaignActionButton>> StageButtons;
    UPROPERTY() TArray<TObjectPtr<UBorder>> LampFrames;
    UPROPERTY() TArray<TObjectPtr<ULNCampaignActionButton>> LampButtons;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> LampDetails;
    UPROPERTY() TArray<TObjectPtr<UProgressBar>> EnergyBars;
    UPROPERTY() TArray<TObjectPtr<ULNCampaignActionButton>> RosterButtons;
    UPROPERTY() TArray<TObjectPtr<ULNCampaignActionButton>> BeamButtons;
    UPROPERTY() TArray<TObjectPtr<UBorder>> SeatFrames;
    UPROPERTY() TArray<TObjectPtr<UImage>> Portraits;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> PortraitFallbacks;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> SeatNames;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> SeatRoles;
    UPROPERTY() TArray<TObjectPtr<UProgressBar>> HealthBars;
    UPROPERTY() TMap<FName,TObjectPtr<UTexture2D>> PortraitCache;
    TArray<FName> DisplayedIds;
    int32 SwapTarget=1;
    float RefreshElapsed=0;
    float BindingCheckDelay=0;
    bool bBindingChecksStarted=false;
    void RunBindingChecks();
    int32 SceneCheckStep=0,SceneCheckStage=0,SceneCheckCount=0,SceneCheckFailures=0,SceneCheckFrames=0;
    float SceneCheckDelay=0;
    bool bSceneChecksFinished=false;
    FString SceneCheckText,SceneCheckDirectory;
    TArray<TSharedPtr<class FJsonValue>> SceneCheckRows,SceneCheckSnapshots;
    void TickSceneChecks(float DeltaSeconds);
    void SceneCheck(bool Passed,const FString& Name);
    void InspectScene(const FString& Label);
    void FinishSceneChecks();

    ALNCampaignGameMode* GetGame() const;
    UTextBlock* Text(const FString& Value,int32 Size,FLinearColor Color);
    ULNCampaignActionButton* Button(const FString& Label,int32 Action,int32 Index=0,bool Primary=false);
    void SetLabel(ULNCampaignActionButton* Button,const FString& Label,FLinearColor Color);
    UBorder* Panel(UCanvasPanel* Root,FVector2D Position,FVector2D Size,bool BattleOnly=false);
    UProgressBar* Bar(UVerticalBox* Parent,FLinearColor Color,float Height);
    void ButtonRow(UVerticalBox* Parent,const TArray<ULNCampaignActionButton*>& Buttons,float Height=28.f);
    UScrollBox* Scroll(UVerticalBox* Parent,float Height=0.f);
    void BuildMenu(UCanvasPanel* Root);
    void BuildBattle(UCanvasPanel* Root);
    void BuildSeats(UCanvasPanel* Root);
    void RefreshPortrait(int32 Index,FName Id);
    void NormalizeSwapTarget(int32 Selected,int32 Count);
    UFUNCTION() void HandleAction(int32 Action,int32 Index);
};
