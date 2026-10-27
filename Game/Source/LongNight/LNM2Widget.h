#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "LNM2Widget.generated.h"

class ALNM2GameMode;
class UBorder;
class UCanvasPanel;
class UImage;
class UProgressBar;
class UTextBlock;
class UTexture2D;
class UVerticalBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLNM2ButtonEvent, int32, Action, int32, Index);

/** Indexed, mouse-only buttons preserve viewport keyboard shortcuts. */
UCLASS()
class LONGNIGHT_API ULNM2ActionButton : public UButton
{
    GENERATED_BODY()
public:
    ULNM2ActionButton(const FObjectInitializer& Initializer);
    void SetAction(int32 InAction, int32 InIndex);
    UPROPERTY() FLNM2ButtonEvent OnAction;
private:
    int32 Action = 0;
    int32 Index = 0;
    UFUNCTION() void ExecuteAction();
};

/** Two-lamp preparation and battle HUD for the first authored level. */
UCLASS()
class LONGNIGHT_API ULNM2Widget : public UUserWidget
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
    UPROPERTY() TObjectPtr<UBorder> ConfigurationPanel;
    UPROPERTY() TObjectPtr<UBorder> DebugPanel;
    UPROPERTY() TObjectPtr<UBorder> NoticePanel;
    UPROPERTY() TObjectPtr<UTextBlock> PhaseText;
    UPROPERTY() TObjectPtr<UTextBlock> ModeText;
    UPROPERTY() TObjectPtr<UTextBlock> WaveText;
    UPROPERTY() TObjectPtr<UTextBlock> DefenseText;
    UPROPERTY() TObjectPtr<UTextBlock> TaskText;
    UPROPERTY() TObjectPtr<UTextBlock> IntelText;
    UPROPERTY() TObjectPtr<UTextBlock> BattleStatsText;
    UPROPERTY() TObjectPtr<UTextBlock> NoticeText;
    UPROPERTY() TObjectPtr<UTextBlock> SelectionText;
    UPROPERTY() TObjectPtr<UTextBlock> SelectionDetail;
    UPROPERTY() TObjectPtr<UTextBlock> DebugText;
    UPROPERTY() TObjectPtr<UProgressBar> TaskBar;
    UPROPERTY() TObjectPtr<ULNM2ActionButton> StartButton;
    UPROPERTY() TObjectPtr<ULNM2ActionButton> PauseButton;
    UPROPERTY() TObjectPtr<ULNM2ActionButton> RetryButton;
    UPROPERTY() TObjectPtr<ULNM2ActionButton> ModuleButton;
    UPROPERTY() TObjectPtr<ULNM2ActionButton> AssistanceButton;
    UPROPERTY() TArray<TObjectPtr<ULNM2ActionButton>> BeamButtons;
    UPROPERTY() TArray<TObjectPtr<ULNM2ActionButton>> LampButtons;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> LampDetails;
    UPROPERTY() TArray<TObjectPtr<UProgressBar>> EnergyBars;
    UPROPERTY() TArray<TObjectPtr<ULNM2ActionButton>> RosterButtons;
    UPROPERTY() TArray<TObjectPtr<ULNM2ActionButton>> SeatButtons;
    UPROPERTY() TArray<TObjectPtr<UBorder>> SeatFrames;
    UPROPERTY() TArray<TObjectPtr<UImage>> Portraits;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> PortraitFallbacks;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> SeatNames;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> SeatRoles;
    UPROPERTY() TArray<TObjectPtr<UProgressBar>> HealthBars;
    UPROPERTY() TMap<FName, TObjectPtr<UTexture2D>> PortraitCache;
    TArray<FName> DisplayedIds;
    float RefreshElapsed = 0;

    ALNM2GameMode* GetGame() const;
    UTextBlock* Text(const FString& Value, int32 Size, FLinearColor Color);
    ULNM2ActionButton* Button(const FString& Label, int32 Action, int32 Index = 0, bool Primary = false);
    UBorder* Panel(UCanvasPanel* Root, FVector2D Position, FVector2D Size);
    UProgressBar* Bar(UVerticalBox* Parent, FLinearColor Color, float Height);
    void ButtonRow(UVerticalBox* Parent, const TArray<ULNM2ActionButton*>& Buttons, float Height = 0.f);
    void BuildSeatCards(UCanvasPanel* Root);
    void RefreshPortrait(int32 Index, FName Id);
    void ReturnFocusToViewport();
    UFUNCTION() void HandleAction(int32 Action, int32 Index);
};
