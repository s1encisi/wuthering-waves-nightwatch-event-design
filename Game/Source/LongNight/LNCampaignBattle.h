#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "LNCampaignData.h"
#include "Rules/LongNightRules.h"

class UWorld;
class ALNCombatUnit;
class ALNLanternVisual;

/** Configuration-driven battle service. Render events never deal damage. */
class LONGNIGHT_API FLNCampaignBattle
{
public:
    FLNCampaignBattle()=default;
    FLNCampaignBattle(const FLNCampaignBattle&)=delete;
    FLNCampaignBattle& operator=(const FLNCampaignBattle&)=delete;
    TFunction<void(const FString&,const FString&)> OnEvent;
    bool Initialize(UWorld* InWorld,TSharedPtr<FJsonObject> InCampaign,TSharedPtr<FJsonObject> InStage,
                    const TArray<FLNCampaignFormation>& InFormation,int32 InWave,int32 InDefense,
                    bool Assistance,bool Training=false);
    void BeginWave();
    void Tick(float DeltaSeconds);
    void Shutdown();
    bool IsCountdown() const { return bBegun&&Time<0&&!bComplete&&!bDefeated; }
    bool IsRunning() const { return bBegun&&Time>=0&&!bComplete&&!bDefeated; }
    bool IsComplete() const { return bComplete; }
    bool IsDefeated() const { return bDefeated; }
    bool IsTrainingComplete() const { return bTraining&&bComplete; }
    bool IsInitialized() const { return bValid; }
    double GetTime() const { return FMath::Max(0.,Time); }
    double GetCountdown() const { return FMath::Max(0.,-Time); }
    int32 GetDefense() const { return Defense; }
    int32 GetSpawned() const { return Spawned; }
    int32 GetKilled() const { return Killed; }
    int32 GetLeaked() const { return Leaked; }
    int32 GetPlannedEnemies() const { return PlannedEnemies; }
    int32 GetLivingEnemies() const;
    int32 GetCompletedBatches() const { return Scheduler.SpawnedCount(); }
    bool AllBatchesSpawned() const { return bTraining||Scheduler.AllSpawned(); }
    int32 GetLampCount() const { return Lamps.Num(); }
    int32 GetTaskCount() const { return Tasks.Num(); }
    int32 GetCurrentSeat(int32 Lamp) const;
    int32 GetNextSeat(int32 Lamp) const;
    float GetEnergyFraction(int32 Lamp) const;
    float GetFaultSeconds(int32 Lamp) const;
    bool IsLampLit(int32 Lamp) const;
    FString GetLampReason(int32 Lamp) const;
    FVector GetLampPosition(int32 Lamp) const;
    ALNCombatUnit* GetUnit(int32 Lamp,int32 Seat) const;
    float GetTaskFraction(int32 TaskIndex=INDEX_NONE) const;
    FString GetTaskText() const;
    FString GetSummary() const;
    FString GetLastError() const { return LastError; }
    void SetDebugVisible(bool Value) { bDebug=Value; }
    void SetHealthScale(float Value) { HealthScale=FMath::Max(.1f,Value); }
    void SetBatchTimeOffset(float Value) { BatchTimeOffset=Value; }
    /** Scoped state tests only; emits an explicit marker so it cannot pass as normal play. */
    bool RunInvariantChecks(FString& OutReport);
    bool RunConfigurationChecks(FString& OutReport);
    static FVector ToWorld(const TArray<TSharedPtr<FJsonValue>>& Value,float Z=0.f);

private:
    struct FCharacter
    {
        TWeakObjectPtr<ALNCombatUnit> Actor;
        FName ConfigId;
        TSharedPtr<FJsonObject> Data;
        int32 Lamp=0,Seat=0;
        TWeakObjectPtr<ALNCombatUnit> Target;
        uint64 Action=0;
        int32 Skill=0;
        double HitAt=0,EndAt=0,NextDecision=0,DodgeReady=0,ImmuneUntil=0;
        bool Submitted=false,Returning=false;
        FVector Aim=FVector::ZeroVector;
    };
    struct FEnemy
    {
        TWeakObjectPtr<ALNCombatUnit> Actor;
        FName Kind;
        int32 Serial=0,Batch=0,Entry=0;
        float Speed=200,Damage=80,Interval=2,Resonance=100;
        int32 Loss=10;
        bool Shooter=false,Heavy=false,WasKilled=false;
        FVector ShootingPoint=FVector::ZeroVector;
        double SlowUntil=0,SuppressedUntil=0,TauntUntil=0,RetryDismantleAt=0,ProtectionUntil=-100,ResonanceRestoreAt=0;
        FVector TauntPoint=FVector::ZeroVector;
        TMap<FName,double> Marks;
        int32 DismantleLamp=INDEX_NONE;
        double DismantleProgress=0;
        TWeakObjectPtr<ALNCombatUnit> AttackTarget;
        uint64 AttackAction=0;
        double AttackHitAt=0;
        TSet<uint64> ResonanceActions;
    };
    struct FLamp
    {
        // STL credit queues must retain a stable address when the UE lamp array grows.
        TUniquePtr<LongNight::EnergyPool> Energy=MakeUnique<LongNight::EnergyPool>();
        LongNight::Relay Relay;
        FVector Position=FVector::ZeroVector;
        FName Id;
        TSet<FName> RouteIds;
        TWeakObjectPtr<ALNLanternVisual> Visual;
        int32 CurrentSeat=INDEX_NONE,Module=0,Priority=0;
        float Yaw=0;
        double LastRelay=-100,WaitStarted=-1,FirstContact=-1,GuardUntil=0,NextHeal=0;
        int32 ReflectionsLeft=0;
        FName Conductor;
        bool FirstReflectionUsed=false,CoopUsed=false,AwaitingFaultRelight=false;
        int32 ReflectionsUsed=0;
        FString Reason=TEXT("整备确认后开战");
        TArray<double> RelayTimes;
    };
    struct FTask
    {
        FName Id;
        FString Kind;
        TArray<int32> LampIndices;
        FVector From=FVector::ZeroVector,To=FVector::ZeroVector,Position=FVector::ZeroVector;
        double Progress=0,Required=10,BlockedSeconds=0;
        FString Reason;
        TWeakObjectPtr<AActor> Marker;
    };
    struct FPendingSpawn {int32 Batch=0,Entry=0;FName Kind;double At=0;};
    struct FBatch {TSharedPtr<FJsonObject> Data;int32 Expected=0,Spawned=0;bool Requested=false,Confirmed=false;};
    struct FFault {int32 Lamp=0;double At=0,Duration=6,Warning=3;bool Warned=false,Applied=false;};
    struct FProjectile
    {
        FVector Position=FVector::ZeroVector,Direction=FVector::ForwardVector;
        TWeakObjectPtr<ALNCombatUnit> Target;
        TWeakObjectPtr<ALNCombatUnit> Owner;
        float Speed=1600,Damage=90,Remaining=2000;
        int32 Lamp=INDEX_NONE;
        FName Source;
        uint64 Action=0;
        bool Hostile=true,FirstArrow=false;
        FName Conductor;
    };
    struct FArea
    {
        FVector Center=FVector::ZeroVector,Direction=FVector::ForwardVector;
        float Radius=400,Width=200,Damage=0,Slow=0,RootSeconds=0,Resonance=0;
        int32 Lamp=0,Shape=0,Remaining=1; // 0 circle, 1 line, 2 cross
        double NextTick=0,Interval=1,Expires=0,StartsAt=0;
        FName Source,Group;
        uint64 Action=0;
        bool LampOwned=false,Pull=false,Push=false,ApplyMark=false,Interrupt=false,RootOnce=false,SingleTarget=false,HeavyOnly=false,Continuous=false;
        int32 CreditCharacter=INDEX_NONE;
        FName Conductor;
        TSet<int32> Affected;
        TWeakObjectPtr<AActor> Visual;
        bool ImpactVisual=false;
    };
    UWorld* World=nullptr;
    TSharedPtr<FJsonObject> Campaign,Stage;
    TArray<FLNCampaignFormation> Formation;
    TArray<FCharacter> Characters;
    TArray<FEnemy> Enemies;
    TArray<FLamp> Lamps;
    TArray<FTask> Tasks;
    TArray<FBatch> Batches;
    TArray<FFault> Faults;
    TArray<FPendingSpawn> Pending;
    TArray<FProjectile> Projectiles;
    TArray<FArea> Areas;
    TArray<TSharedPtr<FJsonObject>> Entries;
    TArray<FVector> NavigationCorners;
    TArray<TWeakObjectPtr<AActor>> OwnedActors;
    TMap<FName,TSharedPtr<FJsonObject>> CharacterDefinitions;
    TMap<int32,double> EntryNextSpawn;
    LongNight::BatchScheduler Scheduler;
    double Time=0,Accumulator=0;
    int64 TickNumber=0;
    uint64 ActionSerial=0;
    int32 WaveIndex=0,Defense=100,Spawned=0,Killed=0,Leaked=0,PlannedEnemies=0,EnemySerial=0,EnemyCap=24;
    bool bValid=false,bBegun=false,bTraining=false,bComplete=false,bDefeated=false,bAssistance=false,bDebug=false,bTrainingRelay=false,bCombatAnnounced=false;
    float HealthScale=1,BatchTimeOffset=0;
    FString LastError;
    double Rule(const TCHAR* Name,double Default) const;
    void Step();
    void Log(const FString& Event,const FString& Detail) const;
    bool SpawnEnemy(int32 Batch,int32 Entry,FName Kind,bool Training=false);
    void SpawnTrainingTargets();
    void UpdateSpawns();
    void UpdateCharacters(float Delta);
    void UpdateEnemies(float Delta);
    void UpdateProjectiles(float Delta);
    void UpdateAreas();
    void UpdateLamps(float Delta);
    void UpdateTasks(float Delta);
    void UpdateFaults();
    int32 LampIndex(FName Id) const;
    int32 EnemyIndex(const ALNCombatUnit* Unit) const;
    int32 CharacterIndex(const ALNCombatUnit* Unit) const;
    bool Visible(FVector From,FVector To) const;
    bool SweptPathClear(const ALNCombatUnit* Unit,FVector From,FVector To) const;
    FVector NavigationStep(const ALNCombatUnit* Unit,FVector Target,float* OutDistance=nullptr) const;
    float PathDistance(const ALNCombatUnit* Unit,FVector Target) const;
    void MoveUnit(ALNCombatUnit* Unit,FVector Target,float Speed,float Delta);
    float RemainingRoute(const FEnemy& Enemy) const;
    ALNCombatUnit* SelectTarget(int32 Character,int32 Skill) const;
    void CommitAction(int32 Character);
    void ApplySkill(int32 Character,int32 Skill,ALNCombatUnit* Target,uint64 Action);
    bool Interrupt(FEnemy& Enemy,float Seconds);
    void DamageEnemy(ALNCombatUnit* Target,float Damage,int32 Lamp,FName Source,uint64 Action=0,float ResonanceDamage=0);
    void DamageCharacter(ALNCombatUnit* Target,float Damage,bool Heavy,uint64 Action=0);
    void MoveEnemy(ALNCombatUnit* Target,FVector Toward,float Distance);
    void AddArea(FArea Area);
    bool AreaContains(const FArea& Area,const FEnemy& Enemy) const;
    bool CanLampHit(int32 Lamp,const ALNCombatUnit* Enemy) const;
    TArray<ALNCombatUnit*> LampTargets(int32 Lamp) const;
    void FireLamp(int32 Lamp,int32 Seat);
    void ApplyCoop(int32 Lamp,FName CharacterId,FVector Center,const TArray<ALNCombatUnit*>& Targets);
    void ApplyReflection(int32 Lamp,ALNCombatUnit* IncomingOwner);
    void AddProjectile(FVector From,ALNCombatUnit* Target,float Damage,int32 Lamp,FName Source,bool Hostile,ALNCombatUnit* Owner=nullptr,uint64 Action=0);
    void StartFault(int32 Lamp,double Seconds,const FString& Cause);
    void HealTeam(int32 Lamp,float Fraction);
    FString UnitIdentity(const ALNCombatUnit* Unit) const;
};
