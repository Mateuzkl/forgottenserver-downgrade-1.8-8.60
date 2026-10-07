#include "../otpch.h"

#include "../configmanager.h"
#include "../database.h"
#include "../kv/kv.h"
#include "../performance_metrics.h"
#include "../player.h"
#include "../reactor.h"
#include "../vocation.h"
#include "test_support.h"

#include <cstdlib>
#include <functional>

extern Vocations g_vocations;

struct PlayerSettingsTestAccess
{
	static void useStore(Player& player, KVStore& store)
	{
		player.cachedPlayerSettings_ =
		    store.scoped("player")->scoped(std::to_string(player.getGUID()))->scoped("settings");
	}
};

struct PerformanceMetricsTestAccess
{
	static uint64_t databaseCalls()
	{
		return g_performanceMetrics.metrics[static_cast<size_t>(PerformanceMetric::DatabaseQueryDispatcher)]
		    .calls.load();
	}
};

namespace {
class TestStore final : public KVStore
{
public:
	std::unordered_map<std::string, ValueWrapper> database;
	std::unordered_map<std::string, size_t> loads;
	bool available = true;
	std::function<void()> duringLoad;
	StoreMap persistedEntries() { return getStore(); }

protected:
	LoadResult loadForCache(const std::string& key) override
	{
		++loads[key];
		auto it = database.find(key);
		auto value = it == database.end() ? std::optional<ValueWrapper>{} : it->second;
		if (duringLoad) {
			auto callback = std::move(duringLoad);
			callback();
		}
		return {std::move(value), available};
	}
};

void loadVocations()
{
	static const bool loaded = [] {
		const auto previous = std::filesystem::current_path();
		std::filesystem::current_path(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path());
		const bool result = g_vocations.loadFromXml();
		std::filesystem::current_path(previous);
		return result;
	}();
	CHECK(loaded);
}

struct Fixture
{
	Fixture(uint16_t vocation = 4)
	{
		loadVocations();
		player.setGUID(700001);
		player.setGroup(std::make_shared<Group>());
		CHECK(player.setVocation(vocation));
		ConfigManager::setBoolean(ConfigManager::CHAIN_SYSTEM_ENABLED, true);
		ConfigManager::setBoolean(ConfigManager::CLEAVE_SYSTEM_ENABLED, true);
		PlayerSettingsTestAccess::useStore(player, store);
	}
	std::string key(const char* setting) const { return "player.700001.settings." + std::string(setting); }
	TestStore store;
	Player player{nullptr};
};
} // namespace

TEST_CASE(confirmed_missing_keys_are_cached_but_not_persisted)
{
	TestStore store;
	for (int i = 0; i < 100; ++i) CHECK(!store.get("missing"));
	CHECK(store.loads["missing"] == 1);
	CHECK(store.persistedEntries().empty());
	store.set("present", ValueWrapper(true));
	store.remove("deleted");
	const auto entries = store.persistedEntries();
	CHECK(entries.size() == 2);
	CHECK(entries.at("present").first.get<BooleanType>());
	CHECK(entries.at("deleted").first.isDeleted());
}

TEST_CASE(database_errors_are_not_cached_as_missing)
{
	TestStore store;
	store.available = false;
	CHECK(!store.get("retry"));
	CHECK(!store.get("retry"));
	CHECK(store.loads["retry"] == 2);
	store.available = true;
	store.database.emplace("retry", ValueWrapper(true));
	CHECK(store.get("retry")->get<BooleanType>());
	CHECK(store.get("retry")->get<BooleanType>());
	CHECK(store.loads["retry"] == 3);
}

TEST_CASE(forced_reload_refreshes_missing_cache_without_overwriting_pending_writes)
{
	TestStore store;
	CHECK(!store.get("option"));
	store.database.emplace("option", ValueWrapper(false));
	CHECK(store.get("option", true).has_value());
	CHECK(store.get("option").has_value());
	store.set("option", ValueWrapper(true));
	CHECK(!store.get("option", true)->get<BooleanType>());
	CHECK(store.get("option")->get<BooleanType>());
	store.remove("option");
	CHECK(!store.get("option"));
	CHECK(store.persistedEntries().at("option").first.isDeleted());
}

TEST_CASE(a_write_during_a_load_wins_over_the_old_database_value)
{
	TestStore store;
	store.database.emplace("option", ValueWrapper(false));
	store.duringLoad = [&] { store.set("option", ValueWrapper(true)); };
	CHECK(store.get("option")->get<BooleanType>());
	CHECK(store.get("option")->get<BooleanType>());
	CHECK(store.loads["option"] == 1);
}

TEST_CASE(cleave_defaults_and_live_toggles_use_the_production_player_path)
{
	Fixture fixture;
	for (int i = 0; i < 100; ++i) CHECK(fixture.player.checkCleaveSystem());
	CHECK(fixture.store.loads[fixture.key("cleaveSystem")] == 1);
	fixture.store.set(fixture.key("cleaveSystem"), ValueWrapper(false));
	CHECK(!fixture.player.checkCleaveSystem());
	fixture.store.set(fixture.key("cleaveSystem"), ValueWrapper(true));
	CHECK(fixture.player.checkCleaveSystem());
	ConfigManager::setBoolean(ConfigManager::CLEAVE_SYSTEM_ENABLED, false);
	CHECK(!fixture.player.checkCleaveSystem());
	ConfigManager::setBoolean(ConfigManager::CLEAVE_SYSTEM_ENABLED, true);
	CHECK(fixture.player.setVocation(1));
	CHECK(!fixture.player.checkCleaveSystem());
	CHECK(fixture.player.setVocation(4));
	CHECK(fixture.player.checkCleaveSystem());
}

TEST_CASE(chain_defaults_legacy_migration_and_toggles_remain_live)
{
	Fixture fixture(1);
	for (int i = 0; i < 100; ++i) CHECK(!fixture.player.checkChainSystem());
	CHECK(fixture.store.loads[fixture.key("chainSystem")] == 1);
	fixture.player.setStorageValue(40001, 1);
	CHECK(fixture.player.checkChainSystem());
	CHECK(fixture.store.get(fixture.key("chainSystem"))->get<BooleanType>());
	fixture.store.set(fixture.key("chainSystem"), ValueWrapper(false));
	CHECK(!fixture.player.checkChainSystem());
	fixture.store.set(fixture.key("chainSystem"), ValueWrapper(true));
	CHECK(fixture.player.checkChainSystem());
	ConfigManager::setBoolean(ConfigManager::CHAIN_SYSTEM_ENABLED, false);
	CHECK(!fixture.player.checkChainSystem());
	ConfigManager::setBoolean(ConfigManager::CHAIN_SYSTEM_ENABLED, true);
	CHECK(fixture.player.setVocation(3));
	CHECK(!fixture.player.checkChainSystem());
	CHECK(fixture.player.setVocation(4));
	CHECK(!fixture.player.checkChainSystem());
}

TEST_CASE(quickloot_honors_unsaved_toggles_instead_of_forcing_stale_database_values)
{
	Fixture fixture;
	fixture.store.database.emplace(fixture.key("quickLoot"), ValueWrapper(false));
	CHECK(!fixture.player.isQuickLootAutoEnabled());
	fixture.store.set(fixture.key("quickLoot"), ValueWrapper(true));
	for (int i = 0; i < 100; ++i) CHECK(fixture.player.isQuickLootAutoEnabled());
	fixture.store.set(fixture.key("quickLoot"), ValueWrapper(false));
	CHECK(!fixture.player.isQuickLootAutoEnabled());
	fixture.store.remove(fixture.key("quickLoot"));
	CHECK(!fixture.player.isQuickLootAutoEnabled());
	CHECK(fixture.store.loads[fixture.key("quickLoot")] == 1);
	Player other{nullptr};
	other.setGUID(700002);
	PlayerSettingsTestAccess::useStore(other, fixture.store);
	CHECK(!other.isQuickLootAutoEnabled());
	CHECK(fixture.store.loads["player.700002.settings.quickLoot"] == 1);
}

TEST_CASE(real_database_distinguishes_missing_rows_and_persists_only_explicit_values)
{
	// Opt-in only. A connection-local temporary table shadows any real kv_store.
	const char* name = std::getenv("TFS_TEST_DB_NAME");
	if (!name || !*name) {
		std::cout << "Database integration skipped (TFS_TEST_DB_NAME not set)\n";
		return;
	}
	const char* host = std::getenv("TFS_TEST_DB_HOST");
	const char* user = std::getenv("TFS_TEST_DB_USER");
	const char* password = std::getenv("TFS_TEST_DB_PASS");
	const char* socket = std::getenv("TFS_TEST_DB_SOCKET");
	ConfigManager::setString(ConfigManager::MYSQL_HOST, host ? host : "localhost");
	ConfigManager::setString(ConfigManager::MYSQL_USER, user ? user : "root");
	ConfigManager::setString(ConfigManager::MYSQL_PASS, password ? password : "");
	ConfigManager::setString(ConfigManager::MYSQL_DB, name);
	ConfigManager::setString(ConfigManager::MYSQL_SOCK, socket ? socket : "");
	ConfigManager::setInteger(ConfigManager::SQL_PORT, 3306);
	auto& db = Database::getInstance();
	CHECK(db.connect());
	CHECK(db.executeQuery(
	    "CREATE TEMPORARY TABLE kv_store (key_name VARCHAR(255) PRIMARY KEY, timestamp BIGINT UNSIGNED NOT NULL, value LONGBLOB NOT NULL)"));
	bool succeeded = false;
	CHECK(!db.storeQuery("SELECT * FROM kv_store", &succeeded));
	CHECK(succeeded);
	CHECK(!db.storeQuery("SELECT nonexistent_column FROM kv_store", &succeeded));
	CHECK(!succeeded);
	g_performanceMetrics.setEnabled(true);
	const auto calls = PerformanceMetricsTestAccess::databaseCalls();
	CHECK(db.storeQuery("SELECT 1"));
	CHECK(PerformanceMetricsTestAccess::databaseCalls() == calls);
	g_reactor.start();
	bool callbackSucceeded = false;
	CHECK(g_reactor.send([&] {
		callbackSucceeded = db.storeQuery("SELECT 1") != nullptr;
		g_reactor.shutdown();
	}));
	g_reactor.runLoop();
	CHECK(callbackSucceeded);
	CHECK(PerformanceMetricsTestAccess::databaseCalls() == calls + 1);
	g_performanceMetrics.setEnabled(false);
	g_reactor.start();
	callbackSucceeded = false;
	CHECK(g_reactor.send([&] {
		callbackSucceeded = db.storeQuery("SELECT 1") != nullptr;
		g_reactor.shutdown();
	}));
	g_reactor.runLoop();
	CHECK(callbackSucceeded);
	CHECK(PerformanceMetricsTestAccess::databaseCalls() == calls + 1);
	g_reactor.shutdown();
	KVStore store;
	CHECK(!store.get("missing"));
	CHECK(db.executeQuery("INSERT INTO kv_store VALUES ('missing', 0, '')"));
	store.flush();
	CHECK(db.storeQuery("SELECT * FROM kv_store WHERE key_name = 'missing'"));
	store.set("option", ValueWrapper(false));
	CHECK(store.saveAll());
	store.set("option", ValueWrapper(true));
	CHECK(!store.get("option", true)->get<BooleanType>());
	CHECK(store.get("option")->get<BooleanType>());
	store.flush();
	CHECK(store.get("option")->get<BooleanType>());
	CHECK(!store.get("prefix.external"));
	CHECK(store.save("prefix.external", ValueWrapper(true)));
	CHECK(store.keys("prefix.").contains("external"));
	CHECK(store.get("prefix.external")->get<BooleanType>());
	store.remove("prefix.external");
	CHECK(!store.keys("prefix.").contains("external"));
	store.remove("option");
	store.flush();
	CHECK(!store.load("option"));
	CHECK(db.executeQuery("DROP TEMPORARY TABLE kv_store"));
}

TFS_TEST_MAIN()
