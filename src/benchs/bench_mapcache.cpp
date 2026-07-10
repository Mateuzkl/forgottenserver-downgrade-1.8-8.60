#include "../otpch.h"

#include "../mapcache.h"

#include "../map.h"
#include "../tile.h"
#include "../zones.h"

#include <benchmark/benchmark.h>

#include <random>

namespace {

BasicItem makeItem(uint16_t actionId, uint16_t charges)
{
	BasicItem item;
	item.id = 100;
	item.actionId = actionId;
	item.charges = charges;
	return item;
}

// Hit-heavy: K distinct items inserted once, shared_ptrs kept alive so the
// weak_ptr entries never expire; the timed loop re-requests the same patterns.
void bench_itemcache_hits(benchmark::State& state)
{
	const auto distinct = static_cast<size_t>(state.range(0));

	MapCache::flush();
	std::vector<std::shared_ptr<BasicItem>> keepAlive;
	keepAlive.reserve(distinct);
	for (size_t i = 0; i < distinct; ++i) {
		keepAlive.push_back(MapCache::tryGetItemFromCache(makeItem(static_cast<uint16_t>(i), 0)));
	}

	size_t i = 0;
	for ([[maybe_unused]] auto _ : state) {
		auto cached = MapCache::tryGetItemFromCache(makeItem(static_cast<uint16_t>(i % distinct), 0));
		benchmark::DoNotOptimize(cached);
		++i;
	}
	state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));

	keepAlive.clear();
	MapCache::flush(); // LOG_INFO inside is macro-gated; silent without initLogger
}
BENCHMARK(bench_itemcache_hits)->Arg(64)->Arg(4096);

// Miss-heavy: every call is unique -> lookup fails -> make_shared + insert.
void bench_itemcache_misses(benchmark::State& state)
{
	const auto batch = static_cast<size_t>(state.range(0));
	uint32_t counter = 0;

	for ([[maybe_unused]] auto _ : state) {
		state.PauseTiming();
		MapCache::flush();
		std::vector<std::shared_ptr<BasicItem>> keepAlive;
		keepAlive.reserve(batch);
		state.ResumeTiming();

		for (size_t i = 0; i < batch; ++i) {
			keepAlive.push_back(MapCache::tryGetItemFromCache(
			    makeItem(static_cast<uint16_t>(counter & 0xFFFF), static_cast<uint16_t>(counter >> 16))));
			++counter;
		}
		benchmark::DoNotOptimize(keepAlive.data());

		state.PauseTiming();
		keepAlive.clear();
		state.ResumeTiming();
	}
	state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(batch));
}
BENCHMARK(bench_itemcache_misses)->Arg(4096)->Arg(65536);

// Map::setTile leaf-cache benefit: FLOOR_SIZE == 8, so row-major insertion hits
// the same-leaf fast path 7/8 calls; shuffled coordinates thrash it.
void runSetTile(benchmark::State& state, bool scattered)
{
	constexpr uint16_t base = 1024;
	constexpr uint16_t span = 128; // 16384 tiles, 256 leaves
	constexpr uint8_t z = 7;

	std::vector<std::pair<uint16_t, uint16_t>> coords;
	coords.reserve(static_cast<size_t>(span) * span);
	for (uint16_t y = 0; y < span; ++y) {
		for (uint16_t x = 0; x < span; ++x) {
			coords.emplace_back(static_cast<uint16_t>(base + x), static_cast<uint16_t>(base + y));
		}
	}
	if (scattered) {
		std::mt19937 rng{12345};
		std::shuffle(coords.begin(), coords.end(), rng);
	}

	for ([[maybe_unused]] auto _ : state) {
		state.PauseTiming();
		std::vector<std::unique_ptr<Tile>> tiles;
		tiles.reserve(coords.size());
		for (const auto& [x, y] : coords) {
			tiles.push_back(std::make_unique<DynamicTile>(x, y, z));
		}
		auto map = std::make_unique<Map>();
		state.ResumeTiming();

		for (size_t i = 0; i < coords.size(); ++i) {
			map->setTile(coords[i].first, coords[i].second, z, std::move(tiles[i]));
		}

		state.PauseTiming();
		map.reset();
		Zones::clear();
		state.ResumeTiming();
	}
	state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(coords.size()));
}

void bench_setTile_sequential(benchmark::State& state) { runSetTile(state, false); }
void bench_setTile_scattered(benchmark::State& state) { runSetTile(state, true); }
BENCHMARK(bench_setTile_sequential)->Unit(benchmark::kMicrosecond);
BENCHMARK(bench_setTile_scattered)->Unit(benchmark::kMicrosecond);

} // namespace

BENCHMARK_MAIN();
