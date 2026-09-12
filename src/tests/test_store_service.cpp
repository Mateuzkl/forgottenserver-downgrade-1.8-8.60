#include "../otpch.h"

#include "../item.h"
#include "../networkmessage.h"
#include "../player.h"
#include "../store/store_catalog.h"
#include "../store/store_daily_offers.h"
#include "../store/store_name_validator.h"
#include "../store/store_protocol.h"
#include "../store/store_service.h"
#include "../store/store_types.h"
#include "../storeinbox.h"
#include "../tools.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_set>

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

std::filesystem::path writeStoreHighlightFixture()
{
	const auto path = std::filesystem::temp_directory_path() / "tfs_store_highlights_test.xml";
	std::ofstream output(path, std::ios::trunc);
	output << R"xml(<?xml version="1.0"?>
<store>
  <category name="Featured" icon="featured" state="new">
    <offer id="990001" name="Sale" price="10" itemid="268" state="sale" saleValidUntilTimestamp="4102444800"/>
    <offer id="990002" name="Timed" price="10" itemid="268" state="timed" validuntil="1"/>
  </category>
  <category name="Normal" icon="normal">
    <offer id="990003" name="Normal" price="10" itemid="268"/>
  </category>
</store>)xml";
	output.close();
	return path;
}

std::filesystem::path writeDailyOffersFixture(const std::filesystem::path& statePath)
{
	const auto path = std::filesystem::temp_directory_path() / "tfs_store_daily_offers_test.xml";
	std::ofstream output(path, std::ios::trunc);
	output << R"xml(<?xml version="1.0"?>
<store>
  <dailyOffers enabled="true" count="2" rotationHours="1" rotateOnStartup="false"
               minDiscountPercent="20" maxDiscountPercent="20" state="sale" stateFile=")xml"
	       << statePath.generic_string() << R"xml("/>
  <category name="Daily">
    <offer id="991001" name="One" price="10" itemid="268"/>
    <offer id="991002" name="Two" price="20" itemid="268"/>
    <offer id="991003" name="Three" price="30" itemid="268"/>
    <offer id="991004" name="Four" price="40" itemid="268"/>
    <offer id="991005" name="Excluded" price="50" itemid="268" dailyEligible="false"/>
  </category>
</store>)xml";
	output.close();
	return path;
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

TEST_CASE(test_store_highlight_states_and_packet_layout)
{
	CHECK(parseStoreHighlightState("none") == StoreHighlightState::None);
	CHECK(parseStoreHighlightState("new") == StoreHighlightState::New);
	CHECK(parseStoreHighlightState("sale") == StoreHighlightState::Sale);
	CHECK(parseStoreHighlightState("timed") == StoreHighlightState::Timed);
	CHECK(parseStoreHighlightState("STATE_NEW") == StoreHighlightState::New);
	CHECK(parseStoreHighlightState("4") == std::nullopt);

	CHECK(StoreProtocol::effectiveHighlightState(StoreHighlightState::Sale, 200, 100) ==
	      StoreHighlightState::Sale);
	CHECK(StoreProtocol::effectiveHighlightState(StoreHighlightState::Timed, 100, 100) ==
	      StoreHighlightState::None);
	CHECK(StoreProtocol::effectiveHighlightState(StoreHighlightState::Timed, 0, 100) ==
	      StoreHighlightState::Timed);

	NetworkMessage legacy;
	legacy.addByte(0xAA);
	StoreProtocol::addCategoryHighlight(legacy, false, StoreHighlightState::New);
	StoreProtocol::addOfferHighlight(legacy, false, StoreHighlightState::Sale, 200, 100);
	legacy.addByte(0xBB);
	CHECK(legacy.getLength() == 2);
	CHECK(legacy.setBufferPosition(0));
	CHECK(legacy.getByte() == 0xAA);
	CHECK(legacy.getByte() == 0xBB);
	CHECK(legacy.getBufferPosition() == NetworkMessage::INITIAL_BUFFER_POSITION + legacy.getLength());

	NetworkMessage highlighted;
	highlighted.addByte(0xAA);
	StoreProtocol::addCategoryHighlight(highlighted, true, StoreHighlightState::New);
	StoreProtocol::addOfferHighlight(highlighted, true, StoreHighlightState::Sale, 200, 100);
	StoreProtocol::addOfferHighlight(highlighted, true, StoreHighlightState::Timed, 300, 100);
	StoreProtocol::addOfferHighlight(highlighted, true, StoreHighlightState::Timed, 100, 100);
	StoreProtocol::addOfferHighlight(highlighted, true, StoreHighlightState::None, 0, 100);
	highlighted.addByte(0xBB);

	CHECK(highlighted.setBufferPosition(0));
	CHECK(highlighted.getByte() == 0xAA);
	CHECK(highlighted.getByte() == static_cast<uint8_t>(StoreHighlightState::New));
	CHECK(highlighted.getByte() == static_cast<uint8_t>(StoreHighlightState::Sale));
	CHECK(highlighted.get<uint32_t>() == 200);
	CHECK(highlighted.getByte() == static_cast<uint8_t>(StoreHighlightState::Timed));
	CHECK(highlighted.get<uint32_t>() == 300);
	CHECK(highlighted.getByte() == static_cast<uint8_t>(StoreHighlightState::None));
	CHECK(highlighted.getByte() == static_cast<uint8_t>(StoreHighlightState::None));
	CHECK(highlighted.getByte() == 0xBB);
	CHECK(highlighted.getBufferPosition() == NetworkMessage::INITIAL_BUFFER_POSITION + highlighted.getLength());
}

TEST_CASE(test_store_catalog_highlights_and_defaults)
{
	ensureItemTypesLoaded();
	const auto fixturePath = writeStoreHighlightFixture();
	const auto catalog = StoreCatalog::loadFromXML(fixturePath.string());
	std::error_code error;
	std::filesystem::remove(fixturePath, error);

	CHECK(catalog != nullptr);
	CHECK(!error);
	if (!catalog) {
		return;
	}

	CHECK(catalog->categories().size() == 2);
	CHECK(catalog->categories()[0].state == StoreHighlightState::New);
	CHECK(catalog->categories()[1].state == StoreHighlightState::None);

	const auto* sale = catalog->findOffer(990001);
	CHECK(sale != nullptr);
	CHECK(sale->state == StoreHighlightState::Sale);
	CHECK(sale->saleValidUntilTimestamp == 4102444800U);

	const auto* timed = catalog->findOffer(990002);
	CHECK(timed != nullptr);
	CHECK(timed->state == StoreHighlightState::Timed);
	CHECK(StoreProtocol::effectiveHighlightState(timed->state, timed->saleValidUntilTimestamp, 2) ==
	      StoreHighlightState::None);

	const auto* normal = catalog->findOffer(990003);
	CHECK(normal != nullptr);
	CHECK(normal->state == StoreHighlightState::None);
	CHECK(normal->saleValidUntilTimestamp == 0);
}

TEST_CASE(test_store_daily_offers_rotate_persist_and_avoid_immediate_repeats)
{
	ensureItemTypesLoaded();
	const auto statePath = std::filesystem::temp_directory_path() / "tfs_store_daily_offers_state.xml";
	const auto fixturePath = writeDailyOffersFixture(statePath);
	std::error_code error;
	std::filesystem::remove(statePath, error);
	error.clear();
	std::filesystem::remove(statePath.string() + ".tmp", error);

	const auto catalog = StoreCatalog::loadFromXML(fixturePath.string());
	CHECK(catalog != nullptr);
	if (!catalog) {
		return;
	}

	const auto& config = catalog->dailyOffersConfig();
	CHECK(config.enabled);
	CHECK(config.offerCount == 2);
	CHECK(config.rotationSeconds == 3600);
	CHECK(config.minimumDiscountPercent == 20);
	CHECK(config.maximumDiscountPercent == 20);

	StoreDailyOffers firstManager(12345);
	firstManager.configure(catalog, 1000);
	const auto first = firstManager.snapshot(1000);
	CHECK(first.offers.size() == 2);
	CHECK(first.validUntilTimestamp == 4600);
	std::unordered_set<uint32_t> firstIds;
	for (const auto& [offerId, dailyOffer] : first.offers) {
		const StoreOffer* catalogOffer = catalog->findOffer(offerId);
		CHECK(catalogOffer != nullptr);
		CHECK(offerId != 991005);
		CHECK(dailyOffer.state == StoreHighlightState::Sale);
		CHECK(dailyOffer.discountPercent == 20);
		CHECK(dailyOffer.price == StoreDailyOffers::discountedPrice(catalogOffer->price, 20));
		firstIds.insert(offerId);
	}

	// A reconnect/restart before expiry restores the same IDs instead of rerolling.
	StoreDailyOffers restoredManager(67890);
	restoredManager.configure(catalog, 2000);
	const auto restored = restoredManager.snapshot(2000);
	CHECK(restored.offers.size() == first.offers.size());
	for (const auto& [offerId, unused] : restored.offers) {
		(void)unused;
		CHECK(firstIds.contains(offerId));
	}

	// At equality the old rotation is expired. With enough alternatives, none
	// of the immediately previous offers is selected again.
	const auto rotated = restoredManager.snapshot(4600);
	CHECK(rotated.offers.size() == 2);
	CHECK(rotated.validUntilTimestamp == 8200);
	for (const auto& [offerId, unused] : rotated.offers) {
		(void)unused;
		CHECK(!firstIds.contains(offerId));
	}

	CHECK(StoreDailyOffers::discountedPrice(1, 99) == 1);
	CHECK(StoreDailyOffers::discountedPrice(100, 25) == 75);

	error.clear();
	std::filesystem::remove(fixturePath, error);
	error.clear();
	std::filesystem::remove(statePath, error);
	error.clear();
	std::filesystem::remove(statePath.string() + ".tmp", error);
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
