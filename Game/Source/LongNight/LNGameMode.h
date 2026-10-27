#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LNData.h"
#include "Rules/LongNightRules.h"
#include "LNGameMode.generated.h"

class ALNCombatUnit;
class AStaticMeshActor;
UCLASS(Blueprintable)
class LONGNIGHT_API ALNGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ALNGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    UFUNCTION(BlueprintCallable) void StartOrRetry();
    UFUNCTION(BlueprintCallable) void PauseToggle();
    UFUNCTION(BlueprintCallable) void DebugToggle();
    UFUNCTION(BlueprintCallable) void CycleModule();
    UFUNCTION(BlueprintCallable) void RotateSeats();
    UFUNCTION(BlueprintCallable) void ResetLab();
    UFUNCTION(BlueprintPure) FString GetStatusText() const;
    UFUNCTION(BlueprintPure) FString GetDetailText() const;
    UFUNCTION(BlueprintPure) FString GetRosterText() const;
    UFUNCTION(BlueprintPure) FString GetEventText() const;
    UFUNCTION(BlueprintPure) float GetEnergyFraction() const;
    UFUNCTION(BlueprintPure) bool CanConfigure() const;
    FString GetPhaseLabel() const;
    int32 GetDefenseValue() const { return Defense; }
    int32 GetBatchNumber() const { return SpawnedBatches; }
    int32 GetKilledCount() const { return TotalKilled; }
    int32 GetSpawnedCount() const { return TotalSpawned; }
    int32 GetLeakedCount() const { return TotalLeaked; }
    FString GetLampLabel() const;
    FName GetSeatCharacterId(int32 Seat) const;
    FString GetSeatCharacterName(int32 Seat) const;
    float GetSeatHealthFraction(int32 Seat) const;
    FString GetSeatStatusLabel(int32 Seat) const;
    int32 GetCurrentSeatIndex() const { return CurrentSeat; }
    int32 GetNextSeatIndex() const { return Relay.NextSeat(); }
    float GetFaultSeconds() const { return Relay.FaultRemaining(SimTime); }
    float GetTaskFraction() const { return TaskProgress/10.; }
    FString GetTaskStatusLabel() const { return TaskReason; }
    bool IsSimulationPaused() const { return bPaused; }
    bool IsDebugVisible() const { return bDebug; }
    bool CanStartOrRetry() const;
    FString GetRelayStatusLabel() const;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Data") TObjectPtr<UDataTable> Characters;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Data") TObjectPtr<UDataTable> Lamps;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Lab|Provisional", meta=(ClampMin="1")) float EnemyHealth = 280.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Lab|Provisional", meta=(ClampMin="1")) float EnemySpeed = 110.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Lab|Provisional", meta=(ClampMin="0")) float EnemyDamage = 18.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Lab") bool bScheduledFault = true;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Lab|Provisional", meta=(ClampMin="0",ClampMax="15")) float BasicEnergyPerAction = 6.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Lab|Provisional", meta=(ClampMin="0",ClampMax="20")) float SkillEnergyPerAction = 10.f;
private:
    enum class EPhase { Preparation, Countdown, Combat, Victory, Defeat };
    EPhase Phase = EPhase::Preparation;
    LongNight::EnergyPool Energy;
    LongNight::Relay Relay;
    UPROPERTY() TArray<TObjectPtr<ALNCombatUnit>> Team;
    UPROPERTY() TArray<TObjectPtr<ALNCombatUnit>> Enemies;
    UPROPERTY() TArray<TObjectPtr<AActor>> Arena;
    UPROPERTY() TObjectPtr<class ALNLanternVisual> LanternVisual;
    UPROPERTY() TObjectPtr<class ALNVisualEffect> BurnVisual;
    TArray<FName> SeatIds;
    TArray<FString> Events;
    TArray<double> RelayTimes;
    double SimTime=0, Countdown=5, TaskProgress=0, NextBurn=0, BurnUntil=0;
    double FirstContact=-1, ScreenshotAt=24, SmokeDeadline=90;
    double RelayWaitStarted=-1;
    FVector BurnCenter=FVector::ZeroVector;
    FVector LampPosition=FVector(-700,-500,0);
    FVector TaskPosition=FVector(900,-450,0);
    FString BlockReason, TaskReason, LogPath;
    FString LastTaskReason;
    int32 Module=0, CurrentSeat=INDEX_NONE, SpawnedBatches=0, Defense=100, ActionSerial=0;
    int32 TotalSpawned=0, TotalKilled=0, TotalLeaked=0;
    bool bPaused=false, bDebug=false, bFaultSent=false, bFaultWarning=false, bSmoke=false, bRenderSmoke=false, bScreenshot=false;
    bool bLabCompleted=false, bDataValid=false, bCrossFire=false;
    bool bComparisonCameraSet=false;
    bool bRelayScreenshot=false;
    float BeamYaw=0;
    void BuildArena();
    void ResetSimulation();
    void SpawnBatch(int32 Batch);
    void UpdateTeam(float DeltaSeconds);
    void UpdateEnemies(float DeltaSeconds);
    void UpdateLamp(float DeltaSeconds);
    void UpdateTask(float DeltaSeconds);
    void DrawDebugOverlay();
    bool LampCanHit(const ALNCombatUnit* Enemy, const FLNLampRow& Row) const;
    void FireLamp(int32 Seat);
    void DealArea(FVector Center,float Radius,float Damage);
    void Log(const FString& Event,const FString& Detail);
    void SavePreparation();
    void LoadPreparation();
    void FinishSmoke();
    void RunStateChecks();
    ALNCombatUnit* SelectEnemy(ALNCombatUnit* Unit,bool HeavyOnly=false) const;
    AStaticMeshActor* Shape(FString Mesh,FVector Location,FVector Scale,FLinearColor Color,bool Blocking=false);
    bool HasLineOfSight(FVector From,FVector To) const;
    const FLNLampRow* LampData() const;
};

