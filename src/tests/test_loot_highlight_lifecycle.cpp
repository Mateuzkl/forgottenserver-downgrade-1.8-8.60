#include "../otpch.h"

#include "../container.h"
#include "../game.h"
#include "../player.h"
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

struct QuickLootTestAccess
{
	static std::shared_ptr<Item> getPlayerCorpse(Player& player)
	{
		return player.getCorpse(nullptr, nullptr);
	}
};

void ensureItemTypesLoaded()
{
	static const bool loaded = [] {
		const auto itemFile = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
		                      "data/items/items.otb";
		return Item::items.loadFromOtb(itemFile.string());
	}();
	CHECK(loaded);
}

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

TEST_CASE(loot_highlight_category_is_only_sent_for_normal_lootable_corpses)
{
	ensureItemTypesLoaded();
	auto viewer = std::make_shared<Player>(nullptr);

	auto normalCorpse = std::make_shared<Container>(ITEM_BAG, 8);
	auto normalLoot = std::make_shared<Item>(2160);
	normalCorpse->internalAddThing(normalLoot.get());
	normalCorpse->setCorpseOwner(viewer->getID());
	normalCorpse->setLootHighlightActive(true);
	CHECK(normalCorpse->getSpecialCategory(viewer.get()) == CONTAINER_SPECIAL_LOOT_HIGHLIGHT);

	auto disabledCorpse = std::make_shared<Container>(ITEM_BAG, 8);
	auto disabledLoot = std::make_shared<Item>(2160);
	disabledCorpse->internalAddThing(disabledLoot.get());
	disabledCorpse->setCorpseOwner(viewer->getID());
	disabledCorpse->setCustomAttribute("QuickLootDisabled", true);
	disabledCorpse->setLootHighlightActive(true);
	CHECK(disabledCorpse->getSpecialCategory(viewer.get()) == CONTAINER_SPECIAL_NONE);

	auto rewardCorpse = std::make_shared<Container>(ITEM_BAG, 8);
	auto rewardContainer = std::make_shared<Item>(ITEM_REWARD_CONTAINER);
	rewardCorpse->internalAddThing(rewardContainer.get());
	rewardCorpse->setCorpseOwner(viewer->getID());
	rewardCorpse->setLootHighlightActive(true);
	CHECK(rewardCorpse->getSpecialCategory(viewer.get()) == CONTAINER_SPECIAL_NONE);

	auto emptyCorpse = std::make_shared<Container>(ITEM_BAG, 8);
	emptyCorpse->setCorpseOwner(viewer->getID());
	emptyCorpse->setLootHighlightActive(true);
	CHECK(emptyCorpse->getSpecialCategory(viewer.get()) == CONTAINER_SPECIAL_NONE);
}

TEST_CASE(player_corpses_disable_normal_quick_loot)
{
	ensureItemTypesLoaded();
	auto player = std::make_shared<Player>(nullptr);
	auto corpse = QuickLootTestAccess::getPlayerCorpse(*player);
	CHECK(corpse);
	CHECK(corpse->getContainer());
	CHECK(corpse->getContainer()->isQuickLootDisabled());
}

static_assert(OBJECTCATEGORY_SOULCORES == 26);

TFS_TEST_MAIN()
