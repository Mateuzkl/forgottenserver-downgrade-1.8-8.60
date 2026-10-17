// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"
#include "../game.h"
#include "../outputmessage.h"
#include "../player.h"
#include "../protocolgame.h"
#include "../tasks.h"
#include "test_support.h"

struct ProtocolGameCombatPacketsTestAccess
{
	static void setViewer(ProtocolGame& protocol, const std::shared_ptr<Player>& viewer) { protocol.player = viewer; }
	static void effect(ProtocolGame& protocol, const Position& pos, uint16_t type) { protocol.sendMagicEffect(pos, type); }
	static void distance(ProtocolGame& protocol, const Position& from, const Position& to, uint16_t type)
	{
		protocol.sendDistanceShoot(from, to, type);
	}
	static void health(ProtocolGame& protocol, const Creature* creature) { protocol.sendCreatureHealth(creature); }
	static void speed(ProtocolGame& protocol, const Creature* creature, uint32_t speed)
	{
		protocol.sendChangeSpeed(creature, speed);
	}
	static void text(ProtocolGame& protocol, const std::string& text, bool structured)
	{
		if (structured) {
			protocol.sendTextMessage(TextMessage(MESSAGE_STATUS_DEFAULT, text));
		} else {
			protocol.sendTextMessage(MESSAGE_STATUS_DEFAULT, text);
		}
	}
	static void animated(ProtocolGame& protocol, std::string_view text, const Position& position)
	{
		protocol.sendAnimatedText(text, position, TEXTCOLOR_RED);
	}
};

namespace {

class TestCreature final : public Creature
{
public:
	const std::string& getName() const override { return name; }
	const std::string& getNameDescription() const override { return name; }
	std::string getDescription(int32_t) const override { return name; }
	CreatureType_t getType() const override { return CREATURETYPE_MONSTER; }
	void setID() override { id = 0x12345678; }
	void removeList() override {}
	void addList() override {}
	void setHealthForTest(int32_t current, int32_t maximum, bool hidden = false)
	{
		health = current;
		healthMax = maximum;
		setHiddenHealth(hidden);
	}

private:
	std::string name = "combat packet test";
};

void checkPayload(const ProtocolGame& protocol, const NetworkMessage& expected)
{
	// getCurrentBuffer is intentionally non-const in Protocol.
	const auto& actual = const_cast<ProtocolGame&>(protocol).getCurrentBuffer();
	CHECK(actual != nullptr);
	CHECK(actual->getLength() == expected.getLength());
	CHECK(actual->getBufferPosition() == NetworkMessage::INITIAL_BUFFER_POSITION + expected.getLength());
	CHECK(std::equal(expected.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION,
	                 expected.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION + expected.getLength(),
	                 actual->getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION));
}

} // namespace

TEST_CASE(combat_fixed_packets_match_legacy_serialization_and_order)
{
	ProtocolGame protocol(nullptr);
	protocol.getCurrentBuffer() = std::make_shared<OutputMessage>();
	TestCreature creature;
	creature.setID();
	creature.setHealthForTest(101, 300);
	const Position from{1030, 1015, 7}, to{1031, 1016, 7};
	NetworkMessage expected;
	expected.addByte(0xAA); // Preserve already queued messages.
	protocol.getCurrentBuffer()->addByte(0xAA);
	for (uint16_t type : {uint16_t{0}, uint16_t{255}, uint16_t{65535}}) {
		ProtocolGameCombatPacketsTestAccess::distance(protocol, from, to, type);
		expected.addByte(0x85);
		expected.addPosition(from);
		expected.addPosition(to);
		expected.add<uint16_t>(type);
	}
	ProtocolGameCombatPacketsTestAccess::health(protocol, &creature);
	expected.addByte(0x8C);
	expected.add<uint32_t>(creature.getID());
	expected.addByte(34);
	ProtocolGameCombatPacketsTestAccess::speed(protocol, &creature, 0x12345);
	expected.addByte(0x8F);
	expected.add<uint32_t>(creature.getID());
	expected.add<uint16_t>(0x2345);
	checkPayload(protocol, expected);
}

TEST_CASE(combat_health_packet_preserves_hidden_and_zero_max_health)
{
	for (const bool hidden : {false, true}) {
		ProtocolGame protocol(nullptr);
		protocol.getCurrentBuffer() = std::make_shared<OutputMessage>();
		TestCreature creature;
		creature.setID();
		creature.setHealthForTest(1, 0, hidden);
		ProtocolGameCombatPacketsTestAccess::health(protocol, &creature);
		NetworkMessage expected;
		expected.addByte(0x8C);
		expected.add<uint32_t>(creature.getID());
		expected.addByte(hidden ? 0 : 100);
		checkPayload(protocol, expected);
	}
}

TEST_CASE(combat_effect_packet_preserves_visibility_ground_and_400_repeated_payloads)
{
	const Position position{1030, 1015, 7};
	auto viewer = std::make_shared<Player>(nullptr);
	auto viewerTile = std::make_shared<StaticTile>(position.x, position.y, position.z);
	viewer->setParent(viewerTile.get());
	g_game.map.setTile(position.x, position.y, position.z,
	                   std::make_unique<StaticTile>(position.x, position.y, position.z));
	Tile* tile = g_game.map.getTile(position);
	ProtocolGame protocol(nullptr);
	ProtocolGameCombatPacketsTestAccess::setViewer(protocol, viewer);
	protocol.getCurrentBuffer() = std::make_shared<OutputMessage>();
	ProtocolGameCombatPacketsTestAccess::effect(protocol, position, 38);
	CHECK(protocol.getCurrentBuffer()->getLength() == 0); // Missing ground.
	tile->setGround(std::make_shared<Item>(0));
	ProtocolGameCombatPacketsTestAccess::effect(protocol, Position{2000, 2000, 7}, 38);
	CHECK(protocol.getCurrentBuffer()->getLength() == 0); // Outside viewport.
	NetworkMessage expected;
	for (size_t recipient = 0; recipient < 400; ++recipient) {
		ProtocolGameCombatPacketsTestAccess::effect(protocol, position, 0x1234);
		expected.addByte(0x83);
		expected.addPosition(position);
		expected.add<uint16_t>(0x1234);
	}
	checkPayload(protocol, expected);
	viewer->setParent(nullptr);
}

TEST_CASE(combat_fixed_packet_rollover_keeps_previous_buffer_intact)
{
	auto protocol = std::make_shared<ProtocolGame>(nullptr);
	auto previous = std::make_shared<OutputMessage>();
	protocol->getCurrentBuffer() = previous;
	for (int32_t index = 0; index < NetworkMessage::MAX_PROTOCOL_BODY_LENGTH - 5; ++index) {
		previous->addByte(0xAA);
	}
	TestCreature creature;
	creature.setID();
	ProtocolGameCombatPacketsTestAccess::speed(*protocol, &creature, 180);
	CHECK(protocol->getCurrentBuffer() != previous);
	CHECK(previous->getLength() == NetworkMessage::MAX_PROTOCOL_BODY_LENGTH - 5);
	NetworkMessage expected;
	expected.addByte(0x8F);
	expected.add<uint32_t>(creature.getID());
	expected.add<uint16_t>(180);
	checkPayload(*protocol, expected);
	OutputMessagePool::getInstance().removeProtocolFromAutosend(protocol);
}

TEST_CASE(combat_text_packets_match_legacy_encoding_and_rejection)
{
	const Position position{1030, 1015, 7};
	auto viewer = std::make_shared<Player>(nullptr);
	auto tile = std::make_shared<StaticTile>(position.x, position.y, position.z);
	viewer->setParent(tile.get());
	std::vector<std::string> texts{"", "You lose 5 hitpoints.", "caf\xC3\xA9", "\xF0\x9F\x98\x80",
	                               std::string(1, static_cast<char>(0xFF)),
	                               std::string(NetworkMessage::MAX_STRING_LENGTH, 'a'),
	                               std::string(NetworkMessage::MAX_STRING_LENGTH + 1, 'b')};
	for (const auto& text : texts) {
		ProtocolGame protocol(nullptr);
		protocol.getCurrentBuffer() = std::make_shared<OutputMessage>();
		ProtocolGameCombatPacketsTestAccess::setViewer(protocol, viewer);
		NetworkMessage expected;
		for (const bool structured : {false, true}) {
			ProtocolGameCombatPacketsTestAccess::text(protocol, text, structured);
			expected.addByte(0xB4);
			expected.addByte(MESSAGE_STATUS_DEFAULT);
			expected.addString(text);
		}
		ProtocolGameCombatPacketsTestAccess::animated(protocol, text, position);
		expected.addByte(0x84);
		expected.addPosition(position);
		expected.addByte(TEXTCOLOR_RED);
		expected.addString(text);
		checkPayload(protocol, expected);
	}
	viewer->setParent(nullptr);
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
