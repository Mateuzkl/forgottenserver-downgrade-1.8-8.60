#include "../otpch.h"

#include "../configmanager.h"
#include "../database.h"
#include "../databasemanager.h"
#include "../game.h"
#include "../inbox.h"
#include "../iologindata.h"
#include "../item.h"
#include "../market.h"
#include "../player.h"
#include "../save_manager.h"
#include "../tasks.h"
#include "../thread_pool.h"
#include "../vocation.h"
#include "../logger.h"
#include "test_support.h"

extern Game g_game;
extern Vocations g_vocations;

struct TaskReactorTestAccess
{
	static void runOnceAsOwner(TaskReactor& reactor)
	{
		const auto previous = TaskReactor::currentReactor;
		TaskReactor::currentReactor = &reactor;
		reactor.runOnce();
		TaskReactor::currentReactor = previous;
	}
};

namespace {
constexpr uint32_t SELLER = 900001;
constexpr uint32_t BUYER = 900002;

uint64_t number(std::string_view query)
{
	auto row = Database::getInstance().storeQuery(query);
	CHECK(row != nullptr);
	return row->getNumber<uint64_t>("value");
}

void sql(std::string_view query) { CHECK(Database::getInstance().executeQuery(query)); }

void dispatch(const std::function<void()>& callback)
{
	std::exception_ptr failure;
	bool ran = false;
	CHECK(g_dispatcher.addTask([&] {
		ran = true;
		try { callback(); } catch (...) { failure = std::current_exception(); }
	}));
	TaskReactorTestAccess::runOnceAsOwner(g_reactor);
	CHECK(ran);
	if (failure) { std::rethrow_exception(failure); }
}

void initialize(Player& player, uint32_t guid, uint32_t town)
{
	player.setGUID(guid);
	player.setName(guid == SELLER ? "Market Seller Test" : "Market Buyer Test");
	player.setGroup(g_game.groups.getSharedGroup(1));
	player.setTown(g_game.map.towns.getSharedTown(town));
	CHECK(player.setVocation(0));
	player.setBankBalance(10000);
}

uint32_t offer(uint32_t owner, uint8_t sale, uint32_t amount = 5, const std::string& attributes = {}, uint8_t tier = 0)
{
	auto& db = Database::getInstance();
	sql(fmt::format("INSERT INTO `market_offers` (`player_id`,`sale`,`itemtype`,`amount`,`created`,`price`,`tier`,`attributes`) "
	    "VALUES ({},{},{},{},1,100,{}, {})", owner, sale, static_cast<uint16_t>(ITEM_BAG), amount, tier, db.escapeString(attributes)));
	return static_cast<uint32_t>(db.getLastInsertId());
}

uint64_t pending(uint32_t guid)
{
	return number(fmt::format("SELECT COUNT(*) AS `value` FROM `market_delivery_pending` WHERE `player_id`={} AND `state`='pending'", guid));
}

uint64_t inboxCount(uint32_t guid, uint32_t depot)
{
	return number(fmt::format("SELECT COUNT(*) AS `value` FROM `player_inboxitems` WHERE `player_id`={} AND `pid`={}", guid, depot));
}

struct Runtime
{
	~Runtime()
	{
		g_threadPool.shutdown();
		g_dispatcher.stop();
		g_reactor.drain();
		g_dispatcher.shutdown();
	}
};
} // namespace

// Requires a disposable schema loaded from schema.sql. Never silently succeeds on
// a connection/migration error once the explicit test environment is configured.
TEST_CASE(market_atomic_delivery_database_regressions)
{
	const char* name = std::getenv("TFS_TEST_DB_NAME");
	if (!name) {
		std::cout << "[skip] TFS_TEST_DB_NAME not set; Market database faults not exercised\n";
		return;
	}
	CHECK(std::string_view{name}.starts_with("tfs_market_test_"));
	const char* host = std::getenv("TFS_TEST_DB_HOST");
	const char* user = std::getenv("TFS_TEST_DB_USER");
	const char* pass = std::getenv("TFS_TEST_DB_PASS");
	const char* port = std::getenv("TFS_TEST_DB_PORT");
	ConfigManager::setString(ConfigManager::MYSQL_HOST, host ? host : "127.0.0.1");
	ConfigManager::setString(ConfigManager::MYSQL_USER, user ? user : "root");
	ConfigManager::setString(ConfigManager::MYSQL_PASS, pass ? pass : "");
	ConfigManager::setString(ConfigManager::MYSQL_DB, name);
	ConfigManager::setString(ConfigManager::MYSQL_SOCK, "");
	ConfigManager::setInteger(ConfigManager::SQL_PORT, port ? std::atoi(port) : 3306);
	ConfigManager::setBoolean(ConfigManager::FORGE_SYSTEM_ENABLED, true);
	CHECK(Database::getInstance().connect());
	CHECK(initLogger());
	const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	std::filesystem::current_path(root);
	DatabaseManager::updateDatabase();
	CHECK(number("SELECT `value` FROM `server_config` WHERE `config`='db_version'") == 69);
	CHECK(Item::items.loadFromOtb("data/items/items.otb"));
	// Reproduce the silent Container::addThing refusal: Market must still insert
	// and persist into an unlimited inbox whose display capacity is zero.
	Item::items.getItemType(ITEM_INBOX).maxItems = 0;
	CHECK(g_game.groups.load());
	CHECK(g_vocations.loadFromXml());
	for (uint32_t town : {1, 2}) {
		CHECK(g_game.map.towns.addTown(town, std::make_shared<Town>(town)));
	}
	sql("DROP TRIGGER IF EXISTS market_test_fail_receipt");
	sql("DROP TRIGGER IF EXISTS market_test_fail_inbox");
	sql("DROP TRIGGER IF EXISTS market_test_fail_history");
	sql("DELETE FROM `players` WHERE `id` IN (900001,900002)");
	sql("INSERT INTO `players` (`id`,`name`,`account_id`,`town_id`,`balance`) VALUES "
	    "(900001,'Market Seller Test',1,1,10000),(900002,'Market Buyer Test',1,2,10000)");
	g_dispatcher.start();
	g_threadPool.start(1);
	Runtime runtime;
	Player seller(nullptr), buyer(nullptr);
	initialize(seller, SELLER, 1);
	initialize(buyer, BUYER, 2);

	dispatch([&] {
		// Each current town receives its own return; another town inbox stays intact.
		for (Player* player : {&seller, &buyer}) {
			auto mail = Item::CreateItem(ITEM_GOLD_COIN, 7);
			const uint32_t depot = player->getTown()->getID();
			const uint32_t otherDepot = depot == 1 ? 2 : 1;
			player->getInbox(otherDepot)->internalAddThing(mail.get());
			const uint32_t id = offer(player->getGUID(), 1);
			CHECK(Market::settleOffer(player, id, 5, 5, 1, 30).value_or(false));
			CHECK(player->getInbox(0)->empty());
			CHECK(pending(player->getGUID()) == 1);
			CHECK(Market::processDeliveries(player));
			CHECK(player->getInbox(depot)->size() == 5);
			CHECK(player->getInbox(otherDepot)->size() == 1);
			CHECK(inboxCount(player->getGUID(), depot) == 5);
			CHECK(inboxCount(player->getGUID(), otherDepot) == 1);
			CHECK(Market::getDeliveryStatus(player->getGUID(), id)->depotId == depot);
			CHECK(Market::getDeliveryStatus(player->getGUID(), id)->applied);
			CHECK(Market::processDeliveries(player));
			CHECK(Market::settleOffer(player, id, 5, 5, 1, 30).value_or(false));
			CHECK(pending(player->getGUID()) == 0);
			CHECK(player->getInbox(depot)->size() == 5);
		}

		// Expiration records the right while the recipient is offline. An old save
		// may still finish before materialization without deleting that right.
		const auto expired = offer(SELLER, 1, 2);
		CHECK(Market::settleOffer(nullptr, expired, 2, 2, 2, 30).value_or(false));
		CHECK(g_saveManager.savePlayer(&seller));
		CHECK(g_saveManager.hasPendingPlayerSave(SELLER));
		CHECK(!Market::processDeliveries(&seller));
		CHECK(pending(SELLER) == 1);
	});
	// Completion callback is delivered on the next dispatcher cycle, as at login.
	for (size_t tries = 0; tries < 1000 && g_saveManager.hasPendingPlayerSave(SELLER); ++tries) {
		TaskReactorTestAccess::runOnceAsOwner(g_reactor);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK(!g_saveManager.hasPendingPlayerSave(SELLER));
	dispatch([&] {
		CHECK(Market::processDeliveries(&seller));
		CHECK(inboxCount(SELLER, 1) == 7);
		CHECK(pending(SELLER) == 0);

		// Failed history must roll back claim AND outbox.
		sql("CREATE TRIGGER market_test_fail_history BEFORE INSERT ON market_history FOR EACH ROW "
		    "SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected history failure'");
		const auto cancelled = offer(SELLER, 1, 1);
		CHECK(Market::settleOffer(&seller, cancelled, 1, 1, 1, 30) == std::optional<bool>{false});
		CHECK(Market::getOffer(cancelled).has_value());
		CHECK(pending(SELLER) == 0);
		sql("DROP TRIGGER market_test_fail_history");
		CHECK(Market::settleOffer(&seller, cancelled, 1, 1, 1, 30).value_or(false));

		// Fail the player SQL after receipt was changed: transaction and RAM both
		// revert. Pre-existing items survive, then a retry applies exactly once.
		sql("CREATE TRIGGER market_test_fail_inbox BEFORE INSERT ON player_inboxitems FOR EACH ROW "
		    "SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected inbox failure'");
		CHECK(!Market::processDeliveries(&seller));
		CHECK(seller.getInbox(1)->size() == 7);
		CHECK(inboxCount(SELLER, 1) == 7);
		CHECK(pending(SELLER) == 1);
		sql("DROP TRIGGER market_test_fail_inbox");
		CHECK(Market::processDeliveries(&seller));
		CHECK(inboxCount(SELLER, 1) == 8);

		// A missing receipt confirmation cannot commit any new item.
		const auto receipt = offer(SELLER, 1, 1);
		CHECK(Market::settleOffer(&seller, receipt, 1, 1, 1, 30).value_or(false));
		sql("CREATE TRIGGER market_test_fail_receipt BEFORE UPDATE ON market_delivery_pending FOR EACH ROW "
		    "SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected receipt failure'");
		CHECK(!Market::processDeliveries(&seller));
		CHECK(seller.getInbox(1)->size() == 8);
		CHECK(pending(SELLER) == 1);
		sql("DROP TRIGGER market_test_fail_receipt");
		CHECK(Market::processDeliveries(&seller));

		// Buy cancellation returns only escrowed money, once, without inventing items.
		const auto buy = offer(BUYER, 0, 3);
		const auto balance = buyer.getBankBalance();
		CHECK(Market::settleOffer(&buyer, buy, 3, 3, 1, 30).value_or(false));
		CHECK(Market::processDeliveries(&buyer));
		CHECK(buyer.getBankBalance() == balance + 300);
		CHECK(Market::processDeliveries(&buyer));
		CHECK(buyer.getBankBalance() == balance + 300);
		CHECK(inboxCount(BUYER, 2) == 5);

		// Distinct partial accepts have distinct stable keys. Stale accepts do not
		// create new rights. Buyer debit and offline seller credit remain consistent.
		const auto sale = offer(SELLER, 1, 5);
		buyer.setBankBalance(buyer.getBankBalance() - 200);
		CHECK(Market::settleOffer(&buyer, sale, 5, 2, 3, 30).value_or(false));
		CHECK(Market::settleOffer(&buyer, sale, 5, 2, 3, 30) == std::optional<bool>{false});
		buyer.setBankBalance(buyer.getBankBalance() - 300);
		CHECK(Market::settleOffer(&buyer, sale, 3, 3, 3, 30).value_or(false));
		CHECK(pending(SELLER) == 2);
		CHECK(pending(BUYER) == 2);
		const auto sellerBalance = seller.getBankBalance();
		CHECK(Market::processDeliveries(&buyer));
		CHECK(Market::processDeliveries(&seller));
		CHECK(seller.getBankBalance() == sellerBalance + 500);
		CHECK(inboxCount(BUYER, 2) == 10);

		// Accept a buy offer: source inventory save and buyer entitlement share the
		// claim transaction. Unique instance bytes and tier survive materialization.
		auto unique = Item::CreateItem(ITEM_BAG);
		unique->setTier(4);
		unique->setStrAttr(ITEM_ATTRIBUTE_TEXT, "Market unique instance");
		PropWriteStream stream;
		unique->serializeAttr(stream);
		const std::string attributes{stream.getStream()};
		const auto order = offer(BUYER, 0, 1, {}, 4);
		seller.setBankBalance(seller.getBankBalance() + 100);
		CHECK(Market::settleOffer(&seller, order, 1, 1, 3, 30, 4, attributes).value_or(false));
		CHECK(Market::processDeliveries(&buyer));
		CHECK(buyer.getInbox(2)->getItemByIndex(0)->getTier() == 4);
		CHECK(inboxCount(BUYER, 2) == 11);

		// Durable but un-recovered WAL blocks mutations even when no in-memory job
		// remains. Recovery replays the old snapshot before applying the new right.
		const auto deferred = offer(SELLER, 1, 1);
		CHECK(Market::settleOffer(&seller, deferred, 1, 1, 1, 30).value_or(false));
		auto snapshot = IOLoginData::buildPlayerSave(&seller);
		CHECK(snapshot.has_value());
		for (size_t i = 0; i < snapshot->queries.size(); ++i) {
			sql(fmt::format("INSERT INTO player_save_async_pending (guid,query_index,query_text,created_at) VALUES ({},{},{},1)",
			    SELLER, i, Database::getInstance().escapeString(snapshot->queries[i])));
		}
		CHECK(!Market::isPlayerReady(&seller));
		CHECK(!Market::processDeliveries(&seller));
		g_saveManager.recoverPendingFlushes();
		CHECK(Market::processDeliveries(&seller));
		CHECK(pending(SELLER) == 0);
		CHECK(inboxCount(SELLER, 1) == 10);

		// A queued return keeps the destination captured at cancellation, even
		// if the character moves back to its home town before materialization.
		seller.setLastDepotId(2);
		const auto travelling = offer(SELLER, 1, 1);
		CHECK(Market::settleOffer(&seller, travelling, 1, 1, 1, 30).value_or(false));
		CHECK(Market::getDeliveryStatus(SELLER, travelling)->depotId == 2);
		CHECK(!Market::getDeliveryStatus(SELLER, travelling)->applied);
		seller.setLastDepotId(1);
		CHECK(Market::processDeliveries(&seller));
		CHECK(Market::getDeliveryStatus(SELLER, travelling)->depotId == 2);
		CHECK(Market::getDeliveryStatus(SELLER, travelling)->applied);
		CHECK(inboxCount(SELLER, 1) == 10);
		CHECK(inboxCount(SELLER, 2) == 2); // existing mail plus returned bag

		// Legacy pending rights at Inbox 0 stay addressable and are not lost just
		// because new deliveries follow the character's town.
		sql(fmt::format("INSERT INTO market_delivery_pending (event_key,offer_id,player_id,depot_id,itemtype,amount,attributes,created_at) "
		    "VALUES ('legacy-pending',999999,{},0,{},1,'',1)", SELLER, static_cast<uint16_t>(ITEM_BAG)));
		CHECK(Market::processDeliveries(&seller));
		CHECK(inboxCount(SELLER, 0) == 1);
		CHECK(Market::getDeliveryStatus(SELLER, 999999)->depotId == 0);
		CHECK(Market::getDeliveryStatus(SELLER, 999999)->applied);

		// The exact reported flow: reserve a real item, create the sale, then
		// cancel in town 2 while the character's home town is still 1.
		auto helmet = Item::CreateItem(3388);
		CHECK(helmet != nullptr);
		helmet->setStrAttr(ITEM_ATTRIBUTE_TEXT, "Reserved helmet for cancellation");
		PropWriteStream helmetStream;
		helmet->serializeAttr(helmetStream);
		const std::string helmetAttributes{helmetStream.getStream()};
		static_cast<Cylinder&>(seller).internalAddThing(CONST_SLOT_HEAD, helmet.get());
		CHECK(g_saveManager.savePlayersSync({&seller}));
		CHECK(number(fmt::format("SELECT COUNT(*) AS value FROM player_items WHERE player_id={} AND itemtype=3388", SELLER)) == 1);
		static_cast<Cylinder&>(seller).removeThing(helmet.get(), 1);
		seller.setBankBalance(seller.getBankBalance() - 20);
		MarketOfferRecord created;
		created.playerId = SELLER;
		created.sale = 1;
		created.itemId = 3388;
		created.amount = 1;
		created.price = 100;
		created.created = static_cast<uint32_t>(time(nullptr));
		created.attributes = helmetAttributes;
		CHECK(Market::createOfferAndSave(&seller, created).value_or(false));
		CHECK(number(fmt::format("SELECT COUNT(*) AS value FROM player_items WHERE player_id={} AND itemtype=3388", SELLER)) == 0);
		const auto createdOffers = Market::getOwnOffers(SELLER, 1);
		CHECK(createdOffers.size() == 1);
		seller.setLastDepotId(2);
		const auto createdId = createdOffers.front().id;
		CHECK(Market::settleOffer(&seller, createdId, 1, 1, 1, 30).value_or(false));
		CHECK(Market::processDeliveries(&seller));
		CHECK(Market::getDeliveryStatus(SELLER, createdId)->depotId == 2);
		CHECK(Market::getDeliveryStatus(SELLER, createdId)->applied);
		CHECK(seller.getInbox(2)->getItemByIndex(0)->getID() == 3388);
		PropWriteStream returnedStream;
		seller.getInbox(2)->getItemByIndex(0)->serializeAttr(returnedStream);
		CHECK(returnedStream.getStream() == helmetAttributes);
		CHECK(inboxCount(SELLER, 2) == 3);
		CHECK(inboxCount(SELLER, 1) == 10);
		CHECK(Market::processDeliveries(&seller));
		CHECK(inboxCount(SELLER, 2) == 3);

	});
}

TFS_TEST_MAIN()
