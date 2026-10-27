#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

namespace LongNight
{
enum class HitSource { Basic, Skill, Lamp, Coop };

struct EnergyConfig
{
    double BasicGain = 5.0;
    double SkillGain = 10.0;
};

// Times use the caller's monotonically increasing, pause-aware simulation clock.
// An action ID must be unique across all three actors for the current wave.
class EnergyPool
{
public:
    explicit EnergyPool(EnergyConfig config = {});
    void Reset(double initial = 40.0);
    double RecordHit(HitSource source, std::uint64_t actionId, double now);
    double Add(double amount);
    double AddTaskFallback(double deltaSeconds, bool anyEnemies, bool taskLampLit, bool paused = false);
    bool TryConsume(double amount = 100.0);
    double Value() const { return value_; }

private:
    struct Credit { double Time; double Amount; };
    EnergyConfig config_;
    double value_ = 40.0;
    std::deque<Credit> basicCredits_;
    std::deque<Credit> skillCredits_;
    std::unordered_set<std::uint64_t> countedActions_;
};

struct TargetAvailability
{
    bool Enemy = false;
    bool Arrow = false;
    bool TaskLamp = false;
};

enum class RelayReason
{
    Success, Paused, NotEnoughEnergy, Fault, FewerThanTwoActionable, Interval, NoTarget
};

struct RelayResult
{
    bool Triggered = false;
    int Seat = -1;
    RelayReason Reason = RelayReason::NotEnoughEnergy;
};

class Relay
{
public:
    void Reset();
    RelayResult TryTrigger(double now, EnergyPool& energy, const std::array<bool, 3>& actionable,
                           TargetAvailability targets, bool paused = false);
    void StartFault(double now, double duration = 6.0);
    // Pass living state separately: crowd control must not be treated as death.
    void UpdateTeam(const std::array<bool, 3>& alive);
    bool IsFaulted(double now) const;
    double FaultRemaining(double now) const;
    bool IsLit() const { return lit_; }
    int NextSeat() const { return nextSeat_; }

private:
    bool lit_ = false;
    int nextSeat_ = 0;
    double faultUntil_ = -1.0;
    double nextTriggerAt_ = -1.0;
};

struct CharacterSelection
{
    std::string CharacterId; // Stable identity shared by every form of the same person.
    std::string ConfigId;
};
enum class LineupError { None, WrongTeamSize, MissingIdentity, DuplicateCharacter };
struct LineupValidation
{
    bool Valid = true;
    LineupError Error = LineupError::None;
    int Lamp = -1;
    int Seat = -1;
};
LineupValidation ValidateLineup(const std::vector<std::vector<CharacterSelection>>& teams);

class TaskProgress
{
public:
    explicit TaskProgress(double requiredSeconds = 10.0);
    void Reset(double requiredSeconds = 10.0);
    double Tick(double deltaSeconds, bool requiredLampsLit, bool enemyWithin3m, bool paused = false);
    double Seconds() const { return seconds_; }
    double RequiredSeconds() const { return requiredSeconds_; }
    bool IsComplete() const { return seconds_ >= requiredSeconds_; }
private:
    double seconds_ = 0.0;
    double requiredSeconds_ = 10.0;
};

bool CanCompleteWave(bool allBatchesActuallySpawned, int remainingEnemies, bool taskComplete);

struct BatchSpec
{
    int Id = 0;
    int EnemyCount = 0;
    double SpawnAt = 5.0;
};
enum class BatchEventType { Warning, Deferred, SpawnReady };
struct BatchEvent
{
    BatchEventType Type = BatchEventType::Warning;
    int Id = 0;
    int EnemyCount = 0;
    double SpawnAt = 0.0;
};

// Tick produces requests. ConfirmSpawn acknowledges successful creation of the
// entire requested batch; pending requests never satisfy AllSpawned().
// A late first warning moves SpawnAt to now + 5, guaranteeing a full warning.
class BatchScheduler
{
public:
    bool Reset(const std::vector<BatchSpec>& batches, int enemyCap = 24);
    std::vector<BatchEvent> Tick(double now, int livingEnemies, bool paused = false);
    bool ConfirmSpawn(int id);
    bool AllSpawned() const;
    int SpawnedCount() const;
private:
    enum class State { Pending, Warned, Ready, Spawned };
    struct Batch { BatchSpec Spec; State Status = State::Pending; };
    std::vector<Batch> batches_;
    int enemyCap_ = 24;
};
} // namespace LongNight

