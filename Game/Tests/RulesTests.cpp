#include "../Source/LongNight/Rules/LongNightRules.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace LongNight;

namespace
{
int checks = 0;
int failures = 0;
int scenarios = 0;
void Check(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::cout << "FAIL line " << line << ": " << expression << '\n';
    }
}
bool Near(double a, double b) { return std::abs(a - b) < 1e-9; }
#define CHECK(expression) Check((expression), #expression, __LINE__)
#define NEAR(a, b) CHECK(Near((a), (b)))

void Begin(const char* name)
{
    ++scenarios;
    std::cout << "SCENARIO " << scenarios << ": " << name << '\n';
}
const std::array<bool, 3> All = {true, true, true};
const TargetAvailability Enemy = {true, false, false};

void EnergyTests()
{
    Begin("Wave reset starts at 40 and clears action IDs and rolling credits");
    EnergyPool energy;
    NEAR(energy.Value(), 40.0);
    NEAR(energy.RecordHit(HitSource::Basic, 1, 0.0), 5.0);
    energy.Reset();
    NEAR(energy.Value(), 40.0);
    NEAR(energy.RecordHit(HitSource::Basic, 1, 0.0), 5.0);

    Begin("Basic hits share rolling 1 second / 15 point budget across three actors");
    energy.Reset(0.0);
    NEAR(energy.RecordHit(HitSource::Basic, 10, 0.0), 5.0);
    NEAR(energy.RecordHit(HitSource::Basic, 11, 0.2), 5.0);
    NEAR(energy.RecordHit(HitSource::Basic, 12, 0.4), 5.0);
    NEAR(energy.RecordHit(HitSource::Basic, 13, 0.999), 0.0);
    NEAR(energy.RecordHit(HitSource::Basic, 14, 1.0), 5.0);
    NEAR(energy.RecordHit(HitSource::Basic, 15, 1.199), 0.0);
    NEAR(energy.RecordHit(HitSource::Basic, 16, 1.201), 5.0);
    NEAR(energy.Value(), 25.0);

    Begin("Skill rolling 3 second boundary and independent basic budget");
    energy.Reset(0.0);
    NEAR(energy.RecordHit(HitSource::Skill, 1, 0.0), 10.0);
    NEAR(energy.RecordHit(HitSource::Skill, 2, 0.0), 10.0);
    NEAR(energy.RecordHit(HitSource::Basic, 3, 0.0), 5.0);
    NEAR(energy.RecordHit(HitSource::Skill, 4, 2.999999), 0.0);
    NEAR(energy.RecordHit(HitSource::Skill, 5, 3.0), 10.0);
    NEAR(energy.RecordHit(HitSource::Skill, 6, 3.0), 10.0);
    NEAR(energy.RecordHit(HitSource::Skill, 7, 3.0), 0.0);

    Begin("Multi-hit actions deduplicate even after rolling window expiry");
    energy.Reset(0.0);
    NEAR(energy.RecordHit(HitSource::Skill, 1, 0.0), 10.0);
    NEAR(energy.RecordHit(HitSource::Skill, 1, 0.1), 0.0);
    NEAR(energy.RecordHit(HitSource::Skill, 1, 4.0), 0.0);
    NEAR(energy.Value(), 10.0);

    Begin("Cap-blocked first hit cannot gain later through another hit of same action");
    energy.Reset(0.0);
    energy.RecordHit(HitSource::Skill, 1, 0.0);
    energy.RecordHit(HitSource::Skill, 2, 0.0);
    NEAR(energy.RecordHit(HitSource::Skill, 3, 0.0), 0.0);
    NEAR(energy.RecordHit(HitSource::Skill, 3, 4.0), 0.0);

    Begin("Lamp and cooperative damage never generate energy");
    energy.Reset();
    NEAR(energy.RecordHit(HitSource::Lamp, 1, 0.0), 0.0);
    NEAR(energy.RecordHit(HitSource::Coop, 2, 0.0), 0.0);
    NEAR(energy.Value(), 40.0);

    Begin("Configurable hit gain, partial cap allowance, actual stored increment");
    EnergyPool tuned({7.0, 12.0});
    tuned.Reset(0.0);
    NEAR(tuned.RecordHit(HitSource::Basic, 1, 0.0), 7.0);
    NEAR(tuned.RecordHit(HitSource::Basic, 2, 0.0), 7.0);
    NEAR(tuned.RecordHit(HitSource::Basic, 3, 0.0), 1.0);
    NEAR(tuned.RecordHit(HitSource::Skill, 4, 0.0), 12.0);
    NEAR(tuned.RecordHit(HitSource::Skill, 5, 0.0), 8.0);
    NEAR(tuned.Add(1000.0), 65.0);
    NEAR(tuned.Add(10.0), 0.0);
    CHECK(tuned.TryConsume());
    NEAR(tuned.Value(), 0.0);
    CHECK(!tuned.TryConsume());
    CHECK(!tuned.TryConsume(-1.0));

    Begin("Task fallback is 20/sec only with no enemies and an unlit task lamp");
    energy.Reset();
    NEAR(energy.AddTaskFallback(0.5, false, false), 10.0);
    NEAR(energy.AddTaskFallback(1.0, true, false), 0.0);
    NEAR(energy.AddTaskFallback(1.0, false, true), 0.0);
    NEAR(energy.AddTaskFallback(1.0, false, false, true), 0.0);
    NEAR(energy.AddTaskFallback(10.0, false, false), 50.0);
    NEAR(energy.Value(), 100.0);
}

void RelayTests()
{
    Begin("First conductor is seat 0, then 1, 2, 0; every success consumes 100");
    EnergyPool energy;
    Relay relay;
    for (int index = 0; index < 4; ++index)
    {
        energy.Add(100.0);
        const auto result = relay.TryTrigger(index * 8.0, energy, All, Enemy);
        CHECK(result.Triggered);
        CHECK(result.Reason == RelayReason::Success);
        CHECK(result.Seat == index % 3);
        NEAR(energy.Value(), 0.0);
        CHECK(relay.IsLit());
    }

    Begin("Trigger interval is at least 8 seconds, including exact boundary");
    energy.Reset(100.0);
    relay.Reset();
    CHECK(relay.TryTrigger(10.0, energy, All, Enemy).Triggered);
    energy.Add(100.0);
    CHECK(relay.TryTrigger(17.999999, energy, All, Enemy).Reason == RelayReason::Interval);
    NEAR(energy.Value(), 100.0);
    CHECK(relay.TryTrigger(18.0, energy, All, Enemy).Triggered);

    Begin("Insufficient energy and only one actionable actor block without rotating");
    relay.Reset();
    energy.Reset();
    CHECK(relay.TryTrigger(0.0, energy, All, Enemy).Reason == RelayReason::NotEnoughEnergy);
    energy.Add(60.0);
    const std::array<bool, 3> one = {false, true, false};
    CHECK(relay.TryTrigger(0.0, energy, one, Enemy).Reason == RelayReason::FewerThanTwoActionable);
    CHECK(relay.NextSeat() == 0);
    NEAR(energy.Value(), 100.0);

    Begin("Skip unavailable seat, then continue from seat after actual conductor");
    const std::array<bool, 3> skipFirst = {false, true, true};
    CHECK(relay.TryTrigger(0.0, energy, skipFirst, Enemy).Seat == 1);
    CHECK(relay.NextSeat() == 2);
    energy.Add(100.0);
    const std::array<bool, 3> skipLast = {true, true, false};
    CHECK(relay.TryTrigger(8.0, energy, skipLast, Enemy).Seat == 0);
    CHECK(relay.NextSeat() == 1);

    Begin("No enemy, arrow or task target preserves full energy and conductor");
    energy.Reset(100.0);
    relay.Reset();
    CHECK(relay.TryTrigger(0.0, energy, All, {}).Reason == RelayReason::NoTarget);
    NEAR(energy.Value(), 100.0);
    CHECK(relay.NextSeat() == 0);
    CHECK(!relay.IsLit());
    CHECK(relay.TryTrigger(0.0, energy, All, {false, false, true}).Triggered);
    energy.Add(100.0);
    CHECK(relay.TryTrigger(8.0, energy, All, {false, true, false}).Triggered);

    Begin("Fault extinguishes for 6 seconds; actors continue accumulating energy");
    relay.Reset();
    energy.Reset(100.0);
    CHECK(relay.TryTrigger(0.0, energy, All, Enemy).Triggered);
    relay.StartFault(10.0);
    CHECK(!relay.IsLit());
    CHECK(relay.IsFaulted(15.999999));
    NEAR(relay.FaultRemaining(11.0), 5.0);
    NEAR(energy.RecordHit(HitSource::Basic, 1, 10.0), 5.0);
    NEAR(energy.RecordHit(HitSource::Skill, 2, 10.0), 10.0);
    energy.Add(100.0);
    CHECK(relay.TryTrigger(15.999999, energy, All, Enemy).Reason == RelayReason::Fault);
    CHECK(!relay.IsFaulted(16.0));
    CHECK(!relay.IsLit());
    CHECK(relay.TryTrigger(16.0, energy, All, Enemy).Triggered);

    Begin("Temporary inability to act is distinct from all actors being down");
    energy.Add(100.0);
    CHECK(relay.TryTrigger(24.0, energy, {false, false, false}, Enemy).Reason == RelayReason::FewerThanTwoActionable);
    CHECK(relay.IsLit());

    Begin("All actors down extinguishes without introducing a fault deadline");
    relay.UpdateTeam({false, false, false});
    CHECK(!relay.IsLit());
    CHECK(!relay.IsFaulted(16.0));
    energy.Add(100.0);
    CHECK(relay.TryTrigger(24.0, energy, {true, false, false}, Enemy).Reason == RelayReason::FewerThanTwoActionable);
    CHECK(!relay.IsLit());
    CHECK(relay.TryTrigger(24.0, energy, All, Enemy).Triggered);

    Begin("Pause does not consume energy, rotate conductor or ignite lamp");
    relay.Reset();
    energy.Reset(100.0);
    CHECK(relay.TryTrigger(1.0, energy, All, Enemy, true).Reason == RelayReason::Paused);
    CHECK(relay.NextSeat() == 0);
    CHECK(!relay.IsLit());
    NEAR(energy.Value(), 100.0);
    CHECK(relay.TryTrigger(1.0, energy, All, Enemy).Triggered);
}

void LineupAndTaskTests()
{
    Begin("Exactly three members per lamp and person identity unique across lamps");
    std::vector<std::vector<CharacterSelection>> teams = {
        {{"rover", "spectro"}, {"jinxi", "default"}, {"changli", "default"}},
        {{"yangyang", "default"}, {"chixia", "default"}, {"baizhi", "default"}}
    };
    CHECK(ValidateLineup(teams).Valid);
    teams[1][0] = {"rover", "other-form"};
    const auto duplicate = ValidateLineup(teams);
    CHECK(!duplicate.Valid);
    CHECK(duplicate.Error == LineupError::DuplicateCharacter);
    CHECK(duplicate.Lamp == 1 && duplicate.Seat == 0);
    teams[1].pop_back();
    CHECK(ValidateLineup(teams).Error == LineupError::WrongTeamSize);

    Begin("Alternate forms within one team also consume one person slot");
    teams = {{{"yangyang", "normal"}, {"yangyang", "xuanling"}, {"rover", "spectro"}}};
    CHECK(ValidateLineup(teams).Error == LineupError::DuplicateCharacter);
    teams[0][1] = {"", "default"};
    CHECK(ValidateLineup(teams).Error == LineupError::MissingIdentity);

    Begin("Task needs lit lamps and no enemies within 3m, retaining interrupted progress");
    TaskProgress task(10.0);
    NEAR(task.Tick(4.0, true, false), 4.0);
    NEAR(task.Tick(20.0, false, false), 0.0);
    NEAR(task.Tick(20.0, true, true), 0.0);
    NEAR(task.Seconds(), 4.0);
    NEAR(task.Tick(20.0, true, false, true), 0.0);
    NEAR(task.Seconds(), 4.0);
    CHECK(!task.IsComplete());
    NEAR(task.Tick(20.0, true, false), 6.0);
    CHECK(task.IsComplete());
    NEAR(task.Seconds(), 10.0);
    NEAR(task.Tick(1.0, true, false), 0.0);

    Begin("Wave complete requires actual spawning, zero enemies and completed task");
    CHECK(!CanCompleteWave(false, 0, true));
    CHECK(!CanCompleteWave(true, 1, true));
    CHECK(!CanCompleteWave(true, 0, false));
    CHECK(CanCompleteWave(true, 0, true));
}

void BatchTests()
{
    Begin("Batch warns 5 seconds early and is complete only after explicit spawn acknowledgement");
    BatchScheduler scheduler;
    CHECK(scheduler.Reset({{1, 10, 5.0}}));
    auto events = scheduler.Tick(0.0, 0);
    CHECK(events.size() == 1);
    CHECK(events[0].Type == BatchEventType::Warning);
    NEAR(events[0].SpawnAt, 5.0);
    CHECK(scheduler.Tick(4.999999, 0).empty());
    CHECK(!scheduler.ConfirmSpawn(1));
    events = scheduler.Tick(5.0, 0);
    CHECK(events.size() == 1 && events[0].Type == BatchEventType::SpawnReady);
    CHECK(!scheduler.AllSpawned());
    CHECK(scheduler.Tick(5.0, 0).empty());
    CHECK(scheduler.ConfirmSpawn(1));
    CHECK(!scheduler.ConfirmSpawn(1));
    CHECK(scheduler.AllSpawned());
    CHECK(scheduler.SpawnedCount() == 1);

    Begin("Over 24 defers whole batch 5 seconds and repeats warning; exactly 24 is allowed");
    CHECK(scheduler.Reset({{1, 10, 5.0}}));
    scheduler.Tick(0.0, 15);
    events = scheduler.Tick(5.0, 15);
    CHECK(events.size() == 2);
    CHECK(events[0].Type == BatchEventType::Deferred);
    CHECK(events[1].Type == BatchEventType::Warning);
    NEAR(events[0].SpawnAt, 10.0);
    CHECK(scheduler.SpawnedCount() == 0);
    CHECK(scheduler.Tick(9.999999, 0).empty());
    events = scheduler.Tick(10.0, 14);
    CHECK(events.size() == 1 && events[0].Type == BatchEventType::SpawnReady);
    CHECK(events[0].EnemyCount == 10);
    CHECK(scheduler.ConfirmSpawn(1));

    Begin("Repeated overflow never drops a batch and rewarns every delayed attempt");
    CHECK(scheduler.Reset({{1, 10, 5.0}}));
    scheduler.Tick(0.0, 24);
    CHECK(scheduler.Tick(5.0, 24).size() == 2);
    events = scheduler.Tick(10.0, 24);
    CHECK(events.size() == 2);
    NEAR(events[0].SpawnAt, 15.0);
    CHECK(!scheduler.AllSpawned());
    events = scheduler.Tick(15.0, 0);
    CHECK(events.size() == 1 && events[0].Type == BatchEventType::SpawnReady);

    Begin("Multiple batches reserve ready counts to prevent same-tick overflow");
    CHECK(scheduler.Reset({{2, 10, 5.0}, {1, 16, 5.0}, {3, 2, 15.0}}));
    CHECK(scheduler.Tick(0.0, 0).size() == 2);
    events = scheduler.Tick(5.0, 0);
    CHECK(events.size() == 3);
    CHECK(events[0].Type == BatchEventType::SpawnReady && events[0].Id == 2);
    CHECK(events[1].Type == BatchEventType::Deferred && events[1].Id == 1);
    CHECK(!scheduler.AllSpawned());
    // Without confirmation, the ready batch still reserves its ten slots.
    events = scheduler.Tick(10.0, 0);
    CHECK(events.size() == 3);
    CHECK(events[0].Type == BatchEventType::Deferred);
    CHECK(events[2].Type == BatchEventType::Warning && events[2].Id == 3);
    CHECK(scheduler.ConfirmSpawn(2));
    events = scheduler.Tick(15.0, 0);
    CHECK(events.size() == 2);
    CHECK(events[0].Type == BatchEventType::SpawnReady && events[0].Id == 1);
    CHECK(events[1].Type == BatchEventType::SpawnReady && events[1].Id == 3);
    CHECK(scheduler.ConfirmSpawn(1));
    CHECK(scheduler.ConfirmSpawn(3));
    CHECK(scheduler.AllSpawned());

    Begin("Paused scheduler emits nothing and late first observation guarantees full warning");
    CHECK(scheduler.Reset({{1, 10, 5.0}}));
    CHECK(scheduler.Tick(0.0, 0, true).empty());
    events = scheduler.Tick(4.0, 0);
    CHECK(events.size() == 1 && events[0].Type == BatchEventType::Warning);
    NEAR(events[0].SpawnAt, 9.0);
    CHECK(scheduler.Tick(9.0, 0, true).empty());
    CHECK(!scheduler.AllSpawned());
    events = scheduler.Tick(9.0, 0);
    CHECK(events.size() == 1 && events[0].Type == BatchEventType::SpawnReady);

    Begin("Impossible oversized batches and duplicate batch IDs are rejected");
    CHECK(!scheduler.Reset({{1, 25, 5.0}}));
    CHECK(!scheduler.Reset({{1, 4, 5.0}, {1, 4, 10.0}}));
    CHECK(scheduler.Reset({{1, 32, 5.0}}, 32));
    CHECK(scheduler.Reset({}));
    CHECK(scheduler.AllSpawned());
}
} // namespace

int main()
{
    std::cout << "LongNight standalone C++17 rule tests (not an Unreal Engine build/run)\n";
    EnergyTests();
    RelayTests();
    LineupAndTaskTests();
    BatchTests();
    std::cout << "RESULT: " << scenarios << " scenarios, " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}

