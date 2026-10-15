// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../configmanager.h"
#include "../condition.h"
#include "../container.h"
#include "../kv/kv.h"
#include "../luascript.h"
#include "../networkmessage.h"
#include "../spells.h"
#include "../teleport.h"
#include "test_support.h"

extern LuaEnvironment g_luaEnvironment;

namespace {

struct LuaFixture
{
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

class CountingSpell final : public Spell
{
public:
	explicit CountingSpell(std::shared_ptr<int> alive) : alive(std::move(alive)) { ++*this->alive; }
	~CountingSpell() override { --*alive; }
	bool castSpell(Creature*) override { return false; }
	bool castSpell(Creature*, Creature*) override { return false; }
	bool isInstant() const override { return true; }

private:
	std::shared_ptr<int> alive;
};

void pushOwnedSpell(lua_State* L, const std::shared_ptr<int>& alive)
{
	Lua::pushOwnedUserdataAs<Spell>(L, std::make_unique<CountingSpell>(alive));
	Lua::setMetatable(L, -1, "Spell");
}

} // namespace

TEST_CASE(unregistered_owned_spell_is_destroyed_by_lua_gc)
{
	auto alive = std::make_shared<int>(0);
	{
		LuaFixture fixture;
		for (int i = 0; i < 100; ++i) {
			pushOwnedSpell(fixture.L, alive);
			lua_pop(fixture.L, 1);
		}
		CHECK(luaL_dostring(fixture.L, "collectgarbage('collect'); collectgarbage('collect')") == LUA_OK);
	}
	CHECK(*alive == 0);
}

TEST_CASE(spell_gc_does_not_delete_borrowed_registry_objects)
{
	LuaFixture fixture;
	auto alive = std::make_shared<int>(0);
	auto owner = std::make_unique<CountingSpell>(alive);
	owner->setName("borrowed spell");
	Lua::pushUserdata<Spell>(fixture.L, owner.get());
	Lua::setMetatable(fixture.L, -1, "Spell");
	lua_setglobal(fixture.L, "borrowedSpell");
	CHECK(luaL_dostring(fixture.L,
	                    "local gc = rawgetmetatable('Spell').__gc\n"
	                    "assert(type(gc) == 'function')\n"
	                    "gc(borrowedSpell); gc(borrowedSpell)\n"
	                    "assert(borrowedSpell:name() == 'borrowed spell')\n"
	                    "borrowedSpell = nil; collectgarbage('collect')") == LUA_OK);
	CHECK(*alive == 1);
	owner.reset();
	CHECK(*alive == 0);
}

TEST_CASE(transferred_spell_owner_survives_lua_finalization)
{
	LuaFixture fixture;
	auto alive = std::make_shared<int>(0);
	pushOwnedSpell(fixture.L, alive);
	auto owner = Lua::releaseOwnedUserdataPtr<Spell>(fixture.L, -1);
	CHECK(owner != nullptr);
	lua_setglobal(fixture.L, "transferredSpell");
	CHECK(luaL_dostring(fixture.L,
	                    "local gc = rawgetmetatable('Spell').__gc\n"
	                    "assert(type(gc) == 'function')\n"
	                    "gc(transferredSpell); gc(transferredSpell)\n"
	                    "assert(transferredSpell:name() == nil)\n"
	                    "transferredSpell = nil; collectgarbage('collect')") == LUA_OK);
	CHECK(*alive == 1);
	owner.reset();
	CHECK(*alive == 0);
}

TEST_CASE(spell_close_releases_owned_object_once_with_live_lua_alias)
{
	LuaFixture fixture;
	auto alive = std::make_shared<int>(0);
	pushOwnedSpell(fixture.L, alive);
	lua_setglobal(fixture.L, "ownedSpell");
	CHECK(luaL_dostring(fixture.L,
	                    "do local s <close> = ownedSpell end\n"
	                    "assert(ownedSpell:name() == nil)\n"
	                    "local gc = rawgetmetatable('Spell').__gc\n"
	                    "gc(ownedSpell); gc(ownedSpell)\n"
	                    "ownedSpell = nil; collectgarbage('collect')") == LUA_OK);
	CHECK(*alive == 0);
}

TEST_CASE(actual_instant_and_rune_owned_layouts_support_close)
{
	LuaFixture fixture;
	Lua::pushOwnedUserdataAs<Spell>(fixture.L, std::make_unique<InstantSpell>(nullptr));
	Lua::setMetatable(fixture.L, -1, "Spell");
	lua_setglobal(fixture.L, "instant");
	Lua::pushOwnedUserdataAs<Spell>(fixture.L, std::make_unique<RuneSpell>(nullptr));
	Lua::setMetatable(fixture.L, -1, "Spell");
	lua_setglobal(fixture.L, "rune");
	CHECK(luaL_dostring(fixture.L,
	                    "do local a <close> = instant; local b <close> = rune end\n"
	                    "assert(instant:name() == nil and rune:name() == nil)\n"
	                    "instant = nil; rune = nil; collectgarbage('collect')") == LUA_OK);
}

TEST_CASE(item_and_derived_metatables_have_idempotent_shared_item_gc)
{
	LuaFixture fixture;
	for (int kind = 0; kind < 3; ++kind) {
		std::shared_ptr<Item> item;
		const char* metatable;
		if (kind == 0) {
			item = std::make_shared<Item>(0);
			metatable = "Item";
		} else if (kind == 1) {
			item = std::make_shared<Container>(0, 1);
			metatable = "Container";
		} else {
			item = std::make_shared<Teleport>(0);
			metatable = "Teleport";
		}
		std::weak_ptr<Item> weak = item;
		CHECK(Lua::pushItem(fixture.L, item.get()));
		lua_setglobal(fixture.L, "heldItem");
		lua_pushstring(fixture.L, metatable);
		lua_setglobal(fixture.L, "heldItemClass");
		item.reset();
		CHECK(weak.use_count() == 1);
		CHECK(luaL_dostring(fixture.L,
		                    "assert(heldItem:getId() == 0)\n"
		                    "local gc = rawgetmetatable(heldItemClass).__gc\n"
		                    "gc(heldItem)\n"
		                    "assert(heldItem:getId() == nil)\n"
		                    "gc(heldItem)") == LUA_OK);
		CHECK(weak.expired());
		lua_getglobal(fixture.L, "heldItem");
		CHECK(Lua::getThing(fixture.L, lua_gettop(fixture.L)) == nullptr);
		lua_pop(fixture.L, 1);
		CHECK(luaL_dostring(fixture.L,
		                    "heldItem = nil\n"
		                    "collectgarbage('collect'); collectgarbage('collect')") == LUA_OK);
	}
}

TEST_CASE(scoped_kv_typed_gc_releases_owner_and_rejects_later_access)
{
	LuaFixture fixture;
	std::shared_ptr<KV> scope = std::make_shared<ScopedKV>(KVStore::getInstance(), "gc-audit");
	std::weak_ptr<KV> weak = scope;
	Lua::pushSharedPtr(fixture.L, std::move(scope));
	Lua::setMetatable(fixture.L, -1, "KV");
	lua_setglobal(fixture.L, "heldKv");
	CHECK(luaL_dostring(fixture.L,
	                    "local gc = rawgetmetatable('KV').__gc\n"
	                    "gc(heldKv); gc(heldKv)\n"
	                    "assert(not pcall(function() heldKv:keys() end))\n"
	                    "heldKv = nil; collectgarbage('collect')") == LUA_OK);
	CHECK(weak.expired());
}

TEST_CASE(network_message_callback_style_shared_owner_survives_lua_gc)
{
	LuaFixture fixture;
	auto owner = std::make_shared<NetworkMessage>();
	std::weak_ptr<NetworkMessage> weak = owner;
	Lua::pushSharedPtr(fixture.L, owner);
	Lua::setMetatable(fixture.L, -1, "NetworkMessage");
	lua_setglobal(fixture.L, "heldMessage");
	CHECK(owner.use_count() == 2);
	CHECK(luaL_dostring(fixture.L,
	                    "do local message <close> = heldMessage end\n"
	                    "local gc = rawgetmetatable('NetworkMessage').__gc\n"
	                    "gc(heldMessage); gc(heldMessage)\n"
	                    "heldMessage = nil; collectgarbage('collect')") == LUA_OK);
	CHECK(owner.use_count() == 1);
	owner->addByte(0x42);
	CHECK(owner->getLength() == 1);
	owner.reset();
	CHECK(weak.expired());
}

TEST_CASE(condition_clone_delete_close_and_gc_remain_compatible)
{
	LuaFixture fixture;
	CHECK(luaL_dostring(fixture.L,
	                    "for i = 1, 1000 do\n"
	                    "  local c <close> = Condition(CONDITION_INFIGHT)\n"
	                    "  c:setTicks(1234)\n"
	                    "  local copy = c:clone()\n"
	                    "  assert(copy:getTicks() == 1234)\n"
	                    "  copy:delete(); copy:delete()\n"
	                    "end\n"
	                    "collectgarbage('collect'); collectgarbage('collect')") == LUA_OK);
}

TEST_CASE(owned_deleter_ignores_non_userdata_and_short_borrowed_slots)
{
	LuaFixture fixture;
	lua_pushinteger(fixture.L, 42);
	CHECK(Lua::deleteOwnedUserdata(fixture.L) == 0);
	lua_pop(fixture.L, 1);
	auto owner = std::make_unique<CountingSpell>(std::make_shared<int>(0));
	Lua::pushUserdata<Spell>(fixture.L, owner.get(), 0);
	CHECK(Lua::deleteOwnedUserdata(fixture.L) == 0);
	CHECK(Lua::getRawUserdata<Spell>(fixture.L, 1, false) != nullptr);
	lua_pop(fixture.L, 1);
}

TFS_TEST_MAIN()
