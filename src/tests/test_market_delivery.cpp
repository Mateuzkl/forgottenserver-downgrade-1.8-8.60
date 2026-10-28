#include "../otpch.h"

#include "../configmanager.h"
#include "../game.h"
#include "../inbox.h"
#include "../item.h"
#include "../luascript.h"
#include "../market.h"
#include "../player.h"
#include "../depotlocker.h"
#include "../tile.h"
#include "test_support.h"

#include <fstream>

extern LuaEnvironment g_luaEnvironment;
extern Game g_game;

namespace {
void loadItems()
{
	if (Item::items.size() == 0) {
		const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
		CHECK(Item::items.loadFromOtb((root / "data/items/items.otb").string()));
	}
}

std::string serialize(const Item& item)
{
	PropWriteStream stream;
	item.serializeAttr(stream);
	return std::string{stream.getStream()};
}
} // namespace

TEST_CASE(market_delivery_preserves_stack_totals_and_unique_attributes)
{
	loadItems();
	std::vector<std::shared_ptr<Item>> items;
	CHECK(Market::buildDeliveryItems(ITEM_GOLD_COIN, 301, {}, items));
	uint32_t total = 0;
	for (const auto& item : items) {
		total += item->getItemCount();
		CHECK(item->getItemCount() <= item->getStackSize());
	}
	CHECK(total == 301);
	ConfigManager::setBoolean(ConfigManager::FORGE_SYSTEM_ENABLED, true);
	auto unique = Item::CreateItem(ITEM_BAG);
	unique->setTier(4);
	unique->setStrAttr(ITEM_ATTRIBUTE_TEXT, std::string("Market attributes survive\0binary", 32));
	const auto attributes = serialize(*unique);
	CHECK(Market::buildDeliveryItems(ITEM_BAG, 1, attributes, items));
	CHECK(items.size() == 1);
	CHECK(items.front()->getTier() == 4);
	CHECK(serialize(*items.front()) == attributes);
}

TEST_CASE(market_rejects_corrupt_or_oversized_delivery_before_applying_any_item)
{
	loadItems();
	std::vector<std::shared_ptr<Item>> items;
	CHECK(!Market::buildDeliveryItems(0, 1, {}, items));
	CHECK(!Market::buildDeliveryItems(ITEM_BAG, 0, {}, items));
	CHECK(!Market::buildDeliveryItems(ITEM_BAG, Market::MAX_DELIVERY_AMOUNT + 1, {}, items));
	CHECK(!Market::buildDeliveryItems(ITEM_BAG, 1, std::string(Market::MAX_DELIVERY_ATTRIBUTES + 1, 'x'), items));
	PropWriteStream payload;
	payload.write<uint8_t>(ATTR_TEXT);
	payload.writeString("A complete attribute payload");
	const std::string attributes{payload.getStream()};
	CHECK(!Market::buildDeliveryItems(ITEM_BAG, 2, attributes, items));
	// The first byte announces a string attribute; every incomplete length/value must fail.
	for (size_t length = 1; length < attributes.size(); ++length) {
		CHECK(!Market::buildDeliveryItems(ITEM_BAG, 1, attributes.substr(0, length), items));
		CHECK(items.empty());
	}
	CHECK(!Market::buildDeliveryItems(ITEM_BAG, 1, std::string(1, '\xff'), items));
	CHECK(!Market::buildDeliveryItems(ITEM_BAG, 1, attributes + '\0' + 'x', items));
}

TEST_CASE(lua_inbox_zero_is_explicit_and_default_keeps_home_town)
{
	loadItems();
	struct WorkingDirectoryGuard {
		std::filesystem::path previous = std::filesystem::current_path();
		~WorkingDirectoryGuard() { std::filesystem::current_path(previous); }
	} directoryGuard;
	// Registration initializes optional catalogs. Keep their runtime files out of
	// the source tree even when these tests run from the repository root.
	std::filesystem::current_path(std::filesystem::temp_directory_path());
	CHECK(g_luaEnvironment.initState());
	LuaScriptInterface script("market-inbox-test");
	CHECK(script.initState());
	auto L = script.getLuaState();
	Player player(nullptr);
	auto group = std::make_shared<Group>();
	group->id = 1;
	player.setGroup(group);
	player.setTown(std::make_shared<Town>(2));
	CHECK(player.getInbox() == player.getInbox(2));
	CHECK(player.getInbox(0) != player.getInbox());
	Lua::pushUserdata<Player>(L, &player);
	Lua::setMetatable(L, -1, "Player");
	lua_setglobal(L, "marketTestPlayer");
	CHECK(luaL_dostring(L, "assert(marketTestPlayer:getInbox(0)); assert(marketTestPlayer:getInbox()); "
	                      "assert(marketTestPlayer:getInbox(0) ~= marketTestPlayer:getInbox())") == LUA_OK);
	const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	for (const auto file : {"data/scripts/network/market/market.lua", "data/scripts/globalevents/market_deliveries.lua",
	                        "data/migrations/68.lua", "data/migrations/69.lua", "data/scripts/globalevents/serversave.lua", "data/scripts/talkactions/player/misc/marketstatus.lua"}) {
		CHECK(luaL_loadfile(L, (root / file).string().c_str()) == LUA_OK);
		lua_pop(L, 1);
	}
}

TEST_CASE(current_depot_overrides_home_town_and_stale_last_depot)
{
	loadItems();
	Player player(nullptr);
	player.setTown(std::make_shared<Town>(1));
	player.setLastDepotId(1);
	const Position position(2100, 2100, 7);
	g_game.map.setTile(position, std::make_unique<StaticTile>(position.x, position.y, position.z));
	const Position depotPosition(2101, 2100, 7);
	g_game.map.setTile(depotPosition, std::make_unique<StaticTile>(depotPosition.x, depotPosition.y, depotPosition.z));
	auto depot = std::make_shared<DepotLocker>(ITEM_LOCKER);
	depot->setDepotId(2);
	g_game.map.getTile(depotPosition)->internalAddThing(depot.get());
	player.setParent(g_game.map.getTile(position));
	CHECK(Market::getInboxDepotId(player) == 2);
	depot->setDepotId(1);
	player.setLastDepotId(2);
	CHECK(Market::getInboxDepotId(player) == 1);
	depot->setDepotId(0);
	CHECK(Market::getInboxDepotId(player) == 0); // explicit map ID 0 is valid
	player.setParent(nullptr);
	CHECK(Market::getInboxDepotId(player) == 2); // no nearby locker: last visited city
	player.setLastDepotId(-1);
	CHECK(Market::getInboxDepotId(player) == 0); // unknown physical depot: legacy Inbox
}

TEST_CASE(market_access_accepts_non_pz_depots_but_rechecks_distance_and_floor)
{
	// Execute the actual Lua access check with map tiles as its input. Timberport
	// has physical depots without PZ flags; it must behave like a normal depot.
	const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	std::ifstream input(root / "data/scripts/network/market/market.lua");
	const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
	const auto first = source.find("local function tileHasMarketAccess(");
	const auto last = source.find("local function hasCurrentMarketAccess(", first);
	CHECK(first != std::string::npos && last != std::string::npos);
	LuaStatePtr state{luaL_newstate()};
	CHECK(state != nullptr);
	auto L = state.get();
	luaL_openlibs(L);
	CHECK(luaL_dostring(L, R"lua(
        ITEM_TYPE_DEPOT = 1
        TILESTATE_PROTECTIONZONE = 2
        MARKET_ITEM_ID = 12903
        local tiles = {}
        local function key(x,y,z) return x..':'..y..':'..z end
        function Position(x,y,z) return {x=x,y=y,z=z} end
        function Tile(p) return tiles[key(p.x,p.y,p.z)] end
        function place(x,y,z,pz,depot,market)
            tiles[key(x,y,z)] = {
                hasFlag = function(self, flag) return flag == TILESTATE_PROTECTIONZONE and pz end,
                getItemByType = function(self, kind) return kind == ITEM_TYPE_DEPOT and depot or nil end,
                getItemById = function(self, id) return id == MARKET_ITEM_ID and market or nil end
            }
        end
    )lua") == LUA_OK);
	const auto chunk = source.substr(first, last - first) + "\nreturn hasMarketAccessAtPosition";
	CHECK(luaL_loadbuffer(L, chunk.data(), chunk.size(), "market-access") == LUA_OK);
	CHECK(lua_pcall(L, 0, 1, 0) == LUA_OK);
	lua_setglobal(L, "hasAccess");
	CHECK(luaL_dostring(L, R"lua(
        local player = {}
        place(10,10,7,false,false,false)
        place(11,10,7,false,true,false)
        assert(hasAccess(player, Position(10,10,7))) -- town 2: no PZ, adjacent depot
        place(10,10,7,true,false,false)
        assert(hasAccess(player, Position(10,10,7))) -- towns 1/3: PZ and depot
        place(14,10,7,true,false,false)
        assert(not hasAccess(player, Position(14,10,7))) -- walking away closes access
        place(10,10,6,true,false,false)
        assert(not hasAccess(player, Position(10,10,6))) -- depot on a different floor
        place(20,20,7,false,false,false)
        place(21,20,7,false,false,true)
        assert(not hasAccess(player, Position(20,20,7))) -- standalone object still requires PZ
        place(20,20,7,true,false,false)
        assert(hasAccess(player, Position(20,20,7)))
        assert(not hasAccess(player, Position(99,99,7))) -- missing tile
    )lua") == LUA_OK);
}

TEST_CASE(market_legacy_amount_validation_does_not_materialize_an_unbounded_batch)
{
	loadItems();
	for (uint32_t amount : {64000, 64001, 65535}) {
		CHECK(Market::validateDelivery(ITEM_BAG, amount, {}));
		std::vector<std::shared_ptr<Item>> items;
		CHECK(!Market::buildDeliveryItems(ITEM_BAG, amount, {}, items));
		CHECK(items.empty());
		CHECK(Market::buildDeliveryItems(ITEM_BAG, Market::DELIVERY_ITEMS_PER_PLAYER, {}, items));
		CHECK(items.size() == Market::DELIVERY_ITEMS_PER_PLAYER);
	}
	CHECK(!Market::validateDelivery(ITEM_BAG, 65536, {}));
}

TEST_CASE(market_expiration_boundary_uses_wide_arithmetic)
{
	CHECK(!Market::isExpired(100, 30, 129));
	CHECK(Market::isExpired(100, 30, 130));
	CHECK(Market::isExpired(100, 30, 131));
	CHECK(!Market::isExpired(UINT32_MAX, 30, UINT32_MAX));
	CHECK(Market::isExpired(UINT32_MAX, 30, uint64_t{UINT32_MAX} + 30));
}

TEST_CASE(persistence_failure_freezes_the_session_before_logout_cleanup)
{
	Player player(nullptr);
	CHECK(!player.isPersistenceQuarantined());
	CHECK(!player.isMovementBlocked());
	player.disconnectForPersistenceFailure();
	CHECK(player.isPersistenceQuarantined());
	CHECK(player.isMovementBlocked());
}

TFS_TEST_MAIN()
