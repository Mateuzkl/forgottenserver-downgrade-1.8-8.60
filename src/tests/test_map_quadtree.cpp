#include "../otpch.h"

#include "../map.h"

#include "../tile.h"
#include "../zones.h"

#include "test_support.h"

#include <memory>

TEST_CASE(set_tile_get_tile_roundtrip)
{
	Map map;
	map.setTile(100, 100, 7, std::make_unique<StaticTile>(100, 100, 7));
	Tile* tile = map.getTile(100, 100, 7);
	CHECK(tile != nullptr);
	CHECK(tile->getPosition() == Position(100, 100, 7));
	CHECK(map.getTile(100, 101, 7) == nullptr);
	CHECK(map.getTile(100, 100, 16) == nullptr); // z bounds-checked
}

TEST_CASE(consecutive_tiles_share_one_leaf) // leaf-cache hit path
{
	Map map;
	for (uint16_t x = 96; x < 104; ++x) {
		map.setTile(x, 96, 7, std::make_unique<StaticTile>(x, 96, 7));
	}
	QTreeLeafNode* leaf = map.getQTNode(96, 96);
	CHECK(leaf != nullptr);
	CHECK(leaf == map.getQTNode(103, 96)); // same 8x8 block
	for (uint16_t x = 96; x < 104; ++x) {
		CHECK(map.getTile(x, 96, 7) != nullptr);
	}
}

TEST_CASE(block_boundary_uses_distinct_leaves) // x=7 -> base 0, x=8 -> base 8
{
	Map map;
	map.setTile(7, 7, 7, std::make_unique<StaticTile>(7, 7, 7));
	map.setTile(8, 7, 7, std::make_unique<StaticTile>(8, 7, 7));
	CHECK(map.getQTNode(7, 7) != nullptr);
	CHECK(map.getQTNode(8, 7) != nullptr);
	CHECK(map.getQTNode(7, 7) != map.getQTNode(8, 7));
	CHECK(map.getTile(7, 7, 7) != nullptr);
	CHECK(map.getTile(8, 7, 7) != nullptr);
}

TEST_CASE(alternating_blocks_thrash_leaf_cache_safely)
{
	Map map;
	map.setTile(0, 0, 7, std::make_unique<StaticTile>(0, 0, 7));
	map.setTile(8, 0, 7, std::make_unique<StaticTile>(8, 0, 7));
	map.setTile(1, 0, 7, std::make_unique<StaticTile>(1, 0, 7)); // back to first block
	CHECK(map.getTile(0, 0, 7) != nullptr);
	CHECK(map.getTile(8, 0, 7) != nullptr);
	CHECK(map.getTile(1, 0, 7) != nullptr);
}

TEST_CASE(two_maps_interleaved_do_not_share_leaf_cache)
{
	Map a;
	Map b;
	a.setTile(50, 50, 7, std::make_unique<StaticTile>(50, 50, 7));
	b.setTile(50, 50, 7, std::make_unique<StaticTile>(50, 50, 7));
	a.setTile(51, 50, 7, std::make_unique<StaticTile>(51, 50, 7)); // same block as a's first write
	CHECK(a.getTile(50, 50, 7) != nullptr);
	CHECK(b.getTile(50, 50, 7) != nullptr);
	CHECK(a.getTile(50, 50, 7) != b.getTile(50, 50, 7));
	CHECK(b.getTile(51, 50, 7) == nullptr); // b never got that tile
}

TEST_CASE(z_layers_are_independent_floors_in_one_leaf)
{
	Map map;
	map.setTile(20, 20, 0, std::make_unique<StaticTile>(20, 20, 0));
	map.setTile(20, 20, 15, std::make_unique<StaticTile>(20, 20, 15));
	CHECK(map.getTile(20, 20, 0) != nullptr);
	CHECK(map.getTile(20, 20, 15) != nullptr);
	CHECK(map.getTile(20, 20, 0) != map.getTile(20, 20, 15));
	map.setTile(20, 20, 16, std::make_unique<StaticTile>(20, 20, 16)); // rejected, must not crash
	CHECK(map.getTile(20, 20, 0) != nullptr);
}

TEST_CASE(set_basic_tile_stores_cache_and_lazily_materializes) // item-less BasicTile only
{
	Map map;
	auto basic = std::make_shared<BasicTile>();
	basic->flags = TILESTATE_PROTECTIONZONE;
	map.setBasicTile(200, 200, 7, basic);

	QTreeLeafNode* leaf = map.getQTNode(200, 200);
	CHECK(leaf != nullptr);
	Floor* floor = leaf->getFloor(7);
	CHECK(floor != nullptr);
	CHECK(floor->getTileCache(200, 200) != nullptr);

	Tile* tile = map.getTile(200, 200, 7); // lazy conversion; no Item::items access (no ground/items)
	CHECK(tile != nullptr);
	CHECK(tile->hasFlag(TILESTATE_PROTECTIONZONE));
	CHECK(floor->getTileCache(200, 200) == nullptr); // cache consumed

	Zones::clear(); // setBasicTile registers positions in global zone state
}

TFS_TEST_MAIN()
