#include "../otpch.h"

#include "../mapcache.h"

#include "test_support.h"

#include <memory>

namespace {

BasicItem makeItem(uint16_t id, uint16_t actionId = 0, std::string text = {})
{
	BasicItem item;
	item.id = id;
	item.actionId = actionId;
	item.text = std::move(text);
	return item;
}

} // namespace

TEST_CASE(equal_items_have_equal_hash_and_compare_equal)
{
	auto a = makeItem(100, 5, "note");
	auto b = makeItem(100, 5, "note");
	CHECK(a == b);
	CHECK(a.hash() == b.hash());
	b.text = "other";
	CHECK(a != b); // text participates in equality
}

TEST_CASE(children_affect_equality_and_hash)
{
	auto parent1 = makeItem(200);
	auto parent2 = makeItem(200);
	CHECK(parent1 == parent2);
	parent1.items.push_back(std::make_shared<BasicItem>(makeItem(300)));
	CHECK(parent1 != parent2);
	parent2.items.push_back(std::make_shared<BasicItem>(makeItem(300)));
	CHECK(parent1 == parent2);
	CHECK(parent1.hash() == parent2.hash());
}

TEST_CASE(item_cache_dedups_equal_items)
{
	MapCache::flush();
	auto first = MapCache::tryGetItemFromCache(makeItem(100, 5));
	auto second = MapCache::tryGetItemFromCache(makeItem(100, 5));
	CHECK(first == second); // same shared_ptr
	CHECK(MapCache::getItemCacheSize() == 1);
	auto third = MapCache::tryGetItemFromCache(makeItem(101));
	CHECK(third != first);
	CHECK(MapCache::getItemCacheSize() == 2);
	MapCache::flush();
}

TEST_CASE(expired_entry_is_replaced_with_fresh_allocation)
{
	MapCache::flush();
	{
		auto tmp = MapCache::tryGetItemFromCache(makeItem(400));
		CHECK(tmp != nullptr);
	} // weak_ptr in cache expires here
	auto fresh = MapCache::tryGetItemFromCache(makeItem(400));
	CHECK(fresh != nullptr);
	CHECK(MapCache::getItemCacheSize() == 1);
	MapCache::flush();
}

TEST_CASE(cleanup_expired_entries_shrinks_cache)
{
	MapCache::flush();
	{
		auto tmp = MapCache::tryGetItemFromCache(makeItem(500));
		CHECK(tmp != nullptr);
	}
	CHECK(MapCache::getItemCacheSize() == 1); // stale weak_ptr still indexed
	MapCache::cleanupExpiredEntries();
	CHECK(MapCache::getItemCacheSize() == 0);
}

TEST_CASE(nested_container_dedup)
{
	MapCache::flush();
	auto child = MapCache::tryGetItemFromCache(makeItem(300));
	BasicItem parentA = makeItem(200);
	parentA.items.push_back(child);
	BasicItem parentB = makeItem(200);
	parentB.items.push_back(child);
	auto a = MapCache::tryGetItemFromCache(std::move(parentA));
	auto b = MapCache::tryGetItemFromCache(std::move(parentB));
	CHECK(a == b);
	MapCache::flush();
}

TEST_CASE(tile_cache_dedup_and_flush)
{
	MapCache::flush();
	BasicTile t1;
	t1.flags = 1;
	t1.zoneIds = {2, 3};
	BasicTile t2;
	t2.flags = 1;
	t2.zoneIds = {2, 3};
	CHECK(t1 == t2);
	CHECK(t1.hash() == t2.hash());
	auto p1 = MapCache::tryGetTileFromCache(std::move(t1));
	auto p2 = MapCache::tryGetTileFromCache(std::move(t2));
	CHECK(p1 == p2);
	CHECK(MapCache::getTileCacheSize() == 1);
	MapCache::flush();
	CHECK(MapCache::getTileCacheSize() == 0);
	CHECK(MapCache::getItemCacheSize() == 0);
}

TFS_TEST_MAIN()
