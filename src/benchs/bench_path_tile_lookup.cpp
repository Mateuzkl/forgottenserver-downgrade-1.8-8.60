#include "../otpch.h"

#include "../map.h"
#include "../tile.h"

#include <benchmark/benchmark.h>

namespace {

struct PathLookupFixture
{
	PathLookupFixture()
	{
		for (uint16_t y = 256; y < 320; ++y) {
			for (uint16_t x = 256; x < 320; ++x) {
				map.setTile(x, y, 7, std::make_unique<StaticTile>(x, y, 7));
			}
		}

		positions.reserve(64 * 64);
		for (uint16_t y = 256; y < 320; ++y) {
			if ((y & 1u) == 0) {
				for (uint16_t x = 256; x < 320; ++x) {
					positions.emplace_back(x, y, 7);
				}
			} else {
				for (uint16_t x = 320; x-- > 256;) {
					positions.emplace_back(x, y, 7);
				}
			}
		}
	}

	Map map;
	std::vector<Position> positions;
};

PathLookupFixture& fixture()
{
	static PathLookupFixture value;
	return value;
}

} // namespace

static void bench_pathTileLookup_fullTreeTraversal(benchmark::State& state)
{
	auto& [map, positions] = fixture();
	for ([[maybe_unused]] auto _ : state) {
		for (const Position& position : positions) {
			benchmark::DoNotOptimize(map.getTile(position));
		}
	}
	state.SetItemsProcessed(state.iterations() * positions.size());
}
BENCHMARK(bench_pathTileLookup_fullTreeTraversal);

static void bench_pathTileLookup_singleLeafCache(benchmark::State& state)
{
	auto& [map, positions] = fixture();
	for ([[maybe_unused]] auto _ : state) {
		QTreeLeafNode* leaf = nullptr;
		Floor* floor = nullptr;
		uint16_t leafBaseX = 0;
		uint16_t leafBaseY = 0;

		for (const Position& position : positions) {
			const uint16_t baseX = position.x & ~FLOOR_MASK;
			const uint16_t baseY = position.y & ~FLOOR_MASK;
			if (!leaf || baseX != leafBaseX || baseY != leafBaseY) {
				leaf = map.getQTNode(position.x, position.y);
				floor = leaf ? leaf->getFloor(position.z) : nullptr;
				leafBaseX = baseX;
				leafBaseY = baseY;
			}

			benchmark::DoNotOptimize(floor ? floor->getTile(position.x, position.y, position.z) : nullptr);
		}
	}
	state.SetItemsProcessed(state.iterations() * positions.size());
}
BENCHMARK(bench_pathTileLookup_singleLeafCache);

BENCHMARK_MAIN();
