#include "LNM2Widget.h"

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
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "LNM2GameMode.h"
#include "Styling/CoreStyle.h"

namespace M2UI
{
    const FLinearColor Background(.013f,.025f,.037f,.95f);
    const FLinearColor Text(.90f,.94f,.94f,1);
    const FLinearColor Muted(.60f,.71f,.73f,1);
    const FLinearColor Gold(1,.73f,.33f,1);
    const FLinearColor Cyan(.32f,.83f,.80f,1);
    const FLinearColor Red(1,.36f,.30f,1);
    const FLinearColor Edge(.10f,.19f,.22f,1);
    enum EAction { Start, Pause, Retry, Lamp, Seat, Roster, Module, Rotate, Swap, BeamLeft, BeamRight, Priority, Undo, Assistance };
}

ULNM2ActionButton::ULNM2ActionButton(const FObjectInitializer& Initializer) : Super(Initializer)
{
    InitIsFocusable(false);
}

void ULNM2ActionButton::SetAction(int32 InAction, int32 InIndex)
{
    Action = InAction;
    Index = InIndex;
    OnClicked.AddUniqueDynamic(this, &ULNM2ActionButton::ExecuteAction);
}

void ULNM2ActionButton::ExecuteAction()
{
    OnAction.Broadcast(Action, Index);
}

UTextBlock* ULNM2Widget::Text(const FString& Value, int32 Size, FLinearColor Color)
{
    auto* Result = WidgetTree->ConstructWidget<UTextBlock>();
    Result->SetText(FText::FromString(Value));
    Result->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), float(Size)));
    Result->SetColorAndOpacity(FSlateColor(Color));
    Result->SetAutoWrapText(true);
    Result->SetVisibility(ESlateVisibility::HitTestInvisible);
    return Result;
}

ULNM2ActionButton* ULNM2Widget::Button(const FString& Label, int32 Action, int32 Index, bool Primary)
{
    auto* Result = WidgetTree->ConstructWidget<ULNM2ActionButton>();
    Result->SetAction(Action, Index);
    Result->OnAction.AddDynamic(this, &ULNM2Widget::HandleAction);
    FButtonStyle Style;
    Style.SetNormal(FSlateColorBrush(Primary ? FLinearColor(.35f,.21f,.075f,1) : FLinearColor(.042f,.09f,.11f,1)));
    Style.SetHovered(FSlateColorBrush(Primary ? FLinearColor(.52f,.33f,.11f,1) : FLinearColor(.07f,.18f,.20f,1)));
    Style.SetPressed(FSlateColorBrush(FLinearColor(.10f,.27f,.28f,1)));
    Style.SetDisabled(FSlateColorBrush(FLinearColor(.025f,.042f,.05f,1)));
    Style.SetNormalPadding(FMargin(6,4));
    Style.SetPressedPadding(FMargin(6,5,6,3));
    Result->SetStyle(Style);
    auto* LabelText = Text(Label, 12, Primary ? M2UI::Gold : M2UI::Text);
    LabelText->SetJustification(ETextJustify::Center);
    Result->AddChild(LabelText);
    return Result;
}

UBorder* ULNM2Widget::Panel(UCanvasPanel* Root, FVector2D Position, FVector2D Size)
{
    auto* Result = WidgetTree->ConstructWidget<UBorder>();
    Result->SetBrush(FSlateColorBrush(M2UI::Background));
    Result->SetPadding(FMargin(14));
    Result->SetClipping(EWidgetClipping::ClipToBounds);
    auto* CanvasSlot = Root->AddChildToCanvas(Result);
    CanvasSlot->SetPosition(Position);
    CanvasSlot->SetSize(Size);
    Panels.Add(Result);
    return Result;
}

UProgressBar* ULNM2Widget::Bar(UVerticalBox* Parent, FLinearColor Color, float Height)
{
    auto* Result = WidgetTree->ConstructWidget<UProgressBar>();
    FProgressBarStyle Style;
    Style.SetBackgroundImage(FSlateColorBrush(FLinearColor(.045f,.082f,.10f,1)));
    Style.SetFillImage(FSlateColorBrush(FLinearColor::White));
    Result->SetWidgetStyle(Style);
    Result->SetFillColorAndOpacity(Color);
    Result->SetPercent(0);
    Result->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* Size = WidgetTree->ConstructWidget<USizeBox>();
    Size->SetHeightOverride(Height);
    Size->SetContent(Result);
    Parent->AddChildToVerticalBox(Size);
    return Result;
}

void ULNM2Widget::ButtonRow(UVerticalBox* Parent, const TArray<ULNM2ActionButton*>& Buttons, float Height)
{
    auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
    for (int32 Index = 0; Index < Buttons.Num(); ++Index)
    {
        UWidget* Child = Buttons[Index];
        if (Height > 0.f)
        {
            auto* FixedSize = WidgetTree->ConstructWidget<USizeBox>();
            FixedSize->SetHeightOverride(Height);
            FixedSize->SetContent(Child);
            Child = FixedSize;
        }
        auto* RowSlot = Row->AddChildToHorizontalBox(Child);
        RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        RowSlot->SetPadding(FMargin(Index == 0 ? 0 : 3, 0, Index == Buttons.Num()-1 ? 0 : 3, 0));
    }
    Parent->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,0,0,5));
}

void ULNM2Widget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(false);
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("M2HUDRoot"));
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    WidgetTree->RootWidget = Root;

    auto* HeaderPanel = Panel(Root, FVector2D(24,20), FVector2D(1392,82));
    auto* HeaderSlot = Cast<UCanvasPanelSlot>(HeaderPanel->Slot);
    HeaderSlot->SetAnchors(FAnchors(0,0,1,0));
    HeaderSlot->SetOffsets(FMargin(24,20,24,82));
    auto* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
    HeaderPanel->SetContent(Header);
    auto* TitleSize = WidgetTree->ConstructWidget<USizeBox>();
    TitleSize->SetWidthOverride(390);
    auto* Title = WidgetTree->ConstructWidget<UVerticalBox>();
    TitleSize->SetContent(Title);
    Title->AddChildToVerticalBox(Text(TEXT("长夜守灯 · 断哨留火"),24,M2UI::Gold));
    ModeText = Text(TEXT("第一关  /  双灯教学  /  九选六 · 普通模式"),11,M2UI::Muted);
    Title->AddChildToVerticalBox(ModeText);
    Header->AddChildToHorizontalBox(TitleSize);
    auto AddMetric = [&](const FString& Caption, UTextBlock*& Value)
    {
        auto* Box = WidgetTree->ConstructWidget<UVerticalBox>();
        Box->AddChildToVerticalBox(Text(Caption,11,M2UI::Muted));
        Value = Text(TEXT("—"),20,M2UI::Text);
        Box->AddChildToVerticalBox(Value)->SetPadding(FMargin(0,5,0,0));
        auto* MetricSlot = Header->AddChildToHorizontalBox(Box);
        MetricSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        MetricSlot->SetPadding(FMargin(16,0,0,0));
    };
    UTextBlock* Value = nullptr;
    AddMetric(TEXT("当前阶段"),Value); PhaseText = Value;
    AddMetric(TEXT("关卡进度"),Value); WaveText = Value;
    AddMetric(TEXT("防线完整度"),Value); DefenseText = Value;

    for (int32 LampIndex = 0; LampIndex < 2; ++LampIndex)
    {
        auto* LampPanel = Panel(Root,FVector2D(24,118+LampIndex*182),FVector2D(300,170));
        auto* Box = WidgetTree->ConstructWidget<UVerticalBox>();
        LampPanel->SetContent(Box);
        auto* Select = Button(FString::Printf(TEXT("选择灯 %d [Tab]"),LampIndex+1),M2UI::Lamp,LampIndex);
        Box->AddChildToVerticalBox(Select)->SetPadding(FMargin(0,0,0,8));
        LampButtons.Add(Select);
        EnergyBars.Add(Bar(Box,LampIndex==0?M2UI::Gold:M2UI::Cyan,6));
        auto* Detail = Text(TEXT(""),12,M2UI::Text);
        Box->AddChildToVerticalBox(Detail)->SetPadding(FMargin(0,8,0,0));
        LampDetails.Add(Detail);
    }

    auto* TaskPanel = Panel(Root,FVector2D(24,482),FVector2D(300,140));
    auto* Task = WidgetTree->ConstructWidget<UVerticalBox>();
    TaskPanel->SetContent(Task);
    Task->AddChildToVerticalBox(Text(TEXT("程望 · 通信校线"),16,M2UI::Cyan))->SetPadding(FMargin(0,0,0,10));
    TaskBar = Bar(Task,M2UI::Cyan,6);
    TaskText = Text(TEXT(""),12,M2UI::Text);
    Task->AddChildToVerticalBox(TaskText)->SetPadding(FMargin(0,8,0,0));

    auto* ActionPanel = Panel(Root,FVector2D(24,634),FVector2D(300,98));
    ActionPanel->SetPadding(FMargin(10));
    auto* Actions = WidgetTree->ConstructWidget<UVerticalBox>();
    ActionPanel->SetContent(Actions);
    StartButton = Button(TEXT("开始 [Space]"),M2UI::Start,0,true);
    PauseButton = Button(TEXT("暂停 [P]"),M2UI::Pause);
    RetryButton = Button(TEXT("失败后重新整备"),M2UI::Retry);
    ButtonRow(Actions,{StartButton,PauseButton});
    Actions->AddChildToVerticalBox(RetryButton);

    auto* IntelPanel = Panel(Root,FVector2D(-24,118),FVector2D(402,148));
    auto* IntelSlot = Cast<UCanvasPanelSlot>(IntelPanel->Slot);
    IntelSlot->SetAnchors(FAnchors(1,0));
    IntelSlot->SetAlignment(FVector2D(1,0));
    auto* Intel = WidgetTree->ConstructWidget<UVerticalBox>();
    IntelPanel->SetContent(Intel);
    Intel->AddChildToVerticalBox(Text(TEXT("本波情报"),14,M2UI::Cyan))->SetPadding(FMargin(0,0,0,6));
    IntelText = Text(TEXT(""),11,M2UI::Text);
    Intel->AddChildToVerticalBox(IntelText);
    BattleStatsText = Text(TEXT(""),11,M2UI::Gold);
    Intel->AddChildToVerticalBox(BattleStatsText)->SetPadding(FMargin(0,4,0,0));

    ConfigurationPanel = Panel(Root,FVector2D(-24,278),FVector2D(402,454));
    auto* ConfigSlot = Cast<UCanvasPanelSlot>(ConfigurationPanel->Slot);
    ConfigSlot->SetAnchors(FAnchors(1,0));
    ConfigSlot->SetAlignment(FVector2D(1,0));
    auto* ConfigurationScroll = WidgetTree->ConstructWidget<UScrollBox>();
    ConfigurationScroll->SetConsumeMouseWheel(EConsumeMouseWheel::Always);
    ConfigurationScroll->SetIsFocusable(false);
    ConfigurationScroll->SetAllowOverscroll(false);
    ConfigurationPanel->SetContent(ConfigurationScroll);
    auto* Configuration = WidgetTree->ConstructWidget<UVerticalBox>();
    ConfigurationScroll->AddChild(Configuration);
    SelectionText = Text(TEXT("选择灯与席位"),16,M2UI::Gold);
    Configuration->AddChildToVerticalBox(SelectionText)->SetPadding(FMargin(0,0,0,6));
    Configuration->AddChildToVerticalBox(Text(TEXT("点击下方席位，再选择候选；已在场角色交换席位。"),11,M2UI::Muted))->SetPadding(FMargin(0,0,0,9));
    for (int32 Row = 0; Row < 3; ++Row)
    {
        TArray<ULNM2ActionButton*> Buttons;
        for (int32 Column = 0; Column < 3; ++Column)
        {
            const int32 Index = Row*3+Column;
            auto* Candidate = Button(TEXT("候选角色"),M2UI::Roster,Index);
            Cast<UTextBlock>(Candidate->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11.f));
            RosterButtons.Add(Candidate);
            Buttons.Add(Candidate);
        }
        ButtonRow(Configuration,Buttons,48.f);
    }
    SelectionDetail = Text(TEXT(""),10,M2UI::Muted);
    auto* DetailSize = WidgetTree->ConstructWidget<USizeBox>();
    DetailSize->SetHeightOverride(36);
    DetailSize->SetContent(SelectionDetail);
    Configuration->AddChildToVerticalBox(DetailSize)->SetPadding(FMargin(0,1,0,7));
    ModuleButton = Button(TEXT("换灯型 [M]"),M2UI::Module);
    ButtonRow(Configuration,{ModuleButton,Button(TEXT("轮换席位 [T]"),M2UI::Rotate)});
    BeamButtons.Add(Button(TEXT("方向 -15° [Q]"),M2UI::BeamLeft));
    BeamButtons.Add(Button(TEXT("方向 +15° [E]"),M2UI::BeamRight));
    ButtonRow(Configuration,{BeamButtons[0],BeamButtons[1]});
    ButtonRow(Configuration,{Button(TEXT("交换两队"),M2UI::Swap),Button(TEXT("索敌优先级"),M2UI::Priority),Button(TEXT("撤回 [Z]"),M2UI::Undo)});
    AssistanceButton = Button(TEXT("辅助模式：关 · 突破损失减半"),M2UI::Assistance);
    AssistanceButton->SetToolTipText(FText::FromString(TEXT("整备阶段切换。辅助模式中，轻波突破损失为5点。")));
    Configuration->AddChildToVerticalBox(AssistanceButton);

    NoticePanel = Panel(Root,FVector2D(342,118),FVector2D(460,64));
    NoticePanel->SetVisibility(ESlateVisibility::Collapsed);
    NoticeText = Text(TEXT(""),13,M2UI::Gold);
    NoticePanel->SetContent(NoticeText);

    BuildSeatCards(Root);

    DebugPanel = Panel(Root,FVector2D(342,194),FVector2D(650,530));
    auto* DebugSlot = Cast<UCanvasPanelSlot>(DebugPanel->Slot);
    DebugSlot->SetZOrder(30);
    auto* Debug = WidgetTree->ConstructWidget<UVerticalBox>();
    DebugPanel->SetContent(Debug);
    Debug->AddChildToVerticalBox(Text(TEXT("事件记录 / F1 收起"),16,M2UI::Cyan))->SetPadding(FMargin(0,0,0,10));
    auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::Always);
    Scroll->SetIsFocusable(false);
    Scroll->SetAllowOverscroll(false);
    Debug->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    DebugText = Text(TEXT(""),11,M2UI::Muted);
    Scroll->AddChild(DebugText);
    DebugPanel->SetVisibility(ESlateVisibility::Collapsed);

    auto* ControlsPanel = Panel(Root,FVector2D(24,-32),FVector2D(1392,24));
    ControlsPanel->SetPadding(FMargin(8,3));
    auto* ControlsSlot = Cast<UCanvasPanelSlot>(ControlsPanel->Slot);
    ControlsSlot->SetAnchors(FAnchors(0,1,1,1));
    ControlsSlot->SetOffsets(FMargin(24,-32,24,24));
    ControlsPanel->SetContent(Text(TEXT("WASD 平移  ·  战场滚轮 缩放  ·  Home 全景  ·  1 / 2 聚焦灯  ·  Tab 选灯  ·  Space 开始 / 下一步  ·  P / Esc 暂停  ·  F1 事件记录"),11,M2UI::Muted));
    RefreshFromGame();
}

void ULNM2Widget::BuildSeatCards(UCanvasPanel* Root)
{
    auto* RosterPanel = Panel(Root,FVector2D(24,-156),FVector2D(1392,112));
    RosterPanel->SetBrush(FSlateColorBrush(FLinearColor::Transparent));
    RosterPanel->SetPadding(FMargin(0));
    auto* RosterSlot = Cast<UCanvasPanelSlot>(RosterPanel->Slot);
    RosterSlot->SetAnchors(FAnchors(0,1,1,1));
    RosterSlot->SetOffsets(FMargin(24,-156,24,112));
    auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
    RosterPanel->SetContent(Row);
    DisplayedIds.SetNum(6);
    for (int32 Index=0; Index<6; ++Index)
    {
        auto* Frame = WidgetTree->ConstructWidget<UBorder>();
        Frame->SetBrush(FSlateColorBrush(FLinearColor::White));
        Frame->SetBrushColor(M2UI::Edge);
        Frame->SetPadding(FMargin(2));
        SeatFrames.Add(Frame);
        auto* CardSlot = Row->AddChildToHorizontalBox(Frame);
        CardSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        CardSlot->SetPadding(FMargin(Index==0?0:4,0,Index==5?0:4,0));
        auto* Select = Button(TEXT(""),M2UI::Seat,Index);
        SeatButtons.Add(Select);
        Frame->SetContent(Select);
        auto* Content = WidgetTree->ConstructWidget<UHorizontalBox>();
        Select->SetContent(Content);
        auto* PortraitSize = WidgetTree->ConstructWidget<USizeBox>();
        PortraitSize->SetWidthOverride(52);
        PortraitSize->SetHeightOverride(72);
        auto* PortraitSlot = Content->AddChildToHorizontalBox(PortraitSize);
        PortraitSlot->SetVerticalAlignment(VAlign_Center);
        PortraitSlot->SetPadding(FMargin(0,0,8,0));
        auto* Layers = WidgetTree->ConstructWidget<UOverlay>();
        PortraitSize->SetContent(Layers);
        auto* Portrait = WidgetTree->ConstructWidget<UImage>();
        Portraits.Add(Portrait);
        auto* Fit = WidgetTree->ConstructWidget<UScaleBox>();
        Fit->SetStretch(EStretch::ScaleToFit);
        Fit->SetContent(Portrait);
        auto* ImageSlot = Layers->AddChildToOverlay(Fit);
        ImageSlot->SetHorizontalAlignment(HAlign_Fill);
        ImageSlot->SetVerticalAlignment(VAlign_Fill);
        auto* Fallback = Text(TEXT("未配\n头像"),11,M2UI::Muted);
        Fallback->SetJustification(ETextJustify::Center);
        auto* FallbackSlot = Layers->AddChildToOverlay(Fallback);
        FallbackSlot->SetHorizontalAlignment(HAlign_Center);
        FallbackSlot->SetVerticalAlignment(VAlign_Center);
        PortraitFallbacks.Add(Fallback);
        auto* Info = WidgetTree->ConstructWidget<UVerticalBox>();
        auto* InfoSlot = Content->AddChildToHorizontalBox(Info);
        InfoSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        InfoSlot->SetVerticalAlignment(VAlign_Center);
        auto* Role = Text(TEXT(""),11,M2UI::Muted);
        SeatRoles.Add(Role);
        Info->AddChildToVerticalBox(Role)->SetPadding(FMargin(0,0,0,5));
        auto* Name = Text(TEXT(""),14,M2UI::Text);
        SeatNames.Add(Name);
        Info->AddChildToVerticalBox(Name)->SetPadding(FMargin(0,0,0,9));
        HealthBars.Add(Bar(Info,M2UI::Cyan,4));
    }
}

void ULNM2Widget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    RefreshElapsed += DeltaSeconds;
    if (RefreshElapsed >= .1f)
    {
        RefreshElapsed = 0;
        RefreshFromGame();
    }
}

ALNM2GameMode* ULNM2Widget::GetGame() const
{
    return GetWorld() ? Cast<ALNM2GameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
}

void ULNM2Widget::RefreshPortrait(int32 Index, FName Id)
{
    if (DisplayedIds[Index] == Id && !Id.IsNone()) return;
    DisplayedIds[Index] = Id;
    if (!PortraitCache.Contains(Id))
    {
        FString Path;
        if (Id == TEXT("rover_spectro")) Path = TEXT("/Game/Portraits/T_rover_spectro.T_rover_spectro");
        else if (Id == TEXT("jinhsi")) Path = TEXT("/Game/Portraits/T_jinhsi.T_jinhsi");
        else if (Id == TEXT("changli")) Path = TEXT("/Game/Portraits/T_changli.T_changli");
        PortraitCache.Add(Id,Path.IsEmpty()?nullptr:LoadObject<UTexture2D>(nullptr,*Path));
    }
    UTexture2D* Texture = PortraitCache.FindRef(Id);
    Portraits[Index]->SetBrushFromTexture(Texture,true);
    Portraits[Index]->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    PortraitFallbacks[Index]->SetVisibility(Texture?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}

void ULNM2Widget::RefreshFromGame()
{
    auto* Game = GetGame();
    if (!Game || !PhaseText || SeatFrames.Num()!=6) return;
    PhaseText->SetText(FText::FromString(Game->GetPhaseLabel()+(Game->IsPaused()?TEXT(" · 暂停"):TEXT(""))));
    PhaseText->SetColorAndOpacity(FSlateColor(Game->IsPaused()?M2UI::Gold:M2UI::Text));
    WaveText->SetText(FText::FromString(FString::Printf(TEXT("第 %d 波 / 共 2 波"),Game->GetWaveIndex()+1)));
    DefenseText->SetText(FText::FromString(FString::Printf(TEXT("%d / 100"),Game->GetDefense())));
    DefenseText->SetColorAndOpacity(FSlateColor(Game->GetDefense()>40?M2UI::Cyan:M2UI::Red));
    TaskBar->SetPercent(FMath::Clamp(Game->GetTaskFraction(),0.f,1.f));
    TaskText->SetText(FText::FromString(Game->GetTaskText()));
    IntelText->SetText(FText::FromString(Game->GetIntelText()));
    FString StatusHeading, BattleStats;
    Game->GetStatusText().Split(TEXT("\n"),&StatusHeading,&BattleStats);
    BattleStatsText->SetText(FText::FromString(BattleStats));
    const FString Notice = Game->GetNotice().TrimStartAndEnd();
    NoticeText->SetText(FText::FromString(Notice));
    NoticePanel->SetVisibility(Notice.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    const bool Configure = Game->CanConfigure();
    ModeText->SetText(FText::FromString(Game->IsAssistanceEnabled()?TEXT("第一关  /  双灯教学  /  九选六 · 辅助模式"):TEXT("第一关  /  双灯教学  /  九选六 · 普通模式")));
    ModeText->SetColorAndOpacity(FSlateColor(Game->IsAssistanceEnabled()?M2UI::Cyan:M2UI::Muted));
    AssistanceButton->SetIsEnabled(Configure);
    Cast<UTextBlock>(AssistanceButton->GetContent())->SetText(FText::FromString(Game->IsAssistanceEnabled()?TEXT("辅助模式：开 · 突破损失减半"):TEXT("辅助模式：关 · 突破损失减半")));
    const int32 SelectedLamp = Game->GetSelectedLamp();
    const int32 SelectedSeat = Game->GetSelectedSeat();
    for (int32 LampIndex=0; LampIndex<2; ++LampIndex)
    {
        const float Energy = FMath::Clamp(Game->GetEnergyFraction(LampIndex),0.f,1.f);
        EnergyBars[LampIndex]->SetPercent(Energy);
        EnergyBars[LampIndex]->SetFillColorAndOpacity(Energy>=1?M2UI::Cyan:M2UI::Gold);
        const FString Heading = FString::Printf(TEXT("%s灯%d · %s · %.0f/100"),SelectedLamp==LampIndex?TEXT("● "):TEXT(""),LampIndex+1,Game->GetModule(LampIndex)==0?TEXT("聚焰"):TEXT("贯阵"),Energy*100);
        Cast<UTextBlock>(LampButtons[LampIndex]->GetContent())->SetText(FText::FromString(Heading));
        Cast<UTextBlock>(LampButtons[LampIndex]->GetContent())->SetColorAndOpacity(FSlateColor(SelectedLamp==LampIndex?M2UI::Gold:M2UI::Text));
        // The selectable heading already carries lamp type and energy.
        FString LampHeading, LampDetail;
        Game->GetLampText(LampIndex).Split(TEXT("\n"),&LampHeading,&LampDetail);
        LampDetails[LampIndex]->SetText(FText::FromString(LampDetail));
    }
    for (int32 Index=0; Index<6; ++Index)
    {
        const int32 LampIndex=Index/3, SeatIndex=Index%3;
        const bool Selected=LampIndex==SelectedLamp && SeatIndex==SelectedSeat;
        const bool Current=Game->GetCurrentSeat(LampIndex)==SeatIndex;
        SeatFrames[Index]->SetBrushColor(Selected?M2UI::Gold:Current?M2UI::Cyan:M2UI::Edge);
        SeatNames[Index]->SetText(FText::FromString(Game->GetSeatName(LampIndex,SeatIndex)));
        SeatRoles[Index]->SetText(FText::FromString(FString::Printf(TEXT("灯%d · 席%d%s"),LampIndex+1,SeatIndex+1,Current?TEXT(" · 主奏"):Selected?TEXT(" · 已选"):TEXT(""))));
        SeatRoles[Index]->SetColorAndOpacity(FSlateColor(Current?M2UI::Cyan:Selected?M2UI::Gold:M2UI::Muted));
        const float Health=FMath::Clamp(Game->GetSeatHealthFraction(LampIndex,SeatIndex),0.f,1.f);
        HealthBars[Index]->SetPercent(Health);
        HealthBars[Index]->SetFillColorAndOpacity(Health>.3f?M2UI::Cyan:M2UI::Red);
        RefreshPortrait(Index,Game->GetSeatId(LampIndex,SeatIndex));
        Portraits[Index]->SetRenderOpacity(Health>0?1:.35f);
    }
    ConfigurationPanel->SetVisibility(Configure?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    SelectionText->SetText(FText::FromString(FString::Printf(TEXT("灯 %d · 席位 %d · %s"),SelectedLamp+1,SelectedSeat+1,*Game->GetSeatName(SelectedLamp,SelectedSeat))));
    const auto& RosterIds=Game->GetRosterIds();
    for (int32 Index=0; Index<RosterButtons.Num(); ++Index)
    {
        auto* Label=Cast<UTextBlock>(RosterButtons[Index]->GetContent());
        if (!RosterIds.IsValidIndex(Index)) {RosterButtons[Index]->SetIsEnabled(false);continue;}
        int32 AssignedLamp=INDEX_NONE,AssignedSeat=INDEX_NONE;
        for (int32 LampIndex=0; LampIndex<2; ++LampIndex)
            for (int32 SeatIndex=0; SeatIndex<3; ++SeatIndex)
                if (Game->GetSeatId(LampIndex,SeatIndex)==RosterIds[Index]) {AssignedLamp=LampIndex;AssignedSeat=SeatIndex;}
        const FString Assigned=AssignedLamp==INDEX_NONE?TEXT("替补"):FString::Printf(TEXT("灯%d·席%d"),AssignedLamp+1,AssignedSeat+1);
        Label->SetText(FText::FromString(Game->GetRosterName(Index)+TEXT("\n")+Assigned));
        Label->SetColorAndOpacity(FSlateColor(AssignedLamp==SelectedLamp&&AssignedSeat==SelectedSeat?M2UI::Gold:AssignedLamp==INDEX_NONE?M2UI::Muted:M2UI::Text));
        RosterButtons[Index]->SetToolTipText(FText::FromString(Game->GetRosterDescription(Index)));
        RosterButtons[Index]->SetIsEnabled(Configure);
    }
    const int32 RosterIndex=RosterIds.IndexOfByKey(Game->GetSeatId(SelectedLamp,SelectedSeat));
    SelectionDetail->SetText(FText::FromString(RosterIndex==INDEX_NONE?TEXT(""):Game->GetRosterDescription(RosterIndex)));
    for (ULNM2ActionButton* BeamButton:BeamButtons) BeamButton->SetIsEnabled(Configure&&Game->GetModule(SelectedLamp)==1);
    Cast<UTextBlock>(ModuleButton->GetContent())->SetText(FText::FromString(Game->GetModule(SelectedLamp)==0?TEXT("聚焰 → 贯阵 [M]"):TEXT("贯阵 → 聚焰 [M]")));
    StartButton->SetIsEnabled(Game->CanStartOrContinue());
    const FString StartLabel=!Game->CanStartOrContinue()?TEXT("演练中"):(Configure?FString(TEXT("开始")):Game->IsDefeated()?FString(TEXT("重试")):Game->IsComplete()?FString(TEXT("重开")):FString(TEXT("下波整备")))+TEXT(" [Space]");
    Cast<UTextBlock>(StartButton->GetContent())->SetText(FText::FromString(StartLabel));
    PauseButton->SetIsEnabled(!Game->CanStartOrContinue());
    Cast<UTextBlock>(PauseButton->GetContent())->SetText(FText::FromString(Game->IsPaused()?TEXT("继续 [P]"):TEXT("暂停 [P]")));
    RetryButton->SetIsEnabled(Game->IsDefeated());
    DebugPanel->SetVisibility(Game->IsDebugVisible()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if (Game->IsDebugVisible()) DebugText->SetText(FText::FromString(Game->GetStatusText()+TEXT("\n\n")+Game->GetEventText()));
}

bool ULNM2Widget::IsPointerOverPanel() const
{
    if (!FSlateApplication::IsInitialized()) return false;
    const FVector2D Pointer=FSlateApplication::Get().GetCursorPos();
    for (const UBorder* Item:Panels)
    {
        if (Item && Item->GetVisibility()!=ESlateVisibility::Collapsed && Item->GetVisibility()!=ESlateVisibility::Hidden
            && Item->GetCachedGeometry().IsUnderLocation(Pointer)) return true;
    }
    return false;
}

FReply ULNM2Widget::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
    return IsPointerOverPanel()?FReply::Handled():Super::NativeOnMouseWheel(Geometry,Event);
}

void ULNM2Widget::ReturnFocusToViewport()
{
    RefreshFromGame();
    UWidgetBlueprintLibrary::SetFocusToGameViewport();
}

void ULNM2Widget::HandleAction(int32 Action, int32 Index)
{
    auto* Game=GetGame();
    if (!Game) return;
    switch (Action)
    {
    case M2UI::Start: Game->StartOrContinue(); break;
    case M2UI::Pause: Game->PauseToggle(); break;
    case M2UI::Retry: if (Game->IsDefeated()) Game->RetryWave(); break;
    case M2UI::Lamp: Game->SelectLamp(Index); break;
    case M2UI::Seat: Game->SelectLamp(Index/3); Game->SelectSeat(Index%3); break;
    default:
        if (!Game->CanConfigure()) break;
        switch (Action)
        {
        case M2UI::Roster: Game->AssignCharacter(Index); break;
        case M2UI::Module: Game->CycleModule(); break;
        case M2UI::Rotate: Game->RotateSeats(); break;
        case M2UI::Swap: Game->SwapTeams(); break;
        case M2UI::BeamLeft: Game->RotateBeam(-15); break;
        case M2UI::BeamRight: Game->RotateBeam(15); break;
        case M2UI::Priority: Game->CyclePriority(); break;
        case M2UI::Undo: Game->UndoPreparation(); break;
        case M2UI::Assistance: Game->ToggleAssistance(); break;
        default: break;
        }
        break;
    }
    ReturnFocusToViewport();
}
