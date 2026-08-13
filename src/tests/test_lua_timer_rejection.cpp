// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

// Regression tests for addEvent() when the scheduler refuses the event.
//
// The bug these cover: luaAddEvent() took a registry reference for the callback and
// one per extra parameter, then registered a scheduler event. Those references are
// released by executeTimerEvent(). When Scheduler::addEvent() returned 0 — the
// scheduler is not running, or the reactor's inbox overflowed — the timer callback
// could never run, so nothing ever released them. The references, and every game
// object the referenced userdata kept alive, leaked until shutdown.
//
// The scheduler is deliberately never started here, so addEvent() always returns 0
// and every case below exercises the rejection path.

#include "../otpch.h"

#include "../configmanager.h"
#include "../luascript.h"
#include "../scheduler.h"

#include "test_support.h"

// Declared per translation unit throughout the codebase rather than in a header.
extern LuaEnvironment g_luaEnvironment;

namespace {

// luaL_unref() pushes a freed slot onto the registry's free list and luaL_ref() pops
// from it, so a single take/release pair round-trips to the same index. Only valid as
// a leak check when exactly one reference is involved: the free list is LIFO, so a
// call that takes several references and releases them in a different order leaves
// the head on a different (still-free) slot without having leaked anything.
int sampleNextFreeRegistrySlot(lua_State* L)
{
	lua_pushboolean(L, 1);
	const int reference = luaL_ref(L, LUA_REGISTRYINDEX);
	luaL_unref(L, LUA_REGISTRYINDEX, reference);
	return reference;
}

// The leak metric for multi-reference calls. luaL_ref() only grows the registry when
// the free list is empty, so once a call has been warmed up, repeating it must not
// grow the registry at all. A call that keeps N references per invocation grows it by
// N every time.
lua_Unsigned registrySize(lua_State* L) { return lua_rawlen(L, LUA_REGISTRYINDEX); }

struct LuaFixture
{
	lua_State* L = nullptr;

	LuaFixture()
	{
		ConfigManager::setBoolean(ConfigManager::WARN_UNSAFE_SCRIPTS, false);
		ConfigManager::setBoolean(ConfigManager::CONVERT_UNSAFE_SCRIPTS, false);

		CHECK(g_luaEnvironment.initState());
		L = g_luaEnvironment.getLuaState();
		CHECK(L != nullptr);

		// addEvent() reads getScriptEnv()->getScriptId(), so a reserved environment
		// has to exist for the binding to run at all.
		CHECK(LuaScriptInterface::reserveScriptEnv());
	}

	~LuaFixture()
	{
		while (LuaScriptInterface::hasScriptEnv()) {
			LuaScriptInterface::resetScriptEnv();
		}
		g_luaEnvironment.closeState();
	}

	// Runs a chunk and leaves its single result on the stack.
	bool run(const char* chunk) { return luaL_dostring(L, chunk) == LUA_OK; }
};

} // namespace

// The scheduler must actually be refusing events, otherwise the tests below would
// pass vacuously against the success path.
TEST_CASE(test_scheduler_refuses_events_while_stopped)
{
	CHECK(g_scheduler.addEvent(0, [] {}) == 0);
}

// The core regression: a refused event must release the callback reference.
TEST_CASE(test_rejected_add_event_releases_callback_reference)
{
	LuaFixture fixture;

	const int before = sampleNextFreeRegistrySlot(fixture.L);

	CHECK(fixture.run("return addEvent(function() end, 100)"));
	CHECK(lua_isnil(fixture.L, -1)); // rejection reports nil, never an event id
	lua_pop(fixture.L, 1);

	const int after = sampleNextFreeRegistrySlot(fixture.L);
	CHECK(before == after);
}

// Extra parameters each take their own reference; all of them must come back. Six
// references per call (callback plus five parameters), so a leak here grows the
// registry by 6 on every iteration.
TEST_CASE(test_rejected_add_event_releases_parameter_references)
{
	LuaFixture fixture;
	static constexpr const char* call = "return addEvent(function() end, 100, 1, 'two', {3}, true, 5.5)";

	// Warm up so the registry has already grown to whatever this call needs; from
	// here a no-leak implementation reuses those same slots forever.
	CHECK(fixture.run(call));
	CHECK(lua_isnil(fixture.L, -1));
	lua_pop(fixture.L, 1);

	const lua_Unsigned before = registrySize(fixture.L);

	for (int i = 0; i < 32; ++i) {
		CHECK(fixture.run(call));
		CHECK(lua_isnil(fixture.L, -1)); // rejection reports nil, never an event id
		lua_pop(fixture.L, 1);
	}

	CHECK(registrySize(fixture.L) == before);
}

// Repeated rejections must not accumulate across differing shapes either.
TEST_CASE(test_repeated_rejections_do_not_accumulate_references)
{
	LuaFixture fixture;

	for (int i = 0; i < 8; ++i) {
		CHECK(fixture.run("return addEvent(function() end, 100, 1, 2, 3)"));
		lua_pop(fixture.L, 1);
	}

	const lua_Unsigned before = registrySize(fixture.L);

	for (int i = 0; i < 64; ++i) {
		CHECK(fixture.run("return addEvent(function() end, 100, 1, 2, 3)"));
		CHECK(lua_isnil(fixture.L, -1));
		lua_pop(fixture.L, 1);
	}

	CHECK(registrySize(fixture.L) == before);
}

// A rejected event must not leave an entry behind that stopEvent() could find: no id
// was ever handed out, so stopping anything must report false.
TEST_CASE(test_rejected_add_event_leaves_no_stoppable_event)
{
	LuaFixture fixture;

	CHECK(fixture.run("return addEvent(function() end, 100, 1)"));
	CHECK(lua_isnil(fixture.L, -1));
	lua_pop(fixture.L, 1);

	// The id the event would have received had it been accepted.
	CHECK(fixture.run("return stopEvent(1)"));
	CHECK(lua_isboolean(fixture.L, -1));
	CHECK(lua_toboolean(fixture.L, -1) == 0);
	lua_pop(fixture.L, 1);
}

// The argument-validation paths reject before taking any reference; they must stay
// leak-free too, and must not be confused with the scheduler-rejection path.
TEST_CASE(test_invalid_add_event_arguments_release_references)
{
	LuaFixture fixture;

	const int before = sampleNextFreeRegistrySlot(fixture.L);

	CHECK(fixture.run("return addEvent(42, 100)")); // not a function
	CHECK(lua_isboolean(fixture.L, -1));
	CHECK(lua_toboolean(fixture.L, -1) == 0);
	lua_pop(fixture.L, 1);

	CHECK(fixture.run("return addEvent(function() end)")); // missing delay
	CHECK(lua_isboolean(fixture.L, -1));
	CHECK(lua_toboolean(fixture.L, -1) == 0);
	lua_pop(fixture.L, 1);

	const int after = sampleNextFreeRegistrySlot(fixture.L);
	CHECK(before == after);
}

TFS_TEST_MAIN()
