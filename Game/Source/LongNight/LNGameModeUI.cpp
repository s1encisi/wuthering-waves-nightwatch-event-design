#include "LNGameMode.h"
#include "LNCombatUnit.h"

FString ALNGameMode::GetPhaseLabel() const
{
    if(bPaused)return TEXT("演练暂停");
    switch(Phase)
    {
        case EPhase::Preparation:return TEXT("自由整备");
        case EPhase::Countdown:return FString::Printf(TEXT("来敌预告 · %.0f 秒"),FMath::CeilToDouble(Countdown));
        case EPhase::Combat:return TEXT("守灯进行中");
        case EPhase::Victory:return TEXT("演练完成");
        case EPhase::Defeat:return TEXT("防线失守");
    }
    return FString();
}
FString ALNGameMode::GetLampLabel()const{return Module==0?TEXT("聚焰灯"):TEXT("贯阵灯");}
FName ALNGameMode::GetSeatCharacterId(int32 Seat)const{return SeatIds.IsValidIndex(Seat)?SeatIds[Seat]:NAME_None;}
FString ALNGameMode::GetSeatCharacterName(int32 Seat)const{return Team.IsValidIndex(Seat)?Team[Seat]->Stats.DisplayName:TEXT("空缺");}
float ALNGameMode::GetSeatHealthFraction(int32 Seat)const{return Team.IsValidIndex(Seat)?Team[Seat]->Health/Team[Seat]->Stats.MaxHealth:0.f;}
FString ALNGameMode::GetSeatStatusLabel(int32 Seat)const
{
    if(!Team.IsValidIndex(Seat))return TEXT("未配置");
    const auto* Unit=Team[Seat].Get();
    if(!Unit->IsAlive())return FString::Printf(TEXT("%.0f 秒后恢复"),FMath::CeilToDouble(FMath::Max(0.,Unit->ReviveAt-SimTime)));
    return FString::Printf(TEXT("生命 %.0f / %.0f"),Unit->Health,Unit->Stats.MaxHealth);
}
bool ALNGameMode::CanStartOrRetry()const{return Phase==EPhase::Preparation||Phase==EPhase::Victory||Phase==EPhase::Defeat;}
FString ALNGameMode::GetRelayStatusLabel()const
{
    if(CanConfigure())return TEXT("确认配置后开始自动战斗");
    if(Phase==EPhase::Countdown)return TEXT("预告结束后接敌积能");
    if(Phase==EPhase::Victory)return TEXT("校线完成，哨线已守住");
    if(Phase==EPhase::Defeat)return TEXT("返回整备，重新配置后再试");
    if(bPaused)return TEXT("协奏与战斗计时已暂停");
    if(Relay.IsFaulted(SimTime))return TEXT("故障中 · 命中仍可积累协奏");
    return BlockReason;
}
