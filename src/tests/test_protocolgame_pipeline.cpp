// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../networkmessage.h"
#include "../packet_backlog.h"
#include "../position.h"
#include "../reactor.h"

#include "test_support.h"

TEST_CASE(network_message_copy_preserves_parser_state)
{
	NetworkMessage original;
	original.addByte(0xA1);
	original.add<uint32_t>(0x12345678);
	original.addString("packet payload");
	CHECK(original.setBufferPosition(0));
	CHECK(original.getByte() == 0xA1);

	auto copy = tfs::net::make_network_message(original);
	CHECK(copy->getLength() == original.getLength());
	CHECK(copy->getBufferPosition() == original.getBufferPosition());
	CHECK(copy->isOverrun() == original.isOverrun());
	CHECK(std::memcmp(copy->getBuffer(), original.getBuffer(), NETWORKMESSAGE_MAXSIZE) == 0);

	original.getBuffer()[NetworkMessage::INITIAL_BUFFER_POSITION] = 0xFF;
	CHECK(copy->getBuffer()[NetworkMessage::INITIAL_BUFFER_POSITION] == 0xA1);

	CHECK(original.setBufferPosition(original.getLength()));
	(void)original.get<uint32_t>();
	CHECK(original.isOverrun());
	auto overrunCopy = tfs::net::make_network_message(original);
	CHECK(overrunCopy->isOverrun());
	CHECK(overrunCopy->getBufferPosition() == original.getBufferPosition());
}

TEST_CASE(packet_backlog_limits_copies_and_disconnects_once)
{
	tfs::net::PacketBacklog backlog(2);
	uint32_t copies = 0;
	uint32_t disconnectRequests = 0;

	auto first = backlog.tryAcquire();
	if (first) {
		++copies;
	}
	auto second = backlog.tryAcquire();
	if (second) {
		++copies;
	}
	auto overflow = backlog.tryAcquire();
	if (overflow) {
		++copies;
	}
	disconnectRequests += overflow.requestDisconnect ? 1 : 0;
	auto rejected = backlog.tryAcquire();
	if (rejected) {
		++copies;
	}
	disconnectRequests += rejected.requestDisconnect ? 1 : 0;

	CHECK(first);
	CHECK(second);
	CHECK(!overflow);
	CHECK(overflow.observedPending == 3);
	CHECK(!rejected);
	CHECK(copies == 2);
	CHECK(disconnectRequests == 1);
	CHECK(backlog.current() == 2);
	CHECK(backlog.floodTriggered());

	first.ticket = {};
	second.ticket = {};
	CHECK(backlog.current() == 0);
	CHECK(!backlog.tryAcquire());
}

TEST_CASE(packet_backlog_ticket_releases_when_task_is_discarded)
{
	tfs::net::PacketBacklog backlog(4);
	auto admission = backlog.tryAcquire();
	CHECK(admission);
	CHECK(backlog.current() == 1);

	ReactorCallback discardedTask = [ticket = std::move(admission.ticket)]() mutable { (void)ticket; };
	discardedTask = {};
	CHECK(backlog.current() == 0);
}

TEST_CASE(packet_pipeline_preserves_connection_order)
{
	tfs::net::PacketBacklog backlog(3);
	TaskReactor reactor;
	reactor.start();
	std::vector<int> processed;

	for (int direction : {DIRECTION_NORTH, DIRECTION_EAST, DIRECTION_SOUTH}) {
		auto admission = backlog.tryAcquire();
		CHECK(admission);
		CHECK(reactor.send([ticket = std::move(admission.ticket), direction, &processed]() mutable {
			(void)ticket;
			processed.push_back(direction);
		}));
	}

	CHECK(backlog.current() == 3);
	reactor.runOnce();
	CHECK(processed == std::vector<int>({DIRECTION_NORTH, DIRECTION_EAST, DIRECTION_SOUTH}));
	CHECK(backlog.current() == 0);
	reactor.shutdown();
}

TEST_CASE(packet_expiration_policy_matches_legacy_gameplay_actions)
{
	for (uint8_t opcode : {0x6F, 0x72, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7E, 0x82, 0x83,
	                       0x84, 0x85, 0x8B, 0x8C, 0x8D, 0xCB, 0xCC}) {
		CHECK(tfs::net::shouldExpireQueuedGamePacket(opcode));
	}

	for (uint8_t opcode : {0x14, 0x1E, 0x32, 0x40, 0x64, 0x65, 0x7D, 0x7F, 0x80, 0x96}) {
		CHECK(!tfs::net::shouldExpireQueuedGamePacket(opcode));
	}
}

TFS_TEST_MAIN()
