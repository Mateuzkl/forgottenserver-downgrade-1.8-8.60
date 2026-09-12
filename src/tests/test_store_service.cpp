#include "../otpch.h"

#include "../store/store_catalog.h"
#include "../store/store_name_validator.h"
#include "../store/store_protocol.h"
#include "../store/store_types.h"
#include "../store/store_service.h"
#include "../tools.h"

#include <filesystem>

#include "test_support.h"

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
	CHECK(parseStoreOfferType("battlepass") == StoreOfferType::BattlePass);
	CHECK(parseStoreOfferType("prey_wildcard") == StoreOfferType::PreyWildcard);

	// Invalid type
	CHECK(parseStoreOfferType("unknown_custom_xyz") == std::nullopt);

	// Roundtrip check
	CHECK(storeOfferTypeToString(StoreOfferType::Outfit) == "outfit");
	CHECK(storeOfferTypeToString(StoreOfferType::Premium) == "premium");
	CHECK(storeOfferTypeToString(StoreOfferType::ChangeName) == "changename");
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

	// Clear entry
	service.clearRateLimit(testPlayerId);

	// Verify that subsequent access yields a fresh, default-initialized entry
	const auto& freshLimit = service.getRateLimit(testPlayerId);
	CHECK(freshLimit.lastPurchase == std::chrono::steady_clock::time_point{});
	CHECK(freshLimit.lastTransfer == std::chrono::steady_clock::time_point{});

	// Clean up after test
	service.clearRateLimit(testPlayerId);
}

TFS_TEST_MAIN()
