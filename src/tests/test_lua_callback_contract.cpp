#include "../otpch.h"
#include "lua.hpp"
#include "test_support.h"

namespace {
void checkCallbackChain(bool reject)
{
	std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), &lua_close);
	CHECK(state != nullptr);
	auto* L = state.get();
	luaL_openlibs(L);
	CHECK(luaL_dostring(L, "unpack = table.unpack; RETURNVALUE_NOERROR = 0; "
	                       "isScriptsInterface = function() return true end; debugPrint = error") == LUA_OK);
	const auto data = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "data";
	CHECK(luaL_dofile(L, (data / "scripts/lib/event_callbacks.lua").string().c_str()) == LUA_OK);
	CHECK(luaL_dofile(L, (data / "scripts/eventcallbacks/creature/default_onTargetCombat.lua").string().c_str()) == LUA_OK);
	lua_pushboolean(L, reject);
	lua_setglobal(L, "auditReject");
	const int result = luaL_dostring(L,
	    "local calls = 0; local actor, target = {}, {}; local hook = Event(); "
	    "function hook.onTargetCombat(a, b) "
	    "assert(a == actor and b == target); calls = calls + 1; "
	    "return auditReject and 5 or RETURNVALUE_NOERROR end; hook:register(100); "
	    "local result = Event.onTargetCombat(actor, target); "
	    "assert(calls == 1, 'default callback swallowed the real combat hook'); "
	    "if auditReject then assert(result == 5, 'real rejection was lost') end");
	if (result != LUA_OK) throw std::runtime_error(lua_tostring(L, -1));
}
} // namespace

TEST_CASE(default_target_combat_callback_preserves_following_hooks) { checkCallbackChain(false); }
TEST_CASE(default_target_combat_callback_preserves_following_rejection) { checkCallbackChain(true); }

TFS_TEST_MAIN()
