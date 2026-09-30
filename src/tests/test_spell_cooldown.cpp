// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../astraclient.h"
#include "../outputmessage.h"
#include "../protocolgame.h"
#include "../tasks.h"
#include "test_support.h"

struct ProtocolGameSpellCooldownTestAccess
{
	static void configure(ProtocolGame& protocol, bool astra, bool extended, bool fonticak = false,
	                      uint16_t version = 860)
	{
		protocol.isOTC = true;
		protocol.isOTCv8 = astra || fonticak;
		protocol.isAstraClient = astra;
		protocol.isFonticakClient = fonticak;
		protocol.supportsAstraExtendedSpellIds = extended;
		protocol.version = version;
		// Preallocate to avoid registering a disconnected test protocol for autosend.
		protocol.getCurrentBuffer() = std::make_shared<OutputMessage>();
	}

	static void send(ProtocolGame& protocol, uint16_t id, uint32_t time) { protocol.sendSpellCooldown(id, time); }
	static void sendGroup(ProtocolGame& protocol, SpellGroup_t group, uint32_t time)
	{
		protocol.sendSpellGroupCooldown(group, time);
	}
	static bool spellListIsWide(const ProtocolGame& protocol) { return protocol.usesExtendedSpellIds(); }
	static void sendFeatures(ProtocolGame& protocol) { protocol.sendFeatures(); }
};

namespace {

void checkBytes(ProtocolGame& protocol, std::initializer_list<uint8_t> expected)
{
	const auto& output = protocol.getCurrentBuffer();
	CHECK(output->getLength() == expected.size());
	CHECK(std::equal(expected.begin(), expected.end(), output->getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION));
}

void checkSpellPacket(bool astra, bool extended, uint16_t id, uint32_t time, std::initializer_list<uint8_t> expected,
                      bool fonticak = false, uint16_t version = 860)
{
	ProtocolGame protocol(nullptr);
	ProtocolGameSpellCooldownTestAccess::configure(protocol, astra, extended, fonticak, version);
	ProtocolGameSpellCooldownTestAccess::send(protocol, id, time);
	checkBytes(protocol, expected);
}

} // namespace

TEST_CASE(spell_cooldown_legacy_packets_keep_u8_ids)
{
	checkSpellPacket(false, false, 42, 1000, {0xA4, 0x2A, 0xE8, 0x03, 0x00, 0x00});
	checkSpellPacket(true, false, 42, 1000, {0xA4, 0x2A, 0xE8, 0x03, 0x00, 0x00});
	checkSpellPacket(true, false, 255, 1000, {0xA4, 0xFF, 0xE8, 0x03, 0x00, 0x00});
	checkSpellPacket(true, false, 42, 0, {0xA4, 0x2A, 0x00, 0x00, 0x00, 0x00});
}

TEST_CASE(spell_cooldown_negotiated_packets_keep_full_u16_ids)
{
	checkSpellPacket(true, true, 42, 1000, {0xA4, 0x2A, 0x00, 0xE8, 0x03, 0x00, 0x00});
	checkSpellPacket(true, true, 256, 1000, {0xA4, 0x00, 0x01, 0xE8, 0x03, 0x00, 0x00});
	checkSpellPacket(true, true, 275, 1000, {0xA4, 0x13, 0x01, 0xE8, 0x03, 0x00, 0x00});
	checkSpellPacket(true, true, 65535, 1000, {0xA4, 0xFF, 0xFF, 0xE8, 0x03, 0x00, 0x00});
	checkSpellPacket(true, true, 275, 0, {0xA4, 0x13, 0x01, 0x00, 0x00, 0x00, 0x00});
}

TEST_CASE(spell_cooldown_unsupported_extended_ids_are_not_truncated)
{
	for (const uint16_t id : {256, 275, 65535}) {
		checkSpellPacket(true, false, id, 1000, {});
		checkSpellPacket(true, false, id, 0, {});
		checkSpellPacket(false, false, id, 1000, {});
	}
}

TEST_CASE(spell_cooldown_preserves_fonticak_and_modern_width)
{
	checkSpellPacket(false, false, 275, 1000, {0xA4, 0x13, 0x01, 0xE8, 0x03, 0x00, 0x00}, true);
	checkSpellPacket(false, false, 275, 1000, {0xA4, 0x13, 0x01, 0xE8, 0x03, 0x00, 0x00}, false, 1300);
}

TEST_CASE(spell_group_cooldown_remains_u8_with_either_capability)
{
	static_assert(sizeof(SpellGroup_t) == sizeof(uint8_t));
	for (const bool extended : {false, true}) {
		ProtocolGame protocol(nullptr);
		ProtocolGameSpellCooldownTestAccess::configure(protocol, true, extended);
		ProtocolGameSpellCooldownTestAccess::sendGroup(protocol, SPELLGROUP_STANCE, 1000);
		checkBytes(protocol, {0xA5, 0x0B, 0xE8, 0x03, 0x00, 0x00});
	}
}

TEST_CASE(spell_cooldown_feature_is_advertised_only_to_capable_astra)
{
	CHECK(static_cast<uint8_t>(AstraClient::ExtendedSpellIds) == (1U << 6));
	CHECK(static_cast<uint8_t>(GameFeature::AstraExtendedSpellIds) == 152);
	for (const bool extended : {false, true}) {
		ProtocolGame protocol(nullptr);
		ProtocolGameSpellCooldownTestAccess::configure(protocol, true, extended);
		CHECK(ProtocolGameSpellCooldownTestAccess::spellListIsWide(protocol));
		ProtocolGameSpellCooldownTestAccess::sendFeatures(protocol);
		auto& msg = *protocol.getCurrentBuffer();
		CHECK(msg.setBufferPosition(0));
		CHECK(msg.getByte() == 0x43);
		const uint16_t count = msg.get<uint16_t>();
		bool found = false;
		for (uint16_t index = 0; index < count; ++index) {
			const auto feature = static_cast<GameFeature>(msg.getByte());
			const bool enabled = msg.getByte() != 0;
			if (feature == GameFeature::AstraExtendedSpellIds) {
				CHECK(enabled);
				found = true;
			}
		}
		CHECK(found == extended);
		CHECK(msg.getBufferPosition() == NetworkMessage::INITIAL_BUFFER_POSITION + msg.getLength());
	}
}

int main()
{
	// Run real send methods in the dispatcher context required by getOutputBuffer().
	g_dispatcher.start();
	int result = EXIT_FAILURE;
	g_dispatcher.addTask([&result] {
		result = tfs::tests::run();
		g_dispatcher.shutdown();
	});
	g_reactor.runLoop();
	return result;
}
