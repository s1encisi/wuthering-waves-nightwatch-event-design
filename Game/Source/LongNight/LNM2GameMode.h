#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LNData.h"
#include "LNM2Data.h"
#include "Rules/LongNightRules.h"
#include "LNM2GameMode.generated.h"

class ALNCombatUnit;
class ALNLanternVisual;

/** First authored level. M1 laboratory remains a separate GameMode and save slot. */
UCLASS(Blueprintable)
class LONGNIGHT_API ALNM2GameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ALNM2GameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    UFUNCTION(BlueprintCallable) void StartOrContinue();
    UFUNCTION(BlueprintCallable) void PauseToggle();
    UFUNCTION(BlueprintCallable) void SelectLamp(int32 Index);
    UFUNCTION(BlueprintCallable) void SelectSeat(int32 Index);
    UFUNCTION(BlueprintCallable) void AssignCharacter(int32 RosterIndex);
    UFUNCTION(BlueprintCallable) void CycleModule();
    UFUNCTION(BlueprintCallable) void RotateSeats();
    UFUNCTION(BlueprintCallable) void SwapTeams();
    UFUNCTION(BlueprintCallable) void RotateBeam(float Degrees);
    UFUNCTION(BlueprintCallable) void CyclePriority();
    UFUNCTION(BlueprintCallable) void UndoPreparation();
    UFUNCTION(BlueprintCallable) void RetryWave();
    UFUNCTION(BlueprintCallable) void DebugToggle();
    UFUNCTION(BlueprintCallable) void RestartLevel();
    UFUNCTION(BlueprintCallable) void ToggleAssistance();
    bool IsAssistanceEnabled() const { return bAssistance; }

    FString GetPhaseLabel() const;
    int32 GetWaveIndex() const { return WaveIndex; }
    FString GetStatusText() const;
    FString GetLampText(int32 Lamp) const;
    FString GetSeatName(int32 Lamp, int32 Seat) const;
    FName GetSeatId(int32 Lamp, int32 Seat) const;
    float GetSeatHealthFraction(int32 Lamp, int32 Seat) const;
    const TArray<FName>& GetRosterIds() const { return RosterIds; }
    FString GetRosterName(int32 Index) const;
    FString GetRosterDescription(int32 Index) const;
    int32 GetSelectedLamp() const { return SelectedLamp; }
    int32 GetSelectedSeat() const { return SelectedSeat; }
    bool CanConfigure() const;
    bool IsPaused() const { return bPaused; }
    bool IsDebugVisible() const { return bDebug; }
    bool IsComplete() const;
    bool IsDefeated() const;
    bool IsWaveComplete() const { return Phase==EPhase::WaveComplete; }
    bool CanStartOrContinue() const { return CanConfigure()||IsWaveComplete()||IsComplete()||IsDefeated(); }
    FString GetStartLabel() const;
    float GetTaskFraction() const { return Task.RequiredSeconds()>0?Task.Seconds()/Task.RequiredSeconds():0; }
    int32 GetDefense() const { return Defense; }
    FString GetTaskText() const;
    FString GetIntelText() const;
    FString GetEventText() const;
    FString GetNotice() const { return Notice; }
    float GetEnergyFraction(int32 Lamp) const;
    float GetBeamYaw(int32 Lamp) const;
    int32 GetModule(int32 Lamp) const;
    int32 GetCurrentSeat(int32 Lamp) const;
    FVector GetLampPosition(int32 Lamp) const;
    FVector GetTaskPosition() const;

private:
    enum class EPhase { Preparation, Countdown, Combat, WaveComplete, Victory, Defeat };
    struct FLampState
    {
        LongNight::EnergyPool Energy{LongNight::EnergyConfig{}};
        LongNight::Relay Relay;
        int32 CurrentSeat = INDEX_NONE;
        double LastRelay = -100, WaitStarted = -1;
        double FirstContact = -1;
        FString Reason = TEXT("整备确认后开战");
        TArray<double> RelayTimes;
    };
    struct FAction
    {
        ALNCombatUnit* Target = nullptr;
        int32 Skill = 0;
        uint64 Serial = 0;
        double HitAt = 0, EndAt = 0;
        bool Submitted = false;
    };
    struct FEnemyState
    {
        ALNCombatUnit* Unit = nullptr;
        int32 Type = 0;
        float Speed = 200, Damage = 80, Interval = 2;
        bool Shooter = false;
        FVector ShootingPoint = FVector::ZeroVector;
        double SlowUntil = 0, SuppressUntil = 0;
        TMap<FName, double> Marks;
        ALNCombatUnit* AttackTarget = nullptr;
        double HitAt = 0;
        int32 BreakthroughLoss = 10;
    };
    struct FArea
    {
        int32 Lamp = 0;
        FVector Center = FVector::ZeroVector;
        float Radius = 600, Damage = 100;
        double NextTick = 0;
        int32 Remaining = 4;
        bool Cross = false;
        bool IceLine = false;
        FVector Direction = FVector::ForwardVector;
        FName Source;
    };
    struct FSnapshot
    {
        TArray<FLNM2Formation> Formation;
        bool Assistance = false;
    };

    EPhase Phase = EPhase::Preparation;
    UPROPERTY() TArray<FLNM2Formation> Formation;
    UPROPERTY() TArray<TObjectPtr<ALNCombatUnit>> Units;
    UPROPERTY() TArray<TObjectPtr<ALNCombatUnit>> Enemies;
    UPROPERTY() TArray<TObjectPtr<ALNLanternVisual>> LampVisuals;
    UPROPERTY() TArray<TObjectPtr<AActor>> RuntimeScenery;
    UPROPERTY() TObjectPtr<UDataTable> CharacterData;
    TArray<FName> RosterIds;
    TArray<FLNCharacterRow> Roster;
    TArray<FSnapshot> UndoStack;
    TArray<FAction> Actions;
    TArray<double> DodgeReady;
    TArray<double> ImmuneUntil;
    TArray<FEnemyState> EnemyState;
    TArray<FArea> Areas;
    TArray<FString> Events;
    FLampState Lamps[2];
    LongNight::BatchScheduler Scheduler;
    LongNight::TaskProgress Task;
    TArray<FLNM2Formation> CheckpointFormation;
    int32 WaveIndex = 0, Defense = 100, CompletedTasks = 0;
    int32 CheckpointDefense = 100, CheckpointTasks = 0;
    int32 SelectedLamp = 0, SelectedSeat = 0;
    int32 Spawned = 0, Killed = 0, Leaked = 0, TotalSpawned = 0, TotalKilled = 0, TotalLeaked = 0;
    uint64 ActionSerial = 0;
    double SimTime = 0, Countdown = 5, SessionTime = 0, TransitionDelay = 0;
    bool bPaused = false, bDebug = false, bSmoke = false, bRenderSmoke = false, bStateChecks = false;
    bool bFinished = false, bScreenshotTaken = false, bSaveProbe = false;
    FString Notice, LogPath, TaskReason;
    FVector LampPositions[2] = {FVector(-1000,-250,0),FVector(1000,-250,0)};
    FVector TaskPositions[2] = {FVector(-500,500,0),FVector(500,500,0)};
    TSharedPtr<class FJsonObject> StageConfig;
    TArray<FVector> Routes[2];
    TArray<FVector> ShooterStations,SeatOffsets;
    TArray<FLNLampRow> LampDefinitions;
    float CountdownSeconds=5,TaskSeconds=10,TaskBlockRadius=300;
    float BasicGain=5,SkillGain=10,InitialEnergy=40,BeamHeavyBonus=900,SynergyBudget=240;
    float BasicWindup=.3f,SkillWindup=.6f,BasicActionSeconds=1,SkillActionSeconds=1;
    float SkillOneCooldown=6,SkillTwoCooldown=10;
    int32 EnemyCap=24;
    bool bConfigValid=false;
    bool bLoadedCompleted=false, bMeleeTest=false;
    bool bAssistance=false, bCheckpointAssistance=false;

    bool LoadStage();
    void InitializeRoster();
    void SetDefaultFormation();
    bool ValidateFormation(const TArray<FLNM2Formation>& Candidate) const;
    void BuildRuntime();
    void ResetWave();
    void SpawnTeam();
    bool SpawnBatch(int32 Batch);
    void UpdateCharacters(float Delta);
    void ResolveAction(int32 Index);
    void UpdateEnemies(float Delta);
    void UpdateLamps(float Delta);
    void UpdateAreas();
    void UpdateTask(float Delta);
    ALNCombatUnit* FindTarget(int32 UnitIndex, int32 Skill) const;
    bool CanLampHit(int32 Lamp, const ALNCombatUnit* Unit) const;
    void FireLamp(int32 Lamp, int32 Seat);
    bool Visible(FVector From, FVector To) const;
    void Damage(ALNCombatUnit* Target,float Amount,int32 Lamp,FName Source);
    void Pull(ALNCombatUnit* Target,FVector Toward,float Distance);
    FEnemyState* FindEnemy(ALNCombatUnit* Unit);
    const FEnemyState* FindEnemy(const ALNCombatUnit* Unit) const;
    int32 LivingEnemies() const;
    int32 FindRoster(FName Id) const;
    void PushUndo();
    void ApplyFormation();
    void Log(const FString& Event,const FString& Detail);
    FString SaveSlot() const;
    bool SavePreparation();
    bool LoadPreparation();
    void FinishSmoke();
    void RunStateChecks();
    void RunSaveProbe(bool Write);
    void RequestEvidence(const FString& Name);
};
