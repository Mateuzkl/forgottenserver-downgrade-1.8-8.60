// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../networkmessage.h"
#include "../supply_stash_protocol.h"

#include "test_support.h"

#include <limits>

using namespace tfs::supply_stash;

namespace {

// Rewinds to the start of the body, which is where a request always begins.
void rewind(NetworkMessage& msg) { CHECK(msg.setBufferPosition(0)); }

NetworkMessage stowItemMessage(uint16_t itemId, uint32_t count)
{
	NetworkMessage msg;
	msg.addByte(static_cast<uint8_t>(Action::StowItem));
	msg.addPosition(Position(0xFFFF, 0x40 | 3, 5));
	msg.add<uint16_t>(itemId);
	msg.addByte(7);
	msg.add<uint32_t>(count);
	rewind(msg);
	return msg;
}

NetworkMessage withdrawMessage(uint16_t itemId, uint32_t amount, uint8_t tier)
{
	NetworkMessage msg;
	msg.addByte(static_cast<uint8_t>(Action::Withdraw));
	msg.add<uint16_t>(itemId);
	msg.add<uint32_t>(amount);
	msg.addByte(tier);
	rewind(msg);
	return msg;
}

} // namespace

TEST_CASE(supply_stash_parses_stow_item)
{
	auto msg = stowItemMessage(2160, 30);
	const auto result = parseRequest(msg);

	CHECK(static_cast<bool>(result));
	CHECK(result.request.action == Action::StowItem);
	CHECK(result.request.itemId == 2160);
	CHECK(result.request.count == 30);
	CHECK(result.request.stackpos == 7);
	// Container positions arrive as (0xFFFF, containerId | 0x40, slot), so the
	// slot index rides in z and must survive the round trip intact.
	CHECK(result.request.position.x == 0xFFFF);
	CHECK(result.request.position.y == (0x40 | 3));
	CHECK(result.request.position.z == 5);
}

TEST_CASE(supply_stash_parses_stow_container_without_count)
{
	NetworkMessage msg;
	msg.addByte(static_cast<uint8_t>(Action::StowContainer));
	msg.addPosition(Position(100, 200, 7));
	msg.add<uint16_t>(1988);
	msg.addByte(1);
	rewind(msg);

	const auto result = parseRequest(msg);

	CHECK(static_cast<bool>(result));
	CHECK(result.request.action == Action::StowContainer);
	CHECK(result.request.itemId == 1988);
	CHECK(result.request.count == 0);
}

TEST_CASE(supply_stash_parses_stow_stack)
{
	NetworkMessage msg;
	msg.addByte(static_cast<uint8_t>(Action::StowStack));
	msg.addPosition(Position(1, 2, 3));
	msg.add<uint16_t>(2160);
	msg.addByte(0);
	rewind(msg);

	const auto result = parseRequest(msg);

	CHECK(static_cast<bool>(result));
	CHECK(result.request.action == Action::StowStack);
}

TEST_CASE(supply_stash_parses_withdraw_with_tier)
{
	auto msg = withdrawMessage(2160, 100, 3);
	const auto result = parseRequest(msg);

	CHECK(static_cast<bool>(result));
	CHECK(result.request.action == Action::Withdraw);
	CHECK(result.request.itemId == 2160);
	CHECK(result.request.count == 100);
	CHECK(result.request.tier == 3);
}

// §17: a truncated packet must be rejected, not read past the end. Reads beyond
// the body return 0 and set the overrun flag rather than throwing, so the parser
// has to consult it instead of trusting the values.
TEST_CASE(supply_stash_rejects_truncated_packet)
{
	NetworkMessage msg;
	msg.addByte(static_cast<uint8_t>(Action::StowItem));
	msg.addPosition(Position(1, 2, 3));
	msg.add<uint16_t>(2160);
	// stackpos and count are missing
	rewind(msg);

	const auto result = parseRequest(msg);

	CHECK(!static_cast<bool>(result));
	CHECK(result.error == ParseError::Truncated);
}

TEST_CASE(supply_stash_rejects_empty_packet)
{
	NetworkMessage msg;
	rewind(msg);

	const auto result = parseRequest(msg);

	CHECK(!static_cast<bool>(result));
	CHECK(result.error == ParseError::Truncated);
}

TEST_CASE(supply_stash_rejects_unknown_action)
{
	NetworkMessage msg;
	msg.addByte(0x7F);
	rewind(msg);

	const auto result = parseRequest(msg);

	CHECK(!static_cast<bool>(result));
	CHECK(result.error == ParseError::UnknownAction);
}

TEST_CASE(supply_stash_rejects_zero_item_id)
{
	auto msg = stowItemMessage(0, 30);
	const auto result = parseRequest(msg);

	CHECK(!static_cast<bool>(result));
	CHECK(result.error == ParseError::InvalidItemId);
}

TEST_CASE(supply_stash_rejects_zero_count)
{
	auto msg = stowItemMessage(2160, 0);
	const auto result = parseRequest(msg);

	CHECK(!static_cast<bool>(result));
	CHECK(result.error == ParseError::InvalidCount);
}

TEST_CASE(supply_stash_rejects_oversized_count)
{
	auto msg = stowItemMessage(2160, MAX_STOW_COUNT + 1);
	const auto result = parseRequest(msg);

	CHECK(!static_cast<bool>(result));
	CHECK(result.error == ParseError::InvalidCount);
}

TEST_CASE(supply_stash_accepts_count_at_the_cap)
{
	auto msg = stowItemMessage(2160, MAX_STOW_COUNT);
	const auto result = parseRequest(msg);

	CHECK(static_cast<bool>(result));
	CHECK(result.request.count == MAX_STOW_COUNT);
}

TEST_CASE(supply_stash_rejects_zero_withdraw_amount)
{
	auto msg = withdrawMessage(2160, 0, 0);
	const auto result = parseRequest(msg);

	CHECK(!static_cast<bool>(result));
	CHECK(result.error == ParseError::InvalidCount);
}

TEST_CASE(supply_stash_rejects_tier_above_maximum)
{
	auto msg = withdrawMessage(2160, 10, Stash::MAX_ITEM_TIER + 1);
	const auto result = parseRequest(msg);

	CHECK(!static_cast<bool>(result));
	CHECK(result.error == ParseError::InvalidTier);
}

TEST_CASE(supply_stash_accepts_tier_at_the_maximum)
{
	auto msg = withdrawMessage(2160, 10, Stash::MAX_ITEM_TIER);
	const auto result = parseRequest(msg);

	CHECK(static_cast<bool>(result));
	CHECK(result.request.tier == Stash::MAX_ITEM_TIER);
}

// §17: extra bytes mean the two sides disagree about the layout. Accepting them
// would leave the remainder to be read as though it were the next opcode.
TEST_CASE(supply_stash_rejects_trailing_bytes)
{
	NetworkMessage msg;
	msg.addByte(static_cast<uint8_t>(Action::Withdraw));
	msg.add<uint16_t>(2160);
	msg.add<uint32_t>(10);
	msg.addByte(0);
	msg.addByte(0xAB); // one byte too many
	rewind(msg);

	const auto result = parseRequest(msg);

	CHECK(!static_cast<bool>(result));
	CHECK(result.error == ParseError::TrailingBytes);
}

// The action numbering changed: the old Lua used OPEN = 1, STOW_ALL = 2,
// WITHDRAW = 3, so 1 and 2 now mean StowContainer and StowStack. A client that
// has not been updated must be refused, never reinterpreted as a command that
// moves items. These three pin that down.
TEST_CASE(supply_stash_rejects_legacy_open_request)
{
	NetworkMessage msg;
	msg.addByte(1); // old ACTION_OPEN, now StowContainer, which needs 8 more bytes
	rewind(msg);

	const auto result = parseRequest(msg);

	CHECK(!static_cast<bool>(result));
	CHECK(result.error == ParseError::Truncated);
}

TEST_CASE(supply_stash_rejects_legacy_stow_all_request)
{
	NetworkMessage msg;
	msg.addByte(2); // old ACTION_STOW_ALL, now StowStack
	rewind(msg);

	const auto result = parseRequest(msg);

	CHECK(!static_cast<bool>(result));
	CHECK(result.error == ParseError::Truncated);
}

TEST_CASE(supply_stash_rejects_legacy_withdraw_without_tier)
{
	// The old withdraw sent the tier byte only when it had one.
	NetworkMessage msg;
	msg.addByte(3);
	msg.add<uint16_t>(2160);
	msg.add<uint32_t>(10);
	rewind(msg);

	const auto result = parseRequest(msg);

	CHECK(!static_cast<bool>(result));
	CHECK(result.error == ParseError::Truncated);
}

TEST_CASE(supply_stash_serializes_contents_tier_aware)
{
	std::vector<StashRecord> rows{
	    StashRecord{2160, 0, 500},
	    StashRecord{2160, 3, 25},
	    StashRecord{2148, 0, 100000},
	};

	NetworkMessage msg;
	CHECK(serializeContents(msg, rows, 42));
	rewind(msg);

	CHECK(msg.get<uint16_t>() == 3);

	CHECK(msg.get<uint16_t>() == 2160);
	CHECK(msg.get<uint32_t>() == 500);
	CHECK(msg.getByte() == 0);

	// Same itemId, different tier: these must stay distinct rows on the wire.
	CHECK(msg.get<uint16_t>() == 2160);
	CHECK(msg.get<uint32_t>() == 25);
	CHECK(msg.getByte() == 3);

	CHECK(msg.get<uint16_t>() == 2148);
	CHECK(msg.get<uint32_t>() == 100000);
	CHECK(msg.getByte() == 0);

	CHECK(msg.get<uint16_t>() == 42);
	CHECK(!msg.isOverrun());
}

TEST_CASE(supply_stash_serializes_empty_contents)
{
	NetworkMessage msg;
	CHECK(serializeContents(msg, {}, 1000));
	rewind(msg);

	CHECK(msg.get<uint16_t>() == 0);
	CHECK(msg.get<uint16_t>() == 1000);
	CHECK(!msg.isOverrun());
}

// Both bounds have to be refused before anything is written. A partial payload is
// worse than none: the count promises rows that never arrive, so the receiver
// reads row bytes as freeSlots and every following packet is misaligned.
TEST_CASE(supply_stash_serializes_at_the_row_limit)
{
	std::vector<StashRecord> rows(maxSerializableRows(), StashRecord{2160, 0, 1});

	NetworkMessage msg;
	CHECK(serializeContents(msg, rows, 0));
	rewind(msg);
	CHECK(msg.get<uint16_t>() == static_cast<uint16_t>(maxSerializableRows()));
	CHECK(!msg.isOverrun());
}

TEST_CASE(supply_stash_refuses_one_row_over_the_limit)
{
	std::vector<StashRecord> rows(maxSerializableRows() + 1, StashRecord{2160, 0, 1});

	NetworkMessage msg;
	CHECK(!serializeContents(msg, rows, 0));
	// Nothing may have been emitted.
	CHECK(msg.getLength() == 0);
}

// The message body runs out before the 16-bit count does, so that is the bound
// that actually applies.
TEST_CASE(supply_stash_row_limit_is_bounded_by_the_message_not_the_count_field)
{
	CHECK(maxSerializableRows() < std::numeric_limits<uint16_t>::max());
	// comfortably above the 1000-row cap the stash itself enforces
	CHECK(maxSerializableRows() > 1000);
}

TFS_TEST_MAIN()
