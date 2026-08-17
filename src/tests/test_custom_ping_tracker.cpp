// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../protocolgame.h"

#include "test_support.h"

struct ProtocolGameCustomPingTestAccess
{
	enum class Result
	{
		Accepted,
		Duplicate,
		Unknown,
	};

	static bool registerPing(ProtocolGame& protocol, uint32_t id, int64_t now)
	{
		return protocol.registerCustomPing(id, now);
	}

	static Result receivePong(ProtocolGame& protocol, uint32_t id, int64_t now)
	{
		switch (protocol.receiveCustomPong(id, now)) {
			case ProtocolGame::CustomPongResult::Accepted: return Result::Accepted;
			case ProtocolGame::CustomPongResult::Duplicate: return Result::Duplicate;
			case ProtocolGame::CustomPongResult::Unknown: return Result::Unknown;
		}
		return Result::Unknown;
	}

	static std::size_t size(const ProtocolGame& protocol)
	{
		return std::count_if(protocol.customPings.begin(), protocol.customPings.end(),
		                     [](const auto& entry) { return entry.id != 0; });
	}

	static constexpr std::size_t capacity() { return ProtocolGame::CUSTOM_PING_MAX_TRACKED; }
	static constexpr int64_t ttl() { return ProtocolGame::CUSTOM_PING_TTL_MS; }
	static uint32_t nextId(uint32_t current) { return ProtocolGame::nextCustomPingId(current); }
	static uint32_t nextSeed() { return ProtocolGame::nextCustomPingSeed(); }
	static std::optional<uint32_t> readId(NetworkMessage& msg) { return ProtocolGame::readCustomPingId(msg); }
};

using PingResult = ProtocolGameCustomPingTestAccess::Result;

TEST_CASE(custom_ping_accepts_once_and_rejects_duplicates)
{
	ProtocolGame protocol(nullptr);
	CHECK(ProtocolGameCustomPingTestAccess::registerPing(protocol, 10, 1'000));
	CHECK(ProtocolGameCustomPingTestAccess::receivePong(protocol, 10, 1'010) == PingResult::Accepted);
	CHECK(ProtocolGameCustomPingTestAccess::receivePong(protocol, 10, 1'011) == PingResult::Duplicate);

	std::size_t accepted = 1;
	for (std::size_t i = 0; i < 1'000; ++i) {
		accepted += ProtocolGameCustomPingTestAccess::receivePong(
		                protocol, 10, 1'012 + static_cast<int64_t>(i)) == PingResult::Accepted ? 1 : 0;
	}
	CHECK(accepted == 1);
}

TEST_CASE(custom_ping_rejects_unknown_and_zero_ids)
{
	ProtocolGame protocol(nullptr);
	std::size_t accepted = 0;
	for (uint32_t id = 1; id <= 1'000; ++id) {
		accepted += ProtocolGameCustomPingTestAccess::receivePong(protocol, id, 2'000) == PingResult::Accepted ? 1 : 0;
	}
	CHECK(accepted == 0);
	CHECK(ProtocolGameCustomPingTestAccess::receivePong(protocol, 0, 2'000) == PingResult::Unknown);
	CHECK(!ProtocolGameCustomPingTestAccess::registerPing(protocol, 0, 2'000));
	CHECK(ProtocolGameCustomPingTestAccess::size(protocol) == 0);
}

TEST_CASE(custom_ping_handles_overlapping_ids_out_of_order)
{
	ProtocolGame protocol(nullptr);
	CHECK(ProtocolGameCustomPingTestAccess::registerPing(protocol, 100, 1'000));
	CHECK(ProtocolGameCustomPingTestAccess::registerPing(protocol, 200, 6'000));
	CHECK(ProtocolGameCustomPingTestAccess::receivePong(protocol, 200, 6'100) == PingResult::Accepted);
	CHECK(ProtocolGameCustomPingTestAccess::receivePong(protocol, 100, 6'200) == PingResult::Accepted);
	CHECK(ProtocolGameCustomPingTestAccess::receivePong(protocol, 200, 6'300) == PingResult::Duplicate);
	CHECK(ProtocolGameCustomPingTestAccess::receivePong(protocol, 100, 6'400) == PingResult::Duplicate);
}

TEST_CASE(custom_ping_expires_without_reopening_a_handshake)
{
	ProtocolGame protocol(nullptr);
	const int64_t ttl = ProtocolGameCustomPingTestAccess::ttl();
	CHECK(ProtocolGameCustomPingTestAccess::registerPing(protocol, 77, 1'000));
	CHECK(ProtocolGameCustomPingTestAccess::receivePong(protocol, 77, 1'000 + ttl) == PingResult::Unknown);
	CHECK(ProtocolGameCustomPingTestAccess::size(protocol) == 0);

	CHECK(ProtocolGameCustomPingTestAccess::registerPing(protocol, 78, 2'000));
	CHECK(ProtocolGameCustomPingTestAccess::receivePong(protocol, 78, 2'001) == PingResult::Accepted);
	CHECK(ProtocolGameCustomPingTestAccess::receivePong(protocol, 78, 2'001 + ttl) == PingResult::Unknown);
}

TEST_CASE(custom_ping_storage_is_fixed_and_evicts_oldest)
{
	ProtocolGame protocol(nullptr);
	const std::size_t capacity = ProtocolGameCustomPingTestAccess::capacity();
	for (std::size_t i = 0; i < capacity; ++i) {
		CHECK(ProtocolGameCustomPingTestAccess::registerPing(protocol, static_cast<uint32_t>(i + 1),
		                                                       static_cast<int64_t>(i)));
	}
	CHECK(ProtocolGameCustomPingTestAccess::size(protocol) == capacity);

	CHECK(ProtocolGameCustomPingTestAccess::registerPing(protocol, 999, static_cast<int64_t>(capacity)));
	CHECK(ProtocolGameCustomPingTestAccess::size(protocol) == capacity);
	CHECK(ProtocolGameCustomPingTestAccess::receivePong(protocol, 1, static_cast<int64_t>(capacity)) ==
	      PingResult::Unknown);
	CHECK(ProtocolGameCustomPingTestAccess::receivePong(protocol, 999, static_cast<int64_t>(capacity)) ==
	      PingResult::Accepted);
}

TEST_CASE(custom_ping_sequence_skips_zero_after_wrap)
{
	CHECK(ProtocolGameCustomPingTestAccess::nextId(0) == 1);
	CHECK(ProtocolGameCustomPingTestAccess::nextId(41) == 42);
	CHECK(ProtocolGameCustomPingTestAccess::nextId((std::numeric_limits<uint32_t>::max)()) == 1);
}

TEST_CASE(custom_ping_seeds_do_not_repeat_across_connections)
{
	// Every connection must start somewhere else, so a relogin cannot reuse ids
	// the DLL still holds closed from the previous session.
	std::set<uint32_t> seeds;
	uint32_t previous = ProtocolGameCustomPingTestAccess::nextSeed();
	seeds.insert(previous);
	for (std::size_t i = 0; i < 4'096; ++i) {
		const uint32_t seed = ProtocolGameCustomPingTestAccess::nextSeed();
		CHECK(seed != previous);
		CHECK(seeds.insert(seed).second);
		previous = seed;
	}

	// A seed is only ever fed through nextCustomPingId, which never yields zero,
	// so a seed landing on zero still produces a usable id.
	CHECK(ProtocolGameCustomPingTestAccess::nextId(0) != 0);
}

TEST_CASE(custom_ping_packet_reader_rejects_truncation_without_overrun)
{
	NetworkMessage truncated;
	truncated.addByte(0x1E);
	CHECK(truncated.setBufferPosition(0));
	CHECK(truncated.getByte() == 0x1E);
	CHECK(!ProtocolGameCustomPingTestAccess::readId(truncated));
	CHECK(!truncated.isOverrun());

	NetworkMessage complete;
	complete.addByte(0x1E);
	complete.add<uint32_t>(0x12345678);
	CHECK(complete.setBufferPosition(0));
	CHECK(complete.getByte() == 0x1E);
	CHECK(ProtocolGameCustomPingTestAccess::readId(complete) == 0x12345678);
	CHECK(!complete.isOverrun());
}

TFS_TEST_MAIN()
