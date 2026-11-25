// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../connection.h"
#include "../game.h"
#include "../networkmessage.h"
#include "../outputmessage.h"
#include "../packet_backlog.h"
#include "../performance_metrics.h"
#include "../player.h"
#include "../position.h"
#include "../protocolgame.h"
#include "../reactor.h"
#include "../scheduler.h"
#include "../tasks.h"

#include "protocolgame_audit_support.h"
#include "test_support.h"

#include <cstring>
#include <thread>

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

TEST_CASE(network_message_invalid_latin1_string_keeps_packet_framing)
{
	NetworkMessage message;
	message.addByte(0xB4);
	message.addByte(0x12);
	message.addString(std::string("\xF0\x9F\x98\x80", 4));

	CHECK(message.getLength() == 4);
	CHECK(message.setBufferPosition(0));
	CHECK(message.getByte() == 0xB4);
	CHECK(message.getByte() == 0x12);
	CHECK(message.get<uint16_t>() == 0);
	CHECK(!message.isOverrun());
}

TEST_CASE(network_message_oversized_string_keeps_packet_framing)
{
	NetworkMessage message;
	message.addString(std::string(8193, 'x'));

	CHECK(message.getLength() == 2);
	CHECK(message.setBufferPosition(0));
	CHECK(message.get<uint16_t>() == 0);
	CHECK(!message.isOverrun());
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

TEST_CASE(clone_incoming_packet_preserves_active_payload_and_parser_state)
{
	NetworkMessage original;
	original.addByte(0x65);
	original.add<uint32_t>(0xCAFEBABE);
	original.addString("exura");
	CHECK(original.setBufferPosition(0));
	CHECK(original.getByte() == 0x65);

	auto cloned = tfs::net::clone_incoming_packet(original);
	CHECK(cloned->getLength() == original.getLength());
	CHECK(cloned->getBufferPosition() == original.getBufferPosition());
	CHECK(!cloned->isOverrun());
	CHECK(cloned->get<uint32_t>() == 0xCAFEBABE);
	CHECK(cloned->getString() == "exura");
	CHECK(!cloned->isOverrun());
	(void)cloned->getByte();
	CHECK(cloned->isOverrun());
}

TEST_CASE(packet_rate_limiter_enforces_monotonic_token_bucket_without_boundary_bursts)
{
	tfs::net::PacketRateLimiter limiter;
	const auto t0 = tfs::net::PacketRateLimiter::Clock::now();

	// Unlimited when <= 0
	for (int i = 0; i < 200; ++i) {
		CHECK(limiter.allow(t0, 0));
		CHECK(limiter.allow(t0, -5));
	}

	// Fresh limiter with max 10 packets/sec: allows burst of 10, rejects 11th immediately.
	tfs::net::PacketRateLimiter strict;
	for (int i = 0; i < 10; ++i) {
		CHECK(strict.allow(t0, 10));
	}
	CHECK(!strict.allow(t0, 10));
	// 50ms later only 0.5 token refilled -> still rejected.
	CHECK(!strict.allow(t0 + std::chrono::milliseconds(50), 10));
	// 100ms later 1 full token refilled -> allows exactly 1, then rejects.
	CHECK(strict.allow(t0 + std::chrono::milliseconds(100), 10));
	CHECK(!strict.allow(t0 + std::chrono::milliseconds(100), 10));
	// Full 1000ms later -> refills full bucket of 10.
	for (int i = 0; i < 10; ++i) {
		CHECK(strict.allow(t0 + std::chrono::milliseconds(1100), 10));
	}
	CHECK(!strict.allow(t0 + std::chrono::milliseconds(1100), 10));
}

// Regression for Points 6 & 7: exercises the REAL ProtocolGame::parsePacket() ->
// incomingPackets -> drainIncomingPacketsOnDispatcher() -> RescheduleGuard ->
// parsePacketOnDispatcher() pipeline for fairness, queue-latency metrics, expiration,
// release cleanup, and immediate backlog overflow disconnect.
TEST_CASE(real_protocolgame_pipeline_fairness_metrics_expiration_and_overflow)
{
	auto makeNewPingMsg = [](uint32_t pingId) {
		NetworkMessage msg;
		msg.addByte(0x40);
		msg.add<uint32_t>(pingId);
		CHECK(msg.setBufferPosition(0));
		return msg;
	};
	auto makeTurnMsg = []() {
		NetworkMessage msg;
		msg.addByte(0x6F); // expiring gameplay opcode (turn north)
		CHECK(msg.setBufferPosition(0));
		return msg;
	};

	const auto prevGameState = g_game.getGameState();
	const bool prevMetrics = g_performanceMetrics.isEnabled();
	// ioContext must outlive every Connection held by ConnectionManager::closeAll().
	asio::io_context ioContext;
	struct Cleanup
	{
		GameState_t prevState;
		bool prevMetricsEnabled;
		~Cleanup()
		{
			g_reactor.setPostCycleHook(nullptr);
			g_reactor.setMaxTasksPerCycle(0);
			g_performanceMetrics.setEnabled(prevMetricsEnabled);
			g_game.setGameState(prevState);
			g_scheduler.shutdown();
			g_dispatcher.shutdown();
			g_reactor.drain();
			ConnectionManager::getInstance().closeAll();
		}
	} cleanup{prevGameState, prevMetrics};

	// Drive exactly one TaskReactor cycle via runLoop() so g_dispatcher.isDispatcherThread()
	// holds for Debug/ASan assertions inside ProtocolGame::drainIncomingPacketsOnDispatcher().
	auto runOneCycle = []() {
		g_reactor.setPostCycleHook([] {
			g_reactor.setPostCycleHook(nullptr);
			g_dispatcher.shutdown();
		});
		g_reactor.runLoop();
		g_dispatcher.start();
	};

	ConnectionManager::getInstance().closeAll();
	g_game.setGameState(GAME_STATE_NORMAL);
	g_performanceMetrics.setEnabled(true);
	g_dispatcher.start();
	g_scheduler.stop();

	auto floodConn = ConnectionManager::getInstance().createConnection(ioContext, {});
	auto legitConn = ConnectionManager::getInstance().createConnection(ioContext, {});
	CHECK(floodConn != nullptr && legitConn != nullptr);

	auto floodProto = std::make_shared<ProtocolGame>(floodConn);
	auto legitProto = std::make_shared<ProtocolGame>(legitConn);
	auto floodPlayer = std::make_shared<Player>(floodProto);
	auto legitPlayer = std::make_shared<Player>(legitProto);

	ProtocolGameAuditTestAccess::bind(*floodProto, floodPlayer);
	ProtocolGameAuditTestAccess::bind(*legitProto, legitPlayer);
	ProtocolGameAuditTestAccess::otc(*floodProto, true);
	ProtocolGameAuditTestAccess::otc(*legitProto, true);
	ProtocolGameAuditTestAccess::setAcceptPackets(*floodProto, true);
	ProtocolGameAuditTestAccess::setAcceptPackets(*legitProto, true);

	const uint64_t callsBefore = g_performanceMetrics.getMetricCalls(PerformanceMetric::ProtocolGameQueueLatency);
	const uint64_t nsBefore =
	    g_performanceMetrics.getMetricTotalNanoseconds(PerformanceMetric::ProtocolGameQueueLatency);

	// Flooder pushes 20 real packets into ProtocolGame::parsePacket() before the legitimate player arrives.
	for (uint32_t i = 1; i <= 20; ++i) {
		auto msg = makeNewPingMsg(i);
		ProtocolGameAuditTestAccess::parsePacket(*floodProto, msg);
	}
	CHECK(ProtocolGameAuditTestAccess::incomingQueueSize(*floodProto) == 20);
	CHECK(ProtocolGameAuditTestAccess::backlogCount(*floodProto) == 20);

	// Legitimate player sends 1 real packet after the flooder's 20-packet burst.
	auto legitMsg = makeNewPingMsg(9999);
	ProtocolGameAuditTestAccess::parsePacket(*legitProto, legitMsg);
	CHECK(ProtocolGameAuditTestAccess::incomingQueueSize(*legitProto) == 1);

	// Wait briefly so end-to-end per-connection queue latency is measurably non-zero.
	std::this_thread::sleep_for(std::chrono::milliseconds(2));

	// Run exactly ONE reactor cycle with maxTasksPerCycle=2:
	// flooder drains 1 packet (re-queuing itself behind legit), and legit MUST execute in cycle 1!
	g_reactor.setMaxTasksPerCycle(2);
	runOneCycle();

	CHECK(ProtocolGameAuditTestAccess::incomingQueueSize(*floodProto) == 19);
	CHECK(ProtocolGameAuditTestAccess::incomingQueueSize(*legitProto) == 0);
	CHECK(ProtocolGameAuditTestAccess::backlogCount(*legitProto) == 0);

	// Verify legit's 0x40 reply (pingId=9999) was already written in cycle 1!
	const auto& legitOut = legitProto->getCurrentBuffer();
	CHECK(legitOut != nullptr && legitOut->getLength() == 5);
	CHECK(legitOut->getBuffer()[NetworkMessage::INITIAL_BUFFER_POSITION] == 0x40);
	uint32_t echoedLegitId = 0;
	std::memcpy(&echoedLegitId, legitOut->getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION + 1, sizeof(uint32_t));
	CHECK(echoedLegitId == 9999);

	// Verify flooder's first drained packet was pingId=1 (strict FIFO).
	const auto& floodOut = floodProto->getCurrentBuffer();
	CHECK(floodOut != nullptr && floodOut->getLength() == 5);
	uint32_t firstFloodId = 0;
	std::memcpy(&firstFloodId, floodOut->getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION + 1, sizeof(uint32_t));
	CHECK(firstFloodId == 1);

	// Verify Point 6: per-connection queue latency metric recorded both drained packets with >0 ns wait.
	CHECK(g_performanceMetrics.getMetricCalls(PerformanceMetric::ProtocolGameQueueLatency) >= callsBefore + 2);
	CHECK(g_performanceMetrics.getMetricTotalNanoseconds(PerformanceMetric::ProtocolGameQueueLatency) > nsBefore);

	// Drain the remaining 19 flooder packets across subsequent cycles and verify strict FIFO ordering.
	g_reactor.setMaxTasksPerCycle(0);
	for (int cycles = 0; cycles < 30 && ProtocolGameAuditTestAccess::incomingQueueSize(*floodProto) > 0; ++cycles) {
		runOneCycle();
	}
	CHECK(ProtocolGameAuditTestAccess::incomingQueueSize(*floodProto) == 0);
	CHECK(ProtocolGameAuditTestAccess::backlogCount(*floodProto) == 0);
	CHECK(floodOut->getLength() == 20 * 5);
	for (uint32_t i = 0; i < 20; ++i) {
		const size_t off = NetworkMessage::INITIAL_BUFFER_POSITION + i * 5;
		CHECK(floodOut->getBuffer()[off] == 0x40);
		uint32_t id = 0;
		std::memcpy(&id, floodOut->getBuffer() + off + 1, sizeof(uint32_t));
		CHECK(id == i + 1);
	}
	CHECK(g_performanceMetrics.getMetricCalls(PerformanceMetric::ProtocolGameQueueLatency) == callsBefore + 21);

	// Verify expiring packets are dropped while non-expiring packets still execute.
	legitProto->getCurrentBuffer() = std::make_shared<OutputMessage>();
	for (int i = 0; i < 3; ++i) {
		auto turn = makeTurnMsg();
		ProtocolGameAuditTestAccess::parsePacket(*legitProto, turn);
	}
	auto afterExpired = makeNewPingMsg(4242);
	ProtocolGameAuditTestAccess::parsePacket(*legitProto, afterExpired);
	ProtocolGameAuditTestAccess::backdateExpiringDeadlines(
	    *legitProto, std::chrono::steady_clock::now() - std::chrono::seconds(1));
	runOneCycle();
	CHECK(ProtocolGameAuditTestAccess::incomingQueueSize(*legitProto) == 0);
	CHECK(ProtocolGameAuditTestAccess::backlogCount(*legitProto) == 0);
	CHECK(legitProto->getCurrentBuffer()->getLength() == 5);
	uint32_t afterExpiredId = 0;
	std::memcpy(&afterExpiredId, legitProto->getCurrentBuffer()->getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION + 1,
	            sizeof(uint32_t));
	CHECK(afterExpiredId == 4242);

	// Verify backlog overflow disconnects immediately from parsePacket() without waiting for dispatcher.
	auto overflowConn = ConnectionManager::getInstance().createConnection(ioContext, {});
	auto overflowProto = std::make_shared<ProtocolGame>(overflowConn);
	const size_t connCountBeforeOverflow = ConnectionManager::getInstance().getConnectionCount();
	const size_t overflowLimit = ProtocolGameAuditTestAccess::backlogLimit(*overflowProto);
	for (size_t i = 0; i < overflowLimit + 1; ++i) {
		auto msg = makeNewPingMsg(static_cast<uint32_t>(i));
		ProtocolGameAuditTestAccess::parsePacket(*overflowProto, msg);
	}
	CHECK(ConnectionManager::getInstance().getConnectionCount() == connCountBeforeOverflow - 1);

	// Verify release() clears queued packets and releases backlog tickets on dispatcher.
	g_dispatcher.addTask([&] {
		ProtocolGameAuditTestAccess::release(*overflowProto);
		ProtocolGameAuditTestAccess::release(*floodProto);
		ProtocolGameAuditTestAccess::release(*legitProto);
	});
	runOneCycle();
	CHECK(ProtocolGameAuditTestAccess::incomingQueueSize(*overflowProto) == 0);
	CHECK(ProtocolGameAuditTestAccess::backlogCount(*overflowProto) == 0);
	ProtocolGameAuditTestAccess::bind(*floodProto, nullptr);
	ProtocolGameAuditTestAccess::bind(*legitProto, nullptr);
}

TFS_TEST_MAIN()
