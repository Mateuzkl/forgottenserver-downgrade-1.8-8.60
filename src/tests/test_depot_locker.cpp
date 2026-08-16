#include "../otpch.h"

#include "../depotchest.h"
#include "../depotlocker.h"
#include "../game.h"
#include "../inbox.h"
#include "../item.h"

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

// A creature pointer is all queryAdd needs to tell a player-driven move from an engine-driven
// one; it is only compared against nullptr and never dereferenced.
Creature* const someActor = reinterpret_cast<Creature*>(0x1);

} // namespace

TEST_CASE(depot_locker_rejects_items_dragged_by_a_player)
{
	ensureItemTypesLoaded();

	DepotLocker locker(ITEM_LOCKER);
	auto item = Item::CreateItem(ITEM_GOLD_COIN, 1);
	CHECK(item != nullptr);

	// This is the reported bug: dropping a normal item on the locker root used to succeed.
	CHECK(locker.queryAdd(INDEX_WHEREEVER, *item, 1, 0, someActor) == RETURNVALUE_NOTPOSSIBLE);
	CHECK(locker.queryAdd(0, *item, 1, 0, someActor) == RETURNVALUE_NOTPOSSIBLE);
}

TEST_CASE(depot_locker_rejects_items_moved_without_the_nolimit_flag)
{
	ensureItemTypesLoaded();

	DepotLocker locker(ITEM_LOCKER);
	auto item = Item::CreateItem(ITEM_GOLD_COIN, 1);
	CHECK(item != nullptr);

	// No actor, but also no FLAG_NOLIMIT: still not a legitimate way into the locker.
	CHECK(locker.queryAdd(INDEX_WHEREEVER, *item, 1, 0, nullptr) == RETURNVALUE_CONTAINERNOTENOUGHROOM);
}

TEST_CASE(depot_locker_still_accepts_engine_driven_structure_items)
{
	ensureItemTypesLoaded();

	DepotLocker locker(ITEM_LOCKER);
	auto inbox = Item::CreateItem(ITEM_INBOX);
	CHECK(inbox != nullptr);

	// The chest, inbox, market and stash are placed with FLAG_NOLIMIT and no actor.
	CHECK(locker.queryAdd(INDEX_WHEREEVER, *inbox, 1, FLAG_NOLIMIT, nullptr) == RETURNVALUE_NOERROR);
}

TEST_CASE(depot_box_still_accepts_items_from_a_player)
{
	ensureItemTypesLoaded();

	// The fix must not touch where items actually belong.
	DepotBox box(ITEM_DEPOT_BOX_1);
	auto item = Item::CreateItem(ITEM_GOLD_COIN, 1);
	CHECK(item != nullptr);

	CHECK(box.queryAdd(INDEX_WHEREEVER, *item, 1, 0, someActor) == RETURNVALUE_NOERROR);
}

TEST_CASE(inbox_keeps_rejecting_player_moves)
{
	ensureItemTypesLoaded();

	// Regression guard for the sibling container the locker fix was modelled on.
	Inbox inbox(ITEM_INBOX);
	auto item = Item::CreateItem(ITEM_GOLD_COIN, 1);
	CHECK(item != nullptr);

	CHECK(inbox.queryAdd(INDEX_WHEREEVER, *item, 1, 0, someActor) == RETURNVALUE_NOTPOSSIBLE);
}

TFS_TEST_MAIN()
