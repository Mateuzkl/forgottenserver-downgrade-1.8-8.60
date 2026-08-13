// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

// Regression tests for LuaScriptInterface::reserveScriptEnv() bounds handling.
//
// The bug these cover: reserveScriptEnv() used to increment scriptEnvIndex first and
// report failure afterwards. No caller unwinds on the failure path, so once the stack
// was exhausted the index stayed permanently past the end of scriptEnv[], and every
// later getScriptEnv() indexed out of bounds for the rest of the process lifetime.

#include "../otpch.h"

#include "../luascript.h"

#include "test_support.h"

namespace {

// The environment stack is process-global static state shared with every other test
// in this binary, so each case must leave it exactly as it found it.
int unwindToEmpty()
{
	int released = 0;
	while (LuaScriptInterface::hasScriptEnv()) {
		LuaScriptInterface::resetScriptEnv();
		++released;
	}
	return released;
}

} // namespace

TEST_CASE(test_reserve_script_env_allows_exactly_the_declared_depth)
{
	unwindToEmpty();
	CHECK(!LuaScriptInterface::hasScriptEnv());

	int reserved = 0;
	while (LuaScriptInterface::reserveScriptEnv()) {
		++reserved;
		CHECK(reserved <= LuaScriptInterface::SCRIPT_ENV_COUNT);
	}

	CHECK(reserved == LuaScriptInterface::SCRIPT_ENV_COUNT);
	CHECK(unwindToEmpty() == LuaScriptInterface::SCRIPT_ENV_COUNT);
}

// The core regression: a failed reservation must not move the index.
TEST_CASE(test_failed_reserve_does_not_advance_the_index)
{
	unwindToEmpty();

	for (int i = 0; i < LuaScriptInterface::SCRIPT_ENV_COUNT; ++i) {
		CHECK(LuaScriptInterface::reserveScriptEnv());
	}

	ScriptEnvironment* const innermost = LuaScriptInterface::getScriptEnv();
	CHECK(LuaScriptInterface::hasScriptEnv());

	// Repeated failures must all be no-ops. Before the fix each one pushed the index
	// further past the end of the array.
	for (int attempt = 0; attempt < 8; ++attempt) {
		CHECK(!LuaScriptInterface::reserveScriptEnv());
		CHECK(LuaScriptInterface::hasScriptEnv());
		CHECK(LuaScriptInterface::getScriptEnv() == innermost);
	}

	CHECK(unwindToEmpty() == LuaScriptInterface::SCRIPT_ENV_COUNT);
	CHECK(!LuaScriptInterface::hasScriptEnv());
}

// After the stack unwinds, later script calls must behave normally. This is what the
// original bug broke permanently.
TEST_CASE(test_reservation_works_again_after_exhaustion_and_unwind)
{
	unwindToEmpty();

	while (LuaScriptInterface::reserveScriptEnv()) {
	}
	CHECK(!LuaScriptInterface::reserveScriptEnv());
	unwindToEmpty();

	CHECK(!LuaScriptInterface::hasScriptEnv());
	CHECK(LuaScriptInterface::reserveScriptEnv());
	CHECK(LuaScriptInterface::hasScriptEnv());

	ScriptEnvironment* const first = LuaScriptInterface::getScriptEnv();
	CHECK(first != nullptr);

	CHECK(LuaScriptInterface::reserveScriptEnv());
	CHECK(LuaScriptInterface::getScriptEnv() == first + 1);

	LuaScriptInterface::resetScriptEnv();
	CHECK(LuaScriptInterface::getScriptEnv() == first);
	LuaScriptInterface::resetScriptEnv();
	CHECK(!LuaScriptInterface::hasScriptEnv());
}

// Nested reserve/reset must round-trip to the same slot at every depth.
TEST_CASE(test_nested_reserve_and_reset_round_trips)
{
	unwindToEmpty();

	std::vector<ScriptEnvironment*> seen;
	for (int i = 0; i < LuaScriptInterface::SCRIPT_ENV_COUNT; ++i) {
		CHECK(LuaScriptInterface::reserveScriptEnv());
		seen.push_back(LuaScriptInterface::getScriptEnv());
	}

	// Every depth is a distinct slot, in ascending order.
	for (size_t i = 1; i < seen.size(); ++i) {
		CHECK(seen[i] == seen[i - 1] + 1);
	}

	for (size_t i = seen.size(); i-- > 0;) {
		CHECK(LuaScriptInterface::getScriptEnv() == seen[i]);
		LuaScriptInterface::resetScriptEnv();
	}

	CHECK(!LuaScriptInterface::hasScriptEnv());
}

TFS_TEST_MAIN()
