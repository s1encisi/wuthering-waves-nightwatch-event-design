#include "LNCampaignBattle.h"
#include "LNCombatUnit.h"
#include "LNData.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
enum EExpectedSkill : uint32
{
    Plain=0,Mark=1,Root=2,Slow=4,Push=8,Pull=16,Interrupts=32,Suppress=64,Ice=128,
    TwoMarks=256,Delayed=512,FourTicks=1024,Dash=2048,Returns=4096,Line=8192,Cone=16384,Area=32768,HeavyTarget=65536
};
struct FSkillExpectation {const TCHAR* Id;uint32 First,Second;};
// Expected effects are transcribed from the authored skill descriptions, not dispatch logs.
const FSkillExpectation SkillExpectations[]={
    {TEXT("rover_spectro"),Cone|Root,Mark},{TEXT("jinhsi"),Line,Mark|HeavyTarget},
    {TEXT("changli"),Dash,Interrupts},{TEXT("yangyang"),Area|Pull,Line|Slow},
    {TEXT("chixia"),Suppress,Mark|HeavyTarget},{TEXT("baizhi"),Area|Slow,Interrupts},
    {TEXT("sanhua"),Line|Ice,Interrupts},{TEXT("taoqi"),Cone|Push,Interrupts},
    {TEXT("yuanwu"),Area|FourTicks,Interrupts},{TEXT("jiyan"),Cone|Push,Line},
    {TEXT("jianxin"),Cone|Push,Area|Pull},{TEXT("yinlin"),Root,TwoMarks},
    {TEXT("mortefi"),Plain,Mark|HeavyTarget},{TEXT("xiangliyao"),Interrupts,Mark|HeavyTarget},
    {TEXT("lingyang"),Dash,Cone|Push},{TEXT("danjin"),Plain,Mark|HeavyTarget},
    {TEXT("zhezhi"),Plain,Mark},{TEXT("verina"),Root,Mark},
    {TEXT("youhu"),Area|Slow,Interrupts},{TEXT("lumi"),Dash|Returns,Plain},
    {TEXT("buling"),Mark|HeavyTarget,Root},{TEXT("aalto"),Suppress,Delayed},
    {TEXT("encore"),Area,Interrupts},{TEXT("camellya"),Area|Pull,Area|FourTicks},
    {TEXT("calcharo"),Plain,Mark|HeavyTarget},{TEXT("yangyang_xuanling"),Line|Interrupts,Line|Pull}
};
FString ScenarioString(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key)
{FString Value;if(Object)Object->TryGetStringField(Key,Value);return Value;}
}

bool FLNCampaignBattle::RunConfigurationChecks(FString& OutReport)
{
    OutReport=TEXT("CONFIGURATION SCENARIOS: isolated state injection; not natural-play evidence.\n");
    if(!World||!Campaign){OutReport+=TEXT("FAIL fixture_world_or_campaign_missing\n");return false;}
    Log(TEXT("test_state_injection"),TEXT("configuration scenarios at isolated coordinates; gameplay profile untouched"));
    TSet<AActor*> ExistingActors;for(TActorIterator<AActor> It(World);It;++It)ExistingActors.Add(*It);
    const FVector Origin(-80000,-80000,0);
    TUniquePtr<FLNCampaignBattle> F;
    int32 Checks=0,Failures=0,SkillCases=0,CoopCases=0,Missing=0;
    TSet<FName> TestedSkills,TestedCoops;
    auto Check=[&](bool Passed,const FString& Id,const FString& Expectation)
    {
        ++Checks;if(!Passed)++Failures;
        OutReport+=FString::Printf(TEXT("%s %s | %s\n"),Passed?TEXT("PASS"):TEXT("FAIL"),*Id,*Expectation);
    };
    auto Cleanup=[&]()
    {
        if(!F)return;
        for(const auto& Effect:F->Areas)if(Effect.Visual.IsValid())Effect.Visual->Destroy();
        F->Shutdown();F.Reset();
    };
    auto Reset=[&](FName Id,int32 Module)
    {
        Cleanup();F=MakeUnique<FLNCampaignBattle>();F->World=World;F->Campaign=Campaign;F->Stage=Stage;
        F->CharacterDefinitions=CharacterDefinitions;F->Time=10;F->bValid=true;
        FLamp Lamp;Lamp.Position=Origin;Lamp.Id=TEXT("scenario_lamp");Lamp.Module=Module;Lamp.Conductor=Id;Lamp.LastRelay=10;
        Lamp.Energy->Reset(100);Lamp.Relay.Reset();Lamp.Relay.TryTrigger(10,*Lamp.Energy,{true,true,true},{true,false,false});F->Lamps.Add(MoveTemp(Lamp));
        for(int32 Seat=0;Seat<3;++Seat)
        {
            auto* Unit=World->SpawnActor<ALNCombatUnit>(Origin+FVector(0,Seat*120,65),FRotator::ZeroRotator);
            if(!Unit)return false;
            FLNCharacterRow Row;Row.PersonId=Id;Row.MaxHealth=1000;
            if(const auto Definition=CharacterDefinitions.FindRef(Id)){bool Ranged=false;Definition->TryGetBoolField(TEXT("ranged"),Ranged);Row.bRanged=Ranged;}
            Unit->Configure(false,Row,Seat,FLinearColor::White);if(Seat>0)Unit->ActionReady=1000;
            FCharacter Character;Character.Actor=Unit;Character.ConfigId=Seat==0?Id:FName(TEXT("scenario_support"));Character.Lamp=0;Character.Seat=Seat;
            Character.Data=CharacterDefinitions.FindRef(Id);F->Characters.Add(Character);F->OwnedActors.Add(Unit);
        }
        return true;
    };
    auto Enemy=[&](FVector Offset,bool Heavy=false,bool Shooter=false,int32 Entry=0)->ALNCombatUnit*
    {
        auto* Unit=World->SpawnActor<ALNCombatUnit>(Origin+Offset+FVector(0,0,65),FRotator::ZeroRotator);
        if(!Unit)return nullptr;
        FLNCharacterRow Row;Row.PersonId=Heavy?TEXT("enemy_heavy"):TEXT("enemy_light");Row.MaxHealth=10000;
        Unit->bHeavy=Heavy;Unit->Configure(true,Row,-1,FLinearColor::White);
        FEnemy Item;Item.Actor=Unit;Item.Heavy=Heavy;Item.Shooter=Shooter;Item.Entry=Entry;Item.Serial=F->Enemies.Num()+1;
        Unit->Route.Add(Unit->GetActorLocation()+FVector(1000+F->Enemies.Num()*100,0,0));
        F->Enemies.Add(Item);F->OwnedActors.Add(Unit);return Unit;
    };
    auto Advance=[&](double Seconds,bool Lamps=false)
    {
        const double Begin=F->Time;const int32 Steps=FMath::CeilToInt(Seconds*60);
        for(int32 Tick=1;Tick<=Steps;++Tick)
        {F->Time=Begin+Tick/60.;F->UpdateProjectiles(1.f/60.f);F->UpdateAreas();if(Lamps)F->UpdateLamps(1.f/60.f);}
    };
    auto Damage=[&](int32 Index){return 10000.f-F->Enemies[Index].Actor->Health;};
    auto Near=[](double A,double B){return FMath::Abs(A-B)<.05;};

    for(const auto& Spec:SkillExpectations)for(int32 Skill=1;Skill<=2;++Skill)
    {
        const FName Id(Spec.Id);const uint32 Expected=Skill==1?Spec.First:Spec.Second;
        const FString Key=FString::Printf(TEXT("%s_%d"),Spec.Id,Skill);const auto Definition=CharacterDefinitions.FindRef(Id);
        if(!Definition){++Missing;OutReport+=TEXT("MISSING ")+Key+TEXT(" no JSON character definition\n");continue;}
        const FString Description=ScenarioString(Definition,Skill==1?TEXT("skill1_description"):TEXT("skill2_description"));
        if(!Reset(Id,0)){Check(false,Key,TEXT("spawn fixture"));continue;}
        auto* Primary=Enemy(FVector(Expected&Dash?650:200,0,0),(Expected&HeavyTarget)!=0,(Expected&Suppress)!=0);
        auto* Secondary=Enemy(FVector(Expected&Cone?250:320,60,0));auto* Outside=Enemy(FVector(1400,600,0));
        if(!Primary||!Secondary||!Outside){Check(false,Key,TEXT("spawn target fixture"));continue;}
        if(Id==TEXT("sanhua")&&Skill==2)F->Enemies[0].Marks.Add(TEXT("ice"),16);
        if((Expected&Interrupts)||(Id==TEXT("calcharo")&&Skill==2)){F->Enemies[0].DismantleLamp=0;F->Enemies[0].DismantleProgress=2;}
        const FVector Before=Primary->GetActorLocation(),SecondaryBefore=Secondary->GetActorLocation();
        if(Expected&Dash)
        {
            // Exercise the accepted action and actual swept movement, not ApplySkill alone.
            F->Enemies[1].Actor->SetActorLocation(Origin+FVector(3000,3000,65),false);
            auto* Unit=F->Characters[0].Actor.Get();Unit->SkillTwoReady=1000;Unit->BasicReady=1000;float PeakTravel=0;
            for(int32 Tick=0;Tick<=110;++Tick)
            {F->Time=10+Tick/60.;F->UpdateCharacters(1.f/60.f);PeakTravel=FMath::Max(PeakTravel,float(FVector::Dist2D(Unit->GetActorLocation(),Origin)));}
            Check(PeakTravel>200,Key,TEXT("dash travels toward the selected light enemy"));
            if(Expected&Returns)Check(FVector::Dist2D(Unit->GetActorLocation(),Unit->Station)<45,Key,TEXT("returns to station after strike"));
        }
        else F->ApplySkill(0,Skill,Primary,1);
        if(Expected&Delayed)Check(Near(Damage(0),0)&&F->Enemies[0].TauntUntil>F->Time,Key,TEXT("taunts first; delayed explosion has not hit immediately"));
        if(Expected&FourTicks)Check(Near(Damage(0),0),Key,TEXT("persistent skill submits a field before its first tick"));
        Advance(Expected&Delayed?3.2:Expected&FourTicks?4.2:.1);
        const double ExpectedDamage=Definition->GetNumberField(Skill==1?TEXT("skill1_damage"):TEXT("skill2_damage"));
        Check(Near(Damage(0),ExpectedDamage),Key,Description+FString::Printf(TEXT("; target damage %.1f / %.1f"),Damage(0),ExpectedDamage));
        Check(Near(Damage(2),0),Key,TEXT("outside geometry receives no damage"));
        if(Expected&(Line|Cone|Area))Check(Near(Damage(1),ExpectedDamage),Key,TEXT("a second target inside the authored shape is hit"));
        if(Expected&Mark)Check(F->Enemies[0].Marks.FindRef(Id)>10&&Near(F->Enemies[0].Marks.FindRef(Id),16),Key,TEXT("own mark lasts six seconds"));
        if(Expected&TwoMarks){int32 Count=0;for(const auto& E:F->Enemies)Count+=E.Marks.FindRef(Id)>10;Check(Count==2,Key,TEXT("marks at most two nearby targets"));}
        if(Expected&Root)Check(Primary->RootUntil>=12-.01,Key,TEXT("light target receives two-second root"));
        if(Expected&Slow)Check(F->Enemies[0].SlowUntil>=12-.01,Key,TEXT("slow state lasts two seconds"));
        if(Expected&Suppress)Check(F->Enemies[0].SuppressedUntil>=12-.01,Key,TEXT("ranged enemy receives two-second suppression"));
        if(Expected&Interrupts)Check(F->Enemies[0].DismantleProgress==0&&Primary->RootUntil>10,Key,TEXT("interrupt cancels dismantling progress"));
        if(Expected&Push)Check(Near(FVector::Dist2D(Before,Primary->GetActorLocation()),300),Key,TEXT("light target is pushed three meters"));
        if(Expected&Pull)
        {
            const bool Moved=FVector::Dist2D(Before,Primary->GetActorLocation())>.1||FVector::Dist2D(SecondaryBefore,Secondary->GetActorLocation())>.1;
            Check(Moved,Key,TEXT("nearby light target is actually pulled"));
        }
        if(Expected&Ice)Check(F->Enemies[0].Marks.FindRef(TEXT("ice"))>F->Time&&F->Enemies[0].SlowUntil>F->Time,Key,TEXT("ice line applies its mark and slow"));
        if(Id==TEXT("buling")&&Skill==2)Check(!F->Enemies[0].Marks.Contains(Id),Key,TEXT("light-root skill does not create the heavy-mark skill's mark"));
        Check(Near(F->Lamps[0].Energy->Value(),10),Key,TEXT("an effective skill credits exactly one ten-point action, including multi-hit and persistent skills"));
        TestedSkills.Add(FName(*Key));++SkillCases;
    }

    for(const auto& Value:Campaign->GetArrayField(TEXT("characters")))
    {
        const auto Definition=Value->AsObject();const FName Id(*ScenarioString(Definition,TEXT("id")));
        for(const auto& Pair:Definition->GetObjectField(TEXT("coop"))->Values)
        {
            const int32 Module=FCString::Atoi(*Pair.Key);const FString Key=Id.ToString()+TEXT("_")+Pair.Key;
            const FString Description=ScenarioString(Pair.Value->AsObject(),TEXT("description"));
            if(!Reset(Id,Module)){Check(false,Key,TEXT("spawn synergy fixture"));continue;}
            bool Covered=true;
            auto MarkPrimary=[&](){F->Enemies[0].Marks.Add(Id,100);};
            auto ThreeTargets=[&](float First=500,float Spacing=100)
            {Enemy(FVector(First,0,0));Enemy(FVector(First+Spacing,30,0));Enemy(FVector(First+Spacing*2,-30,0));};
            auto NoExtraDamage=[&](){bool Zero=true;for(int32 I=0;I<F->Enemies.Num();++I)Zero&=Near(Damage(I),0);return Zero;};
            auto EveryTargetBudget=[&](){bool InBudget=true;for(int32 I=0;I<F->Enemies.Num();++I)InBudget&=Damage(I)>=-.01&&Damage(I)<=240.05;return InBudget;};
            auto EmitCoop=[&](){const auto Targets=F->LampTargets(0);F->ApplyCoop(0,Id,Targets.IsEmpty()?Origin+FVector(0,0,65):Targets[0]->GetActorLocation(),Targets);};

            if(Module==0&&(Id==TEXT("rover_spectro")||Id==TEXT("yangyang")||Id==TEXT("jianxin")||Id==TEXT("jiyan")||Id==TEXT("danjin")||Id==TEXT("aalto")))
            {
                if(Id==TEXT("rover_spectro")||Id==TEXT("danjin"))
                {
                    Enemy(FVector(500,0,0));Enemy(FVector(1500,0,0));Enemy(FVector(1580,40,0));
                    if(Id==TEXT("rover_spectro"))F->Enemies[1].Marks.Add(Id,100);
                    else {F->Enemies[1].Actor->Health=2000;F->Enemies[2].Actor->Health=2000;}
                    const float Before1=F->Enemies[1].Actor->Health,Before2=F->Enemies[2].Actor->Health;
                    F->FireLamp(0,0);Advance(4.2);
                    Check(Near(Damage(0),0)&&Near(Before1-F->Enemies[1].Actor->Health,1000)&&Near(Before2-F->Enemies[2].Actor->Health,1000),Key,Description+TEXT("; selected center changes, base damage stays 600+4x100"));
                }
                else
                {
                    Enemy(FVector(500,0,0));Enemy(FVector(Id==TEXT("jiyan")?1300:800,0,0));Enemy(FVector(550,150,0));
                    const FVector Before=F->Enemies[1].Actor->GetActorLocation();F->FireLamp(0,0);
                    if(Id==TEXT("aalto"))
                    {
                        Advance(.25);Check(NoExtraDamage()&&F->Enemies[0].TauntUntil>F->Time,Key,TEXT("short lure precedes the explosion"));Advance(4.4);
                    }
                    else
                    {
                        Check(FVector::Dist2D(Before,F->Enemies[1].Actor->GetActorLocation())>.1&&FVector::Dist2D(Before,F->Enemies[1].Actor->GetActorLocation())<=300.1,Key,TEXT("selection utility actually moves a light target by at most three meters"));Advance(4.2);
                    }
                    Check(Near(Damage(0),1000)&&Near(Damage(1),1000)&&Near(Damage(2),1000),Key,TEXT("utility adds no damage beyond the base flame and four burn ticks"));
                }
            }
            else if(Module==1&&Id==TEXT("yangyang"))
            {
                Enemy(FVector(700,0,0));Enemy(FVector(800,350,0));Enemy(FVector(1500,700,0));
                const FVector Before=F->Enemies[1].Actor->GetActorLocation();F->FireLamp(0,0);
                Check(Near(Damage(0),600)&&Near(Damage(1),600)&&Near(Damage(2),0),Key,TEXT("off-line light enemy is gathered before the unchanged beam damage"));
                Check(FVector::Dist2D(Before,F->Enemies[1].Actor->GetActorLocation())<=300.1&&FMath::Abs(F->Enemies[1].Actor->GetActorLocation().Y-Origin.Y)<=150&&F->Lamps[0].Yaw==0&&F->Lamps[0].Position==Origin,Key,TEXT("fixed origin and direction, three-meter pull cap, zero extra damage"));
            }
            else if(Module==3)
            {
                if(Id==TEXT("verina"))
                {
                    for(auto& Character:F->Characters)Character.Actor->Health=500;
                    F->FireLamp(0,0);Advance(6,true);bool Healed=true;for(const auto& Character:F->Characters)Healed&=Near(Character.Actor->Health,620);
                    Check(Healed,Key,TEXT("six actual healing ticks restore 120 HP per teammate, including the endpoint"));
                }
                else if(Id==TEXT("baizhi")||Id==TEXT("jianxin")||Id==TEXT("youhu"))
                {
                    auto* Owner=Enemy(FVector(600,0,0),false,true);for(auto& Character:F->Characters)Character.Actor->Health=500;
                    F->FireLamp(0,0);Advance(.8);Owner->Health=10000;
                    const int32 ExpectedCap=Id==TEXT("jianxin")?5:3;
                    const int32 BeforeCounters=F->Lamps[0].ReflectionsUsed;
                    Advance(.1);Check(BeforeCounters==0&&F->Lamps[0].ReflectionsUsed==0,Key,TEXT("no incoming arrow means no counter or counter consumption"));
                    for(int32 Arrow=0;Arrow<7;++Arrow)F->AddProjectile(Origin+FVector(750,0,100),F->Characters[0].Actor.Get(),90,INDEX_NONE,TEXT("scenario_incoming_arrow"),true,Owner);
                    Advance(1.1);
                    const float ExpectedCounterDamage=ExpectedCap*250+(Id==TEXT("youhu")?240:0);
                    Check(F->Lamps[0].ReflectionsUsed==ExpectedCap&&F->Lamps[0].ReflectionsLeft==0&&Near(Damage(0),ExpectedCounterDamage),Key,FString::Printf(TEXT("seven actual intercepted arrows emit at most %d counters; total %.0f damage"),ExpectedCap,ExpectedCounterDamage));
                    if(Id==TEXT("baizhi")){bool Healed=true;for(const auto& Character:F->Characters)Healed&=Near(Character.Actor->Health,650);Check(Healed,Key,TEXT("only the first actual reflection heals 15% maximum HP"));}
                    if(Id==TEXT("youhu"))Check(Near(Damage(0)-750,240),Key,TEXT("two ice follow-ups share the 240-point budget"));
                }
                else if(Id==TEXT("taoqi"))
                {
                    Enemy(FVector(250,0,0),true);F->Enemies[0].DismantleLamp=0;F->Enemies[0].DismantleProgress=3;
                    F->FireLamp(0,0);
                    Check(F->Enemies[0].DismantleProgress==0&&F->Enemies[0].Actor->RootUntil==0,Key,TEXT("guard immediately clears dismantling without inventing a stun"));Advance(1);
                    Check(Near(F->Lamps[0].GuardUntil,18)&&Near(Damage(0),750),Key,TEXT("guard extends from six to eight seconds; no extra damage"));
                }
                else if(Id==TEXT("lumi"))
                {
                    for(int32 I=0;I<4;++I)Enemy(FVector(500+I*100,0,0),false,false,I==3?1:0);
                    F->FireLamp(0,0);Advance(1);float Total=0;for(int32 I=0;I<4;++I)Total+=Damage(I);
                    Check(Near(Total,750)&&Near(Damage(0),500)&&Near(Damage(1),0)&&Near(Damage(2),0)&&Near(Damage(3),250),Key,TEXT("three base arrows use A/B/A across two entries without adding damage"));
                }
                else if(Id==TEXT("jinhsi")||Id==TEXT("mortefi")||Id==TEXT("aalto")||Id==TEXT("calcharo"))
                {
                    ThreeTargets();F->Enemies[0].Shooter=true;MarkPrimary();F->FireLamp(0,0);Advance(1.2);
                    if(Id==TEXT("jinhsi"))Check(Near(Damage(0),490)&&Near(Damage(1),490)&&Near(Damage(2),490),Key,TEXT("first marked arrow splashes 240 to each target within three meters"));
                    if(Id==TEXT("mortefi"))Check(Near(Damage(0),330)&&Near(Damage(1),330)&&Near(Damage(2),330),Key,TEXT("three base arrows each add 80, total additional budget 240"));
                    if(Id==TEXT("aalto"))Check(Near(Damage(0),250)&&Near(Damage(1),250)&&Near(Damage(2),250)&&F->Enemies[0].SuppressedUntil>F->Time,Key,TEXT("first arrow suppresses the shooter with no added damage"));
                    if(Id==TEXT("calcharo"))Check(Near(Damage(0),490)&&Near(Damage(1),250)&&Near(Damage(2),250),Key,TEXT("marked hit adds only one 240-point follow-up"));
                }
                else if(Id==TEXT("chixia")||Id==TEXT("lingyang"))
                {
                    ThreeTargets(400,80);EmitCoop();Advance(1);
                    Check(Id==TEXT("chixia")?(Near(Damage(0),80)&&Near(Damage(1),80)&&Near(Damage(2),80)):(Near(Damage(0),240)&&Near(Damage(1),240)&&Near(Damage(2),240)),Key,Description+TEXT("; actual additional hit budget"));
                    Check(EveryTargetBudget(),Key,TEXT("per-target additional damage never exceeds 240"));
                }
                else Covered=false;
            }
            else
            {
                ThreeTargets(Module==1?700:500);MarkPrimary();
                if(Module==0&&Id==TEXT("sanhua"))F->Enemies[0].Marks.Add(TEXT("ice"),100);
                if(Module==1&&Id==TEXT("danjin"))F->Enemies[0].Actor->Health=2000;
                if(Module==1&&(Id==TEXT("sanhua")||Id==TEXT("yuanwu")))
                {
                    FArea Field;Field.Center=Origin+FVector(Id==TEXT("sanhua")?600:700,0,65);Field.Radius=400;Field.Shape=Id==TEXT("sanhua")?1:0;Field.Width=200;Field.Direction=FVector::ForwardVector;
                    Field.Source=Id==TEXT("sanhua")?TEXT("sanhua_ice"):TEXT("yuanwu_1");Field.NextTick=50;Field.Expires=100;F->Areas.Add(Field);
                }
                if(Module==1&&Id==TEXT("yinlin"))F->Enemies[1].Marks.Add(Id,100);
                if(Module==1&&Id==TEXT("buling")){F->Enemies[0].Heavy=true;F->Enemies[0].Actor->bHeavy=true;}
                if(Module==2&&(Id==TEXT("yuanwu")||Id==TEXT("xiangliyao")||Id==TEXT("taoqi")))
                {F->Enemies[0].Heavy=true;F->Enemies[0].Actor->bHeavy=true;F->Enemies[0].DismantleLamp=0;F->Enemies[0].DismantleProgress=3;}
                const float PrimaryHealth=F->Enemies[0].Actor->Health;const FVector PrimaryLocation=F->Enemies[0].Actor->GetActorLocation();
                EmitCoop();
                if(Module==0&&Id==TEXT("baizhi")){Advance(.5);Check(NoExtraDamage()&&F->Enemies[0].SlowUntil>F->Time,Key,TEXT("cold field slows without additional damage"));}
                else if(Module==0&&Id==TEXT("verina"))
                {
                    Advance(.2);const double FirstRoot=F->Enemies[0].Actor->RootUntil;Advance(.8);
                    Check(NoExtraDamage()&&FirstRoot>10&&Near(F->Enemies[0].Actor->RootUntil,FirstRoot),Key,TEXT("burn-field entrant is rooted once, without damage or repeated refresh"));
                }
                else if(Module==2&&Id==TEXT("taoqi"))Check(NoExtraDamage()&&F->Enemies[0].DismantleProgress==0&&F->Enemies[0].RetryDismantleAt>=11,Key,TEXT("first pulse clears dismantling without extra damage"));
                else if(Module==2&&(Id==TEXT("jiyan")||Id==TEXT("camellya")))
                {Advance(2.1);Check(NoExtraDamage()&&Near(FVector::Dist2D(PrimaryLocation,F->Enemies[0].Actor->GetActorLocation()),Id==TEXT("jiyan")?600:300),Key,TEXT("last pulse performs the authored displacement without added damage"));}
                else if(Module==2&&Id==TEXT("yinlin"))
                {Advance(2.1);Check(NoExtraDamage()&&F->Enemies[0].Actor->RootUntil>F->Time&&F->Enemies[1].Actor->RootUntil==0&&F->Enemies[2].Actor->RootUntil==0,Key,TEXT("last pulse roots only this conductor's marked light enemies"));}
                else if(Module==2&&Id==TEXT("xiangliyao"))
                {Advance(2.1);Check(NoExtraDamage()&&Near(F->Enemies[0].Resonance,80),Key,TEXT("extra 20 resonance reduction with no HP damage"));}
                else if(Module==2&&Id==TEXT("lingyang"))
                {Advance(2.1);Check(NoExtraDamage()&&F->Enemies[0].SlowUntil>F->Time,Key,TEXT("last pulse creates the four-second slow field without damage"));}
                else
                {
                    Advance(4.2);const float PrimaryDamage=PrimaryHealth-F->Enemies[0].Actor->Health;
                    if(Module==0)
                    {
                        const bool Known=Id==TEXT("changli")||Id==TEXT("sanhua")||Id==TEXT("mortefi")||Id==TEXT("zhezhi")||Id==TEXT("buling")||Id==TEXT("encore")||Id==TEXT("camellya");
                        if(!Known)Covered=false;
                        else Check(Near(PrimaryDamage,240),Key,Description+TEXT("; actual full-sequence additional damage is 240"));
                    }
                    else if(Module==1)
                    {
                        if(Id==TEXT("yinlin"))Check(Near(PrimaryDamage,120)&&Near(Damage(1),120)&&Near(Damage(2),0),Key,TEXT("two marked nodes share one 240-point conduction budget"));
                        else if(Id==TEXT("zhezhi")||Id==TEXT("buling"))Check(Near(PrimaryDamage,0)&&Near(Damage(1),240)&&Near(Damage(2),0),Key,TEXT("the selected secondary target receives the 240-point follow-up"));
                        else if(Id==TEXT("lumi"))Check(Near(PrimaryDamage,0)&&Near(Damage(1),0)&&Near(Damage(2),240),Key,TEXT("beam-end projectile actually hits for 240"));
                        else Check(Near(PrimaryDamage,240),Key,Description+TEXT("; actual additional damage is 240"));
                    }
                    else if(Module==2)
                    {
                        const bool Known=Id==TEXT("changli")||Id==TEXT("yuanwu")||Id==TEXT("youhu")||Id==TEXT("encore")||Id==TEXT("yangyang_xuanling");
                        if(!Known)Covered=false;
                        else Check(Near(PrimaryDamage,240),Key,Description+TEXT("; additional damage across all pulses is 240"));
                    }
                    if(!(Module==1&&Id==TEXT("danjin")))Check(EveryTargetBudget(),Key,TEXT("per-target additional sequence damage is capped at 240"));
                    else Check(PrimaryDamage<=240.05&&Damage(1)<=240.05&&Damage(2)<=240.05,Key,TEXT("low-health mark follow-up respects the 240-point budget"));
                }
            }
            if(Covered){TestedCoops.Add(FName(*Key));++CoopCases;}
            else {++Missing;OutReport+=TEXT("MISSING ")+Key+TEXT(" no declared effect fixture: ")+Description+TEXT("\n");}
        }
    }

    // Delayed/periodic character skills must still remove resonance once per action.
    for(const auto& Item:TArray<TPair<FName,int32>>{{TEXT("yuanwu"),1},{TEXT("camellya"),2},{TEXT("aalto"),2}})
    {
        Reset(Item.Key,0);auto* Target=Enemy(FVector(200,0,0),true);F->ApplySkill(0,Item.Value,Target,987);Advance(4.2);
        Check(Near(F->Enemies[0].Resonance,96),Item.Key.ToString()+TEXT("_resonance"),TEXT("the whole persistent/delayed skill removes exactly four resonance once"));
    }
    // A foreign character's mark cannot unlock Jinhsi's beam follow-up.
    Reset(TEXT("jinhsi"),1);Enemy(FVector(700,0,0),true);F->Enemies[0].Marks.Add(TEXT("rover_spectro"),100);
    F->ApplyCoop(0,TEXT("jinhsi"),F->Enemies[0].Actor->GetActorLocation(),F->LampTargets(0));Advance(.1);
    Check(Near(Damage(0),0),TEXT("mark_isolation"),TEXT("another character's mark grants no Jinhsi follow-up"));
    F->Enemies[0].Marks.Add(TEXT("jinhsi"),100);F->ApplyCoop(0,TEXT("jinhsi"),F->Enemies[0].Actor->GetActorLocation(),F->LampTargets(0));
    Check(Near(Damage(0),240),TEXT("mark_isolation"),TEXT("Jinhsi's own mark enables the authored follow-up"));

    // Instant hits from two lamps stack. Persistent fields use the strongest
    // active field even when their tick phases differ.
    Reset(TEXT("scenario_none"),0);Enemy(FVector(400,0,0));
    for(int32 I=0;I<2;++I){FArea A;A.Center=F->Enemies[0].Actor->GetActorLocation();A.Radius=400;A.Damage=600;A.NextTick=10;A.Source=TEXT("lamp_flame_burst");A.Group=A.Source;F->AddArea(A);}
    Advance(.1);Check(Near(Damage(0),1200),TEXT("direct_hit_overlap"),TEXT("two simultaneous flame bursts both deal their 600 damage"));
    Reset(TEXT("scenario_none"),2);Enemy(FVector(400,0,0));
    for(int32 I=0;I<2;++I){FArea A;A.Center=F->Enemies[0].Actor->GetActorLocation();A.Radius=1000;A.Damage=330;A.NextTick=10;A.Source=TEXT("lamp_shock");A.Group=A.Source;F->AddArea(A);}
    Advance(.1);Check(Near(Damage(0),660),TEXT("direct_hit_overlap"),TEXT("two simultaneous shock pulses both deal their 330 damage"));
    Reset(TEXT("scenario_none"),0);Enemy(FVector(400,0,0));
    for(int32 I=0;I<2;++I)
    {FArea A;A.Center=F->Enemies[0].Actor->GetActorLocation();A.Radius=400;A.Damage=I?30:60;A.NextTick=10+(I?.5:0);A.Remaining=4;A.Interval=1;A.Source=I?TEXT("scenario_weak"):TEXT("scenario_strong");A.Group=TEXT("scenario_same_field");F->AddArea(A);}
    Advance(4.2);Check(Near(Damage(0),270),TEXT("persistent_overlap"),TEXT("offset weak ticks are suppressed while strong field is active; only the final weak tick remains"));
    Reset(TEXT("scenario_none"),1);Enemy(FVector(500,0,0),true);Enemy(FVector(700,0,0),true);F->FireLamp(0,0);
    Check(Near(Damage(0),1500)&&Near(Damage(1),600),TEXT("beam_heavy_bonus"),TEXT("the 900-point heavy bonus applies only to the first heavy in the beam"));

    // Dedicated selection predicates: skill intent outranks the configured lamp priority.
    for(const FName Id:{FName(TEXT("chixia")),FName(TEXT("mortefi")),FName(TEXT("zhezhi")),FName(TEXT("aalto"))})
    {
        Reset(Id,0);F->Lamps[0].Priority=1;Enemy(FVector(200,0,0),true);auto* Shooter=Enemy(FVector(500,0,0),false,true);F->Enemies[0].DismantleLamp=0;
        Check(F->SelectTarget(0,1)==Shooter,Id.ToString()+TEXT("_1_target"),TEXT("skill's shooter preference overrides the lamp's dismantler preference"));
    }
    Reset(TEXT("rover_spectro"),0);F->Lamps[0].Priority=1;Enemy(FVector(200,0,0),true);auto* Front=Enemy(FVector(500,0,0));F->Enemies[0].DismantleLamp=0;Front->Route[0]=Front->GetActorLocation()+FVector(50,0,0);
    Check(F->SelectTarget(0,2)==Front,TEXT("rover_spectro_2_target"),TEXT("mark selects the smallest remaining exit path, ahead of lamp priority"));
    Reset(TEXT("jinhsi"),1);Enemy(FVector(200,0,0),true);auto* Dismantler=Enemy(FVector(500,0,0),true);F->Enemies[1].DismantleLamp=0;
    Check(F->SelectTarget(0,2)==Dismantler,TEXT("jinhsi_2_target"),TEXT("mark prefers an actively dismantling heavy over a nearer idle heavy"));
    for(const FName Id:{FName(TEXT("jiyan")),FName(TEXT("danjin")),FName(TEXT("zhezhi")),FName(TEXT("lumi"))})
    {
        Reset(Id,1);auto E1=MakeShared<FJsonObject>(),E2=MakeShared<FJsonObject>();E1->SetStringField(TEXT("id"),TEXT("E1"));E2->SetStringField(TEXT("id"),TEXT("E2"));F->Entries={E1,E2};F->Lamps[0].RouteIds.Add(TEXT("E1"));
        if(Id==TEXT("lumi"))
        {
            Enemy(FVector(200,0,0),false,true,0);auto* SideShooter=Enemy(FVector(500,0,0),false,true,1);
            Check(F->SelectTarget(0,2)==SideShooter,TEXT("lumi_2_target"),TEXT("side-route shooter is preferred over an on-route shooter"));
        }
        else
        {
            Enemy(FVector(200,0,0),Id==TEXT("danjin"),false,1);auto* Home=Enemy(FVector(500,0,0),Id==TEXT("danjin"),false,0);
            Check(F->SelectTarget(0,2)==Home,Id.ToString()+TEXT("_2_target"),TEXT("authored home-route filter excludes the nearer foreign-route enemy"));
        }
    }
    Reset(TEXT("jiyan"),1);Enemy(FVector(1500,0,0));
    Check(F->SelectTarget(0,2)==nullptr,TEXT("global_target_range"),TEXT("melee line skill still obeys the fourteen-meter acquisition limit"));

    // Observe events from actual lamp/fault transitions. Never synthesize the
    // tutorial event in the callback or directly set the restoration marker.
    Reset(TEXT("scenario_none"),1);Enemy(FVector(500,0,0));
    for(auto& Character:F->Characters)Character.Actor->ActionReady=0;
    int32 FirstLights=0,Relights=0,FaultStarts=0,RelayEvents=0;TArray<double> RelightTimes;
    F->OnEvent=[&](const FString& Event,const FString& Detail)
    {
        if(Event==TEXT("lamp_lit"))++FirstLights;
        else if(Event==TEXT("lamp_relit")){++Relights;RelightTimes.Add(F->Time);}
        else if(Event==TEXT("fault_started"))++FaultStarts;
        else if(Event==TEXT("relay_committed"))++RelayEvents;
    };
    F->Lamps[0].Relay.Reset();F->Lamps[0].LastRelay=-100;F->Lamps[0].Energy->Reset(100);
    F->UpdateLamps(0);
    Check(FirstLights==1&&Relights==0&&RelayEvents==1&&F->Lamps[0].Relay.IsLit(),TEXT("fault_relight_event"),TEXT("first real lamp hit emits lamp_lit and never lamp_relit"));
    F->Time=14;F->StartFault(0,6,TEXT("scenario_scheduled_fault"));F->Lamps[0].Energy->Reset(100);
    Check(FaultStarts==1&&!F->Lamps[0].Relay.IsLit(),TEXT("fault_relight_event"),TEXT("StartFault actually extinguishes the lamp and emits the fault event"));
    F->Time=19;F->UpdateLamps(0);
    Check(Relights==0&&RelayEvents==1&&Near(F->Lamps[0].Energy->Value(),100),TEXT("fault_relight_event"),TEXT("full energy and an elapsed relay interval cannot relight during the six-second fault"));
    F->Time=20;F->Lamps[0].Energy->Reset(99);F->UpdateLamps(0);
    Check(Relights==0&&RelayEvents==1&&!F->Lamps[0].Relay.IsLit(),TEXT("fault_relight_event"),TEXT("fault expiry alone cannot relight without full energy"));
    F->Lamps[0].Energy->Reset(100);F->UpdateLamps(0);F->UpdateLamps(0);
    Check(Relights==1&&RelightTimes.Num()==1&&Near(RelightTimes[0],20)&&RelayEvents==2&&F->Lamps[0].Relay.IsLit()&&Near(F->Lamps[0].Energy->Value(),0),TEXT("fault_relight_event"),TEXT("first legal post-fault relay emits exactly one lamp_relit and consumes energy"));
    F->Time=24;F->Lamps[0].Energy->Reset(100);F->UpdateLamps(0);
    Check(Relights==1&&RelayEvents==2,TEXT("fault_relight_event"),TEXT("subsequent relay still respects the eight-second interval"));
    F->Time=28;F->UpdateLamps(0);
    Check(FirstLights==1&&Relights==1&&RelayEvents==3,TEXT("fault_relight_event"),TEXT("later ordinary relays do not replay the completed fault's relight event"));
    F->Time=29;for(auto& Character:F->Characters)Character.Actor->TakeHit(1000,F->Time);F->UpdateLamps(0);
    Check(!F->Lamps[0].Relay.IsLit()&&Relights==1,TEXT("fault_relight_event"),TEXT("all teammates down extinguishes the lamp without creating another fault restoration"));
    F->Time=37;for(auto& Character:F->Characters)Character.Actor->Restore();F->Lamps[0].Energy->Reset(100);F->UpdateLamps(0);
    Check(FirstLights==2&&Relights==1&&RelayEvents==4&&F->Lamps[0].Relay.IsLit(),TEXT("fault_relight_event"),TEXT("revived teammates produce ordinary lamp_lit, not a second fault relight"));
    // Regression for the first six-lamp encounter: TArray grows after EnergyPool
    // already owns rolling-window records. Its std::deque state must stay stable.
    TArray<FLamp> GrowingLamps;GrowingLamps.Reserve(1);TArray<LongNight::EnergyPool*> PoolAddresses;
    const FLamp* PreviousStorage=GrowingLamps.GetData();int32 GrowthEvents=0;
    for(int32 I=0;I<6;++I)
    {
        FLamp NewLamp;NewLamp.Energy->Reset(40);NewLamp.Energy->RecordHit(LongNight::HitSource::Basic,I+1,0);
        PoolAddresses.Add(NewLamp.Energy.Get());GrowingLamps.Add(MoveTemp(NewLamp));
        if(PreviousStorage!=GrowingLamps.GetData()){++GrowthEvents;PreviousStorage=GrowingLamps.GetData();}
    }
    bool Stable=GrowthEvents>0;
    for(int32 I=0;I<GrowingLamps.Num();++I)
    {
        auto& Pool=*GrowingLamps[I].Energy;Stable&=GrowingLamps[I].Energy.Get()==PoolAddresses[I]&&Near(Pool.Value(),45);
        Stable&=Near(Pool.RecordHit(LongNight::HitSource::Skill,100+I,1),10)&&Near(Pool.RecordHit(LongNight::HitSource::Basic,200+I,2),5)&&Near(Pool.Value(),60);
    }
    Check(Stable,TEXT("six_lamp_energy_storage"),TEXT("after actual TArray expansion, all six stable pools retain prior hits and accept new hit records independently"));
    Cleanup();
    TArray<AActor*> Created;for(TActorIterator<AActor> It(World);It;++It)if(!ExistingActors.Contains(*It))Created.Add(*It);
    for(AActor* Actor:Created)if(IsValid(Actor))Actor->Destroy();
    const bool Complete=TestedSkills.Num()==52&&TestedCoops.Num()==52&&Missing==0;
    OutReport+=FString::Printf(TEXT("CONFIGURATION_COVERAGE skills=%d/52 synergies=%d/52; checks=%d failures=%d missing=%d\n"),SkillCases,CoopCases,Checks,Failures,Missing);
    OutReport+=TEXT("SCOPE: isolated effects and targeted selection predicates; natural timing, complete encounters and player input require separate integration checks.\n");
    return Complete&&Failures==0;
}
