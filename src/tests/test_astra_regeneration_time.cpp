// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../protocolgame.h"

#include "test_support.h"

struct ProtocolGameAstraRegenerationTestAccess
{
	static uint16_t seconds(int32_t ticks) { return ProtocolGame::getRegenerationTimeSeconds(ticks); }
};

TEST_CASE(astra_regeneration_time_converts_food_ticks_to_seconds)
{
	CHECK(ProtocolGameAstraRegenerationTestAccess::seconds(0) == 0);
	CHECK(ProtocolGameAstraRegenerationTestAccess::seconds(999) == 0);
	CHECK(ProtocolGameAstraRegenerationTestAccess::seconds(123'000) == 123);
}

TEST_CASE(astra_regeneration_time_handles_infinite_and_wire_limit)
{
	CHECK(ProtocolGameAstraRegenerationTestAccess::seconds(-1) == (std::numeric_limits<uint16_t>::max)());
	CHECK(ProtocolGameAstraRegenerationTestAccess::seconds(70'000'000) ==
	      (std::numeric_limits<uint16_t>::max)());
}

TFS_TEST_MAIN()
