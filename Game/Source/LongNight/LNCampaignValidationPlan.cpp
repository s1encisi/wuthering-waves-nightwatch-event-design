#include "LNCampaignGameMode.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

// A recorded sequence of the same preparation actions exposed by the HUD.
// No simulation state, health, energy, spawn, task or reward is injected here.
void ALNCampaignGameMode::ApplyAutomationPlan()
{
    if(!bAutomation||!AutomationPlan.IsValid()||!CanConfigure())return;
    const FString Key=FString::Printf(TEXT("%s:%d:%d"),*StageId().ToString(),Variant,WaveIndex+1);
    if(PreparedPlanKey==Key)return;PreparedPlanKey=Key;
    const TSharedPtr<FJsonObject>* StagePlan=nullptr;
    if(!AutomationPlan->TryGetObjectField(StageId().ToString(),StagePlan))return;
    const TArray<TSharedPtr<FJsonValue>>* Actions=nullptr;
    if(!(*StagePlan)->TryGetArrayField(FString::FromInt(WaveIndex+1),Actions))return;
    Log(TEXT("validation_plan_begin"),TEXT("plan=")+AutomationPlanId+TEXT("; checkpoint=")+Key);
    for(const auto& V:*Actions)
    {
        const auto Action=V->AsObject();const FString Type=Action->GetStringField(TEXT("action"));
        const int32 Lamp=int32(Action->GetNumberField(TEXT("lamp")))-1;
        if(!Formation.IsValidIndex(Lamp)){Phase=EPhase::Error;Notice=TEXT("验证编队灯位无效");return;}
        SelectLamp(Lamp);
        if(Type==TEXT("swap"))SwapTeams(int32(Action->GetNumberField(TEXT("target")))-1);
        else if(Type==TEXT("module"))
        {
            const int32 Target=Action->GetNumberField(TEXT("value"));
            for(int32 I=0;I<4&&Formation[Lamp].Module!=Target;++I)CycleModule();
            if(Formation[Lamp].Module!=Target){Phase=EPhase::Error;Notice=TEXT("验证方案指定未解锁模块");return;}
        }
        else if(Type==TEXT("yaw"))
        {
            if(Formation[Lamp].Module!=1){Phase=EPhase::Error;Notice=TEXT("仅贯阵可调整方向");return;}
            RotateBeam(Action->GetNumberField(TEXT("value"))-Formation[Lamp].BeamYaw);
        }
        else if(Type==TEXT("priority"))
        {
            const int32 Target=Action->GetNumberField(TEXT("value"));
            if(Target<0||Target>2){Phase=EPhase::Error;return;}
            for(int32 I=0;I<3&&Formation[Lamp].Priority!=Target;++I)CyclePriority();
        }
        else if(Type==TEXT("rotate"))
        {const int32 Count=FMath::Clamp(int32(Action->GetNumberField(TEXT("value"))),0,2);for(int32 I=0;I<Count;++I)RotateSeats();}
        else if(Type==TEXT("assign"))
        {
            const int32 Seat=int32(Action->GetNumberField(TEXT("seat")))-1;
            const int32 Character=AvailableRoster.IndexOfByKey(FName(*Action->GetStringField(TEXT("character"))));
            if(Seat<0||Seat>2||Character==INDEX_NONE){Phase=EPhase::Error;Notice=TEXT("验证方案指定无效人物或席位");return;}
            SelectSeat(Seat);AssignCharacter(Character);
        }
        else{Phase=EPhase::Error;Notice=TEXT("未知验证编队操作");return;}
        if(Phase==EPhase::Error)return;
        TArray<FString> Seats;for(FName Id:Formation[Lamp].Seats)Seats.Add(Id.ToString());
        FString Requested;FJsonSerializer::Serialize(Action.ToSharedRef(),TJsonWriterFactory<>::Create(&Requested));
        Log(TEXT("validation_plan_action"),FString::Printf(TEXT("plan=%s; action=%s; lamp=%d; module=%d; yaw=%.3f; priority=%d; seats=%s; requested=%s"),*AutomationPlanId,*Type,Lamp+1,Formation[Lamp].Module,Formation[Lamp].BeamYaw,Formation[Lamp].Priority,*FString::Join(Seats,TEXT(",")),*Requested));
    }
    SelectLamp(0);Log(TEXT("validation_plan_complete"),Key);
}
