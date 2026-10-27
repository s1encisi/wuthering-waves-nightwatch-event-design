#include "LNCampaignBattle.h"
#include "LNCombatUnit.h"
#include "LNPresentation.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

namespace
{
bool OneOf(FName Id,std::initializer_list<const TCHAR*> Values){for(auto* V:Values)if(Id==V)return true;return false;}
bool HeavyOnlySkill(FName Id,int32 S){return (S==2&&OneOf(Id,{TEXT("jinhsi"),TEXT("chixia"),TEXT("mortefi"),TEXT("xiangliyao"),TEXT("danjin")}))||(S==1&&Id==TEXT("buling"));}
bool LineSkill(FName Id,int32 S){return (S==1&&OneOf(Id,{TEXT("jinhsi"),TEXT("sanhua")}))||(S==2&&OneOf(Id,{TEXT("yangyang"),TEXT("jiyan")}))||Id==TEXT("yangyang_xuanling");}
bool DashSkill(FName Id,int32 S){return S==1&&OneOf(Id,{TEXT("changli"),TEXT("lingyang"),TEXT("lumi")});}
bool AreaSkill(FName Id,int32 S){return (S==1&&OneOf(Id,{TEXT("yangyang"),TEXT("baizhi"),TEXT("yuanwu"),TEXT("youhu"),TEXT("encore"),TEXT("camellya")}))||(S==2&&OneOf(Id,{TEXT("jianxin"),TEXT("camellya")}));}
bool ConeSkill(FName Id,int32 S){return (S==1&&OneOf(Id,{TEXT("rover_spectro"),TEXT("taoqi"),TEXT("jiyan"),TEXT("jianxin")}))||(S==2&&Id==TEXT("lingyang"));}
float ActionRange(FName Id,int32 S,bool Ranged)
{
    if(S==0)return Ranged?1400:250;
    if(LineSkill(Id,S))return Id==TEXT("sanhua")?400:1200;
    if(AreaSkill(Id,S))return Ranged?1400:400;
    if(ConeSkill(Id,S))return 300;
    if(DashSkill(Id,S))return 250;
    return Ranged?1400:250;
}
FVector FlatDirection(FVector From,FVector To){FVector V=To-From;V.Z=0;return V.GetSafeNormal();}
}

ALNCombatUnit* FLNCampaignBattle::SelectTarget(int32 CI,int32 Skill) const
{
    const auto& C=Characters[CI];const auto* U=C.Actor.Get();if(!U)return nullptr;
    ALNCombatUnit* Best=nullptr;int32 BestSkillRank=MAX_int32,BestLampRank=MAX_int32;float BestMetric=MAX_flt,BestRoute=MAX_flt;
    for(const auto& E:Enemies)
    {
        auto* T=E.Actor.Get();if(!T||!T->IsAlive())continue;
        if(HeavyOnlySkill(C.ConfigId,Skill)&&!E.Heavy)continue;
        if(Skill==2&&C.ConfigId==TEXT("buling")&&E.Heavy)continue;
        if(Skill==2&&OneOf(C.ConfigId,{TEXT("jiyan"),TEXT("danjin"),TEXT("zhezhi")})&&!Lamps[C.Lamp].RouteIds.IsEmpty()&&Entries.IsValidIndex(E.Entry)&&!Lamps[C.Lamp].RouteIds.Contains(FName(Entries[E.Entry]->GetStringField(TEXT("id")))))continue;
        if(Skill==2&&C.ConfigId==TEXT("calcharo")&&E.DismantleLamp==INDEX_NONE)continue;
        if(Skill==2&&C.ConfigId==TEXT("sanhua")&&E.Marks.FindRef(TEXT("ice"))<=Time)continue;
        if(DashSkill(C.ConfigId,Skill)&&E.Heavy)continue;
        const float Distance=FVector::Dist2D(U->GetActorLocation(),T->GetActorLocation());
        if(Distance>1400)continue;
        const float Range=ActionRange(C.ConfigId,Skill,U->Stats.bRanged);
        // Melee can acquire beyond its travel boundary when its reach overlaps that boundary.
        if(FVector::Dist2D(Lamps[C.Lamp].Position,T->GetActorLocation())>(U->Stats.bRanged?1800:800+Range))continue;
        if(U->Stats.bRanged&&Distance>(DashSkill(C.ConfigId,Skill)?700:Range))continue;
        if(!Visible(U->GetActorLocation(),T->GetActorLocation()))continue;
        int32 SkillRank=0,LampRank=0;float Metric=0;const float Route=RemainingRoute(E);
        if((Skill==1&&OneOf(C.ConfigId,{TEXT("chixia"),TEXT("mortefi"),TEXT("zhezhi"),TEXT("aalto")}))||(Skill==2&&C.ConfigId==TEXT("lumi")))SkillRank=E.Shooter?0:1;
        if(Skill==2&&C.ConfigId==TEXT("lumi"))
        {
            const bool LocalRoute=Lamps[C.Lamp].RouteIds.IsEmpty()||(Entries.IsValidIndex(E.Entry)&&Lamps[C.Lamp].RouteIds.Contains(FName(Entries[E.Entry]->GetStringField(TEXT("id")))));
            SkillRank=E.Shooter?(LocalRoute?1:0):2;
        }
        if((Skill==1&&OneOf(C.ConfigId,{TEXT("xiangliyao"),TEXT("calcharo")}))||(Skill==2&&OneOf(C.ConfigId,{TEXT("changli"),TEXT("yuanwu"),TEXT("taoqi"),TEXT("jinhsi")}))||(Skill==1&&C.ConfigId==TEXT("yangyang_xuanling")))SkillRank=E.DismantleLamp!=INDEX_NONE?0:E.Heavy?1:2;
        if(Skill==1&&C.ConfigId==TEXT("danjin"))Metric=T->Health;
        if(Skill==1&&C.ConfigId==TEXT("buling"))Metric=Distance;
        if(Skill==2&&C.ConfigId==TEXT("xiangliyao"))Metric=Distance;
        if(Skill==2&&C.ConfigId==TEXT("verina"))Metric=FVector::Dist2D(Lamps[C.Lamp].Position,T->GetActorLocation());
        if((Skill==1&&OneOf(C.ConfigId,{TEXT("changli"),TEXT("lingyang")}))||(Skill==2&&OneOf(C.ConfigId,{TEXT("zhezhi"),TEXT("jiyan")})))Metric=Route;
        if(Skill==2&&C.ConfigId==TEXT("rover_spectro"))Metric=Route;
        if(Skill==2&&C.ConfigId==TEXT("baizhi"))Metric=FVector::Dist2D(Lamps[C.Lamp].Position,T->GetActorLocation());
        if(Lamps[C.Lamp].Priority==1)LampRank=E.DismantleLamp!=INDEX_NONE?0:E.Heavy?1:2;
        if(Lamps[C.Lamp].Priority==2)LampRank=E.Shooter?0:1;
        const bool Better=SkillRank<BestSkillRank||(SkillRank==BestSkillRank&&(Metric<BestMetric||(Metric==BestMetric&&(LampRank<BestLampRank||(LampRank==BestLampRank&&Route<BestRoute)))));
        if(Better){BestSkillRank=SkillRank;BestMetric=Metric;BestLampRank=LampRank;BestRoute=Route;Best=T;}
    }
    return Best;
}

void FLNCampaignBattle::UpdateCharacters(float Delta)
{
    for(int32 CI=0;CI<Characters.Num();++CI)
    {
        auto& C=Characters[CI];auto* U=C.Actor.Get();if(!U)continue;
        if(!U->IsAlive())
        {
            C.Action=0;C.Target.Reset();
            if(Time+1e-8>=U->ReviveAt){U->Restore();Log(TEXT("character_revived"),UnitIdentity(U));}else continue;
        }
        if(C.Action&&Time+1e-8>=C.HitAt&&!C.Submitted&&Time+1e-8>=U->RootUntil)CommitAction(CI);
        if(C.Action&&Time+1e-8>=C.EndAt){C.Action=0;C.Target.Reset();if(C.ConfigId==TEXT("lumi")&&C.Skill==1)C.Returning=true;}
        if(Time+1e-8<U->RootUntil){if(C.Action&&!C.Submitted){Log(TEXT("action_miss"),FString::Printf(TEXT("action=%llu; target=%s; reason=crowd_control; x=%.2f; y=%.2f"),C.Action,*UnitIdentity(C.Target.Get()),U->GetActorLocation().X,U->GetActorLocation().Y));C.Action=0;}continue;}
        if(Time+1e-8>=C.DodgeReady&&!(C.Action&&C.Submitted&&Time+1e-8<C.HitAt+.1))
        {
            bool Threat=false;
            for(const auto& E:Enemies)if(E.AttackTarget.Get()==U&&E.AttackHitAt-Time>0&&E.AttackHitAt-Time<=.2+1e-6){Threat=true;break;}
            if(!Threat)for(const auto& P:Projectiles)if(P.Hostile&&P.Target.Get()==U&&FMath::PointDistToSegment(U->GetActorLocation(),P.Position,P.Position+P.Direction*P.Speed*.2f)<55){Threat=true;break;}
            if(Threat)
            {
                for(const auto& V:{FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0)})
                {
                    const FVector Destination=U->GetActorLocation()+V*200;
                    if(FVector::Dist2D(Destination,Lamps[C.Lamp].Position)>600||!SweptPathClear(U,U->GetActorLocation(),Destination))continue;
                    bool Safe=true;for(const auto& E:Enemies)if(E.Actor.IsValid()&&E.AttackTarget.IsValid()&&FVector::Dist2D(Destination,E.Actor->GetActorLocation())<250){Safe=false;break;}
                    if(!Safe)continue;
                    if(C.Action&&!C.Submitted)Log(TEXT("action_miss"),FString::Printf(TEXT("action=%llu; target=%s; reason=dodge_cancel; x=%.2f; y=%.2f"),C.Action,*UnitIdentity(C.Target.Get()),U->GetActorLocation().X,U->GetActorLocation().Y));
                    U->SetActorLocation(Destination,true);U->PlayCombatAction(3,Destination,.35f);C.Action=0;C.Target.Reset();C.DodgeReady=Time+4;C.ImmuneUntil=Time+.2;U->ActionReady=Time+.35;Log(TEXT("dodge"),UnitIdentity(U));break;
                }
            }
        }
        if(C.Action)
        {
            if(DashSkill(C.ConfigId,C.Skill)&&!C.Submitted&&C.Target.IsValid()&&FVector::Dist2D(U->GetActorLocation(),C.Target->GetActorLocation())>240)MoveUnit(U,C.Target->GetActorLocation(),1200,Delta);
            continue;
        }
        if(Time+1e-8<U->ActionReady)continue;
        if(C.Returning){MoveUnit(U,U->Station,450,Delta);if(FVector::Dist2D(U->GetActorLocation(),U->Station)<40)C.Returning=false;continue;}
        if(Lamps[C.Lamp].WaitStarted>=0&&GetNextSeat(C.Lamp)==C.Seat)continue;
        if(Time+1e-8<C.NextDecision)
        {
            if(C.Target.IsValid()&&!U->Stats.bRanged)MoveUnit(U,C.Target->GetActorLocation(),450,Delta);
            continue;
        }
        C.NextDecision=Time+.1;
        ALNCombatUnit* Target=nullptr;int32 Skill=0;
        // Preparation prioritizes the next conductor's marking/gathering skill.
        const bool Prepare=Lamps[C.Lamp].Energy->Value()>=80&&GetNextSeat(C.Lamp)==C.Seat;
        const int32 First=(Prepare&&OneOf(C.ConfigId,{TEXT("yangyang"),TEXT("camellya"),TEXT("buling")}))?1:2;
        for(int32 S:{First,3-First,0})
        {
            if((S==1&&Time+1e-8<U->SkillOneReady)||(S==2&&Time+1e-8<U->SkillTwoReady)||(S==0&&Time+1e-8<U->BasicReady))continue;
            Target=SelectTarget(CI,S);if(Target){Skill=S;break;}
        }
        C.Target=Target;
        if(!Target){MoveUnit(U,U->Station,450,Delta);continue;}
        const float Range=ActionRange(C.ConfigId,Skill,U->Stats.bRanged);
        const float Distance=FVector::Dist2D(U->GetActorLocation(),Target->GetActorLocation());
        if(Distance>(DashSkill(C.ConfigId,Skill)?700:Range))
        {if(!U->Stats.bRanged)MoveUnit(U,Target->GetActorLocation(),450,Delta);continue;}
        C.Action=++ActionSerial;C.Skill=Skill;C.Submitted=false;C.HitAt=Time+(Skill?.4:.3);C.EndAt=Time+(Skill?.6:1.);C.Aim=Target->GetActorLocation();U->ActionReady=C.EndAt;
        if(Skill==1)U->SkillOneReady=Time+6;else if(Skill==2)U->SkillTwoReady=Time+10;else U->BasicReady=Time+1;
        U->PlayCombatAction(Skill,C.Aim,Skill?.6f:1.f);
        if(Lamps[C.Lamp].FirstContact<0)Lamps[C.Lamp].FirstContact=Time;
        Log(TEXT("action_begin"),FString::Printf(TEXT("action=%llu; lamp=%d; actor=%s; skill=%d; target=%s; hit_at=%.6f; end_at=%.6f"),C.Action,C.Lamp+1,*C.ConfigId.ToString(),Skill,*UnitIdentity(Target),C.HitAt,C.EndAt));
    }
}

void FLNCampaignBattle::CommitAction(int32 CI)
{
    auto& C=Characters[CI];C.Submitted=true;auto* U=C.Actor.Get();auto* T=C.Target.Get();
    FString Reason;
    if(!T||!T->IsAlive())Reason=TEXT("target_invalid");
    else if(!Visible(U->GetActorLocation(),T->GetActorLocation()))Reason=TEXT("wall");
    else if(FVector::Dist2D(U->GetActorLocation(),T->GetActorLocation())>ActionRange(C.ConfigId,C.Skill,U->Stats.bRanged)+5)Reason=TEXT("out_of_range");
    if(!Reason.IsEmpty())
    {Log(TEXT("action_miss"),FString::Printf(TEXT("action=%llu; target=%s; x=%.3f; y=%.3f; reason=%s"),C.Action,*UnitIdentity(T),T?T->GetActorLocation().X:0,T?T->GetActorLocation().Y:0,*Reason));return;}
    ApplySkill(CI,C.Skill,T,C.Action);
}

void FLNCampaignBattle::ApplySkill(int32 CI,int32 S,ALNCombatUnit* Target,uint64 Action)
{
    auto& C=Characters[CI];auto* U=C.Actor.Get();const FName Id=C.ConfigId;const FVector Start=U->GetActorLocation();const FVector Direction=FlatDirection(Start,Target->GetActorLocation());
    const float Damage=S==0?30:S==1?90:150;FName Source(S?*FString::Printf(TEXT("%s_%d"),*Id.ToString(),S):TEXT("basic"));
    const bool Line=S&&LineSkill(Id,S),Area=S&&AreaSkill(Id,S),Cone=(!S&&!U->Stats.bRanged)||(S&&ConeSkill(Id,S));
    FVector Center=Target->GetActorLocation();if(Area&&!U->Stats.bRanged)Center=Start+Direction*200;
    if(S==2&&Id==TEXT("aalto"))
    {
        for(auto& E:Enemies)if(E.Actor.IsValid()&&E.Actor->IsAlive()&&!E.Heavy&&FVector::Dist2D(Center,E.Actor->GetActorLocation())<=400&&Visible(Center,E.Actor->GetActorLocation())){E.TauntUntil=Time+3;E.TauntPoint=Center;}
        FArea A;A.Center=Center;A.Lamp=C.Lamp;A.Radius=400;A.Damage=150;A.Resonance=4;A.NextTick=Time+3;A.Source=Source;A.Group=Source;A.Action=Action;A.CreditCharacter=CI;AddArea(A);return;
    }
    if((S==1&&Id==TEXT("yuanwu"))||(S==2&&Id==TEXT("camellya")))
    {
        FArea A;A.Center=Center;A.Lamp=C.Lamp;A.Radius=400;A.Damage=Damage/4;A.Resonance=4;A.NextTick=Time;A.Interval=1;A.Remaining=4;A.Expires=Time+4;A.Source=Source;A.Group=Source;A.Action=Action;A.CreditCharacter=CI;AddArea(A);return;
    }
    bool Hit=false;
    for(auto& E:Enemies)
    {
        auto* T=E.Actor.Get();if(!T||!T->IsAlive()||!Visible(Start,T->GetActorLocation()))continue;
        const FVector Offset=T->GetActorLocation()-Start;bool In=T==Target;
        if(Line){const float Along=FVector::DotProduct(Offset,Direction);const float Across=FMath::Abs(Offset.X*Direction.Y-Offset.Y*Direction.X);In=Along>=0&&Along<=(Id==TEXT("sanhua")?400:1200)&&Across<=100;}
        if(Area)In=FVector::Dist2D(Center,T->GetActorLocation())<=400;
        if(Cone)In=Offset.Size2D()<=300&&FVector::DotProduct(Offset.GetSafeNormal2D(),Direction)>=.5;
        if(!In)continue;Hit=true;
        DamageEnemy(T,Damage,C.Lamp,Source,Action,S?4:1);
        if(!T->IsAlive()||S==0)continue;
        const bool Mark=(S==2&&OneOf(Id,{TEXT("rover_spectro"),TEXT("jinhsi"),TEXT("chixia"),TEXT("mortefi"),TEXT("xiangliyao"),TEXT("danjin"),TEXT("zhezhi"),TEXT("verina"),TEXT("calcharo")}))||(S==1&&Id==TEXT("buling"));
        if(Mark)E.Marks.Add(Id,Time+6);
        if(S==2&&Id==TEXT("yinlin")){E.Marks.Add(Id,Time+6);int32 OtherCount=0;for(auto& Other:Enemies)if(Other.Actor.IsValid()&&Other.Actor->IsAlive()&&Other.Serial!=E.Serial&&FVector::Dist2D(T->GetActorLocation(),Other.Actor->GetActorLocation())<=400&&Visible(T->GetActorLocation(),Other.Actor->GetActorLocation())){Other.Marks.Add(Id,Time+6);if(++OtherCount==1)break;}}
        if((S==1&&OneOf(Id,{TEXT("rover_spectro"),TEXT("yinlin"),TEXT("verina")}))||(S==2&&Id==TEXT("buling")))if(!E.Heavy)Interrupt(E,2);
        if((S==1&&OneOf(Id,{TEXT("baizhi"),TEXT("youhu")}))||(S==2&&Id==TEXT("yangyang")))E.SlowUntil=Time+2;
        if(S==1&&OneOf(Id,{TEXT("chixia"),TEXT("aalto")})&&E.Shooter)E.SuppressedUntil=Time+2;
        if((S==2&&OneOf(Id,{TEXT("changli"),TEXT("baizhi"),TEXT("sanhua"),TEXT("taoqi"),TEXT("yuanwu"),TEXT("youhu"),TEXT("encore")}))||(S==1&&OneOf(Id,{TEXT("xiangliyao"),TEXT("yangyang_xuanling")})))Interrupt(E,.3f);
        if((S==1&&OneOf(Id,{TEXT("taoqi"),TEXT("jiyan"),TEXT("jianxin")}))||(S==2&&Id==TEXT("lingyang")))MoveEnemy(T,T->GetActorLocation()+Direction*300,300);
        if((S==1&&OneOf(Id,{TEXT("yangyang"),TEXT("camellya")}))||(S==2&&Id==TEXT("jianxin")))MoveEnemy(T,Id==TEXT("camellya")?Lamps[C.Lamp].Position:Center,300);
        if(S==2&&Id==TEXT("yangyang_xuanling")){const float Along=FMath::Clamp(FVector::DotProduct(Offset,Direction),0.,1200.);MoveEnemy(T,Start+Direction*Along,300);}
        if(S==1&&Id==TEXT("sanhua"))E.Marks.Add(TEXT("ice"),Time+6);
    }
    if(S==1&&Id==TEXT("sanhua")){FArea A;A.Center=Start;A.Direction=Direction;A.Shape=1;A.Radius=400;A.Width=200;A.Slow=.6f;A.Expires=Time+6;A.Remaining=60;A.Interval=.1;A.NextTick=Time;A.Source=TEXT("sanhua_ice");A.Group=A.Source;A.Lamp=C.Lamp;A.ApplyMark=true;AddArea(A);}
    if(Hit)
    {
        const double Gain=Lamps[C.Lamp].Energy->RecordHit(S?LongNight::HitSource::Skill:LongNight::HitSource::Basic,Action,Time);
        Log(TEXT("energy_hit"),FString::Printf(TEXT("action=%llu; actor=%s; lamp=%d; source=%s; accepted=%.3f; energy=%.3f"),Action,*Id.ToString(),C.Lamp+1,*Source.ToString(),Gain,Lamps[C.Lamp].Energy->Value()));
    }
    auto* FX=World->SpawnActor<ALNVisualEffect>();if(Line)FX->Beam(Start,Start+Direction*(Id==TEXT("sanhua")?400:1200),200,FLinearColor(.2f,.65f,1));else FX->Burst(Center,Area?400:Cone?300:80,FLinearColor(.3f,.8f,1));
}

void FLNCampaignBattle::AddArea(FArea A)
{
    if(!A.Action)A.Action=++ActionSerial;
    A.StartsAt=A.NextTick;A.Continuous=A.Remaining>1&&!A.SingleTarget;
    if(A.Expires<=0)A.Expires=A.NextTick+(A.Remaining-1)*A.Interval+.02;
    if(A.Group.IsNone())A.Group=A.Source;
    if(A.Source!=TEXT("lamp_shock"))
    {
        auto* FX=World->SpawnActor<ALNVisualEffect>();OwnedActors.Add(FX);
        if(A.Remaining==1){FX->Impact(A.Center,A.Radius,A.Source,A.Shape,A.Direction,A.Width);A.Visual=FX;A.ImpactVisual=true;}
        else{FX->Field(A.Center,A.Radius,float(A.Expires-A.NextTick),A.Shape,A.Direction,A.Width,A.Source);A.Visual=FX;}
        FX->SetStartDelay(FMath::Max(0.,A.NextTick-Time));
    }
    Areas.Add(A);
    Log(TEXT("area_created"),FString::Printf(TEXT("source=%s; lamp=%d; owner=%s; action=%llu; next=%.6f; expires=%.6f; damage=%.3f; ticks=%d"),*A.Source.ToString(),A.Lamp+1,A.LampOwned?TEXT("lamp"):TEXT("character"),A.Action,A.NextTick,A.Expires,A.Damage,A.Remaining));
}
bool FLNCampaignBattle::AreaContains(const FArea& A,const FEnemy& E) const
{
    auto* U=E.Actor.Get();if(!U||!U->IsAlive()||(A.HeavyOnly&&!E.Heavy))return false;
    if(A.Source==TEXT("coop_yinlin_mark_root")&&E.Marks.FindRef(A.Conductor)<=Time)return false;
    FVector Center=A.Center;Center.Z=U->GetActorLocation().Z;
    if(!Visible(Center,U->GetActorLocation()))return false;
    const FVector D=U->GetActorLocation()-Center;
    if(A.Shape==1){const double Along=FVector::DotProduct(D,A.Direction);return Along>=0&&Along<=A.Radius&&FMath::Abs(D.X*A.Direction.Y-D.Y*A.Direction.X)<=A.Width*.5;}
    if(A.Shape==2)return (FMath::Abs(D.X)<=A.Radius&&FMath::Abs(D.Y)<=A.Width*.5)||(FMath::Abs(D.Y)<=A.Radius&&FMath::Abs(D.X)<=A.Width*.5);
    return D.Size2D()<=A.Radius;
}
void FLNCampaignBattle::UpdateAreas()
{
    // Death stops lamp-owned fields immediately, while character fields keep their own lifetime.
    for(int32 L=0;L<Lamps.Num();++L)
    {
        std::array<bool,3> Alive{};for(int32 S=0;S<3;++S){auto* U=GetUnit(L,S);Alive[S]=U&&U->IsAlive();}
        Lamps[L].Relay.UpdateTeam(Alive);
        if(!Lamps[L].Relay.IsLit())for(auto& A:Areas)if(A.Lamp==L&&A.LampOwned)A.Remaining=0;
    }
    TArray<int32> Due;
    for(int32 I=0;I<Areas.Num();++I)if(Areas[I].Remaining>0&&Time+1e-8>=Areas[I].NextTick&&Time<=Areas[I].Expires+1e-8)Due.Add(I);
    for(int32 AI:Due)
    {
        auto& A=Areas[AI];bool Hit=false;int32 Hits=0;
        for(auto& E:Enemies)
        {
            if(!AreaContains(A,E))continue;
            // Evaluate every competing field before advancing any clock: final pulses cannot stack.
            bool Strongest=true;
            for(int32 Other=0;Other<Areas.Num();++Other)if(A.Continuous&&Other!=AI&&Areas[Other].Continuous&&Areas[Other].Remaining>0&&Time>=Areas[Other].StartsAt&&Time<=Areas[Other].Expires&&Areas[Other].Group==A.Group&&AreaContains(Areas[Other],E))
            {
                const float ThisStrength=A.Damage+A.Slow+A.RootSeconds;
                const float ThatStrength=Areas[Other].Damage+Areas[Other].Slow+Areas[Other].RootSeconds;
                if(ThatStrength>ThisStrength||(ThatStrength==ThisStrength&&Other<AI)){Strongest=false;break;}
            }
            if(!Strongest)continue;
            auto* U=E.Actor.Get();Hit|=A.Damage>0;
            if(A.Damage>0||A.Resonance>0)DamageEnemy(U,A.Damage,A.Lamp,A.Source,A.Action,A.Resonance);
            if(!U->IsAlive()){if(A.SingleTarget)break;continue;}
            if(A.Slow>0)E.SlowUntil=Time+2;
            if(A.ApplyMark)E.Marks.Add(A.Source==TEXT("sanhua_ice")?FName(TEXT("ice")):A.Conductor,Time+6);
            if(A.RootSeconds>0&&!E.Heavy&&(!A.RootOnce||!A.Affected.Contains(E.Serial)))Interrupt(E,A.RootSeconds);
            if(A.Interrupt)Interrupt(E,.3);
            if(A.Push)MoveEnemy(U,U->GetActorLocation()+FlatDirection(A.Center,U->GetActorLocation())*A.Width,A.Width);
            if(A.Pull)MoveEnemy(U,A.Center,300);
            A.Affected.Add(E.Serial);
            if(++Hits==1&&A.SingleTarget)break;
        }
        if(Hit&&Characters.IsValidIndex(A.CreditCharacter))
        {
            const double Gain=Lamps[A.Lamp].Energy->RecordHit(LongNight::HitSource::Skill,A.Action,Time);
            Log(TEXT("energy_hit"),FString::Printf(TEXT("action=%llu; actor=%s; lamp=%d; source=%s; accepted=%.3f; energy=%.3f"),A.Action,*Characters[A.CreditCharacter].ConfigId.ToString(),A.Lamp+1,*A.Source.ToString(),Gain,Lamps[A.Lamp].Energy->Value()));
        }
        if(Hit&&bTraining&&A.LampOwned&&Lamps[A.Lamp].LastRelay>=0)bTrainingRelay=true;
        if(!Hit&&A.Damage>0)Log(TEXT("action_miss"),FString::Printf(TEXT("action=%llu; source=%s; target=area; x=%.3f; y=%.3f; reason=no_valid_area_targets"),A.Action,*A.Source.ToString(),A.Center.X,A.Center.Y));
    }
    for(int32 AI:Due){if(Areas[AI].ImpactVisual)Areas[AI].Visual.Reset();--Areas[AI].Remaining;Areas[AI].NextTick+=Areas[AI].Interval;}
    for(int32 I=Areas.Num()-1;I>=0;--I)if(Areas[I].Remaining<=0||Time>Areas[I].Expires+1e-8){if(Areas[I].Visual.IsValid())Areas[I].Visual->Destroy();Areas.RemoveAt(I);}
}
bool FLNCampaignBattle::CanLampHit(int32 L,const ALNCombatUnit* U)const
{
    if(!Lamps.IsValidIndex(L)||!U||!U->IsAlive())return false;
    const auto& Lamp=Lamps[L];FVector Center=Lamp.Position;Center.Z=U->GetActorLocation().Z;
    if(!Visible(Center,U->GetActorLocation()))return false;
    const FVector D=U->GetActorLocation()-Center;
    if(Lamp.Module==1){const FVector Axis=FRotator(0,Lamp.Yaw,0).Vector();const double Along=FVector::DotProduct(D,Axis);return Along>=0&&Along<=2800&&FMath::Abs(D.X*Axis.Y-D.Y*Axis.X)<=150;}
    return D.Size2D()<=(Lamp.Module==2?1000:1800);
}
TArray<ALNCombatUnit*> FLNCampaignBattle::LampTargets(int32 L)const
{
    TArray<ALNCombatUnit*> Result;for(const auto& E:Enemies)if(CanLampHit(L,E.Actor.Get()))Result.Add(E.Actor.Get());
    const auto& Lamp=Lamps[L];const FVector Axis=FRotator(0,Lamp.Yaw,0).Vector();
    Result.Sort([this,&Lamp,Axis](const ALNCombatUnit& A,const ALNCombatUnit& B)
    {
        if(Lamp.Module==1)return FVector::DotProduct(A.GetActorLocation()-Lamp.Position,Axis)<FVector::DotProduct(B.GetActorLocation()-Lamp.Position,Axis);
        const auto& EA=Enemies[EnemyIndex(&A)];const auto& EB=Enemies[EnemyIndex(&B)];
        if(Lamp.Priority==0)return RemainingRoute(EA)<RemainingRoute(EB);
        if(Lamp.Priority==1){if((EA.DismantleLamp!=INDEX_NONE)!=(EB.DismantleLamp!=INDEX_NONE))return EA.DismantleLamp!=INDEX_NONE;if(EA.Heavy!=EB.Heavy)return EA.Heavy;}
        if(Lamp.Priority==2&&EA.Shooter!=EB.Shooter)return EA.Shooter;
        return RemainingRoute(EA)<RemainingRoute(EB);
    });return Result;
}

void FLNCampaignBattle::UpdateLamps(float Delta)
{
    for(int32 L=0;L<Lamps.Num();++L)
    {
        auto& Lamp=Lamps[L];std::array<bool,3> Alive{},Actionable{};int32 Available=0;
        for(int32 S=0;S<3;++S){auto* U=GetUnit(L,S);Alive[S]=U&&U->IsAlive();Actionable[S]=Alive[S]&&Time+1e-8>=U->RootUntil;if(Actionable[S])++Available;if(U)U->SetConductor(S==Lamp.CurrentSeat);}
        Lamp.Relay.UpdateTeam(Alive);
        if(!Lamp.Relay.IsLit()){Lamp.GuardUntil=0;Lamp.ReflectionsLeft=0;}
        if(Lamp.Conductor==TEXT("verina")&&Lamp.GuardUntil>0&&Time<=Lamp.GuardUntil+1e-8&&Time+1e-8>=Lamp.NextHeal){HealTeam(L,.02f);Lamp.NextHeal+=1;}
        LongNight::TargetAvailability Targets;Targets.Enemy=!LampTargets(L).IsEmpty();
        if(Lamp.Module==3)for(const auto& P:Projectiles)if(P.Hostile&&FVector::Dist2D(P.Position,Lamp.Position)<=1800&&Visible(Lamp.Position+FVector(0,0,P.Position.Z),P.Position)){Targets.Arrow=true;break;}
        for(const auto& T:Tasks)if(T.Progress<T.Required&&T.LampIndices.Contains(L)&&!Lamp.Relay.IsLit()){Targets.TaskLamp=true;break;}
        if(Targets.TaskLamp&&GetLivingEnemies()==0)Lamp.Energy->AddTaskFallback(Delta,false,false);
        const bool Gates=Available>=2&&Lamp.Energy->Value()>=100&&!Lamp.Relay.IsFaulted(Time)&&Time+1e-8>=Lamp.LastRelay+8&&(Targets.Enemy||Targets.Arrow||Targets.TaskLamp);
        if(Gates)
        {
            int32 Seat=Lamp.Relay.NextSeat();while(!Actionable[Seat])Seat=(Seat+1)%3;
            auto& C=Characters[L*3+Seat];auto* U=C.Actor.Get();
            if(Time+1e-8<U->ActionReady)
            {
                if(Lamp.WaitStarted<0){Lamp.WaitStarted=Time;Log(TEXT("relay_wait"),FString::Printf(TEXT("lamp=%d; seat=%d; action_end=%.6f; deadline=%.6f"),L+1,Seat,U->ActionReady,Time+1));}
                if(Time-Lamp.WaitStarted<1-1e-8){Lamp.Reason=TEXT("满能，等待下一执灯者动作结束（最多1秒）");if(Lamp.Visual.IsValid())Lamp.Visual->UpdateState(1,Lamp.Relay.IsLit(),false,false);continue;}
            }
            const bool WasLit=Lamp.Relay.IsLit();const auto Result=Lamp.Relay.TryTrigger(Time,*Lamp.Energy,Actionable,Targets);
            if(Result.Triggered)
            {
                Lamp.CurrentSeat=Result.Seat;Lamp.LastRelay=Time;Lamp.RelayTimes.Add(Time);Lamp.WaitStarted=-1;Lamp.Conductor=Characters[L*3+Result.Seat].ConfigId;
                Log(TEXT("relay_committed"),FString::Printf(TEXT("lamp=%s; seat=%d; conductor=%s; energy=%.3f; since_contact=%.6f; training=%d"),*Lamp.Id.ToString(),Result.Seat,*Lamp.Conductor.ToString(),Lamp.Energy->Value(),Lamp.FirstContact<0?-1:Time-Lamp.FirstContact,bTraining));
                if(!WasLit)
                {
                    const bool RecoveredFromFault=Lamp.AwaitingFaultRelight;
                    Lamp.AwaitingFaultRelight=false;
                    Log(RecoveredFromFault?TEXT("lamp_relit"):TEXT("lamp_lit"),FString::Printf(TEXT("lamp=%s; energy=%.3f"),*Lamp.Id.ToString(),Lamp.Energy->Value()));
                }
                FireLamp(L,Result.Seat);
            }
        }
        else Lamp.WaitStarted=-1;
        Lamp.Reason=Lamp.Relay.IsFaulted(Time)?FString::Printf(TEXT("故障 %.1f秒，保留能量"),Lamp.Relay.FaultRemaining(Time)):Available<2?TEXT("至少两人可行动"):Lamp.Energy->Value()<100?TEXT("有效命中蓄能"):Time<Lamp.LastRelay+8?TEXT("等待8秒接力间隔"):!Targets.Enemy&&!Targets.Arrow&&!Targets.TaskLamp?TEXT("满能保留，等待有效目标"):TEXT("接力就绪");
        if(Lamp.Visual.IsValid())Lamp.Visual->UpdateState(Lamp.Energy->Value()/100,Lamp.Relay.IsLit(),Lamp.Relay.IsFaulted(Time),Lamp.Energy->Value()>=80);
    }
}

void FLNCampaignBattle::FireLamp(int32 L,int32 Seat)
{
    auto& Lamp=Lamps[L];const FName Id=Characters[L*3+Seat].ConfigId;auto Targets=LampTargets(L);
    const FVector Axis=FRotator(0,Lamp.Yaw,0).Vector();FVector Center=Targets.IsEmpty()?Lamp.Position+FVector(0,0,65):Targets[0]->GetActorLocation();
    Lamp.CoopUsed=false;Lamp.FirstReflectionUsed=false;Lamp.ReflectionsUsed=0;
    if(Lamp.Module==0)
    {
        if(OneOf(Id,{TEXT("rover_spectro"),TEXT("zhezhi"),TEXT("verina"),TEXT("buling")}))for(auto* T:Targets)if(Enemies[EnemyIndex(T)].Marks.FindRef(Id)>Time){Center=T->GetActorLocation();break;}
        if(OneOf(Id,{TEXT("yangyang"),TEXT("jianxin"),TEXT("danjin"),TEXT("camellya")}))
        {
            int32 BestCount=-1;float Lowest=MAX_flt;for(auto* T:Targets){int32 Count=0;for(auto* Other:Targets)if(FVector::Dist2D(T->GetActorLocation(),Other->GetActorLocation())<=600)++Count;
                if(Count>BestCount||(Count==BestCount&&T->Health<Lowest)){BestCount=Count;Lowest=T->Health;Center=T->GetActorLocation();}}
        }
        if(OneOf(Id,{TEXT("yangyang"),TEXT("jianxin")}))for(auto& E:Enemies)if(E.Actor.IsValid()&&E.Actor->IsAlive()&&FVector::Dist2D(Center,E.Actor->GetActorLocation())<=600&&Visible(Center,E.Actor->GetActorLocation()))MoveEnemy(E.Actor.Get(),Center,300);
        if(Id==TEXT("jiyan"))for(auto& E:Enemies)if(E.Actor.IsValid()&&E.Actor->IsAlive()&&FVector::Dist2D(Center,E.Actor->GetActorLocation())<=900&&Visible(Center,E.Actor->GetActorLocation()))MoveEnemy(E.Actor.Get(),Center,300);
        const double Delay=Id==TEXT("aalto")?.5:0;
        if(Id==TEXT("aalto"))for(auto& E:Enemies)if(E.Actor.IsValid()&&E.Actor->IsAlive()&&!E.Heavy&&FVector::Dist2D(Center,E.Actor->GetActorLocation())<=600){E.TauntUntil=Time+.5;E.TauntPoint=Center;}
        FArea Burst;Burst.Center=Center;Burst.Lamp=L;Burst.Radius=600;Burst.Damage=600;Burst.NextTick=Time+Delay;Burst.Source=TEXT("lamp_flame_burst");Burst.LampOwned=true;AddArea(Burst);
        FArea Burn=Burst;Burn.Damage=100;Burn.NextTick=Time+Delay+1;Burn.Remaining=4;Burn.Interval=1;Burn.Source=TEXT("lamp_flame_burn");Burn.Group=Burn.Source;AddArea(Burn);
    }
    else if(Lamp.Module==1)
    {
        // The approved Yangyang correction gathers before the unchanged fixed beam hit.
        if(Id==TEXT("yangyang")&&!Targets.IsEmpty())
        {
            ALNCombatUnit* Dense=nullptr;int32 Best=-1;
            for(auto* T:Targets)if(!Enemies[EnemyIndex(T)].Heavy){int32 N=0;for(const auto& E:Enemies)if(E.Actor.IsValid()&&E.Actor->IsAlive()&&!E.Heavy&&FVector::Dist2D(T->GetActorLocation(),E.Actor->GetActorLocation())<=400&&Visible(T->GetActorLocation(),E.Actor->GetActorLocation()))++N;if(N>Best){Best=N;Dense=T;}}
            if(Dense)
            {
                const FVector Gather=Dense->GetActorLocation();
                for(auto& E:Enemies)if(E.Actor.IsValid()&&E.Actor->IsAlive()&&!E.Heavy&&FVector::Dist2D(Gather,E.Actor->GetActorLocation())<=400&&Visible(Gather,E.Actor->GetActorLocation()))
                {const double Along=FMath::Clamp(FVector::DotProduct(E.Actor->GetActorLocation()-Lamp.Position,Axis),0.,2800.);MoveEnemy(E.Actor.Get(),Lamp.Position+Axis*Along+FVector(0,0,65),300);}
                Log(TEXT("coop_prepared"),TEXT("effect=pull_to_fixed_beam; before=lamp_beam; max_pull=300; extra_damage=0"));
            }
            Targets=LampTargets(L);
        }
        bool Bonus=false;const uint64 BeamAction=++ActionSerial;
        for(auto* T:Targets){const bool Heavy=Enemies[EnemyIndex(T)].Heavy;DamageEnemy(T,600+(Heavy&&!Bonus?900:0),L,TEXT("lamp_beam"),BeamAction);if(Heavy)Bonus=true;if(bTraining)bTrainingRelay=true;}
        auto* FX=World->SpawnActor<ALNVisualEffect>();FX->Beam(Lamp.Position+FVector(0,0,80),Lamp.Position+Axis*2800+FVector(0,0,80),300,FLinearColor(.15f,.75f,1),.5);
    }
    else if(Lamp.Module==2)
    {
        for(int32 Pulse=0;Pulse<3;++Pulse){FArea A;A.Center=Lamp.Position+FVector(0,0,65);A.Radius=1000;A.Lamp=L;A.Damage=330;A.NextTick=Time+Pulse;A.Source=TEXT("lamp_shock");A.Group=A.Source;A.LampOwned=true;A.Resonance=Pulse==0?15:0;A.Push=Pulse==0;A.Width=300;AddArea(A);}
    }
    else
    {
        Lamp.GuardUntil=Time+(Id==TEXT("taoqi")?8:6);Lamp.ReflectionsLeft=Id==TEXT("jianxin")?5:3;Lamp.NextHeal=Time+1;
        for(auto& E:Enemies)if(E.DismantleLamp==L){E.DismantleProgress=0;Log(TEXT("dismantle_cancelled"),FString::Printf(TEXT("enemy=%d; lamp=%s; cause=reflect_guard"),E.Serial,*Lamp.Id.ToString()));}
        bool TwoEntries=false;
        if(Id==TEXT("lumi")&&Targets.Num()>1){const int32 FirstEntry=Enemies[EnemyIndex(Targets[0])].Entry;for(int32 I=1;I<Targets.Num();++I)if(Enemies[EnemyIndex(Targets[I])].Entry!=FirstEntry){Targets.Swap(1,I);TwoEntries=true;break;}}
        for(int32 Arrow=0;Arrow<3&&!Targets.IsEmpty();++Arrow){const int32 TargetIndex=TwoEntries?Arrow%2:Arrow%Targets.Num();AddProjectile(Lamp.Position+FVector(0,0,175),Targets[TargetIndex],250,L,TEXT("reflect_active"),false);Projectiles.Last().FirstArrow=Arrow==0;}
    }
    if(Lamp.Visual.IsValid())Lamp.Visual->Trigger(Lamp.Module==3?float(Lamp.GuardUntil-Time):6.f);
    ApplyCoop(L,Id,Center,Targets);
}

void FLNCampaignBattle::ApplyCoop(int32 L,FName Id,FVector Center,const TArray<ALNCombatUnit*>& Targets)
{
    auto& Lamp=Lamps[L];const int32 Module=Lamp.Module;const FVector Axis=FRotator(0,Lamp.Yaw,0).Vector();
    const FName Source(*FString::Printf(TEXT("coop_%s_%d"),*Id.ToString(),Module));
    auto Area=[&](FVector At,float Radius,float Damage,double Delay=0.,int32 Ticks=1)
    {FArea A;A.Center=At;A.Radius=Radius;A.Damage=Damage;A.NextTick=Time+Delay;A.Remaining=Ticks;A.Interval=1;A.Lamp=L;A.Source=Source;A.Group=Source;A.LampOwned=true;A.Conductor=Id;return A;};
    auto Marked=[&](ALNCombatUnit* U,FName Mark){const int32 E=EnemyIndex(U);return E!=INDEX_NONE&&Enemies[E].Marks.FindRef(Mark)>Time;};
    auto AddLine=[&](double Delay){FArea A=Area(Lamp.Position+FVector(0,0,65),1200,240,Delay);A.Shape=1;A.Direction=Targets.IsEmpty()?Axis:FlatDirection(Lamp.Position,Targets[0]->GetActorLocation());A.Width=200;AddArea(A);};
    bool Supported=false;const auto* Definition=CharacterDefinitions.Find(Id);
    if(Definition){const TSharedPtr<FJsonObject>* Coop=nullptr;const TSharedPtr<FJsonObject>* Entry=nullptr;if((*Definition)->TryGetObjectField(TEXT("coop"),Coop)&&(*Coop)->TryGetObjectField(FString::FromInt(Module),Entry))Supported=true;}
    if(!Supported)return;
    if(Module==0)
    {
        if(Id==TEXT("changli")){FArea A=Area(Center,600,60,1,4);A.Shape=2;A.Width=200;AddArea(A);}
        else if(Id==TEXT("baizhi")){FArea A=Area(Center,600,0);A.Slow=.6;A.Interval=.1;A.Remaining=20;A.Expires=Time+2;AddArea(A);}
        else if(Id==TEXT("sanhua")){for(auto* U:Targets)if(Marked(U,TEXT("ice")))DamageEnemy(U,240,L,Source);}
        else if(Id==TEXT("mortefi")){for(auto* U:Targets)if(Marked(U,Id))AddArea(Area(U->GetActorLocation(),400,240));}
        else if(Id==TEXT("zhezhi")){for(auto* U:Targets)if(Marked(U,Id)){AddArea(Area(U->GetActorLocation(),300,240));break;}}
        else if(Id==TEXT("verina")){FArea A=Area(Center,600,0);A.RootSeconds=2;A.RootOnce=true;A.Interval=.1;A.Remaining=40;A.Expires=Time+4;AddArea(A);}
        else if(Id==TEXT("buling"))AddArea(Area(Center,300,240,2));
        else if(Id==TEXT("encore"))AddArea(Area(Center,300,240,1.5));
        else if(Id==TEXT("camellya"))AddArea(Area(Center,600,240,4));
        // Rover/Yangyang/Jiyan/Jianxin/Danjin/Aalto alter selection or gathering before the base hit.
    }
    else if(Module==1)
    {
        if(OneOf(Id,{TEXT("rover_spectro"),TEXT("jinhsi"),TEXT("chixia"),TEXT("xiangliyao"),TEXT("danjin")}))
        {
            for(auto* U:Targets)if(Marked(U,Id)&&(Id!=TEXT("danjin")||U->Health/U->Stats.MaxHealth<.3f)){DamageEnemy(U,240,L,Source);break;}
        }
        else if(Id==TEXT("sanhua"))
        {
            TArray<FArea> Ice;for(const auto& A:Areas)if(A.Source==TEXT("sanhua_ice"))Ice.Add(A);
            for(const auto& IceArea:Ice)
            {
                bool Intersects=false;for(auto* U:Targets)if(AreaContains(IceArea,Enemies[EnemyIndex(U)])){Intersects=true;break;}
                if(Intersects){FArea A=Area(IceArea.Center,400,240);A.Shape=1;A.Direction=IceArea.Direction;A.Width=200;AddArea(A);}
            }
        }
        else if(Id==TEXT("yuanwu"))
        {for(auto* U:Targets)for(const auto& A:Areas)if(A.Source==TEXT("yuanwu_1")&&AreaContains(A,Enemies[EnemyIndex(U)])){DamageEnemy(U,240,L,Source);break;}}
        else if(Id==TEXT("yinlin"))
        {
            TArray<ALNCombatUnit*> Nodes;bool HitNode=false;for(auto* U:Targets)HitNode|=Marked(U,Id);
            if(HitNode){for(const auto& E:Enemies)if(E.Actor.IsValid()&&E.Actor->IsAlive()&&E.Marks.FindRef(Id)>Time)Nodes.Add(E.Actor.Get());const int32 N=FMath::Min(2,Nodes.Num());for(int32 I=0;I<N;++I)DamageEnemy(Nodes[I],240.f/N,L,Source);}
        }
        else if(Id==TEXT("zhezhi")&&Targets.Num()>1)DamageEnemy(Targets[1],240,L,Source);
        else if(Id==TEXT("lumi")&&!Targets.IsEmpty())AddProjectile(Lamp.Position+Axis*2800+FVector(0,0,100),Targets.Last(),240,L,Source,false);
        else if(Id==TEXT("buling"))
        {
            for(auto* U:Targets)if(Marked(U,Id)){for(auto& E:Enemies)if(E.Actor.IsValid()&&E.Actor->IsAlive()&&!E.Heavy&&E.Actor.Get()!=U&&FVector::Dist2D(U->GetActorLocation(),E.Actor->GetActorLocation())<=400&&Visible(U->GetActorLocation(),E.Actor->GetActorLocation())){DamageEnemy(E.Actor.Get(),240,L,Source);break;}break;}
        }
        else if(Id==TEXT("calcharo")&&!Targets.IsEmpty())AddArea(Area(Targets.Last()->GetActorLocation(),400,240));
        else if(Id==TEXT("yangyang_xuanling")){FArea A=Area(Lamp.Position+FVector(0,0,65),2800,240,.2);A.Shape=1;A.Direction=Axis;A.Width=300;AddArea(A);}
    }
    else if(Module==2)
    {
        const FVector Origin=Lamp.Position+FVector(0,0,65);
        if(Id==TEXT("taoqi"))for(auto* U:Targets){auto& E=Enemies[EnemyIndex(U)];if(E.DismantleLamp!=INDEX_NONE){E.DismantleProgress=0;E.RetryDismantleAt=Time+1;Log(TEXT("dismantle_cancelled"),FString::Printf(TEXT("enemy=%d; cause=taoqi_progress_reset"),E.Serial));}}
        else if(OneOf(Id,{TEXT("changli"),TEXT("yangyang_xuanling")}))AddLine(2);
        else if(Id==TEXT("yuanwu")){FArea A=Area(Origin,1000,80,0,3);A.HeavyOnly=true;A.SingleTarget=true;AddArea(A);}
        else if(Id==TEXT("jiyan")){FArea A=Area(Origin,1000,0,2);A.Push=true;A.Width=600;AddArea(A);}
        else if(Id==TEXT("yinlin"))
        {FArea A=Area(Origin,1000,0,2);A.RootSeconds=2;A.RootOnce=true;A.Conductor=Id;A.Source=TEXT("coop_yinlin_mark_root");AddArea(A);}
        else if(Id==TEXT("xiangliyao")){FArea A=Area(Origin,1000,0,2);A.HeavyOnly=true;A.SingleTarget=true;A.Resonance=20;AddArea(A);}
        else if(Id==TEXT("lingyang")){FArea A=Area(Origin,1000,0,2);A.Slow=.6;A.Interval=.1;A.Remaining=40;A.Expires=Time+6;AddArea(A);}
        else if(Id==TEXT("youhu"))AddArea(Area(Origin,1000,240,2));
        else if(Id==TEXT("encore"))AddArea(Area(Center,300,240,3));
        else if(Id==TEXT("camellya")){FArea A=Area(Origin,1000,0,2);A.Pull=true;AddArea(A);}
    }
    else
    {
        if(Id==TEXT("chixia"))for(int32 I=0;I<3&&!Targets.IsEmpty();++I)AddProjectile(Lamp.Position+FVector(0,0,150),Targets[I%Targets.Num()],80,L,Source,false);
        else if(Id==TEXT("lingyang")){FArea A=Area(Lamp.Position+Axis*300+FVector(0,0,65),300,240);AddArea(A);}
        // Mark splashes, first-arrow suppression and actual-counter heals are committed at projectile contact.
        // Guard duration, counter capacity, split-entry targeting and timed heals are assigned by FireLamp.
    }
    Log(TEXT("coop_committed"),FString::Printf(TEXT("lamp=%s; conductor=%s; module=%d; source=%s"),*Lamp.Id.ToString(),*Id.ToString(),Module,*Source.ToString()));
}

bool FLNCampaignBattle::RunInvariantChecks(FString& OutReport)
{
    if(!bValid||!World||Characters.Num()<3){OutReport=TEXT("Battle fixture unavailable");return false;}
    bool Passed=true;TArray<FString> Results;
    auto Check=[&](bool Value,const TCHAR* Name){Passed&=Value;Results.Add(FString::Printf(TEXT("%s: %s"),Name,Value?TEXT("PASS"):TEXT("FAIL")));};
    Log(TEXT("state_test"),TEXT("begin; isolated fixture; excluded from natural-play evidence"));
    LongNight::EnergyPool Energy;uint64 Action=1000;
    for(int I=0;I<3;++I)Energy.RecordHit(LongNight::HitSource::Skill,++Action,.4);
    Check(Energy.Value()==60,TEXT("skill window accepts 20 of three hits"));
    for(int I=0;I<3;++I)Energy.RecordHit(LongNight::HitSource::Skill,++Action,1.0);
    Check(Energy.Value()==60,TEXT("second skill inside rolling window credits zero"));
    for(double T:{1.5,2.5,3.5})for(int I=0;I<3;++I)Energy.RecordHit(LongNight::HitSource::Basic,++Action,T);
    Check(Energy.Value()==100,TEXT("basic 15-per-second cap reaches 100 at 3.5"));
    Check(Energy.RecordHit(LongNight::HitSource::Lamp,++Action,4)==0&&Energy.RecordHit(LongNight::HitSource::Coop,++Action,4)==0,TEXT("lamp and synergy never produce energy"));
    auto SavedEnemies=MoveTemp(Enemies);auto SavedCharacters=MoveTemp(Characters);auto SavedLamps=MoveTemp(Lamps);auto SavedAreas=MoveTemp(Areas);auto SavedProjectiles=MoveTemp(Projectiles);
    const double SavedTime=Time;const uint64 SavedAction=ActionSerial;const bool SavedTraining=bTraining,SavedRelay=bTrainingRelay;const int32 SavedKills=Killed;
    TArray<ALNCombatUnit*> Fixtures;
    auto SpawnFixture=[&](bool Enemy,FVector At){auto* U=World->SpawnActor<ALNCombatUnit>(At,FRotator::ZeroRotator);FLNCharacterRow Row;Row.PersonId=Enemy?FName(TEXT("enemy_light")):FName(TEXT("rover_spectro"));Row.MaxHealth=10000;U->Configure(Enemy,Row,0,FLinearColor::White);Fixtures.Add(U);return U;};
    Lamps.AddDefaulted();Lamps[0].Position=FVector(-50000,-50000,0);Lamps[0].Module=1;Lamps[0].Yaw=0;Lamps[0].Id=TEXT("test_lamp");Lamps[0].Relay.Reset();Lamps[0].Energy->Reset(100);
    for(int32 I=0;I<3;++I){FCharacter C;C.Actor=SpawnFixture(false,Lamps[0].Position+FVector(-200,I*100,65));C.Lamp=0;C.Seat=I;C.ConfigId=TEXT("yangyang");Characters.Add(C);}
    for(int32 I=0;I<2;++I){FEnemy E;E.Actor=SpawnFixture(true,Lamps[0].Position+FVector(800,I?350:100,65));E.Serial=I+1;Enemies.Add(E);}
    bTraining=false;Time=3.5;Characters[0].Actor->ActionReady=4.2;UpdateLamps(0);
    Check(Lamps[0].RelayTimes.IsEmpty()&&Lamps[0].Energy->Value()==100,TEXT("busy conductor cannot relay at 3.5"));
    Time=4.183333333;UpdateLamps(0);Check(Lamps[0].RelayTimes.IsEmpty(),TEXT("wait remains until logical action end"));
    Time=4.2;UpdateLamps(0);
    Check(Lamps[0].RelayTimes.Num()==1&&Lamps[0].Energy->Value()==0,TEXT("relay commits at 4.2 and consumes 100"));
    Check(Enemies[1].Actor->Health==9400&&FMath::Abs(Enemies[1].Actor->GetActorLocation().Y-Lamps[0].Position.Y)<=150,TEXT("Yangyang gathers off-line light before fixed beam damage"));
    Check(Lamps[0].Yaw==0&&Lamps[0].Position.Equals(FVector(-50000,-50000,0)),TEXT("gather preserves fixed beam origin and direction"));
    const int32 Before=Lamps[0].RelayTimes.Num();Lamps[0].Energy->Reset(100);Time=5;UpdateLamps(0);Check(Lamps[0].RelayTimes.Num()==Before,TEXT("relay minimum interval remains eight seconds"));
    for(auto& A:Areas)if(A.Visual.IsValid())A.Visual->Destroy();for(auto* U:Fixtures)U->Destroy();
    Enemies=MoveTemp(SavedEnemies);Characters=MoveTemp(SavedCharacters);Lamps=MoveTemp(SavedLamps);Areas=MoveTemp(SavedAreas);Projectiles=MoveTemp(SavedProjectiles);Time=SavedTime;ActionSerial=SavedAction;bTraining=SavedTraining;bTrainingRelay=SavedRelay;Killed=SavedKills;
    FString ConfigurationReport;Passed&=RunConfigurationChecks(ConfigurationReport);
    OutReport=FString::Join(Results,TEXT("\n"))+TEXT("\n")+ConfigurationReport;Log(TEXT("state_test"),Passed?TEXT("end; passed=1"):TEXT("end; passed=0"));return Passed;
}
