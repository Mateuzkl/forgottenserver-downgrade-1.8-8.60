#include "../otpch.h"

#include "../spectators.h"

#include "../creature.h"
#include "../game.h"
#include "../instance_utils.h"
#include "../player.h"

#include <absl/container/flat_hash_set.h>
#include <benchmark/benchmark.h>

#include <random>

namespace {

class TestCreature final : public Creature
{
public:
	const std::string& getName() const override { return name; }
	const std::string& getNameDescription() const override { return name; }
	std::string getDescription(int32_t) const override { return name; }
	CreatureType_t getType() const override { return CREATURETYPE_MONSTER; }
	void setID() override {}
	void removeList() override {}
	void addList() override {}

private:
	std::string name = "test creature";
};

using Vec = std::vector<std::shared_ptr<Creature>>;

SpectatorVec makePlayerSpectators(int64_t playerCount)
{
	SpectatorVec spectators;
	for (int64_t i = 0; i < playerCount; ++i) {
		auto player = std::make_shared<Player>(nullptr);
		player->setInstanceID((i & 1) == 0 ? 1 : 2);
		spectators.emplace_back(std::move(player));
	}
	spectators.partitionByType();
	return spectators;
}

// Builds a destination of `size` creatures and a source of `size` creatures
// where `overlapPct`% are shared with the destination.
std::pair<Vec, Vec> makePools(int64_t size, int64_t overlapPct)
{
	Vec dst;
	dst.reserve(size);
	for (int64_t i = 0; i < size; ++i) {
		dst.push_back(std::make_shared<TestCreature>());
	}

	const int64_t shared = size * overlapPct / 100;
	Vec src;
	src.reserve(size);
	for (int64_t i = 0; i < shared; ++i) {
		src.push_back(dst[i]);
	}
	for (int64_t i = shared; i < size; ++i) {
		src.push_back(std::make_shared<TestCreature>());
	}

	// back-to-back heap allocations hand out nearly-ascending addresses, which
	// would feed std::sort an almost-sorted input and bias the comparison in
	// favor of the sort+unique strategy (~1.4x measured); fixed-seed shuffles
	// make the address order representative while keeping runs deterministic
	std::shuffle(dst.begin(), dst.end(), std::mt19937{12345});
	std::shuffle(src.begin(), src.end(), std::mt19937{54321});
	return {std::move(dst), std::move(src)};
}

// Pre-PR strategy: absl::flat_hash_set-based merge (git show 6a5262a7~1:src/spectators.h)
void mergeAbslHashSet(Vec& vec, const Vec& other)
{
	absl::flat_hash_set<Creature*> existing;
	existing.reserve(vec.size() + other.size());
	for (const auto& spectator : vec) {
		existing.insert(spectator.get());
	}

	for (const auto& spectator : other) {
		if (existing.insert(spectator.get()).second) {
			vec.emplace_back(spectator);
		}
	}
}

// Alternative strategy: linear scan per source element (upstream TFS <= 1.4 style).
// shared_ptr operator== compares the stored pointers, so no .get() is needed.
void mergeLinearFind(Vec& vec, const Vec& other)
{
	for (const auto& spectator : other) {
		if (std::find(vec.begin(), vec.end(), spectator) == vec.end()) {
			vec.emplace_back(spectator);
		}
	}
}

} // namespace

// Current strategy, exercised through the real member function.
// The per-iteration destination copy is included uniformly in all three benchmarks.
static void bench_addSpectators_sortUnique(benchmark::State& state)
{
	auto [dstPool, srcPool] = makePools(state.range(0), state.range(1));
	SpectatorVec dst, src;
	for (const auto& c : dstPool) {
		dst.emplace_back(c);
	}
	for (const auto& c : srcPool) {
		src.emplace_back(c);
	}

	for ([[maybe_unused]] auto _ : state) {
		SpectatorVec copy = dst;
		copy.addSpectators(src);
		benchmark::DoNotOptimize(copy);
	}
}
BENCHMARK(bench_addSpectators_sortUnique)->ArgsProduct({{35, 150}, {0, 50, 90}});

static void bench_addSpectators_abslHashSet(benchmark::State& state)
{
	auto [dst, src] = makePools(state.range(0), state.range(1));

	for ([[maybe_unused]] auto _ : state) {
		Vec copy = dst;
		mergeAbslHashSet(copy, src);
		benchmark::DoNotOptimize(copy);
	}
}
BENCHMARK(bench_addSpectators_abslHashSet)->ArgsProduct({{35, 150}, {0, 50, 90}});

static void bench_addSpectators_linearFind(benchmark::State& state)
{
	auto [dst, src] = makePools(state.range(0), state.range(1));

	for ([[maybe_unused]] auto _ : state) {
		Vec copy = dst;
		mergeLinearFind(copy, src);
		benchmark::DoNotOptimize(copy);
	}
}
BENCHMARK(bench_addSpectators_linearFind)->ArgsProduct({{35, 150}, {0, 50, 90}});

static void bench_combatImpactEffect_filterCopy(benchmark::State& state)
{
	const SpectatorVec spectators = makePlayerSpectators(state.range(1));
	const Position position{100, 100, 7};
	const int64_t tileCount = state.range(0);

	for ([[maybe_unused]] auto _ : state) {
		for (int64_t tile = 0; tile < tileCount; ++tile) {
			SpectatorVec filtered;
			for (const auto& spectator : spectators.players()) {
				Player* player = static_cast<Player*>(spectator.get());
				if (player->compareInstance(1)) {
					filtered.emplace_back(spectator);
				}
			}
			Game::addMagicEffect(filtered, position, CONST_ME_POFF);
		}
	}
	state.SetItemsProcessed(state.iterations() * tileCount);
}
BENCHMARK(bench_combatImpactEffect_filterCopy)->ArgsProduct({{1, 9, 25, 81, 225}, {0, 1, 20, 100}});

static void bench_combatImpactEffect_direct(benchmark::State& state)
{
	const SpectatorVec spectators = makePlayerSpectators(state.range(1));
	const Position position{100, 100, 7};
	const int64_t tileCount = state.range(0);

	for ([[maybe_unused]] auto _ : state) {
		for (int64_t tile = 0; tile < tileCount; ++tile) {
			InstanceUtils::sendMagicEffectToInstance(spectators, position, CONST_ME_POFF, 1);
		}
	}
	state.SetItemsProcessed(state.iterations() * tileCount);
}
BENCHMARK(bench_combatImpactEffect_direct)->ArgsProduct({{1, 9, 25, 81, 225}, {0, 1, 20, 100}});

BENCHMARK_MAIN();
