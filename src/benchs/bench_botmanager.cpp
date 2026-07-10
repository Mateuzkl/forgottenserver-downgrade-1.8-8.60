#include "../otpch.h"

#include "../botregistry.h"
#include "../item.h"
#include "../player.h"

#include <benchmark/benchmark.h>
#include <cstdlib>

namespace {

// Lightweight stand-in for Player so the registry benches measure container
// behaviour, not Player construction. The active-bots map in BotManager (and
// the equivalent private map on the PR branch) has exactly this shape, so
// these numbers model both variants' algorithmic cost.
struct StubPlayer
{
	bool removed = false;
	bool isRemoved() const { return removed; }
};

tfs::bot::Registry<StubPlayer> makeRegistry(int64_t count, double removedRatio = 0.0)
{
	tfs::bot::Registry<StubPlayer> registry;
	const auto removedEvery =
	    removedRatio > 0.0 ? static_cast<int64_t>(1.0 / removedRatio) : std::numeric_limits<int64_t>::max();
	for (int64_t i = 1; i <= count; ++i) {
		auto stub = std::make_shared<StubPlayer>();
		stub->removed = (i % removedEvery) == 0;
		registry.insert(static_cast<uint32_t>(i), std::move(stub));
	}
	return registry;
}

bool ensureItemsLoaded()
{
	static int state = -1;
	if (state != -1) {
		return state == 1;
	}

	std::vector<std::string> candidates;
	if (const char* dataDir = std::getenv("TFS_DATA_DIR")) {
		candidates.push_back(std::string(dataDir) + "/items/items.otb");
	}
	candidates.emplace_back("data/items/items.otb");
	candidates.emplace_back("../data/items/items.otb");
	candidates.emplace_back("../../data/items/items.otb");
	candidates.emplace_back("../../../data/items/items.otb");

	for (const auto& path : candidates) {
		if (Item::items.loadFromOtb(path)) {
			state = 1;
			return true;
		}
	}
	state = 0;
	return false;
}

} // namespace

static void bench_parseGuid(benchmark::State& state)
{
	for ([[maybe_unused]] auto _ : state) {
		auto numeric = tfs::bot::parseGuid("1234567");
		benchmark::DoNotOptimize(numeric);
		auto rejected = tfs::bot::parseGuid("Test Bot");
		benchmark::DoNotOptimize(rejected);
	}
}
BENCHMARK(bench_parseGuid);

// Per-bot fixed allocation cost of a spawn: one socketless Player.
static void bench_playerConstructDestruct(benchmark::State& state)
{
	if (!ensureItemsLoaded()) {
		state.SkipWithError("items.otb not found (set TFS_DATA_DIR)");
		return;
	}

	for ([[maybe_unused]] auto _ : state) {
		auto player = std::make_shared<Player>(nullptr);
		benchmark::DoNotOptimize(player);
	}
}
BENCHMARK(bench_playerConstructDestruct);

static void bench_registryInsertErase(benchmark::State& state)
{
	auto registry = makeRegistry(state.range(0));
	auto stub = std::make_shared<StubPlayer>();
	const uint32_t guid = static_cast<uint32_t>(state.range(0)) + 1;

	for ([[maybe_unused]] auto _ : state) {
		registry.insert(guid, stub);
		benchmark::DoNotOptimize(registry.erase(guid));
	}
}
BENCHMARK(bench_registryInsertErase)->Range(8, 4096);

// The sweep runs at the top of every spawn/despawn/setBroadcast call, so a
// burst spawn of N bots pays N sweeps of up to N entries — this is the
// quadratic-shape cost the run matrix quantifies end to end.
static void bench_registrySweepAllLive(benchmark::State& state)
{
	auto registry = makeRegistry(state.range(0));
	for ([[maybe_unused]] auto _ : state) {
		auto swept = registry.sweepRemoved();
		benchmark::DoNotOptimize(swept);
	}
}
BENCHMARK(bench_registrySweepAllLive)->Range(8, 4096);

static void bench_registrySweepTenPercentRemoved(benchmark::State& state)
{
	for ([[maybe_unused]] auto _ : state) {
		state.PauseTiming();
		auto registry = makeRegistry(state.range(0), 0.10);
		state.ResumeTiming();
		auto swept = registry.sweepRemoved();
		benchmark::DoNotOptimize(swept);
	}
}
BENCHMARK(bench_registrySweepTenPercentRemoved)->Range(8, 4096);

// Game.getBots() copies the live set every BotBrain tick (500 ms).
static void bench_registrySnapshot(benchmark::State& state)
{
	auto registry = makeRegistry(state.range(0));
	for ([[maybe_unused]] auto _ : state) {
		auto bots = registry.snapshot();
		benchmark::DoNotOptimize(bots);
	}
}
BENCHMARK(bench_registrySnapshot)->Range(8, 4096);

BENCHMARK_MAIN();
