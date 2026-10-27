#include "LNCampaignWidget.h"
#include "LNCampaignGameMode.h"
#include "LNCampaignPawn.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
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
#include "Styling/CoreStyle.h"
#include "Misc/CommandLine.h"

namespace CampaignUI
{
    const FLinearColor Background(.013f,.025f,.037f,.96f);
    const FLinearColor Text(.90f,.94f,.94f,1);
    const FLinearColor Muted(.60f,.71f,.73f,1);
    const FLinearColor Gold(1,.73f,.33f,1);
    const FLinearColor Cyan(.32f,.83f,.80f,1);
    const FLinearColor Red(1,.36f,.30f,1);
    const FLinearColor Edge(.10f,.19f,.22f,1);
    enum EAction { Normal,Review,Stage,Menu,Start,Pause,Retry,Lamp,Seat,Roster,Module,Rotate,
        SwapTarget,Swap,BeamLeft,BeamRight,Priority,Undo,SavePreset,LoadPreset,Assistance,
        ReviewWave,Variant,Recommended,Claim,Tutorial,ReplayTutorial,Focus,Overview };
    const TCHAR* Modules[]={TEXT("聚焰"),TEXT("贯阵"),TEXT("震域"),TEXT("返照")};
}

ULNCampaignActionButton::ULNCampaignActionButton(const FObjectInitializer& Initializer):Super(Initializer)
{InitIsFocusable(false);}
void ULNCampaignActionButton::SetAction(int32 InAction,int32 InIndex)
{Action=InAction;Index=InIndex;OnClicked.AddUniqueDynamic(this,&ULNCampaignActionButton::ExecuteAction);}
void ULNCampaignActionButton::ExecuteAction(){OnAction.Broadcast(Action,Index);}

UTextBlock* ULNCampaignWidget::Text(const FString& Value,int32 Size,FLinearColor Color)
{
    auto* Result=WidgetTree->ConstructWidget<UTextBlock>();
    Result->SetText(FText::FromString(Value));
    Result->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),float(Size)));
    Result->SetColorAndOpacity(FSlateColor(Color));
    Result->SetAutoWrapText(true);
    Result->SetVisibility(ESlateVisibility::HitTestInvisible);
    return Result;
}
ULNCampaignActionButton* ULNCampaignWidget::Button(const FString& Label,int32 Action,int32 Index,bool Primary)
{
    auto* Result=WidgetTree->ConstructWidget<ULNCampaignActionButton>();
    Result->SetAction(Action,Index);Result->OnAction.AddDynamic(this,&ULNCampaignWidget::HandleAction);
    FButtonStyle Style;
    Style.SetNormal(FSlateColorBrush(Primary?FLinearColor(.35f,.21f,.075f,1):FLinearColor(.042f,.09f,.11f,1)));
    Style.SetHovered(FSlateColorBrush(Primary?FLinearColor(.52f,.33f,.11f,1):FLinearColor(.07f,.18f,.20f,1)));
    Style.SetPressed(FSlateColorBrush(FLinearColor(.10f,.27f,.28f,1)));
    Style.SetDisabled(FSlateColorBrush(FLinearColor(.025f,.042f,.05f,1)));
    Style.SetNormalPadding(FMargin(5,4));Style.SetPressedPadding(FMargin(5,5,5,3));
    Result->SetStyle(Style);
    auto* LabelText=Text(Label,12,Primary?CampaignUI::Gold:CampaignUI::Text);
    LabelText->SetJustification(ETextJustify::Center);LabelText->SetAutoWrapText(false);Result->SetContent(LabelText);
    if(auto* ContentSlot=Cast<UButtonSlot>(LabelText->Slot))
    {ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Center);}
    return Result;
}
void ULNCampaignWidget::SetLabel(ULNCampaignActionButton* Target,const FString& Label,FLinearColor Color)
{
    if(auto* LabelText=Cast<UTextBlock>(Target->GetContent()))
    {LabelText->SetText(FText::FromString(Label));LabelText->SetColorAndOpacity(FSlateColor(Color));}
}
UBorder* ULNCampaignWidget::Panel(UCanvasPanel* Root,FVector2D Position,FVector2D Size,bool BattleOnly)
{
    auto* Result=WidgetTree->ConstructWidget<UBorder>();
    Result->SetBrush(FSlateColorBrush(CampaignUI::Background));Result->SetPadding(FMargin(12));
    Result->SetClipping(EWidgetClipping::ClipToBounds);
    auto* CanvasSlot=Root->AddChildToCanvas(Result);CanvasSlot->SetPosition(Position);CanvasSlot->SetSize(Size);
    Panels.Add(Result);if(BattleOnly)BattlePanels.Add(Result);return Result;
}
UProgressBar* ULNCampaignWidget::Bar(UVerticalBox* Parent,FLinearColor Color,float Height)
{
    auto* Result=WidgetTree->ConstructWidget<UProgressBar>();
    FProgressBarStyle Style;
    Style.SetBackgroundImage(FSlateColorBrush(FLinearColor(.045f,.082f,.10f,1)));
    Style.SetFillImage(FSlateColorBrush(FLinearColor::White));Result->SetWidgetStyle(Style);
    Result->SetFillColorAndOpacity(Color);Result->SetPercent(0);
    Result->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* FixedSize=WidgetTree->ConstructWidget<USizeBox>();FixedSize->SetHeightOverride(Height);FixedSize->SetContent(Result);
    Parent->AddChildToVerticalBox(FixedSize);return Result;
}
void ULNCampaignWidget::ButtonRow(UVerticalBox* Parent,const TArray<ULNCampaignActionButton*>& Buttons,float Height)
{
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
    for(int32 Index=0;Index<Buttons.Num();++Index)
    {
        auto* FixedSize=WidgetTree->ConstructWidget<USizeBox>();FixedSize->SetHeightOverride(Height);FixedSize->SetContent(Buttons[Index]);
        auto* RowSlot=Row->AddChildToHorizontalBox(FixedSize);RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        RowSlot->SetPadding(FMargin(Index==0?0:2,0,Index==Buttons.Num()-1?0:2,0));
    }
    Parent->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,0,0,4));
}
UScrollBox* ULNCampaignWidget::Scroll(UVerticalBox* Parent,float Height)
{
    auto* Result=WidgetTree->ConstructWidget<UScrollBox>();
    Result->SetConsumeMouseWheel(EConsumeMouseWheel::Always);Result->SetAllowOverscroll(false);Result->SetIsFocusable(false);
    if(Height>0)
    {
        auto* FixedSize=WidgetTree->ConstructWidget<USizeBox>();FixedSize->SetHeightOverride(Height);FixedSize->SetContent(Result);
        Parent->AddChildToVerticalBox(FixedSize)->SetPadding(FMargin(0,0,0,6));
    }
    else Parent->AddChildToVerticalBox(Result)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    return Result;
}

void ULNCampaignWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();SetIsFocusable(false);
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
    auto* Scale=WidgetTree->ConstructWidget<UScaleBox>();Scale->SetStretch(EStretch::ScaleToFit);
    Scale->SetVisibility(ESlateVisibility::SelfHitTestInvisible);WidgetTree->RootWidget=Scale;
    auto* DesignSize=WidgetTree->ConstructWidget<USizeBox>();DesignSize->SetWidthOverride(1440);DesignSize->SetHeightOverride(900);
    DesignSize->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Scale->SetContent(DesignSize);
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("CampaignRoot"));
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);DesignSize->SetContent(Root);

    auto* HeaderPanel=Panel(Root,FVector2D(20,18),FVector2D(1400,80));
    auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>();HeaderPanel->SetContent(Header);
    auto* TitleWidth=WidgetTree->ConstructWidget<USizeBox>();TitleWidth->SetWidthOverride(300);
    auto* TitleBox=WidgetTree->ConstructWidget<UVerticalBox>();TitleWidth->SetContent(TitleBox);
    TitleBox->AddChildToVerticalBox(Text(TEXT("长夜守灯"),25,CampaignUI::Gold));
    ModeText=Text(TEXT("正常体验 · 顺序解锁"),11,CampaignUI::Muted);TitleBox->AddChildToVerticalBox(ModeText);
    Header->AddChildToHorizontalBox(TitleWidth);
    auto AddMetric=[&](const FString& Caption,UTextBlock*& Value,float Width)
    {
        auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();Box->AddChildToVerticalBox(Text(Caption,10,CampaignUI::Muted));
        Value=Text(TEXT("—"),17,CampaignUI::Text);Box->AddChildToVerticalBox(Value)->SetPadding(FMargin(0,4,0,0));
        auto* FixedSize=WidgetTree->ConstructWidget<USizeBox>();FixedSize->SetWidthOverride(Width);FixedSize->SetContent(Box);
        Header->AddChildToHorizontalBox(FixedSize)->SetPadding(FMargin(10,0,0,0));
    };
    UTextBlock* Metric=nullptr;
    AddMetric(TEXT("当前关卡"),Metric,370);StageText=Metric;
    AddMetric(TEXT("阶段 / 波次"),Metric,340);PhaseText=Metric;
    AddMetric(TEXT("防线"),Metric,160);DefenseText=Metric;
    HeaderMenuButton=Button(TEXT("返回选关"),CampaignUI::Menu);
    auto* MenuWidth=WidgetTree->ConstructWidget<USizeBox>();MenuWidth->SetWidthOverride(106);MenuWidth->SetContent(HeaderMenuButton);
    Header->AddChildToHorizontalBox(MenuWidth)->SetPadding(FMargin(12,6,0,6));
    BuildMenu(Root);BuildBattle(Root);BuildSeats(Root);

    auto* MissionPanel=Panel(Root,FVector2D(316,112),FVector2D(792,58),true);
    auto* MissionRow=WidgetTree->ConstructWidget<UHorizontalBox>();MissionPanel->SetContent(MissionRow);
    MissionText=Text(TEXT(""),14,CampaignUI::Text);MissionRow->AddChildToHorizontalBox(MissionText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* FocusButton=Button(TEXT("聚焦灯组"),CampaignUI::Focus);auto* OverviewButton=Button(TEXT("全景"),CampaignUI::Overview);
    OverviewButton->SetToolTipText(FText::FromString(TEXT("Home：恢复全景")));
    for(auto* ViewButton:{FocusButton,OverviewButton}){auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(104);Size->SetContent(ViewButton);MissionRow->AddChildToHorizontalBox(Size)->SetPadding(FMargin(4,0,0,0));}
    NoticePanel=Panel(Root,FVector2D(316,180),FVector2D(792,44));
    NoticePanel->SetVisibility(ESlateVisibility::Collapsed);
    NoticeText=Text(TEXT(""),12,CampaignUI::Gold);NoticePanel->SetContent(NoticeText);
    TutorialPanel=Panel(Root,FVector2D(356,232),FVector2D(712,184));
    Cast<UCanvasPanelSlot>(TutorialPanel->Slot)->SetZOrder(40);
    auto* TutorialBox=WidgetTree->ConstructWidget<UVerticalBox>();TutorialPanel->SetContent(TutorialBox);
    TutorialBox->AddChildToVerticalBox(Text(TEXT("哨线指引"),17,CampaignUI::Gold))->SetPadding(FMargin(0,0,0,8));
    auto* TutorialScroll=Scroll(TutorialBox);
    TutorialText=Text(TEXT(""),14,CampaignUI::Text);TutorialScroll->AddChild(TutorialText);
    ButtonRow(TutorialBox,{Button(TEXT("知道了 · 继续 [Space]"),CampaignUI::Tutorial,0,true)},30);
    TutorialPanel->SetVisibility(ESlateVisibility::Collapsed);

    DebugPanel=Panel(Root,FVector2D(316,178),FVector2D(792,550));
    Cast<UCanvasPanelSlot>(DebugPanel->Slot)->SetZOrder(50);
    auto* DebugBox=WidgetTree->ConstructWidget<UVerticalBox>();DebugPanel->SetContent(DebugBox);
    DebugBox->AddChildToVerticalBox(Text(TEXT("事件记录 / F1 收起"),16,CampaignUI::Cyan))->SetPadding(FMargin(0,0,0,8));
    auto* DebugScroll=Scroll(DebugBox);DebugText=Text(TEXT(""),11,CampaignUI::Muted);DebugScroll->AddChild(DebugText);
    DebugPanel->SetVisibility(ESlateVisibility::Collapsed);

    auto* Footer=Panel(Root,FVector2D(20,868),FVector2D(1400,24));Footer->SetPadding(FMargin(8,3));
    Footer->SetContent(Text(TEXT("WASD 平移 · 战场滚轮 缩放 · Home 全景 · 1—6 聚焦灯 · Tab 选灯 · Space 开始 / 下一步 · P / Esc 暂停 · M 换模组 · T 轮席 · Q/E 方向 · Z 撤回 · F1 事件"),10,CampaignUI::Muted));
    RefreshFromGame();
}

void ULNCampaignWidget::BuildMenu(UCanvasPanel* Root)
{
    MenuPanel=Panel(Root,FVector2D(72,120),FVector2D(1296,724));MenuPanel->SetPadding(FMargin(22));
    auto* Columns=WidgetTree->ConstructWidget<UHorizontalBox>();MenuPanel->SetContent(Columns);
    auto* Left=WidgetTree->ConstructWidget<UVerticalBox>();
    auto* LeftSlot=Columns->AddChildToHorizontalBox(Left);LeftSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));LeftSlot->SetPadding(FMargin(0,0,22,0));
    Left->AddChildToVerticalBox(Text(TEXT("驻灯守线 · 波间调度"),24,CampaignUI::Gold))->SetPadding(FMargin(0,0,0,8));
    Left->AddChildToVerticalBox(Text(TEXT("选择模组、角色和主奏顺序，守住防线并完成程望的交接任务。"),13,CampaignUI::Muted))->SetPadding(FMargin(0,0,0,16));
    NormalModeButton=Button(TEXT("正常体验 · 逐关解锁"),CampaignUI::Normal,0,true);
    ReviewModeButton=Button(TEXT("HR评审 · 自由选关"),CampaignUI::Review);
    ButtonRow(Left,{NormalModeButton,ReviewModeButton},42);
    Left->AddChildToVerticalBox(Text(TEXT("六关主线"),16,CampaignUI::Cyan))->SetPadding(FMargin(0,14,0,8));
    for(int32 Row=0;Row<3;++Row)
    {
        if(Row==2)Left->AddChildToVerticalBox(Text(TEXT("险境复演"),16,CampaignUI::Cyan))->SetPadding(FMargin(0,12,0,8));
        TArray<ULNCampaignActionButton*> Buttons;
        for(int32 Column=0;Column<3;++Column)
        {
            auto* StageButton=Button(TEXT("关卡载入中"),CampaignUI::Stage,Row*3+Column);
            Cast<UTextBlock>(StageButton->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16.f));
            StageButtons.Add(StageButton);Buttons.Add(StageButton);
        }
        ButtonRow(Left,Buttons,112);
    }
    Left->AddChildToVerticalBox(Text(TEXT("正常档保留解锁、教学、波次进度和首通奖励。评审档可在整备时选择波次及复演条件，使用独立进度。"),12,CampaignUI::Muted))->SetPadding(FMargin(0,15,0,0));

    auto* RightWidth=WidgetTree->ConstructWidget<USizeBox>();RightWidth->SetWidthOverride(320);
    auto* Right=WidgetTree->ConstructWidget<UVerticalBox>();RightWidth->SetContent(Right);Columns->AddChildToHorizontalBox(RightWidth);
    Right->AddChildToVerticalBox(Text(TEXT("活动奖励与记录"),19,CampaignUI::Gold))->SetPadding(FMargin(0,0,0,12));
    auto* RewardScroll=Scroll(Right);RewardText=Text(TEXT(""),13,CampaignUI::Text);RewardScroll->AddChild(RewardText);
    MenuClaimButton=Button(TEXT("领取已达成奖励"),CampaignUI::Claim,0,true);
    ButtonRow(Right,{MenuClaimButton},38);
    Right->AddChildToVerticalBox(Text(TEXT("首通条件按当前档案记录，辅助模式和复演变体沿用相应领取条件。"),11,CampaignUI::Muted))->SetPadding(FMargin(0,7,0,0));
}

void ULNCampaignWidget::BuildBattle(UCanvasPanel* Root)
{
    auto* LampPanel=Panel(Root,FVector2D(20,112),FVector2D(280,596),true);
    auto* Lamps=WidgetTree->ConstructWidget<UVerticalBox>();LampPanel->SetContent(Lamps);
    Lamps->AddChildToVerticalBox(Text(TEXT("驻灯小队"),16,CampaignUI::Cyan))->SetPadding(FMargin(0,0,0,8));
    Lamps->AddChildToVerticalBox(Text(TEXT("当前主奏 → 下一位接班"),10,CampaignUI::Muted))->SetPadding(FMargin(0,0,0,6));
    auto* LampScroll=Scroll(Lamps);
    auto* LampList=WidgetTree->ConstructWidget<UVerticalBox>();LampScroll->AddChild(LampList);
    for(int32 Index=0;Index<6;++Index)
    {
        auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrush(FSlateColorBrush(FLinearColor(.025f,.05f,.065f,1)));Frame->SetPadding(FMargin(3));
        LampFrames.Add(Frame);LampList->AddChildToVerticalBox(Frame)->SetPadding(FMargin(0,0,0,4));
        auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();Frame->SetContent(Box);
        auto* Select=Button(TEXT("灯组"),CampaignUI::Lamp,Index);LampButtons.Add(Select);
        ButtonRow(Box,{Select},24);
        EnergyBars.Add(Bar(Box,CampaignUI::Gold,4));
        auto* Detail=Text(TEXT(""),11,CampaignUI::Text);LampDetails.Add(Detail);
        Box->AddChildToVerticalBox(Detail)->SetPadding(FMargin(0,2,0,0));
    }
    auto* ActionsPanel=Panel(Root,FVector2D(20,722),FVector2D(280,130),true);
    auto* Actions=WidgetTree->ConstructWidget<UVerticalBox>();ActionsPanel->SetContent(Actions);
    StartButton=Button(TEXT("确认编队 · 开始 [Space]"),CampaignUI::Start,0,true);
    ButtonRow(Actions,{StartButton},38);
    PauseButton=Button(TEXT("暂停 [P]"),CampaignUI::Pause);RetryButton=Button(TEXT("重试本波"),CampaignUI::Retry);
    ButtonRow(Actions,{PauseButton,RetryButton},30);
    Actions->AddChildToVerticalBox(Text(TEXT("波内观察与暂停；返回选关保留检查点。"),10,CampaignUI::Muted));

    auto* InfoPanel=Panel(Root,FVector2D(1124,112),FVector2D(296,176),true);
    auto* Info=WidgetTree->ConstructWidget<UVerticalBox>();InfoPanel->SetContent(Info);
    Info->AddChildToVerticalBox(Text(TEXT("任务与来敌"),15,CampaignUI::Cyan))->SetPadding(FMargin(0,0,0,6));
    TaskBar=Bar(Info,CampaignUI::Cyan,4);
    auto* InfoScroll=Scroll(Info);
    auto* InfoText=WidgetTree->ConstructWidget<UVerticalBox>();InfoScroll->AddChild(InfoText);
    TaskText=Text(TEXT(""),12,CampaignUI::Text);InfoText->AddChildToVerticalBox(TaskText)->SetPadding(FMargin(0,6,0,4));
    StatusText=Text(TEXT(""),10,CampaignUI::Gold);InfoText->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0,0,0,6));
    IntelText=Text(TEXT(""),11,CampaignUI::Muted);InfoText->AddChildToVerticalBox(IntelText);

    ConfigurationPanel=Panel(Root,FVector2D(1124,300),FVector2D(296,552));
    auto* Configuration=WidgetTree->ConstructWidget<UVerticalBox>();ConfigurationPanel->SetContent(Configuration);
    SelectionText=Text(TEXT(""),14,CampaignUI::Gold);Configuration->AddChildToVerticalBox(SelectionText)->SetPadding(FMargin(0,0,0,5));
    RosterCaption=Text(TEXT("选择下方席位，再点候选；已上阵人物交换。"),10,CampaignUI::Muted);
    Configuration->AddChildToVerticalBox(RosterCaption)->SetPadding(FMargin(0,0,0,6));
    auto* RosterScroll=Scroll(Configuration,150);
    auto* RosterList=WidgetTree->ConstructWidget<UVerticalBox>();RosterScroll->AddChild(RosterList);
    for(int32 Row=0;Row<13;++Row)
    {
        TArray<ULNCampaignActionButton*> Buttons;
        for(int32 Column=0;Column<2;++Column)
        {
            const int32 Index=Row*2+Column;if(Index>=26)break;
            auto* Candidate=Button(TEXT("角色"),CampaignUI::Roster,Index);
            Cast<UTextBlock>(Candidate->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12.f));
            RosterButtons.Add(Candidate);Buttons.Add(Candidate);
        }
        ButtonRow(RosterList,Buttons,42);
    }
    auto* DetailScroll=Scroll(Configuration,60);SelectionDetail=Text(TEXT(""),11,CampaignUI::Muted);DetailScroll->AddChild(SelectionDetail);
    ModuleButton=Button(TEXT("切换模组 [M]"),CampaignUI::Module);
    ButtonRow(Configuration,{ModuleButton,Button(TEXT("轮换三席 [T]"),CampaignUI::Rotate)});
    BeamButtons={Button(TEXT("方向 -15° [Q]"),CampaignUI::BeamLeft),Button(TEXT("方向 +15° [E]"),CampaignUI::BeamRight)};
    ButtonRow(Configuration,{BeamButtons[0],BeamButtons[1]});
    SwapTargetButton=Button(TEXT("交换目标：灯2"),CampaignUI::SwapTarget);
    PriorityButton=Button(TEXT("突破优先"),CampaignUI::Priority);
    ButtonRow(Configuration,{PriorityButton,SwapTargetButton});
    SwapButton=Button(TEXT("与目标交换整队"),CampaignUI::Swap);
    ButtonRow(Configuration,{SwapButton,Button(TEXT("撤回 [Z]"),CampaignUI::Undo)});
    ButtonRow(Configuration,{Button(TEXT("保存阵容"),CampaignUI::SavePreset),Button(TEXT("载入阵容"),CampaignUI::LoadPreset)});
    AssistanceButton=Button(TEXT("辅助模式：关"),CampaignUI::Assistance);
    ButtonRow(Configuration,{AssistanceButton});
    ReviewWaveButton=Button(TEXT("选择波次"),CampaignUI::ReviewWave);VariantButton=Button(TEXT("复演条件"),CampaignUI::Variant);
    ButtonRow(Configuration,{ReviewWaveButton,VariantButton});
    PreparationReplayButton=Button(TEXT("回看本关指引"),CampaignUI::ReplayTutorial);
    ButtonRow(Configuration,{Button(TEXT("推荐阵容"),CampaignUI::Recommended),PreparationReplayButton});

    RecapPanel=Panel(Root,FVector2D(1124,300),FVector2D(296,552));
    auto* Recap=WidgetTree->ConstructWidget<UVerticalBox>();RecapPanel->SetContent(Recap);
    Recap->AddChildToVerticalBox(Text(TEXT("波后复盘"),17,CampaignUI::Gold))->SetPadding(FMargin(0,0,0,10));
    auto* RecapScroll=Scroll(Recap);
    auto* RecapContent=WidgetTree->ConstructWidget<UVerticalBox>();RecapScroll->AddChild(RecapContent);
    RecapText=Text(TEXT(""),12,CampaignUI::Text);RecapContent->AddChildToVerticalBox(RecapText)->SetPadding(FMargin(0,0,0,16));
    RecapRewardText=Text(TEXT(""),11,CampaignUI::Muted);RecapContent->AddChildToVerticalBox(RecapRewardText);
    RecapClaimButton=Button(TEXT("领取已达成奖励"),CampaignUI::Claim,0,true);
    RecapReplayButton=Button(TEXT("回看本关指引"),CampaignUI::ReplayTutorial);
    ButtonRow(Recap,{RecapClaimButton,RecapReplayButton},34);
    RecapPanel->SetVisibility(ESlateVisibility::Collapsed);
}

void ULNCampaignWidget::BuildSeats(UCanvasPanel* Root)
{
    auto* RosterPanel=Panel(Root,FVector2D(316,738),FVector2D(792,114),true);
    RosterPanel->SetBrush(FSlateColorBrush(FLinearColor::Transparent));RosterPanel->SetPadding(FMargin(0));
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();RosterPanel->SetContent(Row);DisplayedIds.SetNum(3);
    for(int32 Index=0;Index<3;++Index)
    {
        auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrush(FSlateColorBrush(FLinearColor::White));Frame->SetBrushColor(CampaignUI::Edge);Frame->SetPadding(FMargin(2));SeatFrames.Add(Frame);
        auto* CardSlot=Row->AddChildToHorizontalBox(Frame);CardSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));CardSlot->SetPadding(FMargin(Index==0?0:4,0,Index==2?0:4,0));
        auto* Select=Button(TEXT(""),CampaignUI::Seat,Index);Frame->SetContent(Select);
        auto* Content=WidgetTree->ConstructWidget<UHorizontalBox>();Select->SetContent(Content);
        if(auto* ContentSlot=Cast<UButtonSlot>(Content->Slot))
        {ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Fill);}
        auto* PortraitSize=WidgetTree->ConstructWidget<USizeBox>();PortraitSize->SetWidthOverride(58);PortraitSize->SetHeightOverride(82);
        auto* PortraitSlot=Content->AddChildToHorizontalBox(PortraitSize);PortraitSlot->SetPadding(FMargin(3,0,9,0));PortraitSlot->SetVerticalAlignment(VAlign_Center);
        auto* Layers=WidgetTree->ConstructWidget<UOverlay>();PortraitSize->SetContent(Layers);
        auto* Portrait=WidgetTree->ConstructWidget<UImage>();Portraits.Add(Portrait);
        auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetContent(Portrait);
        auto* ImageSlot=Layers->AddChildToOverlay(Fit);ImageSlot->SetHorizontalAlignment(HAlign_Fill);ImageSlot->SetVerticalAlignment(VAlign_Fill);
        auto* Fallback=Text(TEXT(""),14,CampaignUI::Cyan);Fallback->SetJustification(ETextJustify::Center);Fallback->SetAutoWrapText(false);PortraitFallbacks.Add(Fallback);
        auto* FallbackSlot=Layers->AddChildToOverlay(Fallback);FallbackSlot->SetHorizontalAlignment(HAlign_Fill);FallbackSlot->SetVerticalAlignment(VAlign_Center);
        auto* Info=WidgetTree->ConstructWidget<UVerticalBox>();auto* InfoSlot=Content->AddChildToHorizontalBox(Info);InfoSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));InfoSlot->SetVerticalAlignment(VAlign_Center);
        auto* Role=Text(TEXT(""),11,CampaignUI::Muted);SeatRoles.Add(Role);Info->AddChildToVerticalBox(Role)->SetPadding(FMargin(0,0,0,5));
        auto* NameText=Text(TEXT(""),16,CampaignUI::Text);SeatNames.Add(NameText);Info->AddChildToVerticalBox(NameText)->SetPadding(FMargin(0,0,0,12));
        HealthBars.Add(Bar(Info,CampaignUI::Cyan,4));
    }
}

ALNCampaignGameMode* ULNCampaignWidget::GetGame() const
{return GetWorld()?GetWorld()->GetAuthGameMode<ALNCampaignGameMode>():nullptr;}
void ULNCampaignWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);RefreshElapsed+=DeltaSeconds;
    if(RefreshElapsed>=.1f){RefreshElapsed=0;RefreshFromGame();}
    if(FParse::Param(FCommandLine::Get(),TEXT("LNCampaignSceneChecks")))TickSceneChecks(DeltaSeconds);
    if(!bBindingChecksStarted&&FParse::Param(FCommandLine::Get(),TEXT("LNCampaignUIChecks")))
    {
        BindingCheckDelay+=DeltaSeconds;
        if(BindingCheckDelay>=.5f){bBindingChecksStarted=true;RunBindingChecks();}
    }
}
void ULNCampaignWidget::NormalizeSwapTarget(int32 Selected,int32 Count)
{if(Count<=1){SwapTarget=0;return;}SwapTarget=FMath::Clamp(SwapTarget,0,Count-1);if(SwapTarget==Selected)SwapTarget=(SwapTarget+1)%Count;}
void ULNCampaignWidget::RefreshPortrait(int32 Index,FName Id)
{
    if(DisplayedIds[Index]==Id&&!Id.IsNone())return;DisplayedIds[Index]=Id;
    if(!PortraitCache.Contains(Id))
    {
        FString Path;
        if(Id==TEXT("rover_spectro"))Path=TEXT("/Game/Portraits/T_rover_spectro.T_rover_spectro");
        else if(Id==TEXT("jinhsi"))Path=TEXT("/Game/Portraits/T_jinhsi.T_jinhsi");
        else if(Id==TEXT("changli"))Path=TEXT("/Game/Portraits/T_changli.T_changli");
        PortraitCache.Add(Id,Path.IsEmpty()?nullptr:LoadObject<UTexture2D>(nullptr,*Path));
    }
    UTexture2D* Texture=PortraitCache.FindRef(Id);Portraits[Index]->SetBrushFromTexture(Texture,true);
    Portraits[Index]->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    PortraitFallbacks[Index]->SetVisibility(Texture?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}

void ULNCampaignWidget::RefreshFromGame()
{
    auto* Game=GetGame();if(!Game||!StageText||SeatFrames.Num()!=3)return;
    const bool Menu=Game->IsInMenu(),Review=Game->IsReviewMode(),Tutorial=Game->HasTutorial();
    const bool Configure=Game->CanConfigure()&&!Tutorial;
    MenuPanel->SetVisibility(Menu?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    for(UBorder* Item:BattlePanels)Item->SetVisibility(Menu?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
    HeaderMenuButton->SetIsEnabled(!Menu);
    ModeText->SetText(FText::FromString(Review?TEXT("HR评审档 · 自由选关 · 独立进度"):TEXT("正常体验 · 顺序解锁 · 首通奖励")));
    ModeText->SetColorAndOpacity(FSlateColor(Review?CampaignUI::Gold:CampaignUI::Muted));
    StageText->SetText(FText::FromString(Menu?TEXT("六关主线 · 三关险境"):Game->GetStageName(Game->GetSelectedStage())));
    PhaseText->SetText(FText::FromString(Menu?TEXT("选择关卡开始"):FString::Printf(TEXT("%s · %d/%d波"),*Game->GetPhaseLabel(),Game->GetWaveIndex()+1,Game->GetWaveCount())));
    DefenseText->SetText(FText::FromString(Menu?TEXT("—"):FString::Printf(TEXT("%d / 100"),Game->GetDefense())));
    DefenseText->SetColorAndOpacity(FSlateColor(Game->GetDefense()>40?CampaignUI::Cyan:CampaignUI::Red));
    SetLabel(NormalModeButton,TEXT("正常体验 · 逐关解锁"),Review?CampaignUI::Muted:CampaignUI::Gold);
    SetLabel(ReviewModeButton,TEXT("HR评审 · 自由选关"),Review?CampaignUI::Gold:CampaignUI::Muted);
    for(int32 Index=0;Index<StageButtons.Num();++Index)
    {
        const bool Exists=Index<Game->GetStageCount();StageButtons[Index]->SetVisibility(Exists?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        const bool Unlocked=Exists&&Game->IsStageUnlocked(Index);StageButtons[Index]->SetIsEnabled(Unlocked);
        if(Exists)SetLabel(StageButtons[Index],Game->GetStageName(Index)+TEXT("\n\n")+(Unlocked?(Review?TEXT("进入评审"):TEXT("进入关卡")):Index>=6?TEXT("完成主线6后开放"):TEXT("完成前一关后开放")),Unlocked?CampaignUI::Text:CampaignUI::Muted);
    }
    RewardText->SetText(FText::FromString(Game->GetRewardSummary()));MenuClaimButton->SetIsEnabled(Game->CanClaimRewards());
    const FString Notice=Game->GetNotice().TrimStartAndEnd();NoticeText->SetText(FText::FromString(Notice));
    if(auto* NoticeSlot=Cast<UCanvasPanelSlot>(NoticePanel->Slot))
    {
        NoticeSlot->SetPosition(Menu?FVector2D(96,764):FVector2D(316,180));
        NoticeSlot->SetSize(Menu?FVector2D(876,58):FVector2D(792,44));
    }
    NoticePanel->SetVisibility(!Tutorial&&!Notice.IsEmpty()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    TutorialText->SetText(FText::FromString(Game->GetTutorialText()));TutorialPanel->SetVisibility(!Menu&&Tutorial?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    DebugPanel->SetVisibility(!Menu&&Game->IsDebugVisible()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(!Menu&&Game->IsDebugVisible())DebugText->SetText(FText::FromString(Game->GetStatusText()+TEXT("\n\n")+Game->GetEventText()));
    ConfigurationPanel->SetVisibility(!Menu&&Configure?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    const FString Recap=Game->GetRecapText();const bool ShowRecap=!Menu&&!Configure&&!Tutorial&&!Recap.IsEmpty()&&Game->CanStartOrContinue();
    RecapPanel->SetVisibility(ShowRecap?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    PreparationReplayButton->SetIsEnabled(Configure&&Game->GetSelectedStage()<6);
    RecapReplayButton->SetIsEnabled(ShowRecap&&!Game->IsDefeated()&&Game->GetSelectedStage()<6);
    if(ShowRecap){RecapText->SetText(FText::FromString(Recap));RecapRewardText->SetText(FText::FromString(Game->GetRewardSummary()));RecapClaimButton->SetIsEnabled(Game->CanClaimRewards());}
    if(Menu)return;

    const int32 LampCount=Game->GetLampCount();const int32 Selected=Game->GetSelectedLamp(),Seat=Game->GetSelectedSeat();
    NormalizeSwapTarget(Selected,LampCount);
    for(int32 L=0;L<LampFrames.Num();++L)
    {
        LampFrames[L]->SetVisibility(L<LampCount?ESlateVisibility::Visible:ESlateVisibility::Collapsed);if(L>=LampCount)continue;
        const float Energy=FMath::Clamp(Game->GetEnergyFraction(L),0.f,1.f);EnergyBars[L]->SetPercent(Energy);
        EnergyBars[L]->SetFillColorAndOpacity(Energy>=1?CampaignUI::Cyan:CampaignUI::Gold);
        SetLabel(LampButtons[L],FString::Printf(TEXT("%s灯%d %s %.0f/100"),L==Selected?TEXT("● "):TEXT(""),L+1,CampaignUI::Modules[FMath::Clamp(Game->GetModule(L),0,3)],Energy*100),L==Selected?CampaignUI::Gold:CampaignUI::Text);
        LampDetails[L]->SetText(FText::FromString(Game->GetLampSummary(L)));
        LampButtons[L]->SetToolTipText(FText::FromString(Game->GetLampText(L)));
    }
    for(int32 S=0;S<3;++S)
    {
        const bool Current=Game->GetCurrentSeat(Selected)==S;
        SeatFrames[S]->SetBrushColor(S==Seat?CampaignUI::Gold:Current?CampaignUI::Cyan:CampaignUI::Edge);
        SeatNames[S]->SetText(FText::FromString(Game->GetSeatName(Selected,S)));
        SeatRoles[S]->SetText(FText::FromString(FString::Printf(TEXT("灯%d · 席%d%s"),Selected+1,S+1,Current?TEXT(" · 当前主奏"):S==Seat?TEXT(" · 已选"):TEXT(""))));
        SeatRoles[S]->SetColorAndOpacity(FSlateColor(Current?CampaignUI::Cyan:S==Seat?CampaignUI::Gold:CampaignUI::Muted));
        const float Health=FMath::Clamp(Game->GetSeatHealthFraction(Selected,S),0.f,1.f);HealthBars[S]->SetPercent(Health);
        HealthBars[S]->SetFillColorAndOpacity(Health>.3f?CampaignUI::Cyan:CampaignUI::Red);
        RefreshPortrait(S,Game->GetSeatId(Selected,S));Portraits[S]->SetRenderOpacity(Health>0?1.f:.35f);
        PortraitFallbacks[S]->SetText(FText::FromString(Game->GetSeatRole(Selected,S)));
        const int32 DetailIndex=Game->GetAvailableRosterIds().IndexOfByKey(Game->GetSeatId(Selected,S));
        SeatFrames[S]->SetToolTipText(FText::FromString(Game->GetRosterDescription(DetailIndex)));
    }
    TaskText->SetText(FText::FromString(Game->GetTaskText()));TaskBar->SetPercent(FMath::Clamp(Game->GetTaskFraction(),0.f,1.f));
    MissionText->SetText(FText::FromString(TEXT("本波目标  ")+Game->GetTaskText()));
    IntelText->SetText(FText::FromString(Game->GetIntelText()));StatusText->SetText(FText::FromString(Game->GetStatusText()));
    StartButton->SetIsEnabled(Tutorial||Game->CanStartOrContinue());
    SetLabel(StartButton,Tutorial?TEXT("继续指引 [Space]"):Game->CanStartOrContinue()?Game->GetStartLabel()+TEXT(" [Space]"):TEXT("战斗进行中"),CampaignUI::Gold);
    PauseButton->SetIsEnabled(!Tutorial&&(!Game->CanStartOrContinue()||Game->IsPaused()));
    SetLabel(PauseButton,Game->IsPaused()?TEXT("继续 [P]"):TEXT("暂停 [P]"),CampaignUI::Text);RetryButton->SetIsEnabled(Game->IsDefeated());
    SelectionText->SetText(FText::FromString(FString::Printf(TEXT("灯%d · 席%d · %s"),Selected+1,Seat+1,*Game->GetSeatName(Selected,Seat))));
    const auto& Roster=Game->GetAvailableRosterIds();
    RosterCaption->SetText(FText::FromString(FString::Printf(TEXT("%d项配置 · 点击当前三席，再选择人物。"),Roster.Num())));
    for(int32 Index=0;Index<RosterButtons.Num();++Index)
    {
        if(Index%2==0&&RosterButtons[Index]->GetParent()&&RosterButtons[Index]->GetParent()->GetParent())
            RosterButtons[Index]->GetParent()->GetParent()->SetVisibility(Index<Roster.Num()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        const bool Exists=Roster.IsValidIndex(Index);RosterButtons[Index]->SetVisibility(Exists?ESlateVisibility::Visible:ESlateVisibility::Collapsed);if(!Exists)continue;
        int32 AssignedLamp=INDEX_NONE,AssignedSeat=INDEX_NONE;
        for(int32 L=0;L<LampCount;++L)for(int32 S=0;S<3;++S)if(Game->GetSeatId(L,S)==Roster[Index]){AssignedLamp=L;AssignedSeat=S;}
        const FString Position=AssignedLamp==INDEX_NONE?TEXT("替补"):FString::Printf(TEXT("灯%d·席%d"),AssignedLamp+1,AssignedSeat+1);
        SetLabel(RosterButtons[Index],Game->GetRosterName(Index)+TEXT("\n")+Position,AssignedLamp==Selected&&AssignedSeat==Seat?CampaignUI::Gold:AssignedLamp==INDEX_NONE?CampaignUI::Muted:CampaignUI::Text);
        RosterButtons[Index]->SetToolTipText(FText::FromString(Game->GetRosterDescription(Index)));RosterButtons[Index]->SetIsEnabled(Configure);
    }
    const int32 RosterIndex=Roster.IndexOfByKey(Game->GetSeatId(Selected,Seat));
    SelectionDetail->SetText(FText::FromString(RosterIndex==INDEX_NONE?TEXT(""):Game->GetRosterFocusDescription(RosterIndex)));
    SelectionDetail->SetToolTipText(FText::FromString(Game->GetRosterDescription(RosterIndex)));
    SetLabel(ModuleButton,FString::Printf(TEXT("%s · 换模组"),CampaignUI::Modules[FMath::Clamp(Game->GetModule(Selected),0,3)]),CampaignUI::Text);
    SetLabel(PriorityButton,Game->GetPriorityLabel(Selected),CampaignUI::Cyan);
    for(ULNCampaignActionButton* Beam:BeamButtons)Beam->SetIsEnabled(Configure&&Game->GetModule(Selected)==1);
    SetLabel(SwapTargetButton,FString::Printf(TEXT("交换目标：灯%d"),SwapTarget+1),CampaignUI::Text);
    SwapTargetButton->SetIsEnabled(Configure&&LampCount>1);SwapButton->SetIsEnabled(Configure&&LampCount>1);
    AssistanceButton->SetIsEnabled(Configure&&Game->GetSelectedStage()<6);
    SetLabel(AssistanceButton,Game->IsAssistanceEnabled()?TEXT("辅助：开 · 突破损失减半"):TEXT("辅助：关 · 标准突破损失"),Game->IsAssistanceEnabled()?CampaignUI::Cyan:CampaignUI::Text);
    ReviewWaveButton->SetIsEnabled(Configure&&Review);SetLabel(ReviewWaveButton,FString::Printf(TEXT("评审波次 %d/%d"),Game->GetWaveIndex()+1,Game->GetWaveCount()),Review?CampaignUI::Gold:CampaignUI::Muted);
    VariantButton->SetIsEnabled(Configure&&Game->GetSelectedStage()>=6);SetLabel(VariantButton,Game->GetVariantLabel(),CampaignUI::Text);
}

bool ULNCampaignWidget::IsPointerOverPanel() const
{
    if(!FSlateApplication::IsInitialized())return false;
    const FVector2D Pointer=FSlateApplication::Get().GetCursorPos();
    for(const UBorder* Item:Panels)
        if(Item&&Item->GetVisibility()!=ESlateVisibility::Collapsed&&Item->GetVisibility()!=ESlateVisibility::Hidden&&Item->GetCachedGeometry().IsUnderLocation(Pointer))return true;
    return false;
}
FReply ULNCampaignWidget::NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event)
{return IsPointerOverPanel()?FReply::Handled():Super::NativeOnMouseWheel(Geometry,Event);}
void ULNCampaignWidget::HandleAction(int32 Action,int32 Index)
{
    auto* Game=GetGame();if(!Game)return;
    Game->PlayUiCue(Action==CampaignUI::Menu);
    switch(Action)
    {
    case CampaignUI::Normal:if(Game->IsInMenu())Game->SetReviewMode(false);break;
    case CampaignUI::Review:if(Game->IsInMenu())Game->SetReviewMode(true);break;
    case CampaignUI::Stage:if(Game->IsInMenu()&&Game->IsStageUnlocked(Index))Game->SelectStage(Index);break;
    case CampaignUI::Menu:Game->ReturnToMenu();break;
    case CampaignUI::Tutorial:Game->AcknowledgeTutorial();break;
    case CampaignUI::ReplayTutorial:if(!Game->HasTutorial())Game->ReplayTutorials();break;
    case CampaignUI::Start:if(Game->HasTutorial())Game->AcknowledgeTutorial();else Game->StartOrContinue();break;
    case CampaignUI::Pause:if(!Game->HasTutorial())Game->PauseToggle();break;
    case CampaignUI::Retry:if(Game->IsDefeated())Game->RetryWave();break;
    case CampaignUI::Claim:if(Game->CanClaimRewards())Game->ClaimRewards();break;
    case CampaignUI::Lamp:Game->SelectLamp(Index);break;
    case CampaignUI::Seat:Game->SelectSeat(Index);break;
    case CampaignUI::Focus:if(auto* P=Cast<ALNCampaignPawn>(GetOwningPlayerPawn()))P->FocusSelectedLamp();break;
    case CampaignUI::Overview:if(auto* P=Cast<ALNCampaignPawn>(GetOwningPlayerPawn()))P->FocusOverview();break;
    default:
        if(!Game->CanConfigure()||Game->HasTutorial())break;
        switch(Action)
        {
        case CampaignUI::Roster:Game->AssignCharacter(Index);break;
        case CampaignUI::Module:Game->CycleModule();break;
        case CampaignUI::Rotate:Game->RotateSeats();break;
        case CampaignUI::BeamLeft:Game->RotateBeam(-15);break;
        case CampaignUI::BeamRight:Game->RotateBeam(15);break;
        case CampaignUI::Priority:Game->CyclePriority();break;
        case CampaignUI::SwapTarget:++SwapTarget;if(SwapTarget>=Game->GetLampCount())SwapTarget=0;NormalizeSwapTarget(Game->GetSelectedLamp(),Game->GetLampCount());break;
        case CampaignUI::Swap:Game->SwapTeams(SwapTarget);break;
        case CampaignUI::Undo:Game->UndoPreparation();break;
        case CampaignUI::SavePreset:Game->SaveFormationPreset();break;
        case CampaignUI::LoadPreset:Game->LoadFormationPreset();break;
        case CampaignUI::Assistance:Game->ToggleAssistance();break;
        case CampaignUI::ReviewWave:if(Game->IsReviewMode())Game->CycleReviewWave();break;
        case CampaignUI::Variant:Game->CycleVariant();break;
        case CampaignUI::Recommended:Game->LoadRecommendedFormation();break;
        default:break;
        }
        break;
    }
    RefreshFromGame();UWidgetBlueprintLibrary::SetFocusToGameViewport();
}
