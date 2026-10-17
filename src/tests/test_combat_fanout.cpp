// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in the LICENSE file.

#include "../otpch.h"
#include "combat_fanout_fixture.h"
#include "../instance_utils.h"
#include "../tasks.h"
#include "test_support.h"

TEST_CASE(effect_fanout_matches_legacy_for_zero_to_600_independent_recipients)
{
	for (size_t recipients : {0, 1, 10, 100, 300, 600}) {
		CombatFanoutFixture fixture(recipients);
		for (uint16_t effect : {0, 255, 256, 65535}) {
			ProtocolGameCombatPacketsTestAccess::legacyFanout(fixture.spectators, fixture.position, effect);
		}
		const auto expected = fixture.payloads();
		fixture.reset();
		for (uint16_t effect : {0, 255, 256, 65535}) {
			g_game.addMagicEffect(fixture.spectators, fixture.position, effect);
		}
		CHECK(fixture.payloads() == expected);
		for (const auto& payload : expected) {
			CHECK(payload.size() == 32);
		}
	}
}

TEST_CASE(effect_fanout_keeps_cast_spies_viewports_floor_checks_and_expired_owner)
{
	CombatFanoutFixture fixture(2, 2, 2);
	auto farTile = std::make_shared<StaticTile>(2000, 2000, 7);
	auto underground = std::make_shared<StaticTile>(1030, 1015, 10);
	fixture.viewers[1]->setParent(farTile.get()); // Invisible cast, even though owner can see.
	fixture.viewers[2]->setParent(underground.get());
	fixture.viewers[3]->setParent(farTile.get());
	ProtocolGameCombatPacketsTestAccess::spy(*fixture.protocols[3], fixture.position);
	ProtocolGameCombatPacketsTestAccess::expireOwner(*fixture.viewers[5]->client);
	ProtocolGameCombatPacketsTestAccess::viewer(*fixture.protocols[9], nullptr);
	ProtocolGameCombatPacketsTestAccess::legacyFanout(fixture.spectators, fixture.position, 65535);
	const auto expected = fixture.payloads();
	fixture.reset();
	g_game.addMagicEffect(fixture.spectators, fixture.position, 65535);
	CHECK(fixture.payloads() == expected);
	CHECK(expected[0].size() == 8);
	CHECK(expected[1].empty());
	CHECK(expected[2].empty());
	CHECK(expected[3].size() == 8); // Spy viewport, not spy character position.
	CHECK(expected[5].empty()); // Expired owner must not suppress its cast/spies.
	CHECK(expected[6].size() == 8);
	CHECK(expected[9].empty());
}

TEST_CASE(effect_packet_is_lazy_and_rechecks_ground_and_viewer_after_preparation)
{
	CHECK(sizeof(MagicEffectPacket) <= 16);
	CombatFanoutFixture fixture(1);
	MagicEffectPacket packet;
	ProtocolGameCombatPacketsTestAccess::cachedEffect(*fixture.protocols[0], Position{2000, 2000, 7}, 38, packet);
	CHECK(!packet);
	ProtocolGameCombatPacketsTestAccess::cachedEffect(*fixture.protocols[0], fixture.position, 38, packet);
	CHECK(packet.has_value());
	CHECK(packet->size() == 8);
	fixture.reset();
	g_game.map.getTile(fixture.position)->setGround(nullptr);
	ProtocolGameCombatPacketsTestAccess::cachedEffect(*fixture.protocols[0], fixture.position, 38, packet);
	CHECK(fixture.protocols[0]->getCurrentBuffer()->getLength() == 0);
	CHECK(g_game.map.removeTile(fixture.position.x, fixture.position.y, fixture.position.z));
	ProtocolGameCombatPacketsTestAccess::cachedEffect(*fixture.protocols[0], fixture.position, 38, packet);
	CHECK(fixture.protocols[0]->getCurrentBuffer()->getLength() == 0);
	g_game.map.setTile(fixture.position.x, fixture.position.y, fixture.position.z,
	                   std::make_unique<StaticTile>(fixture.position.x, fixture.position.y, fixture.position.z));
	ProtocolGameCombatPacketsTestAccess::cachedEffect(*fixture.protocols[0], fixture.position, 38, packet);
	CHECK(fixture.protocols[0]->getCurrentBuffer()->getLength() == 0);
	g_game.map.getTile(fixture.position)->setGround(std::make_shared<Item>(0));
	ProtocolGameCombatPacketsTestAccess::viewer(*fixture.protocols[0], nullptr);
	ProtocolGameCombatPacketsTestAccess::cachedEffect(*fixture.protocols[0], fixture.position, 38, packet);
	CHECK(fixture.protocols[0]->getCurrentBuffer()->getLength() == 0);
}

TEST_CASE(effect_event_cache_does_not_cross_events_or_retain_stack_bytes)
{
	CombatFanoutFixture fixture(2, 1, 1);
	g_game.addMagicEffect(fixture.spectators, fixture.position, 0x1234);
	const auto first = fixture.payloads();
	NetworkMessage unrelated;
	for (size_t index = 0; index < 1024; ++index) {
		unrelated.addByte(0xFF);
	}
	g_game.addMagicEffect(fixture.spectators, fixture.position, 0x5678);
	const auto actual = fixture.payloads();
	for (size_t index = 0; index < actual.size(); ++index) {
		CHECK(actual[index].size() == 16);
		CHECK(std::equal(first[index].begin(), first[index].end(), actual[index].begin()));
		CHECK(actual[index][6] == 0x34);
		CHECK(actual[index][7] == 0x12);
		CHECK(actual[index][14] == 0x78);
		CHECK(actual[index][15] == 0x56);
	}
}

TEST_CASE(effect_instance_filter_and_teleport_remain_per_event)
{
	CombatFanoutFixture fixture(3);
	fixture.spectators.partitionByType();
	fixture.viewers[0]->setInstanceID(7);
	fixture.viewers[1]->setInstanceID(8);
	fixture.viewers[2]->setInstanceID(7);
	InstanceUtils::sendMagicEffectToInstance(fixture.spectators, fixture.position, 38, 7);
	CHECK(fixture.protocols[0]->getCurrentBuffer()->getLength() == 8);
	CHECK(fixture.protocols[1]->getCurrentBuffer()->getLength() == 0);
	CHECK(fixture.protocols[2]->getCurrentBuffer()->getLength() == 8);
	fixture.reset();
	auto farTile = std::make_shared<StaticTile>(2000, 2000, 7);
	fixture.viewers[2]->setParent(farTile.get());
	InstanceUtils::sendMagicEffectToInstance(fixture.spectators, fixture.position, 39, 7);
	CHECK(fixture.protocols[0]->getCurrentBuffer()->getLength() == 8);
	CHECK(fixture.protocols[1]->getCurrentBuffer()->getLength() == 0);
	CHECK(fixture.protocols[2]->getCurrentBuffer()->getLength() == 0);
}

TEST_CASE(effect_prepared_packet_rollover_preserves_previous_buffer)
{
	CombatFanoutFixture fixture(1, 1, 1);
	auto previous = fixture.protocols[0]->getCurrentBuffer();
	for (int32_t index = 0; index < NetworkMessage::MAX_PROTOCOL_BODY_LENGTH - 5; ++index) {
		previous->addByte(0xAA);
	}
	g_game.addMagicEffect(fixture.spectators, fixture.position, 65535);
	CHECK(previous->getLength() == NetworkMessage::MAX_PROTOCOL_BODY_LENGTH - 5);
	CHECK(fixture.protocols[0]->getCurrentBuffer() != previous);
	const auto actual = fixture.payloads();
	CHECK(actual[0].size() == 8);
	CHECK(actual[0] == actual[1]);
	CHECK(actual[0] == actual[2]);
}

TEST_CASE(effect_fanout_skips_null_snapshot_entries_and_lost_player_client)
{
	CombatFanoutFixture fixture(3);
	fixture.spectators.emplaceNull();
	fixture.viewers[1]->client.reset();
	ProtocolGameCombatPacketsTestAccess::legacyFanout(fixture.spectators, fixture.position, 38);
	const auto expected = fixture.payloads();
	fixture.reset();
	g_game.addMagicEffect(fixture.spectators, fixture.position, 38);
	CHECK(fixture.payloads() == expected);
	CHECK(expected[0].size() == 8);
	CHECK(expected[1].empty());
	CHECK(expected[2].size() == 8);
}

int main()
{
	g_dispatcher.start();
	int result = EXIT_FAILURE;
	g_dispatcher.addTask([&result] {
		result = tfs::tests::run();
		g_dispatcher.shutdown();
	});
	g_reactor.runLoop();
	return result;
}
