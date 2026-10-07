#include "../otpch.h"

#include "../creature.h"
#include "../map.h"
#include "../spectators.h"
#include "../tile.h"

#include <benchmark/benchmark.h>

namespace {
class BenchCreature final : public Creature
{
public:
	const std::string& getName() const override { return name; }
	const std::string& getNameDescription() const override { return name; }
	std::string getDescription(int32_t) const override { return name; }
	CreatureType_t getType() const override { return CREATURETYPE_MONSTER; }
	void setID() override {}
	void addList() override {}
	void removeList() override {}

private:
	std::string name = "movement benchmark";
};

void movement(benchmark::State& state, bool reuse)
{
	Map map;
	std::vector<std::shared_ptr<BenchCreature>> creatures;
	for (int i = 0; i < state.range(0); ++i) {
		const Position pos{static_cast<uint16_t>(90 + i % 24), static_cast<uint16_t>(90 + (i / 24) % 24), 7};
		if (!map.getTile(pos)) {
			map.setTile(pos, std::make_unique<StaticTile>(pos.x, pos.y, pos.z));
		}
		auto creature = std::make_shared<BenchCreature>();
		map.getTile(pos)->internalAddThing(creature.get());
		map.getQTNode(pos.x, pos.y)->addCreature(creature.get());
		creatures.push_back(std::move(creature));
	}
	const Position oldPos{101, 101, 7}, newPos{102, 102, 7};
	for (auto _ : state) {
		SpectatorVec spectators;
		if (reuse) {
			map.getMovementSpectators(spectators, oldPos, newPos, false);
		} else {
			SpectatorVec next;
			map.getSpectators(spectators, oldPos, true);
			map.getSpectators(next, newPos, true);
			spectators.addSpectators(next);
			spectators.partitionByType();
		}
		benchmark::DoNotOptimize(spectators.size());
	}
	for (const auto& creature : creatures) {
		const Position pos = creature->getPosition();
		map.getQTNode(pos.x, pos.y)->removeCreature(creature.get());
		map.getTile(pos)->removeThing(creature.get(), 0);
		creature->setParent(nullptr);
	}
}
void movementTwoQueries(benchmark::State& state) { movement(state, false); }
void movementOneQuery(benchmark::State& state) { movement(state, true); }
BENCHMARK(movementTwoQueries)->Arg(300)->Arg(600)->Arg(1000);
BENCHMARK(movementOneQuery)->Arg(300)->Arg(600)->Arg(1000);
} // namespace
BENCHMARK_MAIN();
