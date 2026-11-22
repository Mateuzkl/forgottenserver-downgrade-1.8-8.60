// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"
#include "../account.h"
#include "../game.h"
#include "../iologindata.h"
#include "../player.h"
#include "disposable_database.h"

extern Game g_game;

struct AccountManagerSecurityTestAccess
{
	static std::string reply(Player& player, const std::string& text)
	{
		std::ostringstream message;
		bool showHelp = false;
		if (player.getAccountManagerMode() == ACCOUNT_MANAGER_NEW) {
			player.handleNewAccountManager(text, message, showHelp);
		} else {
			player.handleAccountManager(text, message, showHelp);
		}
		return message.str();
	}
};

namespace {
std::string field(std::string_view query)
{
	const auto row = Database::getInstance().storeQuery(query);
	CHECK(row);
	return std::string{row->getString("value")};
}

std::string displayedCredential(Player& player, const std::string& input, std::string_view marker)
{
	const auto reply = AccountManagerSecurityTestAccess::reply(player, input);
	const auto offset = reply.find(marker);
	CHECK(offset != std::string::npos);
	const auto begin = offset + marker.size();
	const auto end = reply.find(". ", begin);
	CHECK(end != std::string::npos && end > begin);
	return reply.substr(begin, end - begin);
}
} // namespace

TEST_CASE(account_manager_mariadb_dialogues_and_ownership)
{
	connectDisposableDatabase();
	DBTransaction fixture;
	CHECK(fixture.begin());
	auto& db = Database::getInstance();
	using Auth = IOLoginData::AuthenticationResult;
	ConfigManager::setBoolean(ConfigManager::ACCOUNT_MANAGER, true);
	ConfigManager::setBoolean(ConfigManager::GENERATE_ACCOUNT_NUMBER, false);
	ConfigManager::setBoolean(ConfigManager::START_CHOOSEVOC, false);
	ConfigManager::setInteger(ConfigManager::NEW_PLAYER_LEVEL, 1);
	ConfigManager::setInteger(ConfigManager::NEW_PLAYER_TOWN_ID, 1);
	Account account;
	CHECK(IOLoginData::loginserverAuthentication("1", "1", account) == Auth::Success);
	CHECK(IOLoginData::gameworldAuthentication("1", "1", "Account Manager").characterId == 1);
	Player owner(nullptr);
	owner.setAccountManagerMode(ACCOUNT_MANAGER_NEW);
	for (const auto* line : {"account", "fixture-password", "yes", "FixtureOwner", "owner@example.test", "yes"}) {
		owner.manageAccount(line);
	}
	CHECK(owner.getAccountManagerMode() == ACCOUNT_MANAGER_ACCOUNT);
	const uint32_t ownerId = owner.getAccount();
	CHECK(ownerId > 1);
	CHECK(IOLoginData::loginserverAuthentication("OWNER@example.test", "fixture-password", account) == Auth::Success);
	CHECK(account.id == ownerId);
	auto managed = IOLoginData::gameworldAuthentication("FixtureOwner", "fixture-password", "Account Manager");
	CHECK(managed.accountId == ownerId && managed.characterId == 1);
	for (const auto* line : {"character", "Fixture Hero", "yes", "male", "yes"}) {
		owner.manageAccount(line);
	}
	const auto hero = IOLoginData::getGuidByName("Fixture Hero");
	CHECK(hero > 1 && IOLoginData::getAccountIdByPlayerId(hero) == ownerId);
	auto normal = IOLoginData::gameworldAuthentication("FixtureOwner", "fixture-password", "Fixture Hero");
	CHECK(normal.characterId == hero && normal.accountId == ownerId);
	uint32_t other = 0;
	CHECK(IOLoginData::createAccount("FixtureOther", "other-password", other));
	CHECK(other != ownerId && other > 1);
	uint32_t duplicate = 0;
	CHECK(!IOLoginData::createAccount("FixtureDuplicate", "other-password", duplicate, "OWNER@example.test"));
	CHECK(duplicate == 0);
	CHECK(!IOLoginData::setAccountEmail(other, "owner@example.test"));
	CHECK(field("SELECT GET_LOCK('otc_account_registration',0) AS value") == "1");
	auto busyRegistration = std::async(std::launch::async, [] {
		uint32_t id = 0;
		return IOLoginData::createAccount("FixtureBusy", "fixture-password", id);
	});
	const bool createdWhileBusy = busyRegistration.get();
	CHECK(field("SELECT RELEASE_LOCK('otc_account_registration') AS value") == "1");
	CHECK(!createdWhileBusy);
	CHECK(field("SELECT COUNT(*) AS value FROM accounts WHERE name='FixtureBusy'") == "0");
	CHECK(IOLoginData::createPlayer(other, "Other Hero", 1, PLAYERSEX_FEMALE));
	const auto otherHero = IOLoginData::getGuidByName("Other Hero");
	CHECK(otherHero > 1);
	for (const auto* line : {"delete", "Other Hero", "yes"}) { owner.manageAccount(line); }
	CHECK(!IOLoginData::deletePlayer(otherHero, ownerId));
	CHECK(field(fmt::format("SELECT deletion AS value FROM players WHERE id={}", otherHero)) == "0");
	CHECK(g_game.reserveLogin(hero));
	CHECK(!IOLoginData::deletePlayer(hero, ownerId));
	g_game.releaseLogin(hero);
	CHECK(db.executeQuery(fmt::format("INSERT INTO character_auctions (player_id,player_name,seller_account_id,"
	    "created_at,end_at,status) VALUES ({},'Fixture Hero',{},1,2,1)", hero, ownerId)));
	CHECK(!IOLoginData::deletePlayer(hero, ownerId));
	CHECK(field(fmt::format("SELECT deletion AS value FROM players WHERE id={}", hero)) == "0");
	CHECK(db.executeQuery(fmt::format("DELETE FROM character_auctions WHERE player_id={}", hero)));
	for (const auto* line : {"delete", "Fixture Hero", "yes"}) { owner.manageAccount(line); }
	CHECK(std::stoll(field(fmt::format("SELECT deletion AS value FROM players WHERE id={}", hero))) > time(nullptr));
	CHECK(!IOLoginData::deletePlayer(hero, ownerId));
	for (const auto* line : {"password", "updated-password", "yes"}) { owner.manageAccount(line); }
	CHECK(IOLoginData::loginserverAuthentication("FixtureOwner", "updated-password", account) == Auth::Success);
	CHECK(!IOLoginData::setPassword(1, "test-password"));
	CHECK(!IOLoginData::setPassword(UINT32_MAX, "test-password"));
	CHECK(!IOLoginData::setRecoveryKey(1, "fixture-key"));
	CHECK(db.executeQuery(fmt::format("UPDATE accounts SET secret='ABCDEFGHIJKLMNOP' WHERE id={}", ownerId)));
	owner.manageAccount("recovery key");
	auto key = displayedCredential(owner, "yes", "Your recovery key is: ");
	CHECK(key.size() == 19);
	CHECK(!IOLoginData::setRecoveryKey(ownerId, "replacement"));
	CHECK(field(fmt::format("SELECT secret AS value FROM accounts WHERE id={}", ownerId)) == "ABCDEFGHIJKLMNOP");
	Player recovery(nullptr);
	recovery.setAccountManagerMode(ACCOUNT_MANAGER_NEW);
	for (const auto* line : {"recover", "owner@example.test", "typo", "cancel", "recover", "FixtureOwner"}) {
		recovery.manageAccount(line);
	}
	auto password = displayedCredential(recovery, key, "Your new password is: ");
	CHECK(IOLoginData::loginserverAuthentication("FixtureOwner", password, account) == Auth::Success);
	CHECK(IOLoginData::loginserverAuthentication("FixtureOwner", "updated-password", account) == Auth::Rejected);
	ConfigManager::setBoolean(ConfigManager::GENERATE_ACCOUNT_NUMBER, true);
	Player generated(nullptr);
	generated.setAccountManagerMode(ACCOUNT_MANAGER_NEW);
	for (const auto* line : {"account", "generated-password", "yes", "cancel", "generated@example.test"}) {
		generated.manageAccount(line);
	}
	CHECK(generated.getAccountManagerMode() == ACCOUNT_MANAGER_ACCOUNT);
	CHECK(IOLoginData::loginserverAuthentication("generated@example.test", "generated-password", account) == Auth::Success);
	CHECK(account.id == generated.getAccount() && account.id != ownerId);
	ConfigManager::setBoolean(ConfigManager::ACCOUNT_MANAGER, false);
	CHECK(IOLoginData::gameworldAuthentication("1", "1", "Account Manager").status == Auth::Rejected);
	CHECK(IOLoginData::loginserverAuthentication("1", "1", account) == Auth::Rejected);
	CHECK(IOLoginData::gameworldAuthentication("FixtureOwner", password, "Account Manager").characterId == 0);
	CHECK(IOLoginData::gameworldAuthentication("FixtureOther", "other-password", "Other Hero").characterId == otherHero);
	CHECK(fixture.rollback());
}

TEST_CASE(account_manager_namelock_commits_both_changes)
{
	// Connection-local InnoDB tables exercise commit and rollback independently of the other fixtures.
	auto& db = Database::getInstance();
	CHECK(db.executeQuery("CREATE TEMPORARY TABLE players (id INT PRIMARY KEY, name VARCHAR(255) UNIQUE, "
	                      "account_id INT NOT NULL, deletion BIGINT NOT NULL DEFAULT 0) ENGINE=InnoDB"));
	CHECK(db.executeQuery("CREATE TEMPORARY TABLE player_namelocks (player_id INT PRIMARY KEY) ENGINE=InnoDB"));
	CHECK(db.executeQuery("INSERT INTO players (id,name,account_id) VALUES (100,'Old Hero',25)"));
	CHECK(db.executeQuery("INSERT INTO player_namelocks VALUES (100)"));
	CHECK(!IOLoginData::renameNamelockedPlayer(100, 26, "New Hero"));
	CHECK(field("SELECT name AS value FROM players WHERE id=100") == "Old Hero");
	CHECK(IOLoginData::renameNamelockedPlayer(100, 25, "New Hero"));
	CHECK(!IOLoginData::renameNamelockedPlayer(100, 25, "Unrequested Hero"));
	CHECK(field("SELECT name AS value FROM players WHERE id=100") == "New Hero");
	CHECK(field("SELECT COUNT(*) AS value FROM player_namelocks") == "0");
	CHECK(db.executeQuery("DROP TEMPORARY TABLE player_namelocks, players"));
}

int main()
{
	if (!std::getenv("TFS_TEST_DB_NAME")) {
		std::cout << "[SKIP] Set TFS_TEST_DB_NAME to a disposable schema loaded from schema.sql\n";
		return 77;
	}
	return tfs::tests::run();
}
