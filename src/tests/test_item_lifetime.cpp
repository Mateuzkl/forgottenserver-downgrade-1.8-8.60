#include "../otpch.h"

#include "../configmanager.h"
#include "../house.h"
#include "../item.h"
#include "../luascript.h"
#include "../player.h"

#include "test_support.h"
#include <memory>

extern bool isValidItemPointer(Item* item);
extern LuaEnvironment g_luaEnvironment;

namespace {

class LuaFixture
{
public:
	LuaFixture()
	{
		ConfigManager::setBoolean(ConfigManager::WARN_UNSAFE_SCRIPTS, false);
		ConfigManager::setBoolean(ConfigManager::CONVERT_UNSAFE_SCRIPTS, false);
		CHECK(g_luaEnvironment.initState());
		L = g_luaEnvironment.getLuaState();
		CHECK(L != nullptr);
	}

	~LuaFixture() { g_luaEnvironment.closeState(); }

	lua_State* L = nullptr;
};

} // namespace

TEST_CASE(item_lifetime_registry_tracks_destroyed_item)
{
	Item* rawItem = nullptr;
	{
		auto item = std::make_shared<Item>(0);
		rawItem = item.get();
		CHECK(isValidItemPointer(rawItem));
	}

	CHECK(!isValidItemPointer(rawItem));
}

TEST_CASE(house_transfer_item_keeps_identity_until_reset)
{
	auto house = std::make_shared<House>(1);
	house->setName("Lifetime Test House");

	auto first = house->getTransferItem();
	auto second = house->getTransferItem();

	CHECK(first != nullptr);
	CHECK(second == first);

	house->resetTransferItem();
	auto replacement = house->getTransferItem();

	CHECK(replacement != nullptr);
	CHECK(replacement != first);
}

TEST_CASE(house_transfer_item_trade_cancel_resets_document)
{
	auto house = std::make_shared<House>(2);
	house->setName("Cancel Test House");

	auto transferItem = house->getTransferItem();
	CHECK(transferItem != nullptr);

	transferItem->onTradeEvent(ON_TRADE_CANCEL, nullptr);
	auto replacement = house->getTransferItem();

	CHECK(replacement != nullptr);
	CHECK(replacement != transferItem);
}

TEST_CASE(houses_get_house_preserves_house_lifetime)
{
	std::shared_ptr<House> house;
	std::shared_ptr<House> addedHouse;
	{
		Houses houses;
		addedHouse = houses.addHouse(42);
		house = houses.getHouse(42);

		CHECK(house != nullptr);
		CHECK(house == addedHouse);
		CHECK(houses.getHouse(42) == house);
		CHECK(houses.getHouse(43) == nullptr);
	}

	CHECK(house != nullptr);
	CHECK(house == addedHouse);
	CHECK(house->getId() == 42);
}

TEST_CASE(houses_add_house_creates_and_returns_shared_ptr)
{
	Houses houses;
	auto house100 = houses.addHouse(100);
	CHECK(house100 != nullptr);
	CHECK(house100->getId() == 100);

	auto house100_again = houses.addHouse(100);
	CHECK(house100_again != nullptr);
	CHECK(house100 == house100_again);
	CHECK(house100.get() == house100_again.get());

	auto house200 = houses.addHouse(200);
	CHECK(house200 != nullptr);
	CHECK(house200->getId() == 200);
	CHECK(house100 != house200);

	CHECK(houses.getHouse(100) == house100);
	CHECK(houses.getHouse(200) == house200);
	CHECK(houses.getHouse(300) == nullptr);
}

TEST_CASE(houses_add_house_preserves_lifetime_beyond_houses_scope)
{
	std::shared_ptr<House> house;
	{
		Houses houses;
		house = houses.addHouse(100);
		CHECK(house != nullptr);
		CHECK(house->getId() == 100);
	}

	CHECK(house != nullptr);
	CHECK(house->getId() == 100);
}

TEST_CASE(houses_get_house_by_player_id_preserves_lifetime_and_semantics)
{
	Houses houses;
	auto house1 = houses.addHouse(101);
	auto house2 = houses.addHouse(102);

	CHECK(house1 != nullptr);
	CHECK(house2 != nullptr);
	CHECK(house1->getOwner() == 0);
	CHECK(house2->getOwner() == 0);

	// Searching for non-existent owner returns nullptr
	CHECK(houses.getHouseByPlayerId(999) == nullptr);
	CHECK(houses.getHouseByPlayerId(1) == nullptr);

	// Const access test
	const Houses& constHouses = houses;
	CHECK(constHouses.getHouseByPlayerId(999) == nullptr);

	// Searching for owner 0 finds the first house with owner 0
	auto unowned = constHouses.getHouseByPlayerId(0);
	CHECK(unowned != nullptr);
	CHECK(unowned->getId() == 101);
	CHECK(unowned == house1);
	CHECK(unowned.get() == house1.get());
}

TEST_CASE(door_get_house_returns_valid_shared_ptr_and_preserves_identity)
{
	auto house = std::make_shared<House>(100);
	auto door = std::make_shared<Door>(0);
	door->setDoorId(1);

	CHECK(door->getHouse() == nullptr);

	house->addDoor(door.get());

	auto retrievedHouse = door->getHouse();
	CHECK(retrievedHouse != nullptr);
	CHECK(retrievedHouse == house);
	CHECK(retrievedHouse->getId() == 100);
	CHECK(house->getDoors().size() == 1);
	CHECK(house->getDoorByNumber(1) == door.get());
}

TEST_CASE(door_get_house_returns_nullptr_when_house_is_destroyed)
{
	auto door = std::make_shared<Door>(0);
	door->setDoorId(2);

	{
		auto house = std::make_shared<House>(200);
		house->addDoor(door.get());
		CHECK(door->getHouse() != nullptr);
		CHECK(door->getHouse()->getId() == 200);
	}

	CHECK(door->getHouse() == nullptr);
	// onRemoved when house is destroyed should be safe and not throw/crash
	door->onRemoved();
	CHECK(door->getHouse() == nullptr);
}

void ensureItemTypes()
{
	static const bool loaded = [] {
		const auto itemFile = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
		                      "data/items/items.otb";
		return Item::items.loadFromOtb(itemFile.string());
	}();
	CHECK(loaded);
}

std::shared_ptr<Player> makeTestPlayer(uint32_t guid, std::string_view name)
{
	ensureItemTypes();
	auto player = std::make_shared<Player>(nullptr);
	player->setGUID(guid);
	player->setName(name);
	player->setGroup(std::make_shared<Group>());
	return player;
}

TEST_CASE(door_can_use_and_access_list_behavior)
{
	auto house = std::make_shared<House>(300);
	auto door = std::make_shared<Door>(0);
	door->setDoorId(3);
	house->addDoor(door.get());

	auto player = makeTestPlayer(1, "TestPlayer");

	// Uninvited player on restricted door cannot use
	CHECK(!door->canUse(player.get()));

	// Door with wildcard access list allows player
	door->setAccessList("*");
	CHECK(door->canUse(player.get()));

	// When house is reset/destroyed, door allows usage freely
	house.reset();
	CHECK(door->getHouse() == nullptr);
	CHECK(door->canUse(player.get()));
}

TEST_CASE(housetile_get_house_returns_valid_shared_ptr_and_preserves_identity)
{
	auto house = std::make_shared<House>(500);
	auto houseTile = std::make_unique<HouseTile>(100, 100, 7, house.get());
	house->addTile(houseTile.get());

	auto retrievedHouse = houseTile->getHouse();
	CHECK(retrievedHouse != nullptr);
	CHECK(retrievedHouse == house);
	CHECK(retrievedHouse->getId() == 500);
	CHECK(retrievedHouse.get() == house.get());
}

TEST_CASE(housetile_get_house_returns_nullptr_when_house_is_destroyed)
{
	std::unique_ptr<HouseTile> houseTile;
	{
		auto house = std::make_shared<House>(600);
		houseTile = std::make_unique<HouseTile>(101, 101, 7, house.get());
		house->addTile(houseTile.get());
		CHECK(houseTile->getHouse() != nullptr);
		CHECK(houseTile->getHouse()->getId() == 600);
	}

	CHECK(houseTile->getHouse() == nullptr);
}

TEST_CASE(container_get_item_by_index_preserves_item_lifetime)
{
	auto container = std::make_shared<Container>(0, 2);
	auto insertedItem = std::make_shared<Item>(0);
	Item* rawItem = insertedItem.get();
	CHECK(container->addItem(insertedItem));
	insertedItem.reset();

	auto item = container->getItemByIndex(0);
	CHECK(item != nullptr);
	CHECK(item.get() == rawItem);
	CHECK(container->getItemByIndex(container->size()) == nullptr);

	container->removeThing(rawItem, rawItem->getItemCount());
	CHECK(container->empty());
	CHECK(isValidItemPointer(item.get()));
	CHECK(item->getID() == 0);
}

TEST_CASE(container_iterator_preserves_item_lifetime)
{
	auto container = std::make_shared<Container>(0, 1);
	auto insertedItem = std::make_shared<Item>(0);
	Item* rawItem = insertedItem.get();
	CHECK(container->addItem(insertedItem));
	insertedItem.reset();

	std::shared_ptr<Item> item;
	{
		auto iterator = container->iterator();
		CHECK(iterator.hasNext());
		item = *iterator;
		CHECK(item.get() == rawItem);
		iterator.advance();
		CHECK(!iterator.hasNext());
		CHECK(*iterator == nullptr);
	}

	container->removeThing(rawItem, rawItem->getItemCount());
	CHECK(container->empty());
	CHECK(isValidItemPointer(item.get()));
	CHECK(item->getID() == 0);
}

TEST_CASE(lua_container_item_userdata_preserves_item_lifetime)
{
	LuaFixture fixture;
	auto container = std::make_shared<Container>(0, 1);
	auto insertedItem = std::make_shared<Item>(0);
	Item* rawItem = insertedItem.get();
	std::weak_ptr<Item> weakItem = insertedItem;
	CHECK(container->addItem(insertedItem));
	insertedItem.reset();

	Lua::pushSharedPtr(fixture.L, std::static_pointer_cast<Item>(container));
	Lua::setItemMetatable(fixture.L, -1, container.get());
	lua_setglobal(fixture.L, "lifetimeTestContainer");

	CHECK(luaL_dostring(fixture.L,
	                    "heldItem = lifetimeTestContainer:getItem(0)\n"
	                    "return heldItem:getId()") == LUA_OK);
	CHECK(lua_tointeger(fixture.L, -1) == 0);
	lua_pop(fixture.L, 1);

	container->removeThing(rawItem, rawItem->getItemCount());
	CHECK(container->empty());
	CHECK(!weakItem.expired());
	CHECK(luaL_dostring(fixture.L, "return heldItem:getId()") == LUA_OK);
	CHECK(lua_tointeger(fixture.L, -1) == 0);
	lua_pop(fixture.L, 1);

	CHECK(luaL_dostring(fixture.L,
	                    "heldItem = nil\n"
	                    "lifetimeTestContainer = nil\n"
	                    "collectgarbage('collect')\n"
	                    "collectgarbage('collect')") == LUA_OK);
	CHECK(weakItem.expired());
}

TFS_TEST_MAIN()
