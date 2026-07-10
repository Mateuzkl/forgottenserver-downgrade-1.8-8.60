#include "../otpch.h"

#include "../botmanager.h"

#include "../configmanager.h"
#include "../database.h"
#include "../item.h"
#include "../player.h"

#include <cstdlib>

#include "test_support.h"

namespace {

// Player construction touches Items::getItemType (StoreInbox), which indexes
// an empty item vector unless items.otb has been loaded. Resolve the data dir
// from TFS_DATA_DIR or common relative locations; returns false when no OTB
// could be found so callers can skip Player-dependent checks gracefully.
bool ensureItemsLoaded()
{
	static int state = -1;
	if (state != -1) {
		return state == 1;
	}

	std::vector<std::string> candidates;
	if (const char* dataDir = std::getenv("TFS_DATA_DIR")) {
		candidates.push_back(std::string(dataDir) + "/items/items.otb");
	}
	candidates.emplace_back("data/items/items.otb");
	candidates.emplace_back("../data/items/items.otb");
	candidates.emplace_back("../../data/items/items.otb");
	candidates.emplace_back("../../../data/items/items.otb");

	for (const auto& path : candidates) {
		if (Item::items.loadFromOtb(path)) {
			state = 1;
			return true;
		}
	}

	state = 0;
	std::cerr << "[skip] items.otb not found; set TFS_DATA_DIR to enable Player-based checks\n";
	return false;
}

} // namespace

// --- Gate behaviour (no database, no dispatcher thread) -------------------

TEST_CASE(spawn_rejected_when_bot_system_disabled)
{
	ConfigManager::setBoolean(ConfigManager::BOT_SYSTEM_ENABLED, false);

	const auto result = BotManager::getInstance().spawnByGuid(1);
	CHECK(!result.success);
	CHECK(result.message.find("disabled") != std::string::npos);
}

TEST_CASE(register_rejected_when_bot_system_disabled)
{
	ConfigManager::setBoolean(ConfigManager::BOT_SYSTEM_ENABLED, false);

	const auto result = BotManager::getInstance().registerByName("Some Bot");
	CHECK(!result.success);
	CHECK(result.message.find("botSystemEnabled") != std::string::npos);
}

TEST_CASE(set_broadcast_rejected_when_bot_system_disabled)
{
	ConfigManager::setBoolean(ConfigManager::BOT_SYSTEM_ENABLED, false);

	std::string message;
	CHECK(!BotManager::getInstance().setBroadcast(uint32_t(1), true, &message));
	CHECK(message.find("disabled") != std::string::npos);
}

TEST_CASE(spawn_rejected_off_dispatcher_thread)
{
	// The enabled gate is checked first, so enabling isolates the thread gate.
	ConfigManager::setBoolean(ConfigManager::BOT_SYSTEM_ENABLED, true);

	const auto result = BotManager::getInstance().spawnByGuid(1);
	CHECK(!result.success);
	CHECK(result.message.find("dispatcher thread") != std::string::npos);

	ConfigManager::setBoolean(ConfigManager::BOT_SYSTEM_ENABLED, false);
}

TEST_CASE(despawn_rejected_off_dispatcher_thread)
{
	// Despawn is deliberately not gated on the enabled flag (operators must
	// always be able to clean up); the thread gate is its first statement.
	std::string message;
	CHECK(!BotManager::getInstance().despawnByGuid(1, true, &message));
	CHECK(message.find("dispatcher thread") != std::string::npos);
}

// --- Guid parsing through the public API ----------------------------------

TEST_CASE(get_guid_by_name_parses_numeric_input_without_sql)
{
	std::vector<std::string> captured;
	QueryCaptureScope capture(captured);

	const auto& manager = BotManager::getInstance();
	CHECK(manager.getGuidByName("123") == 123U);
	CHECK(manager.getGuidByName("  123  ") == 123U);
	CHECK(manager.getGuidByName("4294967295") == 4294967295U);
	CHECK(captured.empty());
}

TEST_CASE(get_guid_by_name_rejects_empty_and_whitespace_without_sql)
{
	std::vector<std::string> captured;
	QueryCaptureScope capture(captured);

	const auto& manager = BotManager::getInstance();
	CHECK(!manager.getGuidByName(""));
	CHECK(!manager.getGuidByName("   "));
	CHECK(captured.empty());
}

// --- Registry views on the empty registry ----------------------------------

TEST_CASE(unknown_guid_is_not_a_managed_bot)
{
	CHECK(!BotManager::getInstance().isManagedBot(99999));
	CHECK(BotManager::getInstance().getActiveBots().empty());
}

TEST_CASE(forget_unknown_guid_writes_nothing)
{
	std::vector<std::string> captured;
	QueryCaptureScope capture(captured);

	BotManager::getInstance().forget(99999);
	CHECK(captured.empty());
}

// --- Player bot flag --------------------------------------------------------

TEST_CASE(player_bot_flag_roundtrip)
{
	if (!ensureItemsLoaded()) {
		return;
	}

	auto player = std::make_shared<Player>(nullptr);
	CHECK(!player->isBot());
	player->setBot(true);
	CHECK(player->isBot());
	CHECK(player->client != nullptr);
}

// --- SQL-capture cases (sticky singleton state; keep these LAST) -----------

TEST_CASE(ensure_tables_emits_create_table_once)
{
	ConfigManager::setBoolean(ConfigManager::BOT_SYSTEM_ENABLED, true);

	std::vector<std::string> captured;
	QueryCaptureScope capture(captured);

	auto& manager = BotManager::getInstance();
	CHECK(manager.ensureTables());
	CHECK(manager.ensureTables());
	CHECK(captured.size() == 1U);
	CHECK(captured[0].find("CREATE TABLE IF NOT EXISTS `bot_players`") != std::string::npos);

	ConfigManager::setBoolean(ConfigManager::BOT_SYSTEM_ENABLED, false);
}

TEST_CASE(register_rejects_invalid_name_without_registry_writes)
{
	ConfigManager::setBoolean(ConfigManager::BOT_SYSTEM_ENABLED, true);

	std::vector<std::string> captured;
	QueryCaptureScope capture(captured);

	const auto result = BotManager::getInstance().registerByName("!!bad name!!");
	CHECK(!result.success);
	CHECK(result.message == "Invalid player name.");
	for (const auto& query : captured) {
		CHECK(query.find("INSERT") == std::string::npos);
	}

	ConfigManager::setBoolean(ConfigManager::BOT_SYSTEM_ENABLED, false);
}

TFS_TEST_MAIN()
