#include "LNRuntimeWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "LNGameMode.h"
#include "Styling/CoreStyle.h"

namespace LNUI
{
    const FLinearColor Panel(0.013f, 0.025f, 0.037f, 0.96f);
    const FLinearColor Text(0.90f, 0.94f, 0.94f, 1.f);
    const FLinearColor Muted(0.57f, 0.68f, 0.71f, 1.f);
    const FLinearColor Gold(1.f, 0.73f, 0.33f, 1.f);
    const FLinearColor Cyan(0.32f, 0.83f, 0.80f, 1.f);
    const FLinearColor Red(1.f, 0.36f, 0.30f, 1.f);
    const FLinearColor Edge(0.10f, 0.19f, 0.22f, 1.f);
}

ULNHudButton::ULNHudButton(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    InitIsFocusable(false);
}

UTextBlock* ULNRuntimeWidget::MakeText(const FString& Text, int32 Size, FLinearColor Color)
{
    UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
    Block->SetText(FText::FromString(Text));
    Block->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), static_cast<float>(Size)));
    Block->SetColorAndOpacity(FSlateColor(Color));
    Block->SetAutoWrapText(true);
    // The engine composite font includes DroidSansFallback for Chinese in cooked builds.
    Block->SetVisibility(ESlateVisibility::HitTestInvisible);
    return Block;
}

UButton* ULNRuntimeWidget::MakeButton(const FString& Label, bool bPrimary)
{
    UButton* Button = WidgetTree->ConstructWidget<ULNHudButton>();
    FButtonStyle Style;
    Style.SetNormal(FSlateColorBrush(bPrimary ? FLinearColor(0.35f, 0.21f, 0.075f, 1.f) : FLinearColor(0.042f, 0.09f, 0.11f, 1.f)));
    Style.SetHovered(FSlateColorBrush(bPrimary ? FLinearColor(0.52f, 0.33f, 0.11f, 1.f) : FLinearColor(0.07f, 0.18f, 0.20f, 1.f)));
    Style.SetPressed(FSlateColorBrush(FLinearColor(0.10f, 0.27f, 0.28f, 1.f)));
    Style.SetDisabled(FSlateColorBrush(FLinearColor(0.025f, 0.042f, 0.05f, 1.f)));
    Style.SetNormalPadding(FMargin(5.f, 9.f));
    Style.SetPressedPadding(FMargin(5.f, 10.f, 5.f, 8.f));
    Button->SetStyle(Style);
    UTextBlock* LabelText = MakeText(Label, 12, bPrimary ? LNUI::Gold : LNUI::Text);
    LabelText->SetJustification(ETextJustify::Center);
    Button->AddChild(LabelText);
    return Button;
}

UBorder* ULNRuntimeWidget::MakePanel(UCanvasPanel* Root, FVector2D Position, FVector2D Size)
{
    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
    Panel->SetBrush(FSlateColorBrush(LNUI::Panel));
    Panel->SetPadding(FMargin(16.f));
    Panel->SetClipping(EWidgetClipping::ClipToBounds);
    UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
    PanelSlot->SetPosition(Position);
    PanelSlot->SetSize(Size);
    return Panel;
}

UProgressBar* ULNRuntimeWidget::MakeProgressBar(UVerticalBox* Parent, FLinearColor Color, float Height)
{
    UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>();
    FProgressBarStyle Style;
    Style.SetBackgroundImage(FSlateColorBrush(FLinearColor(0.045f, 0.082f, 0.10f, 1.f)));
    Style.SetFillImage(FSlateColorBrush(FLinearColor::White));
    Bar->SetWidgetStyle(Style);
    Bar->SetFillColorAndOpacity(Color);
    Bar->SetPercent(0.f);
    Bar->SetVisibility(ESlateVisibility::HitTestInvisible);
    USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
    Size->SetHeightOverride(Height);
    Size->SetContent(Bar);
    Parent->AddChildToVerticalBox(Size);
    return Bar;
}

void ULNRuntimeWidget::AddButtonRow(UVerticalBox* Parent, UButton* Left, UButton* Right)
{
    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
    UHorizontalBoxSlot* LeftSlot = Row->AddChildToHorizontalBox(Left);
    LeftSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    LeftSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
    UHorizontalBoxSlot* RightSlot = Row->AddChildToHorizontalBox(Right);
    RightSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    RightSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
    Parent->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
}

void ULNRuntimeWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(false);
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HUDRoot"));
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    WidgetTree->RootWidget = Root;

    HeaderPanel = MakePanel(Root, FVector2D(24.f, 20.f), FVector2D(1392.f, 82.f));
    auto* HeaderSlot = Cast<UCanvasPanelSlot>(HeaderPanel->Slot);
    HeaderSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
    HeaderSlot->SetOffsets(FMargin(24.f, 20.f, 24.f, 82.f));
    UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
    HeaderPanel->SetContent(Header);
    USizeBox* TitleSize = WidgetTree->ConstructWidget<USizeBox>();
    TitleSize->SetWidthOverride(276.f);
    UVerticalBox* Title = WidgetTree->ConstructWidget<UVerticalBox>();
    TitleSize->SetContent(Title);
    Title->AddChildToVerticalBox(MakeText(TEXT("长夜守灯"), 25, LNUI::Gold));
    Title->AddChildToVerticalBox(MakeText(TEXT("旧防线哨站  /  一灯三人"), 11, LNUI::Muted));
    Header->AddChildToHorizontalBox(TitleSize);

    auto AddMetric = [&](const FString& Heading, UTextBlock*& Value, const FString& Initial, FLinearColor Color)
    {
        UVerticalBox* Metric = WidgetTree->ConstructWidget<UVerticalBox>();
        Metric->AddChildToVerticalBox(MakeText(Heading, 11, LNUI::Muted));
        Value = MakeText(Initial, 19, Color);
        Metric->AddChildToVerticalBox(Value)->SetPadding(FMargin(0.f, 5.f, 0.f, 0.f));
        auto* MetricSlot = Header->AddChildToHorizontalBox(Metric);
        MetricSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        MetricSlot->SetPadding(FMargin(12.f, 0.f, 4.f, 0.f));
    };
    UTextBlock* MetricValue = nullptr;
    AddMetric(TEXT("当前阶段"), MetricValue, TEXT("整备"), LNUI::Text); PhaseText = MetricValue;
    AddMetric(TEXT("防线完整度"), MetricValue, TEXT("100 / 100"), LNUI::Cyan); DefenseText = MetricValue;
    AddMetric(TEXT("敌人批次"), MetricValue, TEXT("0 / 3"), LNUI::Text); BatchText = MetricValue;
    AddMetric(TEXT("战场记录"), MetricValue, TEXT("击败 0 · 突破 0"), LNUI::Text); OutcomeText = MetricValue;

    LampPanel = MakePanel(Root, FVector2D(24.f, 118.f), FVector2D(306.f, 310.f));
    UVerticalBox* Lamp = WidgetTree->ConstructWidget<UVerticalBox>();
    LampPanel->SetContent(Lamp);
    Lamp->AddChildToVerticalBox(MakeText(TEXT("主玩法灯  /  01"), 11, LNUI::Cyan));
    LampText = MakeText(TEXT("聚焰"), 24, LNUI::Gold);
    Lamp->AddChildToVerticalBox(LampText)->SetPadding(FMargin(0.f, 4.f, 0.f, 12.f));
    CurrentText = MakeText(TEXT("主奏 · 等待首次点亮"), 16, LNUI::Text);
    Lamp->AddChildToVerticalBox(CurrentText)->SetPadding(FMargin(0.f, 0.f, 0.f, 5.f));
    NextText = MakeText(TEXT("下一位 · —"), 13, LNUI::Muted);
    Lamp->AddChildToVerticalBox(NextText)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
    EnergyText = MakeText(TEXT("协奏  0 / 100"), 13, LNUI::Gold);
    Lamp->AddChildToVerticalBox(EnergyText)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
    EnergyBar = MakeProgressBar(Lamp, LNUI::Gold, 8.f);
    RelayText = MakeText(TEXT("等待整备确认"), 13, LNUI::Text);
    Lamp->AddChildToVerticalBox(RelayText)->SetPadding(FMargin(0.f, 12.f, 0.f, 9.f));
    FaultText = MakeText(TEXT("装置正常"), 12, LNUI::Cyan);
    Lamp->AddChildToVerticalBox(FaultText);

    TaskPanel = MakePanel(Root, FVector2D(24.f, 440.f), FVector2D(306.f, 128.f));
    UVerticalBox* Task = WidgetTree->ConstructWidget<UVerticalBox>();
    TaskPanel->SetContent(Task);
    TaskText = MakeText(TEXT("通信校线  0%"), 16, LNUI::Text);
    Task->AddChildToVerticalBox(TaskText)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
    TaskBar = MakeProgressBar(Task, LNUI::Cyan, 6.f);
    TaskStatusText = MakeText(TEXT("点亮主灯，保护通信作业。"), 12, LNUI::Muted);
    Task->AddChildToVerticalBox(TaskStatusText)->SetPadding(FMargin(0.f, 9.f, 0.f, 0.f));

    ActionPanel = MakePanel(Root, FVector2D(24.f, 580.f), FVector2D(306.f, 194.f));
    UVerticalBox* Actions = WidgetTree->ConstructWidget<UVerticalBox>();
    ActionPanel->SetContent(Actions);
    ConfigurationText = MakeText(TEXT("整备中 · 可调整灯型与席位"), 12, LNUI::Muted);
    Actions->AddChildToVerticalBox(ConfigurationText)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
    StartButton = MakeButton(TEXT("开始 [Space]"), true);
    PauseButton = MakeButton(TEXT("暂停 [P]"));
    StartLabel = Cast<UTextBlock>(StartButton->GetContent());
    PauseLabel = Cast<UTextBlock>(PauseButton->GetContent());
    ModuleButton = MakeButton(TEXT("切换灯型 [M]"));
    SeatsButton = MakeButton(TEXT("轮换席位 [T]"));
    ResetButton = MakeButton(TEXT("重置整备"));
    StartButton->OnClicked.AddDynamic(this, &ULNRuntimeWidget::OnStart);
    PauseButton->OnClicked.AddDynamic(this, &ULNRuntimeWidget::OnPause);
    ModuleButton->OnClicked.AddDynamic(this, &ULNRuntimeWidget::OnModule);
    SeatsButton->OnClicked.AddDynamic(this, &ULNRuntimeWidget::OnSeats);
    ResetButton->OnClicked.AddDynamic(this, &ULNRuntimeWidget::OnReset);
    AddButtonRow(Actions, StartButton, PauseButton);
    AddButtonRow(Actions, ModuleButton, SeatsButton);
    Actions->AddChildToVerticalBox(ResetButton);

    BuildSeatCards(Root);

    RelayNoticePanel = MakePanel(Root, FVector2D(350.f, 118.f), FVector2D(500.f, 46.f));
    RelayNoticePanel->SetPadding(FMargin(15.f, 9.f));
    RelayNoticePanel->SetVisibility(ESlateVisibility::Collapsed);
    RelayNoticeText = MakeText(TEXT(""), 16, LNUI::Gold);
    RelayNoticePanel->SetContent(RelayNoticeText);

    DebugPanel = MakePanel(Root, FVector2D(-24.f, 118.f), FVector2D(548.f, 564.f));
    auto* DebugSlot = Cast<UCanvasPanelSlot>(DebugPanel->Slot);
    DebugSlot->SetAnchors(FAnchors(1.f, 0.f));
    DebugSlot->SetAlignment(FVector2D(1.f, 0.f));
    DebugSlot->SetZOrder(20);
    DebugPanel->SetVisibility(ESlateVisibility::Collapsed);
    UVerticalBox* Debug = WidgetTree->ConstructWidget<UVerticalBox>();
    DebugPanel->SetContent(Debug);
    Debug->AddChildToVerticalBox(MakeText(TEXT("调试视图 / F1 收起 · F9 原版镜头"), 16, LNUI::Cyan))->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
    UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::Always);
    Scroll->SetIsFocusable(false);
    Scroll->SetAllowOverscroll(false);
    Debug->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    UVerticalBox* DebugBody = WidgetTree->ConstructWidget<UVerticalBox>();
    Scroll->AddChild(DebugBody);
    DetailText = MakeText(TEXT(""), 12, LNUI::Text);
    DebugBody->AddChildToVerticalBox(DetailText)->SetPadding(FMargin(0.f, 0.f, 10.f, 16.f));
    DebugBody->AddChildToVerticalBox(MakeText(TEXT("原始事件 / 最近记录"), 12, LNUI::Cyan))->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
    EventText = MakeText(TEXT(""), 11, LNUI::Muted);
    DebugBody->AddChildToVerticalBox(EventText)->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));

    ControlsPanel = MakePanel(Root, FVector2D(24.f, -42.f), FVector2D(1392.f, 26.f));
    ControlsPanel->SetPadding(FMargin(12.f, 4.f));
    auto* ControlsSlot = Cast<UCanvasPanelSlot>(ControlsPanel->Slot);
    ControlsSlot->SetAnchors(FAnchors(0.f, 1.f, 1.f, 1.f));
    ControlsSlot->SetOffsets(FMargin(24.f, -42.f, 24.f, 26.f));
    ControlsPanel->SetContent(MakeText(TEXT("WASD 平移   ·   战场滚轮 缩放   ·   Home 全景   ·   1 角色近景   ·   Space 开始 / 重试   ·   P / Esc 暂停   ·   F1 调试视图"), 11, LNUI::Muted));
    RefreshFromGame();
}

void ULNRuntimeWidget::BuildSeatCards(UCanvasPanel* Root)
{
    RosterPanel = MakePanel(Root, FVector2D(350.f, -188.f), FVector2D(1066.f, 128.f));
    RosterPanel->SetBrush(FSlateColorBrush(FLinearColor::Transparent));
    RosterPanel->SetPadding(FMargin(0.f));
    auto* RosterSlot = Cast<UCanvasPanelSlot>(RosterPanel->Slot);
    RosterSlot->SetAnchors(FAnchors(0.f, 1.f, 1.f, 1.f));
    RosterSlot->SetOffsets(FMargin(350.f, -188.f, 24.f, 128.f));
    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
    RosterPanel->SetContent(Row);
    DisplayedSeatIds.SetNum(3);
    for (int32 Seat = 0; Seat < 3; ++Seat)
    {
        UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
        Frame->SetBrush(FSlateColorBrush(FLinearColor::White));
        Frame->SetBrushColor(LNUI::Edge);
        Frame->SetPadding(FMargin(1.f));
        SeatCards.Add(Frame);
        auto* CardSlot = Row->AddChildToHorizontalBox(Frame);
        CardSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        CardSlot->SetPadding(FMargin(Seat == 0 ? 0.f : 5.f, 0.f, Seat == 2 ? 0.f : 5.f, 0.f));
        UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
        Card->SetBrush(FSlateColorBrush(LNUI::Panel));
        Card->SetPadding(FMargin(12.f));
        Frame->SetContent(Card);
        UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>();
        Card->SetContent(Content);

        USizeBox* PortraitSize = WidgetTree->ConstructWidget<USizeBox>();
        PortraitSize->SetWidthOverride(82.f);
        PortraitSize->SetHeightOverride(92.f);
        auto* PortraitSlot = Content->AddChildToHorizontalBox(PortraitSize);
        PortraitSlot->SetVerticalAlignment(VAlign_Center);
        PortraitSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
        UBorder* PortraitBacking = WidgetTree->ConstructWidget<UBorder>();
        PortraitBacking->SetPadding(FMargin(0.f));
        PortraitBacking->SetBrush(FSlateColorBrush(FLinearColor(0.035f, 0.075f, 0.09f, 1.f)));
        PortraitSize->SetContent(PortraitBacking);
        UOverlay* PortraitLayers = WidgetTree->ConstructWidget<UOverlay>();
        PortraitBacking->SetContent(PortraitLayers);
        UImage* Portrait = WidgetTree->ConstructWidget<UImage>();
        Portrait->SetVisibility(ESlateVisibility::Collapsed);
        UScaleBox* PortraitFit = WidgetTree->ConstructWidget<UScaleBox>();
        PortraitFit->SetStretch(EStretch::ScaleToFit);
        PortraitFit->SetContent(Portrait);
        auto* ImageSlot = PortraitLayers->AddChildToOverlay(PortraitFit);
        ImageSlot->SetHorizontalAlignment(HAlign_Fill);
        ImageSlot->SetVerticalAlignment(VAlign_Fill);
        SeatPortraits.Add(Portrait);
        UTextBlock* Fallback = MakeText(TEXT("头像未接入"), 10, LNUI::Muted);
        Fallback->SetJustification(ETextJustify::Center);
        Fallback->SetWrapTextAt(76.f);
        auto* FallbackSlot = PortraitLayers->AddChildToOverlay(Fallback);
        FallbackSlot->SetHorizontalAlignment(HAlign_Center);
        FallbackSlot->SetVerticalAlignment(VAlign_Center);
        SeatPortraitFallbacks.Add(Fallback);

        UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>();
        auto* InfoSlot = Content->AddChildToHorizontalBox(Info);
        InfoSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        InfoSlot->SetVerticalAlignment(VAlign_Center);
        UTextBlock* Role = MakeText(FString::Printf(TEXT("%d 号席 · 候场"), Seat + 1), 11, LNUI::Muted);
        Info->AddChildToVerticalBox(Role)->SetPadding(FMargin(0.f, 0.f, 0.f, 5.f));
        SeatRoles.Add(Role);
        UTextBlock* Name = MakeText(TEXT("角色数据待载入"), 16, LNUI::Text);
        Info->AddChildToVerticalBox(Name)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
        SeatNames.Add(Name);
        SeatHealthBars.Add(MakeProgressBar(Info, LNUI::Cyan, 4.f));
        UTextBlock* State = MakeText(TEXT("生命 100%"), 11, LNUI::Muted);
        Info->AddChildToVerticalBox(State)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
        SeatStates.Add(State);
    }
}

void ULNRuntimeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    RefreshElapsed += InDeltaTime;
    if (RefreshElapsed >= 0.1f)
    {
        RefreshElapsed = 0.f;
        RefreshFromGame();
    }
    if (NoticeSeconds > 0.f)
    {
        NoticeSeconds = FMath::Max(0.f, NoticeSeconds - InDeltaTime);
        if (NoticeSeconds <= 0.f) RelayNoticePanel->SetVisibility(ESlateVisibility::Collapsed);
    }
}

ALNGameMode* ULNRuntimeWidget::GetGame() const
{
    return GetWorld() ? Cast<ALNGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
}

void ULNRuntimeWidget::RefreshPortrait(int32 Seat, FName CharacterId, const FString& Name)
{
    SeatPortraitFallbacks[Seat]->SetText(FText::FromString(Name + TEXT("\n头像未接入")));
    if (DisplayedSeatIds[Seat] == CharacterId && !CharacterId.IsNone()) return;
    DisplayedSeatIds[Seat] = CharacterId;
    if (!PortraitCache.Contains(CharacterId))
    {
        // Exact visual identity only. Never substitute another person's portrait.
        FString AssetPath;
        if (CharacterId == TEXT("rover_spectro")) AssetPath = TEXT("/Game/Portraits/T_rover_spectro.T_rover_spectro");
        else if (CharacterId == TEXT("jinhsi")) AssetPath = TEXT("/Game/Portraits/T_jinhsi.T_jinhsi");
        else if (CharacterId == TEXT("changli")) AssetPath = TEXT("/Game/Portraits/T_changli.T_changli");
        PortraitCache.Add(CharacterId, AssetPath.IsEmpty() ? nullptr : LoadObject<UTexture2D>(nullptr, *AssetPath));
    }
    UTexture2D* Texture = PortraitCache.FindRef(CharacterId);
    SeatPortraits[Seat]->SetBrushFromTexture(Texture, true);
    SeatPortraits[Seat]->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    SeatPortraitFallbacks[Seat]->SetText(FText::FromString(Name + TEXT("\n头像未接入")));
    SeatPortraitFallbacks[Seat]->SetVisibility(Texture ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void ULNRuntimeWidget::ShowNotice(const FString& Text, FLinearColor Color)
{
    RelayNoticeText->SetText(FText::FromString(Text));
    RelayNoticeText->SetColorAndOpacity(FSlateColor(Color));
    RelayNoticePanel->SetVisibility(ESlateVisibility::HitTestInvisible);
    NoticeSeconds = 2.6f;
}

void ULNRuntimeWidget::RefreshFromGame()
{
    ALNGameMode* Game = GetGame();
    if (!Game || !PhaseText || SeatCards.Num() != 3) return;
    const bool bPaused = Game->IsSimulationPaused();
    PhaseText->SetText(FText::FromString(Game->GetPhaseLabel() + (bPaused ? TEXT(" · 暂停") : TEXT(""))));
    PhaseText->SetColorAndOpacity(FSlateColor(bPaused ? LNUI::Gold : LNUI::Text));
    DefenseText->SetText(FText::FromString(FString::Printf(TEXT("%d / 100"), Game->GetDefenseValue())));
    DefenseText->SetColorAndOpacity(FSlateColor(Game->GetDefenseValue() > 40 ? LNUI::Cyan : LNUI::Red));
    BatchText->SetText(FText::FromString(FString::Printf(TEXT("%d / 3"), Game->GetBatchNumber())));
    OutcomeText->SetText(FText::FromString(FString::Printf(TEXT("击败 %d · 突破 %d"), Game->GetKilledCount(), Game->GetLeakedCount())));
    LampText->SetText(FText::FromString(Game->GetLampLabel()));
    const int32 Current = Game->GetCurrentSeatIndex();
    const int32 Next = Game->GetNextSeatIndex();
    CurrentText->SetText(FText::FromString(Current == INDEX_NONE ? TEXT("主奏 · 等待首次点亮") : TEXT("主奏 · ") + Game->GetSeatCharacterName(Current)));
    NextText->SetText(FText::FromString(TEXT("下一位 · ") + Game->GetSeatCharacterName(Next)));
    RelayText->SetText(FText::FromString(Game->GetRelayStatusLabel()));
    const float FaultSeconds = Game->GetFaultSeconds();
    FaultText->SetText(FText::FromString(FaultSeconds > 0.f
        ? FString::Printf(TEXT("故障 %.1f 秒 · 仍可积累协奏"), FaultSeconds)
        : TEXT("装置正常")));
    FaultText->SetColorAndOpacity(FSlateColor(FaultSeconds > 0.f ? LNUI::Red : LNUI::Cyan));
    const float Energy = FMath::Clamp(Game->GetEnergyFraction(), 0.f, 1.f);
    const float TaskProgress = FMath::Clamp(Game->GetTaskFraction(), 0.f, 1.f);
    const bool bCanConfigure = Game->CanConfigure();
    EnergyBar->SetPercent(Energy);
    EnergyBar->SetFillColorAndOpacity(Energy >= 1.f ? LNUI::Cyan : LNUI::Gold);
    EnergyText->SetText(FText::FromString(FString::Printf(TEXT("协奏  %.0f / 100%s"), Energy * 100.f, Energy >= 1.f ? TEXT(" · 已满") : TEXT(""))));
    if (!bCanConfigure && Current != LastCurrentSeat && Current != INDEX_NONE)
    {
        ShowNotice(Game->GetSeatCharacterName(Current) + TEXT(" 接任主奏"), LNUI::Gold);
    }
    else if (!bCanConfigure && LastFaultSeconds == 0.f && FaultSeconds > 0.f)
    {
        ShowNotice(TEXT("灯具故障 · 协奏仍在积累"), LNUI::Red);
    }
    else if (!bCanConfigure && LastFaultSeconds > 0.f && FaultSeconds <= 0.f)
    {
        ShowNotice(TEXT("灯具恢复 · 接力继续"), LNUI::Cyan);
    }
    else if (!bCanConfigure && LastTaskProgress >= 0.f && LastTaskProgress < 1.f && TaskProgress >= 1.f)
    {
        ShowNotice(TEXT("通信校线完成"), LNUI::Cyan);
    }
    else if (!bCanConfigure && LastEnergy >= 0.f && LastEnergy < 1.f && Energy >= 1.f)
    {
        ShowNotice(TEXT("协奏已满 · 等待接力条件"), LNUI::Cyan);
    }
    LastCurrentSeat = Current;
    LastEnergy = Energy;
    LastFaultSeconds = FaultSeconds;
    LastTaskProgress = TaskProgress;

    TaskText->SetText(FText::FromString(TaskProgress >= 1.f ? TEXT("通信校线 · 已完成") : FString::Printf(TEXT("通信校线  %.0f%%"), TaskProgress * 100.f)));
    TaskText->SetColorAndOpacity(FSlateColor(TaskProgress >= 1.f ? LNUI::Cyan : LNUI::Text));
    TaskBar->SetPercent(TaskProgress);
    TaskStatusText->SetText(FText::FromString(Game->GetTaskStatusLabel()));

    for (int32 Seat = 0; Seat < 3; ++Seat)
    {
        const FString Name = Game->GetSeatCharacterName(Seat);
        RefreshPortrait(Seat, Game->GetSeatCharacterId(Seat), Name);
        SeatNames[Seat]->SetText(FText::FromString(Name));
        const bool bCurrent = Seat == Current;
        const bool bNext = Seat == Next;
        SeatCards[Seat]->SetBrushColor(bCurrent ? LNUI::Gold : bNext ? LNUI::Cyan : LNUI::Edge);
        SeatNames[Seat]->SetColorAndOpacity(FSlateColor(bCurrent ? LNUI::Gold : LNUI::Text));
        SeatRoles[Seat]->SetText(FText::FromString(FString::Printf(TEXT("%d 号席 · %s"), Seat + 1, bCurrent ? TEXT("当前主奏") : bNext ? TEXT("下一主奏") : TEXT("协奏支援"))));
        SeatRoles[Seat]->SetColorAndOpacity(FSlateColor(bCurrent ? LNUI::Gold : bNext ? LNUI::Cyan : LNUI::Muted));
        const float Health = FMath::Clamp(Game->GetSeatHealthFraction(Seat), 0.f, 1.f);
        SeatHealthBars[Seat]->SetPercent(Health);
        SeatHealthBars[Seat]->SetFillColorAndOpacity(Health > 0.3f ? LNUI::Cyan : LNUI::Red);
        SeatPortraits[Seat]->SetRenderOpacity(Health > 0.f ? 1.f : 0.35f);
        SeatStates[Seat]->SetText(FText::FromString(Game->GetSeatStatusLabel(Seat)));
    }

    const bool bCanStart = Game->CanStartOrRetry();
    ModuleButton->SetIsEnabled(bCanConfigure);
    SeatsButton->SetIsEnabled(bCanConfigure);
    ResetButton->SetIsEnabled(bCanConfigure);
    StartButton->SetIsEnabled(bCanStart);
    PauseButton->SetIsEnabled(!bCanStart && !bCanConfigure);
    StartLabel->SetText(FText::FromString(bCanConfigure ? TEXT("开始 [Space]") : TEXT("重试 [Space]")));
    PauseLabel->SetText(FText::FromString(bPaused ? TEXT("继续 [P]") : TEXT("暂停 [P]")));
    ConfigurationText->SetText(FText::FromString(bCanConfigure ? TEXT("整备中 · 可调整灯型与席位") : bCanStart ? TEXT("演练结束 · 重试返回整备") : TEXT("战斗中 · 灯型与席位已锁定")));
    if (bCanConfigure && Current == INDEX_NONE)
    {
        NoticeSeconds = 0.f;
        RelayNoticePanel->SetVisibility(ESlateVisibility::Collapsed);
    }
    const bool bDebug = Game->IsDebugVisible();
    DebugPanel->SetVisibility(bDebug ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (bDebug)
    {
        DetailText->SetText(FText::FromString(Game->GetStatusText() + TEXT("\n\n") + Game->GetDetailText()));
        EventText->SetText(FText::FromString(Game->GetEventText()));
    }
}

bool ULNRuntimeWidget::IsPointerOverPanel() const
{
    if (!FSlateApplication::IsInitialized()) return false;
    const FVector2D Pointer = FSlateApplication::Get().GetCursorPos();
    const UBorder* Panels[] = {HeaderPanel, LampPanel, TaskPanel, ActionPanel, RosterPanel, DebugPanel, ControlsPanel};
    for (const UBorder* Panel : Panels)
    {
        if (Panel && Panel->GetVisibility() != ESlateVisibility::Collapsed && Panel->GetVisibility() != ESlateVisibility::Hidden
            && Panel->GetCachedGeometry().IsUnderLocation(Pointer)) return true;
    }
    return false;
}

FReply ULNRuntimeWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    return IsPointerOverPanel() ? FReply::Handled() : Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

void ULNRuntimeWidget::ReturnFocusToViewport()
{
    RefreshFromGame();
    UWidgetBlueprintLibrary::SetFocusToGameViewport();
}

void ULNRuntimeWidget::OnStart()
{
    if (ALNGameMode* Game = GetGame()) Game->StartOrRetry();
    ReturnFocusToViewport();
}

void ULNRuntimeWidget::OnPause()
{
    if (ALNGameMode* Game = GetGame()) Game->PauseToggle();
    ReturnFocusToViewport();
}

void ULNRuntimeWidget::OnModule()
{
    if (ALNGameMode* Game = GetGame(); Game && Game->CanConfigure()) Game->CycleModule();
    ReturnFocusToViewport();
}

void ULNRuntimeWidget::OnSeats()
{
    if (ALNGameMode* Game = GetGame(); Game && Game->CanConfigure()) Game->RotateSeats();
    ReturnFocusToViewport();
}

void ULNRuntimeWidget::OnReset()
{
    if (ALNGameMode* Game = GetGame(); Game && Game->CanConfigure()) Game->ResetLab();
    ReturnFocusToViewport();
}

