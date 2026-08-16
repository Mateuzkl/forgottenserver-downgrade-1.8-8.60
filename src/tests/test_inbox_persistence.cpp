#include "../otpch.h"

#include "../container.h"
#include "../game.h"
#include "../inbox.h"
#include "../iologindata.h"
#include "../item.h"
#include "../player.h"

#include <chrono>
#include <iostream>
#include <map>

#include "test_support.h"

namespace {

void ensureItemTypesLoaded()
{
	if (Item::items.size() != 0) {
		return;
	}

	const auto itemsPath = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
	                       "data/items/items.otb";
	CHECK(Item::items.loadFromOtb(itemsPath.string()));
}

// A freshly constructed Player has no group, and getDepotLocker() reaches getMaxDepotItems(),
// which dereferences it. Loading a player from the database normally supplies one.
void giveGroup(Player& player)
{
	auto group = std::make_shared<Group>();
	group->name = "test";
	group->flags = 0;
	group->maxDepotItems = 0; // 0 falls back to the configured depot limit
	group->maxVipEntries = 0;
	group->id = 1;
	group->access = false;
	player.setGroup(group);
}

// Goes through the same call the server uses, so the locker is built exactly as in production
// (market, supply stash, inbox and depot chest).
Inbox* attachInbox(Player& player, uint32_t depotId)
{
	DepotLocker* locker = player.getDepotLocker(depotId);
	CHECK(locker != nullptr);

	Inbox* inbox = player.getInbox(depotId);
	CHECK(inbox != nullptr);
	return inbox;
}

// Items are added with internalAddThing, the same call House::transferToDepot ends up using
// through a FLAG_NOLIMIT move: the inbox is allowed to exceed its visible capacity.
void fillInbox(Inbox* inbox, size_t count, uint16_t itemId = ITEM_GOLD_COIN)
{
	for (size_t i = 0; i < count; ++i) {
		auto item = Item::CreateItem(itemId, 1);
		CHECK(item != nullptr);
		inbox->internalAddThing(item.get());
	}
}

size_t countForDepot(const ItemBlockList& itemList, int32_t depotId)
{
	size_t total = 0;
	for (const auto& [pid, item] : itemList) {
		if (pid == depotId) {
			++total;
		}
	}
	return total;
}

} // namespace

TEST_CASE(inbox_persists_every_item_regardless_of_count)
{
	ensureItemTypesLoaded();

	// The bug truncated at 100. These bracket that boundary and go well past it.
	for (const size_t count : {size_t{0}, size_t{1}, size_t{99}, size_t{100}, size_t{101}, size_t{255},
	                           size_t{256}, size_t{500}, size_t{1000}, size_t{2500}, size_t{5000}}) {
		Player player(nullptr);
		giveGroup(player);
		Inbox* inbox = attachInbox(player, 1);
		fillInbox(inbox, count);

		ItemBlockList itemList;
		IOLoginData::collectInboxItems(&player, itemList);

		CHECK(itemList.size() == count);
		CHECK(countForDepot(itemList, 1) == count);
	}
}

TEST_CASE(inbox_count_is_per_town_not_shared)
{
	ensureItemTypesLoaded();

	// The old counter was global across depotLockerMap, so 80 + 80 persisted only 100 total.
	Player player(nullptr);
	giveGroup(player);
	fillInbox(attachInbox(player, 1), 80);
	fillInbox(attachInbox(player, 2), 80);

	ItemBlockList itemList;
	IOLoginData::collectInboxItems(&player, itemList);

	CHECK(itemList.size() == 160);
	CHECK(countForDepot(itemList, 1) == 80);
	CHECK(countForDepot(itemList, 2) == 80);
}

TEST_CASE(inbox_keeps_each_town_separate_at_scale)
{
	ensureItemTypesLoaded();

	Player player(nullptr);
	giveGroup(player);
	fillInbox(attachInbox(player, 1), 150);
	fillInbox(attachInbox(player, 2), 200);
	fillInbox(attachInbox(player, 3), 500);

	ItemBlockList itemList;
	IOLoginData::collectInboxItems(&player, itemList);

	CHECK(itemList.size() == 850);
	CHECK(countForDepot(itemList, 1) == 150);
	CHECK(countForDepot(itemList, 2) == 200);
	CHECK(countForDepot(itemList, 3) == 500);
}

TEST_CASE(inbox_handles_three_thousand_across_three_towns)
{
	ensureItemTypesLoaded();

	Player player(nullptr);
	giveGroup(player);
	fillInbox(attachInbox(player, 1), 1000);
	fillInbox(attachInbox(player, 2), 1000);
	fillInbox(attachInbox(player, 3), 1000);

	ItemBlockList itemList;
	IOLoginData::collectInboxItems(&player, itemList);

	CHECK(itemList.size() == 3000);
	for (int32_t depotId = 1; depotId <= 3; ++depotId) {
		CHECK(countForDepot(itemList, depotId) == 1000);
	}
}

TEST_CASE(inbox_collects_top_level_containers_and_saveitems_walks_their_contents)
{
	ensureItemTypesLoaded();

	// collectInboxItems only lists the inbox's direct children; nested contents are expanded
	// later by saveItems, which follows every Container it is handed. This pins that split so a
	// future change cannot start double counting the children.
	Player player(nullptr);
	giveGroup(player);
	Inbox* inbox = attachInbox(player, 1);

	auto backpack = Item::CreateItem(ITEM_BAG);
	CHECK(backpack != nullptr);
	Container* outer = backpack->getContainer();
	CHECK(outer != nullptr);

	auto innerBag = Item::CreateItem(ITEM_BAG);
	CHECK(innerBag != nullptr);
	Container* inner = innerBag->getContainer();
	CHECK(inner != nullptr);

	for (int i = 0; i < 5; ++i) {
		auto item = Item::CreateItem(ITEM_GOLD_COIN, 1);
		CHECK(item != nullptr);
		inner->internalAddThing(item.get());
	}

	outer->internalAddThing(innerBag.get());
	inbox->internalAddThing(backpack.get());
	fillInbox(inbox, 3);

	ItemBlockList itemList;
	IOLoginData::collectInboxItems(&player, itemList);

	// 3 loose items + the backpack itself; the bag and its 5 coins hang off the backpack.
	CHECK(itemList.size() == 4);
	CHECK(outer->getItemList().size() == 1);
	CHECK(inner->getItemList().size() == 5);
}

TEST_CASE(inbox_collection_stays_linear)
{
	ensureItemTypesLoaded();

	// A single pass over a few thousand items is far too fast to time reliably, so each size is
	// collected repeatedly and the per-run average reported in nanoseconds.
	constexpr int repeats = 50;

	const auto measure = [](size_t count) {
		Player player(nullptr);
		giveGroup(player);
		fillInbox(attachInbox(player, 1), count);

		const auto started = std::chrono::steady_clock::now();
		for (int i = 0; i < repeats; ++i) {
			ItemBlockList itemList;
			IOLoginData::collectInboxItems(&player, itemList);
			CHECK(itemList.size() == count);
		}
		const auto elapsed = std::chrono::steady_clock::now() - started;

		return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() / repeats;
	};

	measure(1000); // warm up, result intentionally discarded

	const auto small = std::max<int64_t>(1, measure(1000));
	const auto large = measure(10000);

	std::cout << "[perf] collectInboxItems  1000 items: " << small << " ns/run\n"
	          << "[perf] collectInboxItems 10000 items: " << large << " ns/run\n";

	// Ten times the items should cost roughly ten times the work. The bound is deliberately loose
	// so a busy machine cannot make it flake, while an accidental O(n^2) rewrite still trips it.
	CHECK(large < small * 30);
}

TFS_TEST_MAIN()
