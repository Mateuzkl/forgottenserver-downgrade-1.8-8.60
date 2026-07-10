// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

// Unit tests for the monster local walk cache (Monster::getWalkCache and its
// incremental maintenance). Every cached bit is validated against a live
// Tile::queryAdd with the same flags the cache was built with, so any drift
// between cache maintenance and real walkability fails loudly.
//
// The item registry is loaded from TFS_DATA_DIR/items/items.otb (with relative
// fallbacks). Known intentional gaps, mirrored from the walk-cache design:
// damaging magic fields are always re-queried live by Map::canWalkTo (their
// walkability depends on volatile monster state), and Tile::internalAddThing
// does not dispatch tile-item events (an upstream-inherited limitation), which
// T6 exploits on purpose to prove the cache is actually being consulted.

#include "../otpch.h"

#include "../game.h"
#include "../item.h"
#include "../items.h"
#include "../monster.h"
#include "../monsters.h"
#include "../tile.h"
#include "../tools.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "test_support.h"

extern Game g_game;

namespace {

constexpr uint8_t kZ = 7;
constexpr int32_t kArenaSize = 31;
constexpr int32_t kCacheRadiusX = Map::maxViewportX; // walk cache half-width
constexpr int32_t kCacheRadiusY = Map::maxViewportY; // walk cache half-height
constexpr uint32_t kPathFlags = FLAG_PATHFINDING | FLAG_IGNOREFIELDDAMAGE;

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

struct Arena
{
	Position base{0, 0, kZ};
	uint16_t groundId = 0;
	uint16_t blockerId = 0;

	Position pos(int32_t x, int32_t y) const
	{
		return {static_cast<uint16_t>(base.x + x), static_cast<uint16_t>(base.y + y), base.z};
	}
};

// A kArenaSize² block of ground tiles with a deterministic sprinkle of holes
// (missing tiles) outside the central 5×5, so cache contents are non-trivial.
// Everything outside the block has no tiles at all.
Arena buildArena(uint16_t baseX, uint16_t baseY, uint8_t z = kZ)
{
	Arena arena;
	arena.base = Position(baseX, baseY, z);
	arena.groundId = findGroundId();
	arena.blockerId = findBlockerId();
	CHECK(arena.groundId != 0);
	CHECK(arena.blockerId != 0);

	const int32_t center = kArenaSize / 2;
	for (int32_t y = 0; y < kArenaSize; ++y) {
		for (int32_t x = 0; x < kArenaSize; ++x) {
			const bool nearCenter = std::abs(x - center) <= 2 && std::abs(y - center) <= 2;
			const bool hole = !nearCenter && (x * 31 + y * 17) % 7 == 0;
			if (hole) {
				continue;
			}
			makeGroundTile(arena.pos(x, y), arena.groundId);
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
// (g_moveEvents and friends are not loaded in the test binary). `observer`, when
// given, receives the same onCreatureMove notification a spectator would get.
void stepMonster(Monster& monster, const Position& toPos, bool teleport, Monster* observer = nullptr)
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
	if (observer) {
		observer->onCreatureMove(&monster, newTile, toPos, oldTile, oldPos, teleport);
	}
}

void removeMonster(std::shared_ptr<Monster>& monster, Monster* observer = nullptr)
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
	if (observer) {
		observer->onRemoveCreature(monster.get(), false);
	}
	monster.reset();
}

// Ground truth: every in-window bit must match a live queryAdd with the cache's
// flags; probes outside the window or across floors must not be answered.
void checkCacheMatchesWorld(const Monster& monster)
{
	const Position myPos = monster.getPosition();
	for (int32_t dy = -kCacheRadiusY; dy <= kCacheRadiusY; ++dy) {
		for (int32_t dx = -kCacheRadiusX; dx <= kCacheRadiusX; ++dx) {
			const Position pos(static_cast<uint16_t>(myPos.x + dx), static_cast<uint16_t>(myPos.y + dy), myPos.z);
			const auto cached = monster.getWalkCache(pos);
			CHECK(cached.has_value());
			if (dx == 0 && dy == 0) {
				CHECK(*cached);
				continue;
			}
			const Tile* tile = g_game.map.getTile(pos.x, pos.y, pos.z);
			const bool live = tile && tile->queryAdd(0, monster, 1, kPathFlags) == RETURNVALUE_NOERROR;
			CHECK(*cached == live);
		}
	}

	const Position outsideX(static_cast<uint16_t>(myPos.x + kCacheRadiusX + 1), myPos.y, myPos.z);
	CHECK(!monster.getWalkCache(outsideX).has_value());
	const Position outsideY(myPos.x, static_cast<uint16_t>(myPos.y + kCacheRadiusY + 1), myPos.z);
	CHECK(!monster.getWalkCache(outsideY).has_value());
	const Position otherFloor(myPos.x, myPos.y, static_cast<uint8_t>(myPos.z + 1));
	CHECK(!monster.getWalkCache(otherFloor).has_value());
}

} // namespace

// T1: first getWalkCache call lazily builds the cache and every bit matches the world.
TEST_CASE(walkcache_lazy_build_matches_world)
{
	CHECK(loadItemRegistry());
	Arena arena = buildArena(30000, 30000);
	auto monster = makeMonster(arena.pos(kArenaSize / 2, kArenaSize / 2));

	checkCacheMatchesWorld(*monster);

	removeMonster(monster);
}

// T2: single-step moves in all 8 directions keep the shifted cache identical to the
// world — this validates the row/column shift logic, including diagonals and the
// vacated-tile refresh, and crosses 8-tile QTree leaf boundaries on the way.
TEST_CASE(walkcache_incremental_shifts_all_directions)
{
	CHECK(loadItemRegistry());
	Arena arena = buildArena(30200, 30000);
	const int32_t cx = kArenaSize / 2;
	auto monster = makeMonster(arena.pos(cx, cx));

	checkCacheMatchesWorld(*monster); // load the cache before stepping

	const std::vector<Direction> loop = {
	    DIRECTION_EAST,  DIRECTION_SOUTHEAST, DIRECTION_SOUTH, DIRECTION_SOUTHWEST,
	    DIRECTION_WEST,  DIRECTION_NORTHWEST, DIRECTION_NORTH, DIRECTION_NORTHEAST,
	    DIRECTION_WEST,  DIRECTION_EAST, // ping-pong across the same leaf boundary
	};
	for (const Direction dir : loop) {
		const Position next = getNextPosition(dir, monster->getPosition());
		CHECK(g_game.map.getTile(next.x, next.y, next.z) != nullptr); // stays inside the open 5×5 center
		stepMonster(*monster, next, false);
		checkCacheMatchesWorld(*monster);
	}

	removeMonster(monster);
}

// T3: teleports (long jumps and floor changes) rebuild the cache correctly.
TEST_CASE(walkcache_teleport_rebuilds)
{
	CHECK(loadItemRegistry());
	Arena arena = buildArena(30400, 30000);
	Arena upperArena = buildArena(30400, 30000, kZ - 1);
	const int32_t cx = kArenaSize / 2;
	auto monster = makeMonster(arena.pos(cx, cx));

	checkCacheMatchesWorld(*monster);

	stepMonster(*monster, arena.pos(cx - 5, cx + 4), /*teleport=*/true);
	checkCacheMatchesWorld(*monster);

	stepMonster(*monster, upperArena.pos(cx, cx), /*teleport=*/true); // cross-floor
	checkCacheMatchesWorld(*monster);

	removeMonster(monster);
}

// T4: another creature appearing, moving and disappearing inside the window
// flips exactly the affected bits.
TEST_CASE(walkcache_tracks_other_creatures)
{
	CHECK(loadItemRegistry());
	Arena arena = buildArena(30600, 30000);
	const int32_t cx = kArenaSize / 2;
	auto monster = makeMonster(arena.pos(cx, cx));

	checkCacheMatchesWorld(*monster); // load the cache

	const Position otherPos = arena.pos(cx + 2, cx);
	auto other = makeMonster(otherPos);
	monster->onCreatureAppear(other.get(), true);
	CHECK(monster->getWalkCache(otherPos).has_value());
	CHECK(!*monster->getWalkCache(otherPos)); // creatures block pathfinding
	checkCacheMatchesWorld(*monster);

	const Position movedPos = arena.pos(cx + 2, cx + 1);
	stepMonster(*other, movedPos, false, monster.get());
	CHECK(*monster->getWalkCache(otherPos));
	CHECK(!*monster->getWalkCache(movedPos));
	checkCacheMatchesWorld(*monster);

	removeMonster(other, monster.get());
	CHECK(*monster->getWalkCache(movedPos));
	checkCacheMatchesWorld(*monster);

	removeMonster(monster);
}

// T5: solid items added/removed through the real spectator dispatch
// (Tile::onAddTileItem / Tile::onRemoveTileItem) refresh the affected bit.
TEST_CASE(walkcache_tracks_tile_items)
{
	CHECK(loadItemRegistry());
	Arena arena = buildArena(30800, 30000);
	const int32_t cx = kArenaSize / 2;
	auto monster = makeMonster(arena.pos(cx, cx));

	checkCacheMatchesWorld(*monster); // load the cache

	const Position blockPos = arena.pos(cx - 1, cx - 1);
	Tile* tile = g_game.map.getTile(blockPos.x, blockPos.y, blockPos.z);
	CHECK(tile != nullptr);
	CHECK(*monster->getWalkCache(blockPos));

	auto blocker = Item::CreateItem(arena.blockerId);
	CHECK(blocker != nullptr);
	tile->addThing(blocker.get());
	CHECK(!*monster->getWalkCache(blockPos));
	checkCacheMatchesWorld(*monster);

	tile->removeThing(blocker.get(), 1);
	CHECK(*monster->getWalkCache(blockPos));
	checkCacheMatchesWorld(*monster);

	removeMonster(monster);
}

// T6: prove Map::canWalkTo actually answers from the cache. internalAddThing
// intentionally skips tile-item events, so the cache goes stale on purpose;
// canWalkTo must keep answering with the stale bit until the event is delivered.
TEST_CASE(walkcache_is_consulted_by_map_canwalkto)
{
	CHECK(loadItemRegistry());
	Arena arena = buildArena(31000, 30000);
	const int32_t cx = kArenaSize / 2;
	auto monster = makeMonster(arena.pos(cx, cx));

	checkCacheMatchesWorld(*monster); // load the cache

	const Position blockPos = arena.pos(cx + 1, cx);
	Tile* tile = g_game.map.getTile(blockPos.x, blockPos.y, blockPos.z);
	CHECK(tile != nullptr);
	CHECK(g_game.map.canWalkTo(*monster, blockPos) == tile);

	auto blocker = Item::CreateItem(arena.blockerId);
	CHECK(blocker != nullptr);
	tile->internalAddThing(blocker.get()); // no events: cache now stale on purpose
	CHECK(tile->queryAdd(0, *monster, 1, kPathFlags) != RETURNVALUE_NOERROR);
	CHECK(g_game.map.canWalkTo(*monster, blockPos) == tile); // still answered from the stale cache

	monster->onAddTileItem(tile, blockPos, blocker.get()); // deliver the missed event
	CHECK(g_game.map.canWalkTo(*monster, blockPos) == nullptr);

	tile->removeThing(blocker.get(), 1); // real dispatch refreshes the bit again
	CHECK(g_game.map.canWalkTo(*monster, blockPos) == tile);

	removeMonster(monster);
}

TFS_TEST_MAIN()
