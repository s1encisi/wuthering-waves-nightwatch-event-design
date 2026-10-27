#include "LongNightRules.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace LongNight
{
EnergyPool::EnergyPool(EnergyConfig config) : config_(config) {}

void EnergyPool::Reset(double initial)
{
    value_ = std::isfinite(initial) ? std::clamp(initial, 0.0, 100.0) : 40.0;
    basicCredits_.clear();
    skillCredits_.clear();
    countedActions_.clear();
}

double EnergyPool::Add(double amount)
{
    if (!std::isfinite(amount) || amount <= 0.0) return 0.0;
    const double added = std::min(amount, 100.0 - value_);
    value_ += added;
    return added;
}

double EnergyPool::RecordHit(HitSource source, std::uint64_t actionId, double now)
{
    if (source == HitSource::Lamp || source == HitSource::Coop || !std::isfinite(now)) return 0.0;
    if (!countedActions_.insert(actionId).second) return 0.0;

    const bool basic = source == HitSource::Basic;
    auto& credits = basic ? basicCredits_ : skillCredits_;
    const double window = basic ? 1.0 : 3.0;
    const double cap = basic ? 15.0 : 20.0;
    const double gain = basic ? config_.BasicGain : config_.SkillGain;
    // A rolling window is (now - window, now]; the exact lower bound expires.
    while (!credits.empty() && credits.front().Time <= now - window) credits.pop_front();
    const double spent = std::accumulate(credits.begin(), credits.end(), 0.0,
        [](double sum, const Credit& credit) { return sum + credit.Amount; });
    const double added = Add(std::min(gain, std::max(0.0, cap - spent)));
    if (added > 0.0) credits.push_back({now, added});
    return added;
}

double EnergyPool::AddTaskFallback(double deltaSeconds, bool anyEnemies, bool taskLampLit, bool paused)
{
    if (paused || anyEnemies || taskLampLit) return 0.0;
    return Add(20.0 * deltaSeconds);
}

bool EnergyPool::TryConsume(double amount)
{
    if (!std::isfinite(amount) || amount < 0.0 || value_ < amount) return false;
    value_ -= amount;
    return true;
}

void Relay::Reset()
{
    lit_ = false;
    nextSeat_ = 0;
    faultUntil_ = -1.0;
    nextTriggerAt_ = -1.0;
}

void Relay::UpdateTeam(const std::array<bool, 3>& alive)
{
    if (std::none_of(alive.begin(), alive.end(), [](bool active) { return active; })) lit_ = false;
}

void Relay::StartFault(double now, double duration)
{
    if (!std::isfinite(now) || !std::isfinite(duration) || duration <= 0.0) return;
    lit_ = false;
    faultUntil_ = std::max(faultUntil_, now + duration);
}

bool Relay::IsFaulted(double now) const { return now < faultUntil_; }
double Relay::FaultRemaining(double now) const { return std::max(0.0, faultUntil_ - now); }

RelayResult Relay::TryTrigger(double now, EnergyPool& energy, const std::array<bool, 3>& actionable,
                             TargetAvailability targets, bool paused)
{
    if (paused) return {false, -1, RelayReason::Paused};
    if (IsFaulted(now)) return {false, -1, RelayReason::Fault};
    if (std::count(actionable.begin(), actionable.end(), true) < 2)
        return {false, -1, RelayReason::FewerThanTwoActionable};
    if (energy.Value() < 100.0) return {false, -1, RelayReason::NotEnoughEnergy};
    if (now < nextTriggerAt_) return {false, -1, RelayReason::Interval};
    if (!targets.Enemy && !targets.Arrow && !targets.TaskLamp)
        return {false, -1, RelayReason::NoTarget};

    int seat = nextSeat_;
    while (!actionable[seat]) seat = (seat + 1) % 3;
    energy.TryConsume();
    lit_ = true;
    nextSeat_ = (seat + 1) % 3;
    nextTriggerAt_ = now + 8.0;
    return {true, seat, RelayReason::Success};
}

LineupValidation ValidateLineup(const std::vector<std::vector<CharacterSelection>>& teams)
{
    std::unordered_set<std::string> identities;
    for (std::size_t lamp = 0; lamp < teams.size(); ++lamp)
    {
        if (teams[lamp].size() != 3)
            return {false, LineupError::WrongTeamSize, static_cast<int>(lamp), -1};
        for (std::size_t seat = 0; seat < teams[lamp].size(); ++seat)
        {
            const auto& character = teams[lamp][seat];
            if (character.CharacterId.empty() || character.ConfigId.empty())
                return {false, LineupError::MissingIdentity, static_cast<int>(lamp), static_cast<int>(seat)};
            if (!identities.insert(character.CharacterId).second)
                return {false, LineupError::DuplicateCharacter, static_cast<int>(lamp), static_cast<int>(seat)};
        }
    }
    return {};
}

TaskProgress::TaskProgress(double requiredSeconds) { Reset(requiredSeconds); }
void TaskProgress::Reset(double requiredSeconds)
{
    seconds_ = 0.0;
    requiredSeconds_ = std::isfinite(requiredSeconds) ? std::max(0.0, requiredSeconds) : 10.0;
}

double TaskProgress::Tick(double deltaSeconds, bool requiredLampsLit, bool enemyWithin3m, bool paused)
{
    if (paused || !requiredLampsLit || enemyWithin3m || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0)
        return 0.0;
    const double added = std::min(deltaSeconds, requiredSeconds_ - seconds_);
    seconds_ += added;
    return added;
}

bool CanCompleteWave(bool allBatchesActuallySpawned, int remainingEnemies, bool taskComplete)
{
    return allBatchesActuallySpawned && remainingEnemies == 0 && taskComplete;
}

bool BatchScheduler::Reset(const std::vector<BatchSpec>& batches, int enemyCap)
{
    if (enemyCap <= 0) return false;
    std::unordered_set<int> ids;
    for (const auto& batch : batches)
    {
        if (batch.EnemyCount <= 0 || batch.EnemyCount > enemyCap || !std::isfinite(batch.SpawnAt)
            || !ids.insert(batch.Id).second) return false;
    }
    batches_.clear();
    enemyCap_ = enemyCap;
    for (const auto& spec : batches) batches_.push_back({spec, State::Pending});
    std::stable_sort(batches_.begin(), batches_.end(),
        [](const Batch& a, const Batch& b) { return a.Spec.SpawnAt < b.Spec.SpawnAt; });
    return true;
}

std::vector<BatchEvent> BatchScheduler::Tick(double now, int livingEnemies, bool paused)
{
    std::vector<BatchEvent> events;
    if (paused || !std::isfinite(now) || livingEnemies < 0) return events;
    int reserved = livingEnemies;
    for (const auto& batch : batches_)
        if (batch.Status == State::Ready) reserved += batch.Spec.EnemyCount;

    for (auto& batch : batches_)
    {
        auto& spec = batch.Spec;
        if (batch.Status == State::Pending && now >= spec.SpawnAt - 5.0)
        {
            spec.SpawnAt = std::max(spec.SpawnAt, now + 5.0);
            batch.Status = State::Warned;
            events.push_back({BatchEventType::Warning, spec.Id, spec.EnemyCount, spec.SpawnAt});
        }
        if (batch.Status != State::Warned || now < spec.SpawnAt) continue;
        if (reserved + spec.EnemyCount > enemyCap_)
        {
            spec.SpawnAt = now + 5.0;
            events.push_back({BatchEventType::Deferred, spec.Id, spec.EnemyCount, spec.SpawnAt});
            events.push_back({BatchEventType::Warning, spec.Id, spec.EnemyCount, spec.SpawnAt});
            continue;
        }
        reserved += spec.EnemyCount;
        batch.Status = State::Ready;
        events.push_back({BatchEventType::SpawnReady, spec.Id, spec.EnemyCount, spec.SpawnAt});
    }
    return events;
}

bool BatchScheduler::ConfirmSpawn(int id)
{
    for (auto& batch : batches_)
    {
        if (batch.Spec.Id == id && batch.Status == State::Ready)
        {
            batch.Status = State::Spawned;
            return true;
        }
    }
    return false;
}

bool BatchScheduler::AllSpawned() const
{
    return std::all_of(batches_.begin(), batches_.end(),
        [](const Batch& batch) { return batch.Status == State::Spawned; });
}

int BatchScheduler::SpawnedCount() const
{
    return static_cast<int>(std::count_if(batches_.begin(), batches_.end(),
        [](const Batch& batch) { return batch.Status == State::Spawned; }));
}
} // namespace LongNight

