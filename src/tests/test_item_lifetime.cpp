#include "../otpch.h"

#include "../configmanager.h"
#include "../house.h"
#include "../item.h"
#include "../luascript.h"

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
