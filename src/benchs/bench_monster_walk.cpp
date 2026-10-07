#include "../otpch.h"

#include "../game.h"
#include "../item.h"
#include "../monster.h"
#include "../tile.h"

#include <benchmark/benchmark.h>

// Keep the pre-cache implementation executable on the same fixture, compiler
// and current library. This measures only canWalkTo, not server capacity.
struct MonsterWalkTestAccess
{
	static bool current(const Monster& monster, Direction direction)
	{
		return monster.canWalkTo(monster.getPosition(), direction);
	}
	static bool previous(const Monster& monster, Direction direction)
	{
		const Position pos = getNextPosition(direction, monster.getPosition());
		if (!monster.isInSpawnRange(pos)) {
			return false;
		}
		Tile* tile = g_game.map.getTile(pos);
		uint32_t flags = FLAG_PATHFINDING;
		if (monster.isFamiliar() || monster.ignoreFieldDamage) {
			flags |= FLAG_IGNOREFIELDDAMAGE;
		}
		if (monster.isSummon() && !monster.isFamiliar() && tile && tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
			return false;
		}
		return tile && tile->getTopVisibleCreature(&monster) == nullptr &&
		       tile->queryAdd(0, monster, 1, flags) == RETURNVALUE_NOERROR;
	}
};

namespace {
void walk(benchmark::State& state, bool cached)
{
	auto type = std::make_shared<MonsterType>();
	type->name = "walk benchmark";
	std::vector<std::shared_ptr<Monster>> monsters;
	for (int i = 0; i < state.range(0); ++i) {
		const Position center{static_cast<uint16_t>(56000 + (i % 40) * 4), static_cast<uint16_t>(56000 + (i / 40) * 4),
		                      7};
		for (int dx = -1; dx <= 1; ++dx) {
			for (int dy = -1; dy <= 1; ++dy) {
				const Position pos{static_cast<uint16_t>(center.x + dx), static_cast<uint16_t>(center.y + dy), 7};
				if (!g_game.map.getTile(pos)) {
					g_game.map.setTile(pos, std::make_unique<DynamicTile>(pos.x, pos.y, pos.z));
				}
				Tile* tile = g_game.map.getTile(pos);
				tile->setGround(Item::make<Item>(0));
				tile->resetFlag(UINT32_MAX);
				if (state.range(1) && (dx || dy)) {
					tile->setFlag(TILESTATE_IMMOVABLEBLOCKSOLID);
				}
			}
		}
		auto monster = std::make_shared<Monster>(type);
		g_game.map.getTile(center)->internalAddThing(monster.get());
		monsters.push_back(std::move(monster));
	}
	for (auto _ : state) {
		for (const auto& monster : monsters) {
			for (Direction direction : {DIRECTION_NORTH, DIRECTION_EAST, DIRECTION_SOUTH, DIRECTION_WEST}) {
				benchmark::DoNotOptimize(cached ? MonsterWalkTestAccess::current(*monster, direction)
				                                : MonsterWalkTestAccess::previous(*monster, direction));
			}
		}
	}
	state.SetItemsProcessed(state.iterations() * state.range(0) * 4);
	for (const auto& monster : monsters) {
		monster->getTile()->removeThing(monster.get(), 0);
		monster->setParent(nullptr);
	}
}
void walkPrevious(benchmark::State& state) { walk(state, false); }
void walkCached(benchmark::State& state) { walk(state, true); }
BENCHMARK(walkPrevious)->Args({300, 0})->Args({1000, 0})->Args({300, 1})->Args({1000, 1});
BENCHMARK(walkCached)->Args({300, 0})->Args({1000, 0})->Args({300, 1})->Args({1000, 1});
} // namespace
BENCHMARK_MAIN();
