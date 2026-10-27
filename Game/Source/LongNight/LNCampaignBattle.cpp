#include "LNCampaignBattle.h"
#include "LNCombatUnit.h"
#include "LNPresentation.h"
#include "LNData.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DrawDebugHelpers.h"
#include "CollisionQueryParams.h"

namespace
{
double Number(const TSharedPtr<FJsonObject>& O,const TCHAR* K,double D=0){double V;return O.IsValid()&&O->TryGetNumberField(K,V)?V:D;}
FString String(const TSharedPtr<FJsonObject>& O,const TCHAR* K,const FString& D=FString()){FString V;return O.IsValid()&&O->TryGetStringField(K,V)?V:D;}
bool Flag(const TSharedPtr<FJsonObject>& O,const TCHAR* K,bool D=false){bool V;return O.IsValid()&&O->TryGetBoolField(K,V)?V:D;}
constexpr double StepSeconds=1.0/60.0;
}

double FLNCampaignBattle::Rule(const TCHAR* Name,double Default)const
{return Campaign.IsValid()?Number(Campaign->GetObjectField(TEXT("rules")),Name,Default):Default;}
FVector FLNCampaignBattle::ToWorld(const TArray<TSharedPtr<FJsonValue>>& V,float Z)
{return V.Num()>=2?FVector(V[0]->AsNumber()*100,-V[1]->AsNumber()*100,V.Num()>2?V[2]->AsNumber()*100:Z):FVector(0,0,Z);}
void FLNCampaignBattle::Log(const FString& Event,const FString& Detail)const
{if(OnEvent)OnEvent(Event,FString::Printf(TEXT("battle_seconds=%.6f; %s"),Time,*Detail));}
void FLNCampaignBattle::Shutdown()
{
    for(auto& Weak:OwnedActors)if(Weak.IsValid())Weak->Destroy();
    OwnedActors.Empty();Characters.Empty();Enemies.Empty();Lamps.Empty();Tasks.Empty();Batches.Empty();Faults.Empty();Pending.Empty();
    Projectiles.Empty();Areas.Empty();Entries.Empty();NavigationCorners.Empty();EntryNextSpawn.Empty();CharacterDefinitions.Empty();
    bValid=bBegun=bComplete=bDefeated=bTrainingRelay=bCombatAnnounced=false;World=nullptr;
}

bool FLNCampaignBattle::Initialize(UWorld* InWorld,TSharedPtr<FJsonObject> InCampaign,TSharedPtr<FJsonObject> InStage,
 const TArray<FLNCampaignFormation>& InFormation,int32 InWave,int32 InDefense,bool Assistance,bool Training)
{
    Shutdown();World=InWorld;Campaign=InCampaign;Stage=InStage;Formation=InFormation;WaveIndex=InWave;Defense=InDefense;
    bAssistance=Assistance;bTraining=Training;Time=0;TickNumber=0;Accumulator=0;ActionSerial=0;Spawned=Killed=Leaked=EnemySerial=PlannedEnemies=0;LastError.Empty();
    if(!World||!Campaign.IsValid()||!Stage.IsValid()){LastError=TEXT("关卡配置缺失");return false;}
    const auto& StageLamps=Stage->GetArrayField(TEXT("lamps"));
    if(Formation.Num()!=StageLamps.Num()||Formation.IsEmpty()){LastError=TEXT("灯组与编队数量不一致");return false;}
    for(const auto& Value:Campaign->GetArrayField(TEXT("characters"))){auto O=Value->AsObject();CharacterDefinitions.Add(FName(String(O,TEXT("id"))),O);}
    if(bTraining){Formation.SetNum(1);Formation[0].Seats={TEXT("rover_spectro"),TEXT("jinhsi"),TEXT("changli")};Formation[0].Module=0;Formation[0].Priority=0;}
    const auto& Offsets=Campaign->GetObjectField(TEXT("rules"))->GetArrayField(TEXT("station_offsets_m"));
    for(int32 L=0;L<Formation.Num();++L)
    {
        if(Formation[L].Seats.Num()!=3){LastError=TEXT("每灯需要三名人物");Shutdown();return false;}
        auto O=StageLamps[L]->AsObject();FLamp Lamp;Lamp.Id=FName(String(O,TEXT("id")));Lamp.Position=ToWorld(O->GetArrayField(TEXT("position_m")));
        Lamp.Module=Formation[L].Module;Lamp.Yaw=-Formation[L].BeamYaw;Lamp.Priority=Formation[L].Priority;
        const TArray<TSharedPtr<FJsonValue>>* RouteIds=nullptr;if(O->TryGetArrayField(TEXT("route_ids"),RouteIds))for(const auto& Id:*RouteIds)Lamp.RouteIds.Add(FName(Id->AsString()));
        Lamp.Energy=MakeUnique<LongNight::EnergyPool>(LongNight::EnergyConfig{Rule(TEXT("basic_energy_gain"),5),Rule(TEXT("skill_energy_gain"),10)});Lamp.Energy->Reset(Rule(TEXT("energy_initial"),40));Lamp.Relay.Reset();
        auto* Visual=World->SpawnActor<ALNLanternVisual>(Lamp.Position,FRotator::ZeroRotator);
        Lamp.Visual=Visual;OwnedActors.Add(Visual);Visual->SetModule(Lamp.Module,Lamp.Yaw);Visual->UpdateState(.4f,false,false,true);Lamps.Add(MoveTemp(Lamp));
        for(int32 S=0;S<3;++S)
        {
            const FName Id=Formation[L].Seats[S];auto* Definition=CharacterDefinitions.Find(Id);
            if(!Definition){LastError=TEXT("角色配置不存在: ")+Id.ToString();Shutdown();return false;}
            const auto D=*Definition;FLNCharacterRow Row;Row.PersonId=FName(String(D,TEXT("person_id")));Row.DisplayName=String(D,TEXT("name"));
            Row.MaxHealth=Number(D,TEXT("hp"),1000);Row.BasicDamage=Number(D,TEXT("basic_damage"),30);Row.BasicInterval=Number(D,TEXT("basic_seconds"),1);
            Row.SkillOneDamage=Number(D,TEXT("skill1_damage"),90);Row.SkillTwoDamage=Number(D,TEXT("skill2_damage"),150);Row.bRanged=Flag(D,TEXT("ranged"));
            Row.SkillOne=String(D,TEXT("skill1_name"));Row.SkillTwo=String(D,TEXT("skill2_name"));
            FVector Offset=Offsets.IsValidIndex(S)?ToWorld(Offsets[S]->AsArray(),65):FVector((S-1)*200,0,65);
            auto* U=World->SpawnActor<ALNCombatUnit>(Lamps[L].Position+Offset,FRotator::ZeroRotator);
            if(!U){LastError=TEXT("人物生成失败");Shutdown();return false;}
            U->Configure(false,Row,S,FLinearColor(.12f,.62f,.7f),Id);OwnedActors.Add(U);
            FCharacter C;C.Actor=U;C.ConfigId=Id;C.Data=D;C.Lamp=L;C.Seat=S;Characters.Add(MoveTemp(C));
        }
    }
    for(const auto& Value:Stage->GetArrayField(TEXT("entries")))Entries.Add(Value->AsObject());
    for(const auto& Value:Stage->GetArrayField(TEXT("obstacles_m")))
    {
        const auto& Box=Value->AsArray();if(Box.Num()!=2)continue;FVector A=ToWorld(Box[0]->AsArray(),65),B=ToWorld(Box[1]->AsArray(),65);
        const double MinX=FMath::Min(A.X,B.X)-45,MaxX=FMath::Max(A.X,B.X)+45,MinY=FMath::Min(A.Y,B.Y)-45,MaxY=FMath::Max(A.Y,B.Y)+45;
        NavigationCorners.Append({FVector(MinX,MinY,65),FVector(MinX,MaxY,65),FVector(MaxX,MinY,65),FVector(MaxX,MaxY,65)});
    }
    EnemyCap=String(Stage,TEXT("kind"))==TEXT("hazard")?Rule(TEXT("hazard_enemy_cap"),32):Rule(TEXT("main_enemy_cap"),24);
    if(!bTraining)
    {
        const auto& Waves=Stage->GetArrayField(TEXT("waves"));if(!Waves.IsValidIndex(WaveIndex)){LastError=TEXT("波次不存在");Shutdown();return false;}
        const auto W=Waves[WaveIndex]->AsObject();std::vector<LongNight::BatchSpec> Specs;
        for(const auto& V:W->GetArrayField(TEXT("batches")))
        {
            FBatch B;B.Data=V->AsObject();B.Expected=Number(B.Data,TEXT("count"));PlannedEnemies+=B.Expected;
            const double At=FMath::Max(0.,Number(B.Data,TEXT("time"))+((Number(B.Data,TEXT("time"))>0)?BatchTimeOffset:0));
            Specs.push_back({Batches.Num(),B.Expected,At+5});Batches.Add(MoveTemp(B));
        }
        if(!Scheduler.Reset(Specs,EnemyCap)){LastError=TEXT("批次配置超出上限或无效");Shutdown();return false;}
        for(const auto& V:W->GetArrayField(TEXT("tasks")))
        {
            const auto O=V->AsObject();FTask T;T.Id=FName(String(O,TEXT("id")));T.Kind=String(O,TEXT("kind"));T.Required=Number(O,TEXT("seconds"),10);
            T.From=ToWorld(O->GetArrayField(TEXT("from_m")));T.To=ToWorld(O->GetArrayField(TEXT("to_m")));T.Position=T.From;
            for(const auto& Id:O->GetArrayField(TEXT("lamp_ids"))){int32 Index=LampIndex(FName(Id->AsString()));if(Index==INDEX_NONE){LastError=TEXT("任务引用未知灯");Shutdown();return false;}T.LampIndices.Add(Index);}
            auto* Marker=World->SpawnActor<ALNCampaignTaskVisual>();Marker->Configure(T.Kind==TEXT("escort"));
            Marker->UpdateProgress(T.Position,0,false,true,false);T.Marker=Marker;OwnedActors.Add(Marker);
            Tasks.Add(MoveTemp(T));
        }
        for(const auto& V:W->GetArrayField(TEXT("faults")))
        {
            const auto O=V->AsObject();FFault F;F.Lamp=LampIndex(FName(String(O,TEXT("lamp_id"))));F.At=Number(O,TEXT("time"));F.Duration=Number(O,TEXT("seconds"),6);F.Warning=Number(O,TEXT("warning_seconds"),3);if(F.Lamp!=INDEX_NONE)Faults.Add(F);
        }
    }
    bValid=true;return true;
}

void FLNCampaignBattle::BeginWave()
{
    if(!bValid||bBegun)return;bBegun=true;Time=bTraining?0:-5;TickNumber=0;
    if(bTraining)SpawnTrainingTargets();
    else for(const auto& Event:Scheduler.Tick(0,0))Log(TEXT("batch_warning"),FString::Printf(TEXT("batch=%d; count=%d; time=%.3f"),Event.Id,Event.EnemyCount,Event.SpawnAt-5));
}
void FLNCampaignBattle::Tick(float Delta)
{
    if(!bValid||!bBegun||bComplete||bDefeated||!FMath::IsFinite(Delta)||Delta<=0||World->IsPaused())return;
    Accumulator+=Delta;
    while(Accumulator+1e-10>=StepSeconds&&!bComplete&&!bDefeated){Accumulator-=StepSeconds;Step();}
}
void FLNCampaignBattle::Step()
{
    ++TickNumber;Time=TickNumber*StepSeconds-(bTraining?0:5);
    if(Time<-1e-8)return;Time=FMath::Max(0.,Time);
    if(!bCombatAnnounced){bCombatAnnounced=true;Log(TEXT("wave_started"),FString::Printf(TEXT("training=%d; planned=%d"),bTraining,PlannedEnemies));}
    if(!bTraining)UpdateSpawns();
    UpdateFaults();UpdateCharacters(StepSeconds);UpdateEnemies(StepSeconds);UpdateProjectiles(StepSeconds);UpdateAreas();UpdateLamps(StepSeconds);UpdateTasks(StepSeconds);
    if(Time<=10&&TickNumber%60==0)for(const auto& E:Enemies)if(E.Actor.IsValid())Log(TEXT("enemy_position"),FString::Printf(TEXT("enemy=%d; alive=%d; speed=%.3f; waypoint=%d; route_points=%d; x=%.3f; y=%.3f; z=%.3f; root_until=%.3f"),E.Serial,E.Actor->IsAlive(),E.Speed,E.Actor->Waypoint,E.Actor->Route.Num(),E.Actor->GetActorLocation().X,E.Actor->GetActorLocation().Y,E.Actor->GetActorLocation().Z,E.Actor->RootUntil));
    for(auto& C:Characters)if(C.Actor.IsValid())C.Actor->SetDebugVisible(bDebug||!C.Actor->HasCharacterVisual());
    for(auto& E:Enemies)if(E.Actor.IsValid())E.Actor->SetDebugVisible(bDebug);
    if(Defense<=0){bDefeated=true;Log(TEXT("battle_defeat"),TEXT("defense=0"));}
    else if(bTraining&&bTrainingRelay){bComplete=true;Log(TEXT("training_complete"),TEXT("real_relay_and_lamp_hit=1; excluded_from_formal_counts=1"));}
    else if(!bTraining)
    {
        bool TasksDone=true;for(const auto& T:Tasks)TasksDone&=T.Progress+1e-6>=T.Required;
        if(LongNight::CanCompleteWave(Scheduler.AllSpawned(),GetLivingEnemies(),TasksDone))
        {bComplete=true;Log(TEXT("wave_complete"),FString::Printf(TEXT("spawned=%d; killed=%d; leaked=%d; defense=%d; tasks=%d; task_fraction=1; assistance=%d"),Spawned,Killed,Leaked,Defense,Tasks.Num(),bAssistance));}
    }
    if(bDebug)
    {
        for(const auto& L:Lamps){DrawDebugCircle(World,L.Position+FVector(0,0,10),800,32,FColor::Cyan,false,-1,0,2,FVector(1,0,0),FVector(0,1,0),false);}
        for(const auto& T:Tasks)DrawDebugCircle(World,T.Position+FVector(0,0,10),300,24,FColor::Green,false,-1,0,2,FVector(1,0,0),FVector(0,1,0),false);
    }
}

void FLNCampaignBattle::UpdateSpawns()
{
    int32 ReadyAlive=0;for(const auto& E:Enemies)if(E.Actor.IsValid()&&E.Actor->IsAlive()&&Batches.IsValidIndex(E.Batch)&&Batches[E.Batch].Requested&&!Batches[E.Batch].Confirmed)++ReadyAlive;
    // Scheduler reserves each whole in-flight batch; exclude its already visible units from the input to avoid double-counting.
    for(const auto& Event:Scheduler.Tick(Time+5,FMath::Max(0,GetLivingEnemies()-ReadyAlive)))
    {
        if(Event.Type!=LongNight::BatchEventType::SpawnReady)
        {Log(Event.Type==LongNight::BatchEventType::Warning?TEXT("batch_warning"):TEXT("batch_deferred"),FString::Printf(TEXT("batch=%d; count=%d; time=%.3f"),Event.Id,Event.EnemyCount,Event.SpawnAt-5));continue;}
        FBatch& Batch=Batches[Event.Id];Batch.Requested=true;int32 Entry=INDEX_NONE;const FString EntryName=String(Batch.Data,TEXT("entry"));
        for(int32 I=0;I<Entries.Num();++I)if(String(Entries[I],TEXT("id"))==EntryName){Entry=I;break;}
        if(Entry==INDEX_NONE){LastError=TEXT("批次入口不存在");Log(TEXT("configuration_error"),LastError);continue;}
        double Next=FMath::Max(Time,EntryNextSpawn.FindRef(Entry));
        for(int32 I=0;I<Batch.Expected;++I){Pending.Add({Event.Id,Entry,FName(String(Batch.Data,TEXT("enemy"))),Next});Next+=Rule(TEXT("spawn_spacing_seconds"),.35);}
        EntryNextSpawn.Add(Entry,Next);
    }
    for(int32 I=Pending.Num()-1;I>=0;--I)
    {
        const auto Item=Pending[I];if(Time+1e-8<Item.At)continue;
        if(GetLivingEnemies()>=EnemyCap)continue;
        if(!SpawnEnemy(Item.Batch,Item.Entry,Item.Kind))continue;
        ++Batches[Item.Batch].Spawned;Pending.RemoveAt(I);
        auto& B=Batches[Item.Batch];if(B.Spawned==B.Expected){B.Confirmed=Scheduler.ConfirmSpawn(Item.Batch);Log(TEXT("batch_complete"),FString::Printf(TEXT("batch=%d; count=%d; confirmed=%d"),Item.Batch,B.Spawned,B.Confirmed));}
    }
}
bool FLNCampaignBattle::SpawnEnemy(int32 Batch,int32 Entry,FName Kind,bool Training)
{
    if(!Entries.IsValidIndex(Entry))return false;
    const TSharedPtr<FJsonObject>* Def=nullptr;const auto All=Campaign->GetObjectField(TEXT("enemies"));if(!All->TryGetObjectField(Kind.ToString(),Def))return false;
    const auto D=*Def;FEnemy E;E.Kind=Kind;E.Serial=++EnemySerial;E.Batch=Batch;E.Entry=Entry;E.Heavy=Flag(D,TEXT("heavy"));E.Shooter=String(D,TEXT("role"))==TEXT("ranged");
    const double Level=Number(Stage,TEXT("level"),70);FLNCharacterRow Row;Row.PersonId=E.Heavy?TEXT("enemy_heavy"):TEXT("enemy_light");Row.DisplayName=String(D,TEXT("name"));
    Row.MaxHealth=Training?10000:Number(D,TEXT("hp"))*(1+.01*(Level-70))*HealthScale;E.Damage=Number(D,TEXT("damage"))*(1+.005*(Level-70));E.Speed=Number(D,TEXT("speed_mps"))*100;E.Interval=Number(D,TEXT("interval"));E.Loss=Number(D,TEXT("breach_loss"),10);
    FVector Position=ToWorld(Entries[Entry]->GetArrayField(TEXT("position_m")),65);
    if(Training)
    {
        for(const auto& V:Campaign->GetObjectField(TEXT("rules"))->GetObjectField(TEXT("training"))->GetArrayField(TEXT("targets")))
        {const auto T=V->AsObject();if(FName(*String(T,TEXT("enemy")))==Kind){Position=Lamps[0].Position+ToWorld(T->GetArrayField(TEXT("offset_m")),65);Row.MaxHealth=Number(T,TEXT("hp"),10000);break;}}
    }
    auto* U=World->SpawnActor<ALNCombatUnit>(Position,FRotator::ZeroRotator);if(!U)return false;
    U->bHeavy=E.Heavy;U->Configure(true,Row,-1,E.Heavy?FLinearColor(.72f,.25f,.56f):FLinearColor(.8f,.2f,.06f));OwnedActors.Add(U);E.Actor=U;
    for(const auto& V:Entries[Entry]->GetArrayField(TEXT("route_m")))U->Route.Add(ToWorld(V->AsArray(),65));
    if(U->Route.Num()>1)U->Route.RemoveAt(0);
    E.ShootingPoint=ToWorld(Entries[Entry]->GetArrayField(TEXT("shooter_m")),65);Enemies.Add(MoveTemp(E));
    if(!Training)++Spawned;
    Log(Training?TEXT("training_spawn"):TEXT("enemy_spawn"),FString::Printf(TEXT("enemy=%d; kind=%s; batch=%d; entry=%d; hp=%.3f; spawned=%d"),EnemySerial,*Kind.ToString(),Batch,Entry,Row.MaxHealth,Spawned));return true;
}
void FLNCampaignBattle::SpawnTrainingTargets()
{
    for(const auto& V:Campaign->GetObjectField(TEXT("rules"))->GetObjectField(TEXT("training"))->GetArrayField(TEXT("targets")))SpawnEnemy(0,0,FName(*String(V->AsObject(),TEXT("enemy"))),true);
}

int32 FLNCampaignBattle::LampIndex(FName Id)const{for(int32 I=0;I<Lamps.Num();++I)if(Lamps[I].Id==Id)return I;return INDEX_NONE;}
int32 FLNCampaignBattle::EnemyIndex(const ALNCombatUnit* U)const{for(int32 I=0;I<Enemies.Num();++I)if(Enemies[I].Actor.Get()==U)return I;return INDEX_NONE;}
int32 FLNCampaignBattle::CharacterIndex(const ALNCombatUnit* U)const{for(int32 I=0;I<Characters.Num();++I)if(Characters[I].Actor.Get()==U)return I;return INDEX_NONE;}
int32 FLNCampaignBattle::GetLivingEnemies()const{int32 N=0;for(const auto& E:Enemies)if(E.Actor.IsValid()&&E.Actor->IsAlive())++N;return N;}
int32 FLNCampaignBattle::GetCurrentSeat(int32 L)const{return Lamps.IsValidIndex(L)?Lamps[L].CurrentSeat:INDEX_NONE;}
int32 FLNCampaignBattle::GetNextSeat(int32 L)const{return Lamps.IsValidIndex(L)?Lamps[L].Relay.NextSeat():INDEX_NONE;}
float FLNCampaignBattle::GetEnergyFraction(int32 L)const{return Lamps.IsValidIndex(L)?Lamps[L].Energy->Value()/100:0;}
float FLNCampaignBattle::GetFaultSeconds(int32 L)const{return Lamps.IsValidIndex(L)?Lamps[L].Relay.FaultRemaining(Time):0;}
bool FLNCampaignBattle::IsLampLit(int32 L)const{return Lamps.IsValidIndex(L)&&Lamps[L].Relay.IsLit();}
FString FLNCampaignBattle::GetLampReason(int32 L)const{return Lamps.IsValidIndex(L)?Lamps[L].Reason:FString();}
FVector FLNCampaignBattle::GetLampPosition(int32 L)const{return Lamps.IsValidIndex(L)?Lamps[L].Position:FVector::ZeroVector;}
ALNCombatUnit* FLNCampaignBattle::GetUnit(int32 L,int32 S)const{const int32 I=L*3+S;return Characters.IsValidIndex(I)?Characters[I].Actor.Get():nullptr;}
float FLNCampaignBattle::GetTaskFraction(int32 I)const
{
    if(Tasks.IsValidIndex(I))return Tasks[I].Required>0?Tasks[I].Progress/Tasks[I].Required:1;
    if(Tasks.IsEmpty())return 0;double Value=0;for(const auto& T:Tasks)Value+=T.Required>0?T.Progress/T.Required:1;return Value/Tasks.Num();
}
FString FLNCampaignBattle::GetTaskText()const
{
    TArray<FString> Lines;for(const auto& T:Tasks){FString IDs;for(int32 L:T.LampIndices)IDs+=Lamps[L].Id.ToString()+TEXT(" ");Lines.Add(FString::Printf(TEXT("%s%s %.1f/%.0f秒 · %s"),*IDs,T.Kind==TEXT("escort")?TEXT("护送"):T.Kind==TEXT("signal")?TEXT("发信"):TEXT("作业"),T.Progress,T.Required,*T.Reason));}return FString::Join(Lines,TEXT("\n"));
}
FString FLNCampaignBattle::GetSummary()const
{
    double Stalled=0;int32 Relays=0;for(const auto& T:Tasks)Stalled+=T.BlockedSeconds;for(const auto& L:Lamps)Relays+=L.RelayTimes.Num();
    return FString::Printf(TEXT("出现 %d/%d · 击败 %d · 突破 %d · 防线 %d\n接力 %d次 · 任务停滞 %.1f秒 · 战斗 %.1f秒"),Spawned,PlannedEnemies,Killed,Leaked,Defense,Relays,Stalled,FMath::Max(0.,Time));
}
FString FLNCampaignBattle::UnitIdentity(const ALNCombatUnit* U)const
{const int32 E=EnemyIndex(U);if(E!=INDEX_NONE)return FString::Printf(TEXT("enemy_%d"),Enemies[E].Serial);const int32 C=CharacterIndex(U);return C!=INDEX_NONE?Characters[C].ConfigId.ToString():TEXT("none");}

bool FLNCampaignBattle::Visible(FVector A,FVector B)const
{FHitResult Hit;return World&&!World->LineTraceSingleByChannel(Hit,A,B,ECC_Visibility);}
bool FLNCampaignBattle::SweptPathClear(const ALNCombatUnit* U,FVector A,FVector B)const
{
    if(!World||!U)return false;FCollisionQueryParams Q(SCENE_QUERY_STAT(CampaignNavigation),false,U);FHitResult Hit;
    return !World->SweepSingleByChannel(Hit,A,B,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(U->Body->GetScaledCapsuleRadius(),U->Body->GetScaledCapsuleHalfHeight()-1),Q);
}
FVector FLNCampaignBattle::NavigationStep(const ALNCombatUnit* U,FVector Target,float* OutDistance)const
{
    FVector Start=U->GetActorLocation();Target.Z=Start.Z;
    if(SweptPathClear(U,Start,Target)){if(OutDistance)*OutDistance=FVector::Dist2D(Start,Target);return Target;}
    TArray<FVector> Nodes;Nodes.Add(Start);Nodes.Append(NavigationCorners);Nodes.Add(Target);const int32 N=Nodes.Num();
    TArray<double> Distance;Distance.Init(DBL_MAX,N);Distance[0]=0;TArray<int32> Prev;Prev.Init(INDEX_NONE,N);TArray<bool> Done;Done.Init(false,N);
    const int32 C=CharacterIndex(U);const float Boundary=Rule(TEXT("melee_chase_radius_m"),8)*100;
    for(int32 Step=0;Step<N;++Step)
    {
        int32 Best=INDEX_NONE;for(int32 I=0;I<N;++I)if(!Done[I]&&(Best==INDEX_NONE||Distance[I]<Distance[Best]))Best=I;
        if(Best==INDEX_NONE||Distance[Best]==DBL_MAX)break;Done[Best]=true;if(Best==N-1)break;
        for(int32 I=1;I<N;++I)
        {
            if(Done[I]||(C!=INDEX_NONE&&FVector::Dist2D(Nodes[I],Lamps[Characters[C].Lamp].Position)>Boundary+1)||!SweptPathClear(U,Nodes[Best],Nodes[I]))continue;
            const double D=Distance[Best]+FVector::Dist2D(Nodes[Best],Nodes[I]);if(D<Distance[I]){Distance[I]=D;Prev[I]=Best;}
        }
    }
    if(Distance.Last()==DBL_MAX){if(OutDistance)*OutDistance=FLT_MAX;return Start;}
    if(OutDistance)*OutDistance=Distance.Last();int32 Next=N-1;while(Prev[Next]>0)Next=Prev[Next];return Nodes[Next];
}
float FLNCampaignBattle::PathDistance(const ALNCombatUnit* U,FVector Target)const{float Distance=FLT_MAX;NavigationStep(U,Target,&Distance);return Distance;}
void FLNCampaignBattle::MoveUnit(ALNCombatUnit* U,FVector Target,float Speed,float Delta)
{
    if(!U||!U->IsAlive())return;const int32 C=CharacterIndex(U);
    if(C!=INDEX_NONE){const FVector Base=Lamps[Characters[C].Lamp].Position;FVector Offset=Target-Base;Offset.Z=0;Target=Base+Offset.GetClampedToMaxSize(Rule(TEXT("melee_chase_radius_m"),8)*100);Target.Z=U->GetActorLocation().Z;}
    const FVector Before=U->GetActorLocation();const FVector Next=NavigationStep(U,Target);U->MoveTowards(Next,Speed,Delta);
    if(Time<=10&&TickNumber%60==0&&FVector::Dist2D(Before,Target)>50&&FVector::Dist2D(Before,U->GetActorLocation())<.01)
    {
        FCollisionQueryParams Q(SCENE_QUERY_STAT(CampaignBlockedTrace),false,U);FHitResult Hit;
        World->SweepSingleByChannel(Hit,Before,Target,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(U->Body->GetScaledCapsuleRadius(),U->Body->GetScaledCapsuleHalfHeight()),Q);
        Log(TEXT("movement_blocked"),FString::Printf(TEXT("unit=%s; speed=%.3f; x=%.3f; y=%.3f; z=%.3f; next_x=%.3f; next_y=%.3f; goal_x=%.3f; goal_y=%.3f; blocking=%s; penetration=%d; hit_time=%.6f; normal=%s"),*UnitIdentity(U),Speed,Before.X,Before.Y,Before.Z,Next.X,Next.Y,Target.X,Target.Y,*GetNameSafe(Hit.GetActor()),Hit.bStartPenetrating,Hit.Time,*Hit.Normal.ToString()));
    }
}
float FLNCampaignBattle::RemainingRoute(const FEnemy& E)const
{if(!E.Actor.IsValid())return FLT_MAX;FVector P=E.Actor->GetActorLocation();float D=0;for(int32 I=E.Actor->Waypoint;I<E.Actor->Route.Num();++I){D+=FVector::Dist2D(P,E.Actor->Route[I]);P=E.Actor->Route[I];}return D;}

void FLNCampaignBattle::UpdateFaults()
{
    for(auto& F:Faults)
    {
        if(!F.Warned&&Time+1e-8>=F.At-F.Warning){F.Warned=true;Log(TEXT("fault_warning"),FString::Printf(TEXT("lamp=%s; starts=%.3f; seconds=%.3f"),*Lamps[F.Lamp].Id.ToString(),F.At,F.Duration));}
        if(!F.Applied&&Time+1e-8>=F.At){F.Applied=true;StartFault(F.Lamp,F.Duration,TEXT("scheduled"));}
    }
}
void FLNCampaignBattle::StartFault(int32 L,double Seconds,const FString& Cause)
{
    if(!Lamps.IsValidIndex(L))return;auto& Lamp=Lamps[L];Lamp.Relay.StartFault(Time,Seconds);if(Lamp.Relay.IsFaulted(Time))Lamp.AwaitingFaultRelight=true;Lamp.GuardUntil=0;Lamp.ReflectionsLeft=0;
    for(auto& A:Areas)if(A.Lamp==L&&A.LampOwned){A.Remaining=0;if(A.Visual.IsValid())A.Visual->Destroy();}
    for(auto& E:Enemies)if(E.DismantleLamp==L){E.DismantleLamp=INDEX_NONE;E.DismantleProgress=0;}
    Log(TEXT("fault_started"),FString::Printf(TEXT("lamp=%s; cause=%s; seconds=%.3f; energy=%.3f"),*Lamp.Id.ToString(),*Cause,Seconds,Lamp.Energy->Value()));
}

bool FLNCampaignBattle::Interrupt(FEnemy& E,float Seconds)
{
    auto* U=E.Actor.Get();if(!U||!U->IsAlive()||(E.Heavy&&Time+1e-8<E.ProtectionUntil))return false;
    U->RootUntil=FMath::Max(U->RootUntil,Time+Seconds);U->ActionReady=FMath::Max(U->ActionReady,Time+Seconds);
    if(E.AttackTarget.IsValid())Log(TEXT("action_miss"),FString::Printf(TEXT("action=%llu; target=%s; x=%.3f; y=%.3f; reason=interrupted"),E.AttackAction,*UnitIdentity(E.AttackTarget.Get()),U->GetActorLocation().X,U->GetActorLocation().Y));E.AttackTarget.Reset();E.AttackAction=0;
    if(E.Heavy)E.ProtectionUntil=Time+Rule(TEXT("heavy_interrupt_protection_seconds"),8);
    if(E.DismantleLamp!=INDEX_NONE){Log(TEXT("dismantle_cancelled"),FString::Printf(TEXT("enemy=%d; lamp=%s; cause=interrupt"),E.Serial,*Lamps[E.DismantleLamp].Id.ToString()));E.DismantleProgress=0;E.RetryDismantleAt=Time+1;}
    Log(TEXT("interrupt"),FString::Printf(TEXT("enemy=%d; seconds=%.3f"),E.Serial,Seconds));return true;
}
void FLNCampaignBattle::DamageEnemy(ALNCombatUnit* U,float Amount,int32 L,FName Source,uint64 Action,float ResonanceDamage)
{
    const int32 I=EnemyIndex(U);if(I==INDEX_NONE||!U->IsAlive())return;auto& E=Enemies[I];const float Before=U->Health;
    U->TakeHit(Amount,Time);
    if(E.Heavy&&ResonanceDamage>0&&Time+1e-8>=U->RootUntil&&(Action==0||!E.ResonanceActions.Contains(Action)))
    {
        if(Action)E.ResonanceActions.Add(Action);E.Resonance=FMath::Max(Time+1e-8<E.ProtectionUntil?1.f:0.f,E.Resonance-ResonanceDamage);
        if(E.Resonance<=0){E.ResonanceRestoreAt=Time+Rule(TEXT("resonance_stun_seconds"),2);Interrupt(E,Rule(TEXT("resonance_stun_seconds"),2));Log(TEXT("resonance_broken"),FString::Printf(TEXT("enemy=%d; reset_at=%.6f; stun=2; protection=8"),E.Serial,E.ResonanceRestoreAt));}
    }
    Log(TEXT("damage"),FString::Printf(TEXT("lamp=%d; source=%s; action=%llu; enemy=%d; amount=%.3f; before=%.3f; hp=%.3f; resonance=%.3f"),L+1,*Source.ToString(),Action,E.Serial,Amount,Before,U->Health,E.Resonance));
    if(!U->IsAlive()&&!E.WasKilled){E.WasKilled=true;E.DismantleLamp=INDEX_NONE;if(!bTraining)++Killed;Log(TEXT("enemy_defeated"),FString::Printf(TEXT("enemy=%d; lamp=%d; killed=%d; training=%d"),E.Serial,L+1,Killed,bTraining));}
}
void FLNCampaignBattle::DamageCharacter(ALNCombatUnit* U,float Amount,bool Heavy,uint64 Action)
{
    const int32 I=CharacterIndex(U);if(I==INDEX_NONE||!U->IsAlive())return;
    if(Time<=Characters[I].ImmuneUntil+1e-8){Log(TEXT("action_miss"),FString::Printf(TEXT("action=%llu; target=%s; x=%.3f; y=%.3f; reason=dodge_immunity"),Action,*UnitIdentity(U),U->GetActorLocation().X,U->GetActorLocation().Y));return;}
    auto& C=Characters[I];U->TakeHit(Amount,Time);Log(TEXT("character_hit"),FString::Printf(TEXT("person=%s; hp=%.3f; amount=%.3f"),*C.ConfigId.ToString(),U->Health,Amount));
    if(!U->IsAlive()){C.Target.Reset();Log(TEXT("character_down"),FString::Printf(TEXT("person=%s; return_at=%.3f"),*C.ConfigId.ToString(),U->ReviveAt));}
    else if(Heavy&&Time-U->LastInterrupt+1e-8>=Rule(TEXT("character_stagger_protection_seconds"),2))
    {
        U->LastInterrupt=Time;
        if(!(C.Submitted&&Time<C.HitAt+.1)){U->RootUntil=Time+Rule(TEXT("heavy_hit_stagger_seconds"),.3);if(C.Target.IsValid()&&!C.Submitted){Log(TEXT("action_miss"),FString::Printf(TEXT("action=%llu; target=%s; x=%.3f; y=%.3f; reason=heavy_stagger"),C.Action,*UnitIdentity(C.Target.Get()),U->GetActorLocation().X,U->GetActorLocation().Y));C.Target.Reset();C.Action=0;}}
    }
}
void FLNCampaignBattle::MoveEnemy(ALNCombatUnit* U,FVector Toward,float Distance)
{if(!U||!U->IsAlive()||U->bHeavy)return;FVector Delta=Toward-U->GetActorLocation();Delta.Z=0;U->AddActorWorldOffset(Delta.GetClampedToMaxSize(Distance),true);}

void FLNCampaignBattle::UpdateEnemies(float Delta)
{
    if(bTraining)return;
    for(auto& E:Enemies)
    {
        auto* U=E.Actor.Get();if(!U||!U->IsAlive())continue;
        if(E.ResonanceRestoreAt>0&&Time+1e-8>=E.ResonanceRestoreAt){E.Resonance=100;E.ResonanceRestoreAt=0;Log(TEXT("resonance_restored"),FString::Printf(TEXT("enemy=%d; resonance=100"),E.Serial));}
        if(Time+1e-8<U->RootUntil||Time+1e-8<E.SuppressedUntil){if(E.AttackTarget.IsValid())Log(TEXT("action_miss"),FString::Printf(TEXT("action=%llu; target=%s; x=%.3f; y=%.3f; reason=enemy_controlled"),E.AttackAction,*UnitIdentity(E.AttackTarget.Get()),U->GetActorLocation().X,U->GetActorLocation().Y));E.AttackTarget.Reset();E.AttackAction=0;continue;}
        if(E.AttackTarget.IsValid())
        {
            if(Time+1e-8<E.AttackHitAt)continue;auto* Target=E.AttackTarget.Get();
            if(Target->IsAlive()&&Visible(U->GetActorLocation(),Target->GetActorLocation())&&FVector::Dist2D(U->GetActorLocation(),Target->GetActorLocation())<=(E.Shooter?1400:270))
            {if(E.Shooter)AddProjectile(U->GetActorLocation()+FVector(0,0,45),Target,E.Damage,INDEX_NONE,TEXT("enemy_arrow"),true,U,E.AttackAction);else DamageCharacter(Target,E.Damage,E.Heavy,E.AttackAction);}
            else Log(TEXT("action_miss"),FString::Printf(TEXT("action=%llu; target=%s; x=%.3f; y=%.3f; reason=enemy_target_dead_wall_or_range"),E.AttackAction,*UnitIdentity(Target),Target->GetActorLocation().X,Target->GetActorLocation().Y));
            E.AttackTarget.Reset();E.AttackAction=0;continue;
        }
        if(!E.Heavy&&Time+1e-8<E.TauntUntil){MoveUnit(U,E.TauntPoint,E.Speed*(Time<E.SlowUntil?.6f:1.f),Delta);continue;}
        if(E.Heavy)
        {
            if(E.DismantleLamp!=INDEX_NONE&&(!Lamps[E.DismantleLamp].Relay.IsLit()||Lamps[E.DismantleLamp].Relay.IsFaulted(Time))){E.DismantleLamp=INDEX_NONE;E.DismantleProgress=0;}
            if(E.DismantleLamp==INDEX_NONE)
            {
                float Best=FLT_MAX;for(int32 L=0;L<Lamps.Num();++L)
                {
                    if(!Lamps[L].Relay.IsLit()||Lamps[L].Relay.IsFaulted(Time))continue;bool Taken=false;
                    for(const auto& Other:Enemies)if(Other.Actor.IsValid()&&Other.Actor->IsAlive()&&Other.DismantleLamp==L){Taken=true;break;}
                    if(Taken)continue;const float D=PathDistance(U,Lamps[L].Position+FVector(0,0,65));if(D<Best){Best=D;E.DismantleLamp=L;}
                }
            }
            if(E.DismantleLamp!=INDEX_NONE)
            {
                const int32 L=E.DismantleLamp;const float D=FVector::Dist2D(U->GetActorLocation(),Lamps[L].Position);
                if(D>Rule(TEXT("dismantle_radius_m"),3)*100){MoveUnit(U,Lamps[L].Position+FVector(0,0,65),E.Speed,Delta);continue;}
                if(Time+1e-8<Lamps[L].GuardUntil){E.DismantleProgress=0;continue;}
                if(Time+1e-8<E.RetryDismantleAt)continue;
                if(E.DismantleProgress==0)Log(TEXT("dismantle_started"),FString::Printf(TEXT("enemy=%d; lamp=%s"),E.Serial,*Lamps[L].Id.ToString()));
                E.DismantleProgress+=Delta;
                if(E.DismantleProgress+1e-8>=Rule(TEXT("dismantle_seconds"),4))
                {E.DismantleLamp=INDEX_NONE;E.DismantleProgress=0;StartFault(L,Rule(TEXT("fault_seconds"),6),TEXT("enemy_dismantle"));}
                continue;
            }
        }
        if(E.Shooter&&FVector::Dist2D(U->GetActorLocation(),E.ShootingPoint)>30){MoveUnit(U,E.ShootingPoint,E.Speed*(Time<E.SlowUntil?.6f:1.f),Delta);continue;}
        ALNCombatUnit* Nearest=nullptr;float Distance=E.Shooter?1400:250;
        for(const auto& C:Characters)if(C.Actor.IsValid()&&C.Actor->IsAlive())
        {float D=FVector::Dist2D(U->GetActorLocation(),C.Actor->GetActorLocation());if(D<Distance&&Visible(U->GetActorLocation(),C.Actor->GetActorLocation())){Distance=D;Nearest=C.Actor.Get();}}
        if(Nearest&&!E.Shooter&&Time+1e-8>=U->NextEngagement){U->EngagementUntil=Time+3;U->NextEngagement=Time+7;}
        if(Nearest&&(E.Shooter||Time+1e-8<U->EngagementUntil))
        {
            if(Time+1e-8>=U->BasicReady){E.AttackTarget=Nearest;E.AttackAction=++ActionSerial;E.AttackHitAt=Time+.4;U->BasicReady=Time+E.Interval;U->PlayCombatAction(0,Nearest->GetActorLocation(),.6f);
                auto* FX=World->SpawnActor<ALNVisualEffect>();FX->Beam(U->GetActorLocation()+FVector(0,0,30),Nearest->GetActorLocation()+FVector(0,0,30),3,FLinearColor(1,.15f,.03f),.4f);
                Log(TEXT("enemy_warning"),FString::Printf(TEXT("enemy=%d; action=%llu; target=%s; hit_at=%.6f"),E.Serial,E.AttackAction,*UnitIdentity(Nearest),E.AttackHitAt));}
            continue;
        }
        if(E.Shooter)continue;
        if(U->Waypoint<U->Route.Num()){MoveUnit(U,U->Route[U->Waypoint],E.Speed*(Time<E.SlowUntil?.6f:1.f),Delta);if(FVector::Dist2D(U->GetActorLocation(),U->Route[U->Waypoint])<35)++U->Waypoint;}
        else{const int32 Loss=bAssistance?E.Loss/2:E.Loss;Defense=FMath::Max(0,Defense-Loss);U->bExited=true;U->SetActorHiddenInGame(true);++Leaked;Log(TEXT("breach"),FString::Printf(TEXT("enemy=%d; entry=%d; loss=%d; defense=%d"),E.Serial,E.Entry,Loss,Defense));}
    }
}

void FLNCampaignBattle::AddProjectile(FVector From,ALNCombatUnit* Target,float Damage,int32 L,FName Source,bool Hostile,ALNCombatUnit* Owner,uint64 Action)
{
    if(!Target||!Target->IsAlive())return;FProjectile P;P.Position=From;P.Target=Target;P.Owner=Owner;P.Direction=(Target->GetActorLocation()-From).GetSafeNormal();P.Damage=Damage;P.Lamp=L;P.Source=Source;P.Hostile=Hostile;P.Action=Action?Action:++ActionSerial;P.Speed=Rule(TEXT("arrow_speed_mps"),16)*100;P.Remaining=Hostile?1600:2000;
    if(Lamps.IsValidIndex(L))P.Conductor=Lamps[L].Conductor;Projectiles.Add(P);Log(TEXT("projectile_spawn"),FString::Printf(TEXT("source=%s; target=%s; hostile=%d; speed=%.3f"),*Source.ToString(),*UnitIdentity(Target),Hostile,P.Speed));
}
void FLNCampaignBattle::UpdateProjectiles(float Delta)
{
    for(int32 I=Projectiles.Num()-1;I>=0;--I)
    {
        FProjectile P=Projectiles[I];const FVector End=P.Position+P.Direction*P.Speed*Delta;bool Remove=false;auto* Target=P.Target.Get();
        if(!Target||!Target->IsAlive()){Remove=true;Log(TEXT("action_miss"),FString::Printf(TEXT("action=%llu; source=%s; target=%s; x=%.3f; y=%.3f; reason=projectile_target_invalid"),P.Action,*P.Source.ToString(),*UnitIdentity(Target),P.Position.X,P.Position.Y));}
        if(!Remove&&!Visible(P.Position,End)){Remove=true;Log(TEXT("action_miss"),FString::Printf(TEXT("action=%llu; source=%s; target=%s; x=%.3f; y=%.3f; reason=projectile_wall"),P.Action,*P.Source.ToString(),*UnitIdentity(Target),P.Position.X,P.Position.Y));}
        if(!Remove&&P.Hostile)
        {
            for(int32 L=0;L<Lamps.Num();++L)if(Time<Lamps[L].GuardUntil&&Lamps[L].Relay.IsLit()&&Visible(Lamps[L].Position+FVector(0,0,80),P.Position))
            {
                const FVector Center=Lamps[L].Position+FVector(0,0,P.Position.Z);
                if(FMath::PointDistToSegment(Center,P.Position,End)<=800){Remove=true;Log(TEXT("arrow_intercepted"),FString::Printf(TEXT("lamp=%s; owner=%s"),*Lamps[L].Id.ToString(),*UnitIdentity(P.Owner.Get())));ApplyReflection(L,P.Owner.Get());break;}
            }
        }
        if(!Remove&&Target&&Target->IsAlive()&&FMath::PointDistToSegment(Target->GetActorLocation(),P.Position,End)<=50)
        {
            Remove=true;if(P.Hostile)DamageCharacter(Target,P.Damage,false,P.Action);else
            {
                const int32 EI=EnemyIndex(Target);const bool Marked=EI!=INDEX_NONE&&Enemies[EI].Marks.FindRef(P.Conductor)>Time;const FVector HitPoint=Target->GetActorLocation();
                DamageEnemy(Target,P.Damage,P.Lamp,P.Source,P.Action);
                if(P.Source==TEXT("reflect_active")&&Lamps.IsValidIndex(P.Lamp))
                {
                    auto& Lamp=Lamps[P.Lamp];
                    if(P.Conductor==TEXT("mortefi"))DamageEnemy(Target,80,P.Lamp,TEXT("coop_mortefi"));
                    if(P.FirstArrow&&P.Conductor==TEXT("aalto")&&EI!=INDEX_NONE&&Enemies[EI].Shooter)Enemies[EI].SuppressedUntil=Time+2;
                    if(P.FirstArrow&&P.Conductor==TEXT("jinhsi")&&Marked){FArea A;A.Center=HitPoint;A.Lamp=P.Lamp;A.Radius=300;A.Damage=240;A.NextTick=Time;A.Source=TEXT("coop_jinhsi_splash");A.Group=A.Source;A.LampOwned=true;AddArea(A);}
                    if(P.Conductor==TEXT("calcharo")&&Marked&&!Lamp.CoopUsed){Lamp.CoopUsed=true;DamageEnemy(Target,240,P.Lamp,TEXT("coop_calcharo"));}
                }
            }
            Log(TEXT("projectile_hit"),FString::Printf(TEXT("source=%s; target=%s"),*P.Source.ToString(),*UnitIdentity(Target)));
        }
        if(!Remove)
        {
            auto* FX=World->SpawnActor<ALNVisualEffect>();FX->Beam(P.Position,End,5,P.Hostile?FLinearColor(1,.2f,.08f):FLinearColor(.2f,.8f,1),.045f);
            P.Position=End;P.Remaining-=P.Speed*Delta;Remove=P.Remaining<=0;
            if(Remove)Log(TEXT("action_miss"),FString::Printf(TEXT("action=%llu; source=%s; target=%s; x=%.3f; y=%.3f; reason=projectile_range_expired"),P.Action,*P.Source.ToString(),*UnitIdentity(Target),P.Position.X,P.Position.Y));
        }
        if(Remove)Projectiles.RemoveAt(I);else Projectiles[I]=P;
    }
}
void FLNCampaignBattle::ApplyReflection(int32 L,ALNCombatUnit* IncomingOwner)
{
    auto& Lamp=Lamps[L];if(Lamp.ReflectionsLeft<=0)return;
    auto Targets=LampTargets(L);ALNCombatUnit* Target=IncomingOwner&&IncomingOwner->IsAlive()&&CanLampHit(L,IncomingOwner)?IncomingOwner:Targets.IsEmpty()?nullptr:Targets[0];if(!Target)return;
    --Lamp.ReflectionsLeft;++Lamp.ReflectionsUsed;AddProjectile(Lamp.Position+FVector(0,0,175),Target,250,L,TEXT("reflect_counter"),false);
    if(Lamp.Conductor==TEXT("baizhi")&&!Lamp.FirstReflectionUsed){Lamp.FirstReflectionUsed=true;HealTeam(L,.15f);}
    if(Lamp.Conductor==TEXT("youhu")&&Lamp.ReflectionsUsed<=2)AddProjectile(Lamp.Position+FVector(0,0,150),Target,120,L,TEXT("coop_youhu"),false);
}
void FLNCampaignBattle::HealTeam(int32 L,float Fraction)
{
    for(auto& C:Characters)if(C.Lamp==L&&C.Actor.IsValid()&&C.Actor->IsAlive())
    {auto* U=C.Actor.Get();const float Before=U->Health;U->Health=FMath::Min(U->Stats.MaxHealth,U->Health+U->Stats.MaxHealth*Fraction);U->UpdateLabel();Log(TEXT("heal"),FString::Printf(TEXT("lamp=%d; person=%s; accepted=%.3f; hp=%.3f"),L+1,*C.ConfigId.ToString(),U->Health-Before,U->Health));}
}
void FLNCampaignBattle::UpdateTasks(float Delta)
{
    if(bTraining)return;
    for(auto& T:Tasks)
    {
        bool Lit=true;for(int32 L:T.LampIndices)Lit&=Lamps[L].Relay.IsLit();
        bool Blocked=false;for(const auto& E:Enemies)if(E.Actor.IsValid()&&E.Actor->IsAlive()&&FVector::Dist2D(E.Actor->GetActorLocation(),T.Position)<=Rule(TEXT("task_block_radius_m"),3)*100){Blocked=true;break;}
        const bool Done=T.Progress+1e-6>=T.Required;
        if(!Done){if(Lit&&!Blocked)T.Progress=FMath::Min(T.Required,T.Progress+Delta);else T.BlockedSeconds+=Delta;}
        T.Position=FMath::Lerp(T.From,T.To,T.Required>0?T.Progress/T.Required:1.);
        const FString Reason=T.Progress+1e-6>=T.Required?TEXT("完成"):!Lit?TEXT("等待所需灯点亮"):Blocked?TEXT("3米内有敌，进度保留"):TEXT("进行中");
        if(Reason!=T.Reason){T.Reason=Reason;Log(TEXT("task_state"),FString::Printf(TEXT("task=%s; progress=%.3f; required=%.3f; state=%s; x=%.1f; y=%.1f"),*T.Id.ToString(),T.Progress,T.Required,*Reason,T.Position.X,T.Position.Y));}
        if(!Done&&T.Progress+1e-6>=T.Required)Log(TEXT("task_complete"),FString::Printf(TEXT("task_id=%s; progress=%.3f"),*T.Id.ToString(),T.Progress));
        if(auto* Marker=Cast<ALNCampaignTaskVisual>(T.Marker.Get()))Marker->UpdateProgress(T.Position,T.Required>0?T.Progress/T.Required:1,Blocked,!Lit,T.Progress>=T.Required);
        if(bDebug)DrawDebugSphere(World,T.Position+FVector(0,0,90),T.Kind==TEXT("escort")?70:35,12,T.Progress>=T.Required?FColor::Green:FColor::Cyan,false,-1,0,3);
    }
}
