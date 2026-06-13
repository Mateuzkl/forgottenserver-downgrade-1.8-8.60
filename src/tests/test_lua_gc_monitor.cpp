// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../lua_gc_monitor.h"

#include "test_support.h"

TEST_CASE(test_lua_gc_monitor_basic)
{
	auto L = luaL_newstate();
	CHECK(L != nullptr);
	luaL_openlibs(L);

	LuaGcMonitor::configure(L);

	auto memoryKb = LuaGcMonitor::getMemoryKb(L);
	(void)memoryKb;

	auto memoryMb = LuaGcMonitor::getMemoryMb(L);
	(void)memoryMb;

	LuaGcMonitor::step(L);

	CHECK(LuaGcMonitor::getModeName(LUA_GCGEN) == "generational");
	CHECK(LuaGcMonitor::getModeName(LUA_GCINC) == "incremental");
	CHECK(LuaGcMonitor::getModeName(LUA_GCSTOP) == "stopped");
	CHECK(LuaGcMonitor::getModeName(999) == "unknown");

	lua_close(L);
}

TEST_CASE(test_lua_gc_monitor_null_safety)
{
	LuaGcMonitor::configure(nullptr);
	LuaGcMonitor::step(nullptr);
	LuaGcMonitor::fullCollect(nullptr);
	CHECK(LuaGcMonitor::getMemoryKb(nullptr) == 0);
	CHECK(LuaGcMonitor::getMemoryMb(nullptr) == 0.0);
	LuaGcMonitor::logIfNeeded(nullptr);
}

TEST_CASE(test_lua_gc_monitor_memory)
{
	auto L = luaL_newstate();
	CHECK(L != nullptr);
	luaL_openlibs(L);
	LuaGcMonitor::configure(L);

	auto before = LuaGcMonitor::getMemoryKb(L);

	lua_newtable(L);
	for (int i = 0; i < 1000; ++i) {
		lua_pushinteger(L, i);
		lua_pushinteger(L, i * 2);
		lua_settable(L, -3);
	}
	lua_setglobal(L, "testTable");

	auto after = LuaGcMonitor::getMemoryKb(L);
	CHECK(after >= before);

	lua_pushnil(L);
	lua_setglobal(L, "testTable");
	LuaGcMonitor::fullCollect(L);

	lua_close(L);
}

TEST_CASE(test_lua_gc_monitor_full_collect)
{
	auto L = luaL_newstate();
	CHECK(L != nullptr);
	luaL_openlibs(L);
	LuaGcMonitor::configure(L);

	LuaGcMonitor::fullCollect(L, "test_full_collect");

	lua_close(L);
}

TFS_TEST_MAIN()
