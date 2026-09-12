#include "../otpch.h"

#include "../item.h"
#include "../player.h"
#include "../store/store_catalog.h"
#include "../store/store_name_validator.h"
#include "../store/store_protocol.h"
#include "../store/store_service.h"
#include "../store/store_types.h"
#include "../storeinbox.h"
#include "../tools.h"

#include <filesystem>
#include <limits>

#include "test_support.h"

struct StoreServiceTestAccess
{
	static std::string deliverItem(Player& player, const StoreOffer& offer)
	{
		return StoreService::getInstance().deliverItem(player, offer);
	}
};

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

} // namespace

TEST_CASE(test_character_name_validation)
{
	// Formatting: trimmed and capitalized words
	CHECK(CharacterNameValidator::formatName("  john   doe  ") == "John Doe");
	CHECK(CharacterNameValidator::formatName("alice") == "Alice");

	// Valid names
	CHECK(CharacterNameValidator::validate("John Doe").empty());
	CHECK(CharacterNameValidator::validate("Hero").empty());

	// Length checks
	CHECK(!CharacterNameValidator::validate("").empty());
	CHECK(!CharacterNameValidator::validate("A").empty());
	CHECK(!CharacterNameValidator::validate("This Name Is Way Too Long To Be Accepted By The Server").empty());

	// Word count (max 5 words)
	CHECK(!CharacterNameValidator::validate("One Two Three Four Five Six").empty());

	// Consecutive spaces
	CHECK(!CharacterNameValidator::validate("John  Doe").empty());

	// Forbidden prefixes/words
	CHECK(!CharacterNameValidator::validate("GM Bob").empty());
	CHECK(!CharacterNameValidator::validate("God John").empty());
	CHECK(!CharacterNameValidator::validate("Admin Alice").empty());
	CHECK(!CharacterNameValidator::validate("Senior Tutor").empty());
	CHECK(!CharacterNameValidator::validate("Tutor").empty());
	CHECK(!CharacterNameValidator::validate("Xangel Warrior").empty());
}

TEST_CASE(test_store_offer_type_roundtrip)
{
	// Check known types parse correctly
	CHECK(parseStoreOfferType("item") == StoreOfferType::Item);
	CHECK(parseStoreOfferType("outfit") == StoreOfferType::Outfit);
	CHECK(parseStoreOfferType("mount") == StoreOfferType::Mount);
	CHECK(parseStoreOfferType("premium") == StoreOfferType::Premium);
	CHECK(parseStoreOfferType("blessing") == StoreOfferType::Blessing);
	CHECK(parseStoreOfferType("bless") == StoreOfferType::Blessing);
	CHECK(parseStoreOfferType("expboost") == StoreOfferType::ExpBoost);
	CHECK(parseStoreOfferType("xpboost") == StoreOfferType::ExpBoost);
	CHECK(parseStoreOfferType("house") == StoreOfferType::House);
	CHECK(parseStoreOfferType("changename") == StoreOfferType::ChangeName);
	CHECK(parseStoreOfferType("sexchange") == StoreOfferType::SexChange);
	CHECK(parseStoreOfferType("hireling") == StoreOfferType::Hireling);
	CHECK(parseStoreOfferType("hireling_skill") == StoreOfferType::HirelingSkill);
	CHECK(parseStoreOfferType("hireling_outfit") == StoreOfferType::HirelingOutfit);
	CHECK(parseStoreOfferType("battlepass") == StoreOfferType::BattlePass);
	CHECK(parseStoreOfferType("prey_wildcard") == StoreOfferType::PreyWildcard);
	CHECK(parseStoreOfferType("bounty_kill_boost") == StoreOfferType::BountyKillBoost);
	CHECK(parseStoreOfferType("weekly_kill_boost") == StoreOfferType::WeeklyKillBoost);
	CHECK(parseStoreOfferType("weekly_reduced_items") == StoreOfferType::WeeklyReducedItems);
	CHECK(parseStoreOfferType("weekly_task_expansion") == StoreOfferType::WeeklyTaskExpansion);

	// Invalid type
	CHECK(parseStoreOfferType("unknown_custom_xyz") == std::nullopt);

	// Roundtrip check
	CHECK(storeOfferTypeToString(StoreOfferType::Outfit) == "outfit");
	CHECK(storeOfferTypeToString(StoreOfferType::Premium) == "premium");
	CHECK(storeOfferTypeToString(StoreOfferType::ChangeName) == "changename");
	CHECK(storeOfferTypeToString(StoreOfferType::HirelingSkill) == "hireling_skill");
	CHECK(storeOfferTypeToString(StoreOfferType::HirelingOutfit) == "hireling_outfit");
	CHECK(storeOfferTypeToString(StoreOfferType::BountyKillBoost) == "bounty_kill_boost");
	CHECK(storeOfferTypeToString(StoreOfferType::WeeklyKillBoost) == "weekly_kill_boost");
	CHECK(storeOfferTypeToString(StoreOfferType::WeeklyReducedItems) == "weekly_reduced_items");
	CHECK(storeOfferTypeToString(StoreOfferType::WeeklyTaskExpansion) == "weekly_task_expansion");
}

TEST_CASE(test_store_protocol_opcodes)
{
	CHECK(static_cast<uint8_t>(StoreProtocol::ClientOpcode::Transfer) == 0xF8);
	CHECK(static_cast<uint8_t>(StoreProtocol::ClientOpcode::History) == 0xFA);
	CHECK(static_cast<uint8_t>(StoreProtocol::ClientOpcode::Open) == 0xFB);
	CHECK(static_cast<uint8_t>(StoreProtocol::ClientOpcode::Buy) == 0xFC);
	CHECK(StoreProtocol::ServerOpcode == 0xFD);

	CHECK(static_cast<uint8_t>(StoreProtocol::ResponseType::Error) == 0x00);
	CHECK(static_cast<uint8_t>(StoreProtocol::ResponseType::Catalog) == 0x01);
	CHECK(static_cast<uint8_t>(StoreProtocol::ResponseType::Success) == 0x02);
	CHECK(static_cast<uint8_t>(StoreProtocol::ResponseType::History) == 0x03);
}

TEST_CASE(test_store_catalog_load)
{
	ensureItemTypesLoaded();
	const auto repoPath = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
	                      "data/store/gamestore.xml";
	auto catalog = StoreCatalog::loadFromXML(repoPath.string());
	if (!catalog) {
		catalog = StoreCatalog::loadFromXML("data/store/gamestore.xml");
	}
	if (!catalog) {
		catalog = StoreCatalog::loadFromXML("../data/store/gamestore.xml");
	}
	CHECK(catalog != nullptr);
	if (catalog) {
		CHECK(!catalog->categories().empty());
		CHECK(catalog->bannerDelay() > 0);
		const StoreOffer* manaPotionPackage = catalog->findOffer(3002);
		CHECK(manaPotionPackage != nullptr);
		CHECK(manaPotionPackage->itemId == 268);
		CHECK(manaPotionPackage->count == 300);
	}
}

TEST_CASE(test_store_item_delivery_preserves_full_stackable_quantity)
{
	ensureItemTypesLoaded();
	const ItemType& itemType = Item::items[268]; // mana potion in the Store catalog
	CHECK(itemType.id == 268);
	CHECK(itemType.stackable);
	CHECK(itemType.stackSize > 0);

	for (const uint16_t count : {uint16_t{1}, uint16_t{100}, uint16_t{101}, uint16_t{125},
	                             uint16_t{250}, uint16_t{255}, uint16_t{256}, uint16_t{300},
	                             std::numeric_limits<uint16_t>::max()}) {
		Player player(nullptr);
		StoreOffer offer;
		offer.itemId = itemType.id;
		offer.count = count;

		CHECK(StoreServiceTestAccess::deliverItem(player, offer).empty());

		const StoreInbox* inbox = player.getStoreInbox();
		CHECK(inbox != nullptr);
		uint32_t deliveredCount = 0;
		for (const auto& item : inbox->getItemList()) {
			CHECK(item != nullptr);
			CHECK(item->getID() == offer.itemId);
			CHECK(item->getItemCount() <= itemType.stackSize);
			deliveredCount += item->getItemCount();
		}
		CHECK(deliveredCount == count);
		CHECK(inbox->size() ==
		      (static_cast<uint32_t>(count) + itemType.stackSize - 1) / itemType.stackSize);
	}
}

TEST_CASE(test_store_item_delivery_creates_distinct_nonstackable_items)
{
	ensureItemTypesLoaded();
	const ItemType& itemType = Item::items[ITEM_BAG];
	CHECK(itemType.id == ITEM_BAG);
	CHECK(!itemType.stackable);

	Player player(nullptr);
	StoreOffer offer;
	offer.itemId = ITEM_BAG;
	offer.count = 3;

	CHECK(StoreServiceTestAccess::deliverItem(player, offer).empty());
	const StoreInbox* inbox = player.getStoreInbox();
	CHECK(inbox != nullptr);
	CHECK(inbox->size() == 3);
	for (const auto& item : inbox->getItemList()) {
		CHECK(item != nullptr);
		CHECK(item->getID() == ITEM_BAG);
	}
}

TEST_CASE(test_transfer_target_normalization)
{
	// Whitespace trimming
	CHECK(asTrimmedString("  Bob  ") == "Bob");
	CHECK(asTrimmedString("\tAlice\n") == "Alice");
	CHECK(asTrimmedString("Charlie") == "Charlie");

	// Case-insensitive comparison against sender
	CHECK(caseInsensitiveEqual("John", "john"));
	CHECK(caseInsensitiveEqual("John", "JOHN"));
	CHECK(caseInsensitiveEqual("John", "JoHn"));
	CHECK(!caseInsensitiveEqual("John", "Johnny"));
}

TEST_CASE(test_store_rate_limit_cleanup)
{
	auto& service = StoreService::getInstance();
	constexpr uint32_t testPlayerId = 999999;

	// Populate rate limit entry with a non-default timestamp
	service.clearRateLimit(testPlayerId);
	auto& limit = service.getRateLimit(testPlayerId);
	limit.lastPurchase = std::chrono::steady_clock::now();
	limit.lastTransfer = std::chrono::steady_clock::now();
	limit.lastCatalog = std::chrono::steady_clock::now();
	limit.lastHistory = std::chrono::steady_clock::now();

	// Clear entry
	service.clearRateLimit(testPlayerId);

	// Verify that subsequent access yields a fresh, default-initialized entry
	const auto& freshLimit = service.getRateLimit(testPlayerId);
	CHECK(freshLimit.lastPurchase == std::chrono::steady_clock::time_point{});
	CHECK(freshLimit.lastTransfer == std::chrono::steady_clock::time_point{});
	CHECK(freshLimit.lastCatalog == std::chrono::steady_clock::time_point{});
	CHECK(freshLimit.lastHistory == std::chrono::steady_clock::time_point{});

	// Clean up after test
	service.clearRateLimit(testPlayerId);
}

TFS_TEST_MAIN()
