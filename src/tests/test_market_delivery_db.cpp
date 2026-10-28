#include "../otpch.h"

#include "../configmanager.h"
#include "../container.h"
#include "../chat.h"
#include "../globalevent.h"
#include "../tile.h"
#include "../database.h"
#include "../databasemanager.h"
#include "../game.h"
#include "../inbox.h"
#include "../iologindata.h"
#include "../item.h"
#include "../market.h"
#include "../player.h"
#include "../save_manager.h"
#include "../scriptmanager.h"
#include "../tasks.h"
#include "../thread_pool.h"
#include "../vocation.h"
#include "../logger.h"
#include "test_support.h"

extern Game g_game;
extern Vocations g_vocations;
extern Chat* g_chat;
extern GlobalEvents* g_globalEvents;

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

// Inject the worker completion boundary without adding production DB fault hooks.
// The unknown case below first performs a real COMMIT, then loses its acknowledgement.
struct SaveManagerTestAccess
{
	static void lostAcknowledgement(SaveManager& manager, uint32_t guid, IOLoginData::PlayerSaveSnapshot snapshot)
	{
		auto save = std::make_shared<IOLoginData::PlayerSaveSnapshot>(std::move(snapshot));
		manager.flushInFlight.insert(guid);
		manager.activeFlushes.emplace(guid, SaveManager::ActiveFlush{save, {}, false});
		manager.onPlayerFlushed(guid, IOLoginData::FlushResult::Indeterminate, save);
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
	player.setLastDepotId(town);
}

uint32_t offer(uint32_t owner, uint8_t sale, uint32_t amount = 5, const std::string& attributes = {}, uint8_t tier = 0)
{
	auto& db = Database::getInstance();
	sql(fmt::format("INSERT INTO `market_offers` (`player_id`,`sale`,`itemtype`,`amount`,`created`,`price`,`tier`,`attributes`,`delivery_depot_id`) "
	    "VALUES ({},{},{},{},{},100,{}, {},{})", owner, sale, static_cast<uint16_t>(ITEM_BAG), amount,
	    time(nullptr), tier, db.escapeString(attributes), owner == SELLER ? 1 : 2));
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

struct WorldEvents
{
	GlobalEvents events;
	Chat chat;
	GlobalEvents* previousEvents = g_globalEvents;
	Chat* previousChat = g_chat;
	WorldEvents() { g_globalEvents = &events; g_chat = &chat; }
	~WorldEvents() { g_globalEvents = previousEvents; g_chat = previousChat; }
};

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
	CHECK(name != nullptr);
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
	// Reproduce an older Market schema; migration 69 must add all three columns.
	sql("ALTER TABLE `market_offers` DROP COLUMN `delivery_depot_id`");
	sql("ALTER TABLE `market_delivery_pending` DROP COLUMN `delivered_amount`, DROP COLUMN `retry_after`");
	CHECK(!DatabaseManager::isMarketSchemaReady());
	DatabaseManager::updateDatabase();
	CHECK(number("SELECT `value` FROM `server_config` WHERE `config`='db_version'") == 70);
	CHECK(DatabaseManager::isMarketSchemaReady());
	// The item XML registers native equipment/movement events while parsing.
	CHECK(ScriptingManager::getInstance().loadPreItems());
	CHECK(Item::items.loadFromOtb("data/items/items.otb"));
	CHECK(Item::items.loadFromXml());
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
		// Reserve fractional coins without floor fallback. Known failure restores
		// the original objects/parents and bank even at UINT64_MAX boundaries.
		auto purse = Item::CreateItem(ITEM_BAG);
		auto platinum = Item::CreateItem(ITEM_PLATINUM_COIN, 2);
		static_cast<Cylinder&>(seller).internalAddThing(CONST_SLOT_BACKPACK, purse.get());
		purse->getContainer()->internalAddThing(platinum.get());
		CHECK(Market::reserveMoney(&seller, 25));
		CHECK(seller.getMoney() == 175);
		CHECK(Market::finishMoney(&seller, true));
		CHECK(seller.getMoney() == 200);
		CHECK(platinum->getParent() == purse->getContainer());
		CHECK(platinum->getItemCount() == 2);
		seller.setBankBalance(UINT64_MAX - 5);
		CHECK(Market::reserveMoney(&seller, 201));
		CHECK(seller.getBankBalance() == UINT64_MAX - 6);
		CHECK(Market::finishMoney(&seller, true));
		CHECK(seller.getBankBalance() == UINT64_MAX - 5);
		CHECK(seller.getMoney() == 200);
		seller.setBankBalance(10000);
		CHECK(Market::reserveMoney(&seller, 25));
		MarketOfferRecord invalid;
		invalid.playerId = SELLER;
		CHECK(Market::createOfferAndSave(&seller, invalid) == std::optional<bool>{false});
		CHECK(seller.getMoney() == 200);
		CHECK(!Market::finishMoney(&seller, true)); // native guard already released the reservation
		Player noBackpack(nullptr);
		initialize(noBackpack, SELLER, 1);
		noBackpack.setBankBalance(0);
		auto heldPlatinum = Item::CreateItem(ITEM_PLATINUM_COIN, 1);
		static_cast<Cylinder&>(noBackpack).internalAddThing(CONST_SLOT_LEFT, heldPlatinum.get());
		CHECK(Market::reserveMoney(&noBackpack, 20));
		CHECK(noBackpack.getMoney() == 80);
		CHECK(noBackpack.getBankBalance() == 0);
		CHECK(Market::finishMoney(&noBackpack, true));
		CHECK(noBackpack.getMoney() == 100);
		CHECK(noBackpack.getInventoryItem(CONST_SLOT_LEFT) == heldPlatinum.get());
		std::weak_ptr<Item> paidCoin = heldPlatinum;
		CHECK(Market::reserveMoney(&noBackpack, 20));
		CHECK(Market::finishMoney(&noBackpack, false));
		CHECK(noBackpack.getMoney() == 80); // success keeps the debit
		CHECK(noBackpack.getBankBalance() == 0);
		heldPlatinum.reset();
		CHECK(paidCoin.expired()); // reservation no longer retains a consumed coin
		Player bankFallback(nullptr);
		initialize(bankFallback, SELLER, 1);
		bankFallback.setBankBalance(100);
		auto crystal = Item::CreateItem(ITEM_CRYSTAL_COIN, 1);
		for (uint32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
			auto item = slot == CONST_SLOT_LEFT ? crystal : Item::CreateItem(3388);
			static_cast<Cylinder&>(bankFallback).internalAddThing(slot, item.get());
		}
		CHECK(Market::reserveMoney(&bankFallback, 20));
		CHECK(bankFallback.getBankBalance() == 80);
		CHECK(bankFallback.getInventoryItem(CONST_SLOT_LEFT) == crystal.get());
		CHECK(Market::finishMoney(&bankFallback, true));
		CHECK(bankFallback.getBankBalance() == 100);


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
		sql(fmt::format("UPDATE market_offers SET created=1 WHERE id={}", expired));
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
		unique->setStrAttr(ITEM_ATTRIBUTE_TEXT, "Book text: `onlinetime` = `onlinetime` + 123");
		PropWriteStream stream;
		unique->serializeAttr(stream);
		const std::string attributes{stream.getStream()};
		const auto order = offer(BUYER, 0, 1, {}, 1); // legacy tier disagrees with serialized tier=4
		seller.setBankBalance(seller.getBankBalance() + 100);
		CHECK(Market::settleOffer(&seller, order, 1, 1, 3, 30, 4, attributes).value_or(false));
		CHECK(Market::processDeliveries(&buyer));
		CHECK(buyer.getInbox(2)->getItemByIndex(0)->getTier() == 4);
		CHECK(inboxCount(BUYER, 2) == 11);
		auto bookSnapshot = IOLoginData::buildPlayerSave(&buyer);
		CHECK(bookSnapshot.has_value());
		for (size_t i = 0; i < bookSnapshot->queries.size(); ++i) {
			sql(fmt::format("INSERT INTO player_save_async_pending(guid,query_index,query_text,created_at) VALUES({},{},{},1)",
			    BUYER, i, Database::getInstance().escapeString(bookSnapshot->queries[i])));
		}
		g_saveManager.recoverPendingFlushes();
		auto recoveredBook = Database::getInstance().storeQuery(fmt::format(
		    "SELECT attributes FROM player_inboxitems WHERE player_id={} AND pid=2 ORDER BY sid LIMIT 1", BUYER));
		CHECK(recoveredBook != nullptr);
		CHECK(recoveredBook->getString("attributes").find("`onlinetime` = `onlinetime` + 123") != std::string_view::npos);


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

		// Creation destination survives movement for automatic expiry, while a
		// manual cancellation above deliberately follows the current city.
		seller.setLastDepotId(0);
		created.created = 1;
		CHECK(Market::createOfferAndSave(&seller, created).value_or(false));
		const auto fixed = Market::getOwnOffers(SELLER, 1).front();
		CHECK(fixed.deliveryDepotId == std::optional<uint32_t>{0});
		seller.setLastDepotId(2);
		CHECK(Market::settleOffer(nullptr, fixed.id, 1, 1, 2, 30).value_or(false));
		CHECK(Market::getDeliveryStatus(SELLER, fixed.id)->depotId == 0);
		CHECK(Market::processDeliveries(&seller));

		// Legacy offers have no recorded city: deterministic Inbox 0, regardless
		// of the owner's residence or online session's last depot.
		const auto legacy = offer(SELLER, 1, 1);
		sql(fmt::format("UPDATE market_offers SET created=1,delivery_depot_id=NULL WHERE id={}", legacy));
		CHECK(Market::settleOffer(nullptr, legacy, 1, 1, 2, 30).value_or(false));
		CHECK(Market::getDeliveryStatus(SELLER, legacy)->depotId == 0);
		CHECK(Market::processDeliveries(&seller));

		// A past-due offer is rejected even before the periodic expiry event.
		const auto stale = offer(SELLER, 1, 1);
		sql(fmt::format("UPDATE market_offers SET created=1 WHERE id={}", stale));
		CHECK(Market::settleOffer(&buyer, stale, 1, 1, 3, 30) == std::optional<bool>{false});
		CHECK(Market::getOffer(stale).has_value());

		// Invalid first row is backed off; a later valid entitlement still runs.
		sql(fmt::format("INSERT INTO market_delivery_pending (event_key,offer_id,player_id,itemtype,amount,attributes,created_at) "
		    "VALUES ('invalid-head',123456,{},0,1,'',1),('valid-after-invalid',123457,{},0,0,'',1)", SELLER, SELLER));
		sql("UPDATE market_delivery_pending SET bank_credit=7 WHERE event_key='valid-after-invalid'");
		const auto beforeCredit = seller.getBankBalance();
		CHECK(Market::processDeliveries(&seller));
		CHECK(seller.getBankBalance() == beforeCredit + 7);
		CHECK(number("SELECT retry_after AS value FROM market_delivery_pending WHERE event_key='invalid-head'") > 0);

		// Real DB commits preserve partial progress; a second delivery call cannot
		// reapply the same slice. Stackables keep this integration fixture small.
		for (uint32_t count : {64000, 64001, 65535}) {
			const auto large = offer(SELLER, 1, count);
			sql(fmt::format("UPDATE market_offers SET itemtype={},price=1 WHERE id={}", static_cast<uint16_t>(ITEM_GOLD_COIN), large));
			CHECK(Market::settleOffer(&seller, large, count, count, 1, 30).value_or(false));
			const auto previous = number(fmt::format("SELECT COALESCE(SUM(count),0) AS value FROM player_inboxitems "
			    "WHERE player_id={} AND itemtype={}", SELLER, static_cast<uint16_t>(ITEM_GOLD_COIN)));
			CHECK(Market::processDeliveries(&seller));
			CHECK(!Market::getDeliveryStatus(SELLER, large)->applied);
			for (size_t pass = 0; pass < 20 && !Market::getDeliveryStatus(SELLER, large)->applied; ++pass) {
				CHECK(Market::processDeliveries(&seller));
			}
			CHECK(Market::getDeliveryStatus(SELLER, large)->applied);
			CHECK(Market::processDeliveries(&seller)); // no due rows is a successful no-op
			CHECK(number(fmt::format("SELECT COALESCE(SUM(count),0) AS value FROM player_inboxitems "
			    "WHERE player_id={} AND itemtype={}", SELLER, static_cast<uint16_t>(ITEM_GOLD_COIN))) == previous + count);
		}

		// The Lua signed balance ceiling must defer bank credits without blocking
		// item-only receipts in the same batch, including legacy unsigned balances.
		const auto previousBalance = seller.getBankBalance();
		seller.setBankBalance(static_cast<uint64_t>(INT64_MAX) - 1);
		const auto cappedCredit = offer(SELLER, 0, 1);
		sql(fmt::format("UPDATE market_offers SET price=2 WHERE id={}", cappedCredit));
		CHECK(Market::settleOffer(&seller, cappedCredit, 1, 1, 1, 30).value_or(false));
		const auto itemAfterCredit = offer(SELLER, 1, 1);
		CHECK(Market::settleOffer(&seller, itemAfterCredit, 1, 1, 1, 30).value_or(false));
		CHECK(Market::processDeliveries(&seller));
		CHECK(seller.getBankBalance() == static_cast<uint64_t>(INT64_MAX) - 1);
		CHECK(!Market::getDeliveryStatus(SELLER, cappedCredit)->applied);
		CHECK(Market::getDeliveryStatus(SELLER, itemAfterCredit)->applied);
		seller.setBankBalance(UINT64_MAX);
		const auto unsignedItem = offer(SELLER, 1, 1);
		CHECK(Market::settleOffer(&seller, unsignedItem, 1, 1, 1, 30).value_or(false));
		CHECK(Market::processDeliveries(&seller));
		CHECK(Market::getDeliveryStatus(SELLER, unsignedItem)->applied);
		CHECK(seller.getBankBalance() == UINT64_MAX);
		seller.setBankBalance(static_cast<uint64_t>(INT64_MAX) - 2);
		const auto exactCredit = offer(SELLER, 0, 1);
		sql(fmt::format("UPDATE market_offers SET price=2 WHERE id={}", exactCredit));
		CHECK(Market::settleOffer(&seller, exactCredit, 1, 1, 1, 30).value_or(false));
		CHECK(Market::processDeliveries(&seller));
		CHECK(seller.getBankBalance() == static_cast<uint64_t>(INT64_MAX));
		CHECK(Market::getDeliveryStatus(SELLER, exactCredit)->applied);
		seller.setBankBalance(previousBalance);

		// Checked flush: rollback is confirmed and a later retry applies once.
		auto rollbackSnapshot = IOLoginData::buildPlayerSave(&seller);
		CHECK(rollbackSnapshot.has_value());
		const auto balanceBeforeFailure = number(fmt::format("SELECT balance AS value FROM players WHERE id={}", SELLER));
		rollbackSnapshot->queries.push_back("UPDATE tfs_market_test_missing_table SET value=1");
		CHECK(IOLoginData::flushPlayerSaveChecked(*rollbackSnapshot) == IOLoginData::FlushResult::Failed);
		CHECK(number(fmt::format("SELECT balance AS value FROM players WHERE id={}", SELLER)) == balanceBeforeFailure);
		rollbackSnapshot->queries.pop_back();
		CHECK(IOLoginData::flushPlayerSaveChecked(*rollbackSnapshot) == IOLoginData::FlushResult::Committed);
		const auto onlineTime = number(fmt::format("SELECT onlinetime AS value FROM players WHERE id={}", SELLER));
		CHECK(IOLoginData::flushPlayerSaveChecked(*rollbackSnapshot) == IOLoginData::FlushResult::Committed);
		CHECK(number(fmt::format("SELECT onlinetime AS value FROM players WHERE id={}", SELLER)) == onlineTime);
	});

	// Queue two generations behind a busy worker; the newest durable WAL must
	// survive the older worker's completion and win in FIFO order.
	std::promise<void> releaseWorker;
	auto gate = releaseWorker.get_future().share();
	auto blocked = g_threadPool.submit_task([gate] { gate.wait(); });
	dispatch([&] {
		seller.setBankBalance(11111);
		CHECK(g_saveManager.savePlayer(&seller));
		seller.setBankBalance(22222);
		CHECK(g_saveManager.savePlayer(&seller));
	});
	releaseWorker.set_value();
	blocked.get();
	for (size_t i = 0; i < 2000 && g_saveManager.hasPendingPlayerSave(SELLER); ++i) {
		TaskReactorTestAccess::runOnceAsOwner(g_reactor);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK(!g_saveManager.hasPendingPlayerSave(SELLER));
	dispatch([&] {
		CHECK(number(fmt::format("SELECT balance AS value FROM players WHERE id={}", SELLER)) == 22222);
		CHECK(number(fmt::format("SELECT COUNT(*) AS value FROM player_save_async_pending WHERE guid={}", SELLER)) == 0);
		auto committed = IOLoginData::buildPlayerSave(&seller);
		CHECK(committed.has_value());
		CHECK(IOLoginData::flushPlayerSaveChecked(*committed) == IOLoginData::FlushResult::Committed);
		SaveManagerTestAccess::lostAcknowledgement(g_saveManager, SELLER, std::move(*committed));
		CHECK(g_saveManager.hasFailedRecovery(SELLER));
		CHECK(!Market::isPlayerReady(&seller));
		CHECK(!g_saveManager.savePlayerSync(&seller));
		CHECK(!Market::reserveMoney(&seller, 1));
		CHECK(number(fmt::format("SELECT balance AS value FROM players WHERE id={}", SELLER)) == 22222);
		// Re-entry hydrates a fresh player from the committed DB state; readiness
		// alone cannot detect an item or bank load regression.
		SaveManager afterRestart;
		sql(fmt::format("UPDATE players SET onlinetime=54321 WHERE id={}", SELLER));
		Player reloaded(nullptr);
		CHECK(IOLoginData::loadPlayerById(&reloaded, SELLER));
		CHECK(reloaded.getBankBalance() == 22222);
		const auto savedOnlineTime = number(fmt::format("SELECT onlinetime AS value FROM players WHERE id={}", SELLER));
		CHECK(reloaded.getOnlineTimeAt(time(nullptr)) == savedOnlineTime);
		Player reloadedByName(nullptr);
		CHECK(IOLoginData::loadPlayerByName(&reloadedByName, seller.getName()));
		CHECK(reloadedByName.getOnlineTimeAt(time(nullptr)) == savedOnlineTime);
		for (uint32_t depot : {0, 1, 2}) {
			CHECK(reloaded.getInbox(depot)->size() == seller.getInbox(depot)->size());
		}
		CHECK(afterRestart.canSavePlayersSync({&reloaded}));

	});

	// Exercise actual online lookup and deferred structural removal. Ordinary
	// logout/de-equip save paths must not run after the GUID was quarantined.
	WorldEvents world;
	auto online = std::make_shared<Player>(nullptr);
	initialize(*online, BUYER, 2);
	const auto savedLoginPosition = online->getLoginPosition();
	const Position quarantinePosition(2300, 2300, 7);
	auto tile = std::make_unique<StaticTile>(quarantinePosition.x, quarantinePosition.y, quarantinePosition.z);
	for (size_t id = 1; id < Item::items.size(); ++id) {
		const auto& type = Item::items[static_cast<uint16_t>(id)];
		if (type.isGroundTile() && !type.blockSolid) {
			tile->internalAddThing(Item::CreateItem(static_cast<uint16_t>(id)).get());
			break;
		}
	}
	g_game.map.setTile(quarantinePosition, std::move(tile));
	dispatch([&] {
		CHECK(g_game.internalPlaceCreature(online.get(), quarantinePosition, false, true));
		CHECK(g_game.getPlayerByGUID(BUYER) == online);
		const auto bankBefore = number(fmt::format("SELECT balance AS value FROM players WHERE id={}", BUYER));
		online->setBankBalance(777);
		g_saveManager.quarantinePlayer(BUYER, "integration-test", true);
		CHECK(online->isPersistenceQuarantined());
		CHECK(online->isMovementBlocked());
		CHECK(!online->isRemoved()); // the current handler still owns its Player
		CHECK(number(fmt::format("SELECT balance AS value FROM players WHERE id={}", BUYER)) == bankBefore);
	});
	TaskReactorTestAccess::runOnceAsOwner(g_reactor);
	CHECK(online->isRemoved());
	// An ordinary logout would replace loginPosition with quarantinePosition.
	CHECK(online->getLoginPosition() == savedLoginPosition);
	CHECK(!g_game.getPlayerByGUID(BUYER));
	CHECK(!g_saveManager.hasPendingPlayerSave(BUYER));
}

int main()
{
	if (!std::getenv("TFS_TEST_DB_NAME")) {
		std::cout << "[SKIP] TFS_TEST_DB_NAME is required for real MySQL integration tests\n";
		return 77;
	}
	return tfs::tests::run();
}
