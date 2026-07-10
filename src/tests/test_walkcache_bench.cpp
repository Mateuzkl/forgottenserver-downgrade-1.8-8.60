// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

// Deterministic pathfinding benchmark used to compare monster walk-cache variants.
// It intentionally uses only APIs that exist both with and without the walk cache,
// so the same file can be compiled on every variant under comparison.
//
// Output (one line per scenario, parseable):
//   BENCH,<scenario>,<samples>,<median_ns>,<p10_ns>,<p90_ns>,<total_ms>,<checksum>
// The checksum accumulates every path result and must be identical across variants;
// a mismatch means walkability results changed, and timings must not be compared.
//
// Iteration count is controlled by TFS_BENCH_ITERS (default 50, so ctest stays fast).
// The item registry is loaded from TFS_DATA_DIR/items/items.otb (with relative fallbacks).

#include "../otpch.h"

#include "../game.h"
#include "../item.h"
#include "../items.h"
#include "../monster.h"
#include "../monsters.h"
#include "../tile.h"
#include "../tools.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "test_support.h"

extern Game g_game;

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint16_t kBaseX = 20000;
constexpr uint16_t kBaseY = 20000;
constexpr uint8_t kZ = 7;
constexpr int32_t kArenaSize = 64;
constexpr int32_t kCorridorY = kArenaSize / 2; // this row is kept fully walkable
constexpr double kWallDensity = 0.18;

uint64_t benchIterations()
{
	if (const char* env = std::getenv("TFS_BENCH_ITERS")) {
		const auto value = std::strtoull(env, nullptr, 10);
		if (value > 0) {
			return value;
		}
	}
	return 50;
}

bool loadItemRegistry()
{
	static const bool loaded = [] {
		std::vector<std::string> candidates;
		if (const char* dataDir = std::getenv("TFS_DATA_DIR")) {
			candidates.push_back(std::string(dataDir) + "/items/items.otb");
		}
		candidates.emplace_back("data/items/items.otb");
		candidates.emplace_back("../data/items/items.otb");
		candidates.emplace_back("../../data/items/items.otb");
		candidates.emplace_back("../../../data/items/items.otb");
		for (const std::string& path : candidates) {
			std::error_code ec;
			if (std::filesystem::exists(path, ec) && Item::items.loadFromOtb(path)) {
				return true;
			}
		}
		return false;
	}();
	return loaded;
}

// Scan the registry instead of hardcoding client-version specific ids.
uint16_t findGroundId()
{
	for (uint16_t id = 100; id < Item::items.size(); ++id) {
		const ItemType& it = Item::items[id];
		if (it.id == id && it.isGroundTile() && !it.blockSolid && !it.blockPathFind && !it.isMagicField() &&
		    !it.isTeleport() && it.floorChange == 0) {
			return id;
		}
	}
	return 0;
}

uint16_t findBlockerId()
{
	for (uint16_t id = 100; id < Item::items.size(); ++id) {
		const ItemType& it = Item::items[id];
		if (it.id == id && it.blockSolid && it.blockPathFind && !it.moveable && !it.isGroundTile() &&
		    !it.isMagicField() && !it.isTeleport() && it.floorChange == 0) {
			return id;
		}
	}
	return 0;
}

struct Arena
{
	Position base{0, 0, kZ};
	uint16_t groundId = 0;
	uint16_t blockerId = 0;
	std::vector<std::pair<int32_t, int32_t>> openCells;

	Position pos(int32_t x, int32_t y) const
	{
		return {static_cast<uint16_t>(base.x + x), static_cast<uint16_t>(base.y + y), kZ};
	}
};

Tile* makeGroundTile(const Position& pos, uint16_t groundId)
{
	Tile* tile = new StaticTile(pos.x, pos.y, pos.z);
	// setTile must run before anything is added: it establishes shared ownership of the
	// tile (Creature::setParent goes through weak_from_this) and creates the QTree leaf.
	g_game.map.setTile(pos.x, pos.y, pos.z, tile);
	auto ground = Item::CreateItem(groundId);
	CHECK(ground != nullptr);
	tile->internalAddThing(ground.get());
	return tile;
}

// Walls are simply missing tiles: unwalkable on every variant, no extra items needed.
Arena buildArena(uint16_t baseX, uint16_t baseY, uint32_t wallSeed)
{
	Arena arena;
	arena.base = Position(baseX, baseY, kZ);
	arena.groundId = findGroundId();
	arena.blockerId = findBlockerId();
	CHECK(arena.groundId != 0);
	CHECK(arena.blockerId != 0);

	std::mt19937 gen(wallSeed);
	std::uniform_real_distribution<double> roll(0.0, 1.0);
	for (int32_t y = 0; y < kArenaSize; ++y) {
		for (int32_t x = 0; x < kArenaSize; ++x) {
			const bool isWall = roll(gen) < kWallDensity && y != kCorridorY;
			if (isWall) {
				continue;
			}
			makeGroundTile(arena.pos(x, y), arena.groundId);
			arena.openCells.emplace_back(x, y);
		}
	}
	return arena;
}

std::shared_ptr<Monster> makeMonster(const Position& pos)
{
	auto monster = std::make_shared<Monster>(std::make_shared<MonsterType>());
	CHECK(g_game.map.placeCreature(pos, monster.get()));
	CHECK(monster->getPosition() == pos);
	return monster;
}

// Minimal replica of Map::moveCreature without player/postNotification machinery
// (g_moveEvents and friends are not loaded in the test binary). Dispatching
// onCreatureMove directly is equivalent to spectator dispatch in this
// single-monster world, and keeps the walk-cache maintenance path exercised.
void stepMonster(Monster& monster, const Position& toPos, bool teleport)
{
	Tile* newTile = g_game.map.getTile(toPos.x, toPos.y, toPos.z);
	CHECK(newTile != nullptr);
	Tile* oldTile = monster.getTile();
	CHECK(oldTile != nullptr);
	const Position oldPos = oldTile->getPosition();

	oldTile->removeThing(&monster, 0);
	QTreeLeafNode* oldLeaf = g_game.map.getQTNode(oldPos.x, oldPos.y);
	QTreeLeafNode* newLeaf = g_game.map.getQTNode(toPos.x, toPos.y);
	CHECK(oldLeaf != nullptr);
	CHECK(newLeaf != nullptr);
	if (oldLeaf != newLeaf) {
		oldLeaf->removeCreature(&monster);
		newLeaf->addCreature(&monster);
	}
	newTile->addThing(&monster);

	monster.onCreatureMove(&monster, newTile, toPos, oldTile, oldPos, teleport);
}

void removeMonster(std::shared_ptr<Monster>& monster)
{
	if (!monster) {
		return;
	}
	if (Tile* tile = monster->getTile()) {
		const Position pos = tile->getPosition();
		tile->removeThing(monster.get(), 0);
		if (QTreeLeafNode* leaf = g_game.map.getQTNode(pos.x, pos.y)) {
			leaf->removeCreature(monster.get());
		}
	}
	monster.reset();
}

FindPathParams chaseParams()
{
	// Real melee-chase parameters (see Monster::getPathSearchParams / Creature follow logic).
	FindPathParams fpp;
	fpp.fullPathSearch = true;
	fpp.clearSight = true;
	fpp.allowDiagonal = true;
	fpp.keepDistance = false;
	fpp.maxSearchDist = 10;
	fpp.minTargetDist = 1;
	fpp.maxTargetDist = 1;
	return fpp;
}

uint64_t pathChecksum(bool found, const std::vector<Direction>& dirs)
{
	uint64_t sum = found ? 1000000007ULL : 0ULL;
	sum += dirs.size() * 131ULL;
	for (const Direction dir : dirs) {
		sum = sum * 31ULL + static_cast<uint64_t>(dir);
	}
	return sum;
}

void report(const char* scenario, std::vector<uint64_t>& samplesNs, double totalMs, uint64_t checksum)
{
	CHECK(!samplesNs.empty());
	std::sort(samplesNs.begin(), samplesNs.end());
	const auto at = [&](double q) { return samplesNs[static_cast<size_t>(q * (samplesNs.size() - 1))]; };
	std::cout << "BENCH," << scenario << ',' << samplesNs.size() << ',' << at(0.5) << ',' << at(0.1) << ','
	          << at(0.9) << ',' << totalMs << ',' << checksum << std::endl;
}

double msSince(Clock::time_point start)
{
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

} // namespace

// S1: steady-state chase. Path + one step per iteration; on cached variants every
// canWalkTo during the search is answerable from the cache and each step pays the
// incremental maintenance cost (which the total_ms column keeps visible).
TEST_CASE(walkcache_bench_warm_chase)
{
	CHECK(loadItemRegistry());
	const uint64_t iters = benchIterations();
	Arena arena = buildArena(kBaseX, kBaseY, 1337);
	auto monster = makeMonster(arena.pos(2, kCorridorY));

	const Position targetA = arena.pos(12, kCorridorY);
	const Position targetB = arena.pos(2, kCorridorY);
	Position target = targetA;
	const FindPathParams fpp = chaseParams();

	{
		std::vector<Direction> warmup;
		monster->getPathTo(target, warmup, fpp); // prime any lazy caches; unmeasured
	}

	std::vector<uint64_t> samples;
	samples.reserve(iters);
	uint64_t checksum = 0;
	const auto wallStart = Clock::now();
	for (uint64_t i = 0; i < iters; ++i) {
		std::vector<Direction> dirs;
		const auto t0 = Clock::now();
		const bool found = monster->getPathTo(target, dirs, fpp);
		const auto t1 = Clock::now();
		samples.push_back(static_cast<uint64_t>(std::chrono::nanoseconds(t1 - t0).count()));
		checksum += pathChecksum(found, dirs);

		if (found && !dirs.empty()) {
			const Position next = getNextPosition(dirs.back(), monster->getPosition());
			stepMonster(*monster, next, false);
		}
		const Position myPos = monster->getPosition();
		if (!found || dirs.size() <= 1 || (myPos.getDistanceX(target) <= 1 && myPos.getDistanceY(target) <= 1)) {
			target = (target == targetA) ? targetB : targetA;
		}
	}
	const double totalMs = msSince(wallStart);

	report("S1_warm_chase", samples, totalMs, checksum);
	removeMonster(monster);
}

// S2: teleport churn. Every iteration teleports the monster to a random open cell;
// every second iteration also runs a path search from the new spot. This exposes the
// cost of cache rebuilds on teleports, including teleports never followed by a search.
TEST_CASE(walkcache_bench_teleport_cold)
{
	CHECK(loadItemRegistry());
	const uint64_t iters = benchIterations();
	Arena arena = buildArena(static_cast<uint16_t>(kBaseX + 1000), kBaseY, 1337);
	auto monster = makeMonster(arena.pos(2, kCorridorY));
	const FindPathParams fpp = chaseParams();

	std::mt19937 gen(777);
	std::uniform_int_distribution<size_t> pick(0, arena.openCells.size() - 1);

	{
		std::vector<Direction> warmup;
		monster->getPathTo(arena.pos(12, kCorridorY), warmup, fpp); // prime; unmeasured
	}

	std::vector<uint64_t> samples;
	samples.reserve(iters);
	uint64_t checksum = 0;
	const auto wallStart = Clock::now();
	for (uint64_t i = 0; i < iters; ++i) {
		const auto [tx, ty] = arena.openCells[pick(gen)];
		int32_t gx = tx;
		int32_t gy = ty;
		while (gx == tx && gy == ty) {
			const auto [cx, cy] = arena.openCells[pick(gen)];
			if (std::max(std::abs(cx - tx), std::abs(cy - ty)) <= 8) {
				gx = cx;
				gy = cy;
			}
		}
		const bool doPath = (i % 2) == 0;

		const auto t0 = Clock::now();
		stepMonster(*monster, arena.pos(tx, ty), /*teleport=*/true);
		if (doPath) {
			std::vector<Direction> dirs;
			const bool found = monster->getPathTo(arena.pos(gx, gy), dirs, fpp);
			checksum += pathChecksum(found, dirs);
		}
		const auto t1 = Clock::now();
		samples.push_back(static_cast<uint64_t>(std::chrono::nanoseconds(t1 - t0).count()));
	}
	const double totalMs = msSince(wallStart);

	report("S2_teleport_cold", samples, totalMs, checksum);
	removeMonster(monster);
}

// S3: tile-item churn around a stationary monster. Add/remove a solid blocker on a
// random nearby tile (real spectator dispatch through Tile::onAddTileItem/onRemoveTileItem)
// followed by a path search — isolates the incremental cache maintenance cost.
TEST_CASE(walkcache_bench_item_churn)
{
	CHECK(loadItemRegistry());
	const uint64_t iters = benchIterations();
	Arena arena = buildArena(static_cast<uint16_t>(kBaseX + 2000), kBaseY, 1337);
	const Position monsterPos = arena.pos(32, kCorridorY);
	const Position target = arena.pos(42, kCorridorY);
	auto monster = makeMonster(monsterPos);
	const FindPathParams fpp = chaseParams();

	// Open cells within the ±10 search window around the monster, excluding its own tile.
	std::vector<std::pair<int32_t, int32_t>> nearbyCells;
	for (const auto& [x, y] : arena.openCells) {
		if (std::max(std::abs(x - 32), std::abs(y - kCorridorY)) <= 10 && !(x == 32 && y == kCorridorY)) {
			nearbyCells.emplace_back(x, y);
		}
	}
	CHECK(!nearbyCells.empty());

	auto blocker = Item::CreateItem(arena.blockerId);
	CHECK(blocker != nullptr);

	std::mt19937 gen(4242);
	std::uniform_int_distribution<size_t> pick(0, nearbyCells.size() - 1);

	{
		std::vector<Direction> warmup;
		monster->getPathTo(target, warmup, fpp); // prime; unmeasured
	}

	std::vector<uint64_t> samples;
	samples.reserve(iters);
	uint64_t checksum = 0;
	const auto wallStart = Clock::now();
	for (uint64_t i = 0; i < iters; ++i) {
		const auto [x, y] = nearbyCells[pick(gen)];
		const Position cellPos = arena.pos(x, y);
		Tile* tile = g_game.map.getTile(cellPos.x, cellPos.y, cellPos.z);
		CHECK(tile != nullptr);

		const auto t0 = Clock::now();
		tile->addThing(blocker.get());
		tile->removeThing(blocker.get(), 1);
		std::vector<Direction> dirs;
		const bool found = monster->getPathTo(target, dirs, fpp);
		const auto t1 = Clock::now();
		samples.push_back(static_cast<uint64_t>(std::chrono::nanoseconds(t1 - t0).count()));
		checksum += pathChecksum(found, dirs);
	}
	const double totalMs = msSince(wallStart);

	report("S3_item_churn", samples, totalMs, checksum);
	removeMonster(monster);
}

TFS_TEST_MAIN()
