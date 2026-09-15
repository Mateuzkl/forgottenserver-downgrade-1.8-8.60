#include "../otpch.h"

#include "../container.h"
#include "../game.h"
#include "../scheduler.h"

#include "test_support.h"

struct LootHighlightTestAccess
{
	static void registerEvent(const std::shared_ptr<Item>& item, uint32_t eventId)
	{
		g_game.lootHighlightEvents[std::weak_ptr<Item>{item}] = eventId;
	}

	static bool hasEvent(const std::shared_ptr<Item>& item)
	{
		return g_game.lootHighlightEvents.contains(std::weak_ptr<Item>{item});
	}

	static void checkItem(const std::shared_ptr<Item>& item, uint32_t eventId)
	{
		g_game.checkLootHighlight(item, 1, 0, 0, eventId);
	}
};

TEST_CASE(rejected_loot_highlight_schedule_clears_active_state)
{
	CHECK(g_scheduler.getState() != THREAD_STATE_RUNNING);

	auto corpse = std::make_shared<Container>(ITEM_BAG, 8);
	auto loot = std::make_shared<Item>(2160);
	corpse->internalAddThing(loot.get());

	g_game.startLootHighlight(corpse.get(), 1);

	CHECK(!corpse->hasLootHighlight());
	CHECK(!LootHighlightTestAccess::hasEvent(corpse));
}

TEST_CASE(empty_loot_corpse_removal_erases_registered_event)
{
	auto corpse = std::make_shared<Container>(ITEM_BAG, 8);
	auto loot = std::make_shared<Item>(2160);
	corpse->internalAddThing(loot.get());
	corpse->setLootHighlightActive(true);
	LootHighlightTestAccess::registerEvent(corpse, 1001);

	corpse->removeThing(loot.get(), loot->getItemCount());

	CHECK(corpse->empty());
	CHECK(!corpse->hasLootHighlight());
	CHECK(!LootHighlightTestAccess::hasEvent(corpse));
}

TEST_CASE(non_container_loot_highlight_callback_erases_registered_event)
{
	auto item = std::make_shared<Item>(2160);
	constexpr uint32_t eventId = 1002;
	LootHighlightTestAccess::registerEvent(item, eventId);

	LootHighlightTestAccess::checkItem(item, eventId);

	CHECK(!LootHighlightTestAccess::hasEvent(item));
}

TEST_CASE(terminal_loot_highlight_callback_clears_active_state)
{
	auto corpse = std::make_shared<Container>(ITEM_BAG, 8);
	auto loot = std::make_shared<Item>(2160);
	corpse->internalAddThing(loot.get());
	corpse->setLootHighlightActive(true);
	constexpr uint32_t eventId = 1003;
	LootHighlightTestAccess::registerEvent(corpse, eventId);

	LootHighlightTestAccess::checkItem(corpse, eventId);

	CHECK(!corpse->hasLootHighlight());
	CHECK(!LootHighlightTestAccess::hasEvent(corpse));
}

TFS_TEST_MAIN()
