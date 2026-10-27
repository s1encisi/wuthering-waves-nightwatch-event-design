#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "LNRuntimeWidget.generated.h"

class ALNGameMode;
class UBorder;
class UCanvasPanel;
class UImage;
class UProgressBar;
class UTextBlock;
class UVerticalBox;
class UTexture2D;

/** Mouse-only HUD buttons leave gameplay shortcuts on the viewport. */
UCLASS()
class LONGNIGHT_API ULNHudButton : public UButton
{
    GENERATED_BODY()
public:
    ULNHudButton(const FObjectInitializer& ObjectInitializer);
};

/** Compact player HUD. Verbose simulation evidence is only visible in F1 mode. */
UCLASS()
class LONGNIGHT_API ULNRuntimeWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void RefreshFromGame();
    bool IsPointerOverPanel() const;

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
    UPROPERTY() TObjectPtr<UBorder> HeaderPanel;
    UPROPERTY() TObjectPtr<UBorder> LampPanel;
    UPROPERTY() TObjectPtr<UBorder> TaskPanel;
    UPROPERTY() TObjectPtr<UBorder> ActionPanel;
    UPROPERTY() TObjectPtr<UBorder> RosterPanel;
    UPROPERTY() TObjectPtr<UBorder> DebugPanel;
    UPROPERTY() TObjectPtr<UBorder> ControlsPanel;
    UPROPERTY() TObjectPtr<UBorder> RelayNoticePanel;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseText;
    UPROPERTY() TObjectPtr<UTextBlock> DefenseText;
    UPROPERTY() TObjectPtr<UTextBlock> BatchText;
    UPROPERTY() TObjectPtr<UTextBlock> OutcomeText;
    UPROPERTY() TObjectPtr<UTextBlock> LampText;
    UPROPERTY() TObjectPtr<UTextBlock> CurrentText;
    UPROPERTY() TObjectPtr<UTextBlock> NextText;
    UPROPERTY() TObjectPtr<UTextBlock> RelayText;
    UPROPERTY() TObjectPtr<UTextBlock> FaultText;
    UPROPERTY() TObjectPtr<UTextBlock> EnergyText;
    UPROPERTY() TObjectPtr<UTextBlock> TaskText;
    UPROPERTY() TObjectPtr<UTextBlock> TaskStatusText;
    UPROPERTY() TObjectPtr<UTextBlock> ConfigurationText;
    UPROPERTY() TObjectPtr<UTextBlock> DetailText;
    UPROPERTY() TObjectPtr<UTextBlock> EventText;
    UPROPERTY() TObjectPtr<UTextBlock> StartLabel;
    UPROPERTY() TObjectPtr<UTextBlock> PauseLabel;
    UPROPERTY() TObjectPtr<UTextBlock> RelayNoticeText;
    UPROPERTY() TObjectPtr<UProgressBar> EnergyBar;
    UPROPERTY() TObjectPtr<UProgressBar> TaskBar;
    UPROPERTY() TObjectPtr<UButton> StartButton;
    UPROPERTY() TObjectPtr<UButton> PauseButton;
    UPROPERTY() TObjectPtr<UButton> ModuleButton;
    UPROPERTY() TObjectPtr<UButton> SeatsButton;
    UPROPERTY() TObjectPtr<UButton> ResetButton;
    UPROPERTY() TArray<TObjectPtr<UBorder>> SeatCards;
    UPROPERTY() TArray<TObjectPtr<UImage>> SeatPortraits;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> SeatPortraitFallbacks;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> SeatNames;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> SeatRoles;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> SeatStates;
    UPROPERTY() TArray<TObjectPtr<UProgressBar>> SeatHealthBars;
    UPROPERTY() TMap<FName, TObjectPtr<UTexture2D>> PortraitCache;
    TArray<FName> DisplayedSeatIds;
    float RefreshElapsed = 0.f;
    float NoticeSeconds = 0.f;
    int32 LastCurrentSeat = INDEX_NONE;
    float LastEnergy = -1.f;
    float LastFaultSeconds = -1.f;
    float LastTaskProgress = -1.f;

    ALNGameMode* GetGame() const;
    UTextBlock* MakeText(const FString& Text, int32 Size, FLinearColor Color);
    UButton* MakeButton(const FString& Label, bool bPrimary = false);
    UBorder* MakePanel(UCanvasPanel* Root, FVector2D Position, FVector2D Size);
    UProgressBar* MakeProgressBar(UVerticalBox* Parent, FLinearColor Color, float Height);
    void AddButtonRow(UVerticalBox* Parent, UButton* Left, UButton* Right);
    void BuildSeatCards(UCanvasPanel* Root);
    void RefreshPortrait(int32 Seat, FName CharacterId, const FString& Name);
    void ShowNotice(const FString& Text, FLinearColor Color);
    void ReturnFocusToViewport();

    UFUNCTION() void OnStart();
    UFUNCTION() void OnPause();
    UFUNCTION() void OnModule();
    UFUNCTION() void OnSeats();
    UFUNCTION() void OnReset();
};
