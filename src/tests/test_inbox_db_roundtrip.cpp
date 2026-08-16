#include "../otpch.h"

#include "../configmanager.h"
#include "../database.h"
#include "../databasemanager.h"
#include "../game.h"
#include "../inbox.h"
#include "../iologindata.h"
#include "../item.h"
#include "../player.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

#include "test_support.h"

// Drives the real save path into a throwaway MySQL/MariaDB schema and reads the rows back, so the
// "no truncation" claim is backed by an actual database round trip rather than only by the
// in-memory collection test.
//
// Skipped unless TFS_TEST_DB_* is set, so it never touches a real server database:
//
//   TFS_TEST_DB_HOST=127.0.0.1 TFS_TEST_DB_USER=root TFS_TEST_DB_PASS=secret
//   TFS_TEST_DB_NAME=tfs_inbox_test ./test_inbox_db_roundtrip
//
// The schema only needs player_inboxitems; see the test plan in the pull request.

namespace {

const char* envOrNull(const char* name)
{
	const char* value = std::getenv(name);
	return (value && *value) ? value : nullptr;
}

bool configureTestDatabase()
{
	const char* host = envOrNull("TFS_TEST_DB_HOST");
	const char* user = envOrNull("TFS_TEST_DB_USER");
	const char* name = envOrNull("TFS_TEST_DB_NAME");
	if (!host || !user || !name) {
		return false;
	}

	const char* pass = std::getenv("TFS_TEST_DB_PASS");
	const char* port = envOrNull("TFS_TEST_DB_PORT");

	ConfigManager::setString(ConfigManager::MYSQL_HOST, host);
	ConfigManager::setString(ConfigManager::MYSQL_USER, user);
	ConfigManager::setString(ConfigManager::MYSQL_PASS, pass ? pass : "");
	ConfigManager::setString(ConfigManager::MYSQL_DB, name);
	ConfigManager::setString(ConfigManager::MYSQL_SOCK, "");
	ConfigManager::setInteger(ConfigManager::SQL_PORT, port ? std::atoi(port) : 3306);
	return true;
}

void ensureItemTypesLoaded()
{
	if (Item::items.size() != 0) {
		return;
	}

	const auto itemsPath = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
	                       "data/items/items.otb";
	CHECK(Item::items.loadFromOtb(itemsPath.string()));
}

void giveGroup(Player& player)
{
	auto group = std::make_shared<Group>();
	group->name = "test";
	group->flags = 0;
	group->maxDepotItems = 0;
	group->maxVipEntries = 0;
	group->id = 1;
	group->access = false;
	player.setGroup(group);
}

Inbox* attachInbox(Player& player, uint32_t depotId)
{
	CHECK(player.getDepotLocker(depotId) != nullptr);
	Inbox* inbox = player.getInbox(depotId);
	CHECK(inbox != nullptr);
	return inbox;
}

void fillInbox(Inbox* inbox, size_t count)
{
	for (size_t i = 0; i < count; ++i) {
		auto item = Item::CreateItem(ITEM_GOLD_COIN, 1);
		CHECK(item != nullptr);
		inbox->internalAddThing(item.get());
	}
}

uint64_t countRows(Database& db, uint32_t guid)
{
	DBResult_ptr result = db.storeQuery(
	    fmt::format("SELECT COUNT(*) AS `total` FROM `player_inboxitems` WHERE `player_id` = {:d}", guid));
	if (!result) {
		return 0;
	}
	return result->getNumber<uint64_t>("total");
}

// Runs the production save path for one player and returns how many rows reached the table.
uint64_t saveAndCount(Database& db, Player& player, uint32_t guid, int64_t& elapsedMicros)
{
	CHECK(db.executeQuery(fmt::format("DELETE FROM `player_inboxitems` WHERE `player_id` = {:d}", guid)));

	ItemBlockList itemList;
	IOLoginData::collectInboxItems(&player, itemList);

	DBInsert query(
	    "INSERT INTO `player_inboxitems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");
	PropWriteStream propWriteStream;

	const auto started = std::chrono::steady_clock::now();
	CHECK(IOLoginData::saveItems(&player, itemList, query, propWriteStream));
	elapsedMicros =
	    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();

	return countRows(db, guid);
}

} // namespace

TEST_CASE(inbox_rows_survive_a_real_database_round_trip)
{
	if (!configureTestDatabase()) {
		std::cout << "[skip] TFS_TEST_DB_* not set; database round trip not exercised\n";
		return;
	}

	ensureItemTypesLoaded();

	Database& db = Database::getInstance();
	if (!db.connect()) {
		std::cout << "[skip] could not connect to the test database; round trip not exercised\n";
		return;
	}

	std::cout << "[db] items | rows after save | rows after re-save | save ms\n";

	for (const size_t count : {size_t{0}, size_t{1}, size_t{99}, size_t{100}, size_t{101}, size_t{255},
	                           size_t{256}, size_t{500}, size_t{1000}, size_t{2500}, size_t{5000}}) {
		Player player(nullptr);
		giveGroup(player);
		fillInbox(attachInbox(player, 1), count);

		int64_t firstMicros = 0;
		const uint64_t firstPass = saveAndCount(db, player, player.getGUID(), firstMicros);

		// Saving twice must not drift: the delete-then-insert has to land on the same total.
		int64_t secondMicros = 0;
		const uint64_t secondPass = saveAndCount(db, player, player.getGUID(), secondMicros);

		std::cout << "[db] " << count << " | " << firstPass << " | " << secondPass << " | "
		          << (firstMicros / 1000.0) << "\n";

		CHECK(firstPass == count);
		CHECK(secondPass == count);
	}
}

TEST_CASE(inbox_rows_keep_their_town_pid_in_the_database)
{
	if (!configureTestDatabase()) {
		return;
	}

	ensureItemTypesLoaded();

	Database& db = Database::getInstance();
	if (!db.connect()) {
		return;
	}

	Player player(nullptr);
	giveGroup(player);
	fillInbox(attachInbox(player, 1), 150);
	fillInbox(attachInbox(player, 2), 200);
	fillInbox(attachInbox(player, 3), 500);

	int64_t micros = 0;
	const uint32_t guid = player.getGUID();
	CHECK(saveAndCount(db, player, guid, micros) == 850);

	// pid carries the depot id for top level inbox items, which is how the loader routes them
	// back to the right town.
	for (const auto& [depotId, expected] : {std::pair<int, int>{1, 150}, {2, 200}, {3, 500}}) {
		DBResult_ptr result = db.storeQuery(fmt::format(
		    "SELECT COUNT(*) AS `total` FROM `player_inboxitems` WHERE `player_id` = {:d} AND `pid` = {:d}", guid,
		    depotId));
		CHECK(result != nullptr);
		CHECK(result->getNumber<uint64_t>("total") == static_cast<uint64_t>(expected));
	}
}

TEST_CASE(nested_inbox_containers_are_expanded_into_rows)
{
	if (!configureTestDatabase()) {
		return;
	}

	ensureItemTypesLoaded();

	Database& db = Database::getInstance();
	if (!db.connect()) {
		return;
	}

	Player player(nullptr);
	giveGroup(player);
	Inbox* inbox = attachInbox(player, 1);

	// backpack -> bag -> 5 coins, plus 3 loose items: 10 rows in total.
	auto backpack = Item::CreateItem(ITEM_BAG);
	CHECK(backpack != nullptr);
	Container* outer = backpack->getContainer();
	CHECK(outer != nullptr);

	auto innerBag = Item::CreateItem(ITEM_BAG);
	CHECK(innerBag != nullptr);
	Container* inner = innerBag->getContainer();
	CHECK(inner != nullptr);

	for (int i = 0; i < 5; ++i) {
		auto coin = Item::CreateItem(ITEM_GOLD_COIN, 1);
		CHECK(coin != nullptr);
		inner->internalAddThing(coin.get());
	}
	outer->internalAddThing(innerBag.get());
	inbox->internalAddThing(backpack.get());
	fillInbox(inbox, 3);

	int64_t micros = 0;
	const uint32_t guid = player.getGUID();
	CHECK(saveAndCount(db, player, guid, micros) == 10);

	// The nested rows point at their parent's sid, which is always >= 100 so it can never be
	// mistaken for a town id by the loader.
	DBResult_ptr result = db.storeQuery(fmt::format(
	    "SELECT COUNT(*) AS `total` FROM `player_inboxitems` WHERE `player_id` = {:d} AND `pid` >= 100", guid));
	CHECK(result != nullptr);
	CHECK(result->getNumber<uint64_t>("total") == 6); // the inner bag + its 5 coins
}

TFS_TEST_MAIN()
