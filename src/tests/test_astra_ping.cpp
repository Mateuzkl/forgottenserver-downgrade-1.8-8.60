#include "../otpch.h"

#include "../astra_ping.h"
#include "../astraclient.h"
#include "../game.h"
#include "../outputmessage.h"
#include "../player.h"
#include "../protocolgame.h"
#include "../tasks.h"
#include "../tile.h"
#include "test_support.h"

struct ProtocolGamePingTelemetryTestAccess
{
	static void configure(ProtocolGame& p, bool astra, bool capability)
	{
		p.isAstraClient = astra;
		p.isOTC = p.isOTCv8 = true;
		p.supportsAstraPingTelemetry = capability;
		p.version = 860;
		p.getCurrentBuffer() = std::make_shared<OutputMessage>();
	}
	static bool negotiate(ProtocolGame& p)
	{
		p.sendFeatures();
		auto& msg = *p.getCurrentBuffer();
		CHECK(msg.setBufferPosition(0));
		CHECK(msg.getByte() == 0x43);
		const auto count = msg.get<uint16_t>();
		bool found = false;
		for (uint16_t i = 0; i < count; ++i) {
			const auto id = msg.getByte();
			const auto enabled = msg.getByte();
			if (id == static_cast<uint8_t>(GameFeature::AstraPingTelemetry)) {
				CHECK(enabled == 1);
				found = true;
			}
		}
		CHECK(p.pingTelemetryEnabled == found);
		msg.reset();
		return found;
	}
	static void send(ProtocolGame& p, uint32_t id, uint32_t queue) { p.sendNewPing(id, queue); }
	static void receive(ProtocolGame& p, NetworkMessage_ptr& packet, AstraClient::PingClock::time_point arrival)
	{
		p.parsePacketOnDispatcher(packet, arrival);
	}
	static void setPlayer(ProtocolGame& p, const std::shared_ptr<Player>& player, bool spy, bool spectator)
	{
		p.player = player;
		p.acceptPackets = true;
		p.spyActive_ = spy;
		p.isSpectator = spectator;
	}
	static void stop(ProtocolGame& p) { p.acceptPackets = false; }
	static void parse(ProtocolGame& p, NetworkMessage& msg) { p.parseNewPing(msg, 1234); }
};

namespace {
std::vector<uint8_t> body(ProtocolGame& p)
{
	const auto& msg = p.getCurrentBuffer();
	return {msg->getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION,
	        msg->getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION + msg->getLength()};
}
} // namespace

TEST_CASE(astra_ping_feature_gates_exact_wire_bytes)
{
	CHECK(static_cast<uint8_t>(AstraClient::PingTelemetry) == 128);
	CHECK(static_cast<uint8_t>(GameFeature::AstraPingTelemetry) == 153);
	for (const bool astra : {false, true}) {
		for (const bool capability : {false, true}) {
			ProtocolGame p(nullptr);
			ProtocolGamePingTelemetryTestAccess::configure(p, astra, capability);
			// A capability alone must not extend a response before advertisement.
			ProtocolGamePingTelemetryTestAccess::send(p, 0x12345678, 1000);
			CHECK(body(p) == std::vector<uint8_t>({0x40, 0x78, 0x56, 0x34, 0x12}));
			p.getCurrentBuffer()->reset();
			CHECK(ProtocolGamePingTelemetryTestAccess::negotiate(p) == (astra && capability));
			ProtocolGamePingTelemetryTestAccess::send(p, 0x12345678, 1000);
			auto expected = std::vector<uint8_t>{0x40, 0x78, 0x56, 0x34, 0x12};
			if (astra && capability) {
				expected.insert(expected.end(), {0xE8, 0x03, 0x00, 0x00});
			}
			CHECK(body(p) == expected);
		}
	}
}

TEST_CASE(astra_ping_queue_is_monotonic_and_saturates)
{
	using namespace std::chrono_literals;
	const auto start = AstraClient::PingClock::time_point{};
	CHECK(AstraClient::pingQueueMicros(start, start + 82ms) == 82000);
	CHECK(AstraClient::pingQueueMicros(start, start) == 0);
	CHECK(AstraClient::pingQueueMicros(start + 1ms, start) == 0);
	CHECK(AstraClient::pingQueueMicros(start, start + 2h) == UINT32_MAX);
}

TEST_CASE(astra_ping_short_requests_do_not_read_or_reply)
{
	g_game.setGameState(GAME_STATE_NORMAL);
	ProtocolGame p(nullptr);
	ProtocolGamePingTelemetryTestAccess::configure(p, true, true);
	auto player = std::make_shared<Player>(nullptr);
	ProtocolGamePingTelemetryTestAccess::setPlayer(p, player, false, false);
	for (int length = 0; length < 4; ++length) {
		NetworkMessage msg;
		for (int i = 0; i < length; ++i) {
			msg.addByte(0xAA);
		}
		CHECK(msg.setBufferPosition(0));
		ProtocolGamePingTelemetryTestAccess::parse(p, msg);
		CHECK(msg.getBufferPosition() == NetworkMessage::INITIAL_BUFFER_POSITION);
		CHECK(!msg.isOverrun());
		CHECK(body(p).empty());
	}
}

TEST_CASE(astra_ping_dispatcher_handles_normal_spy_spectator_and_stopped_sessions)
{
	g_game.setGameState(GAME_STATE_NORMAL);
	for (int mode = 0; mode < 3; ++mode) {
		auto handle = std::make_shared<ProtocolGame>(nullptr);
		auto& p = *handle;
		ProtocolGamePingTelemetryTestAccess::configure(p, true, true);
		CHECK(ProtocolGamePingTelemetryTestAccess::negotiate(p));
		auto player = std::make_shared<Player>(handle);
		StaticTile tile(100, 100, 7);
		player->setParent(&tile);
		ProtocolGamePingTelemetryTestAccess::setPlayer(p, player, mode == 1, mode == 2);
		auto packet = std::make_shared<NetworkMessage>();
		packet->addByte(0x40);
		packet->add<uint32_t>(42);
		packet->add<uint16_t>(15); // Preserve ignored OTCv8 local RTT/fps fields.
		packet->add<uint16_t>(60);
		CHECK(packet->setBufferPosition(0));
		const auto arrival = AstraClient::PingClock::now() - std::chrono::milliseconds(82);
		ProtocolGamePingTelemetryTestAccess::receive(p, packet, arrival);
		CHECK(body(p).size() == 9);
		auto& msg = *p.getCurrentBuffer();
		CHECK(msg.setBufferPosition(0));
		CHECK(msg.getByte() == 0x40 && msg.get<uint32_t>() == 42);
		CHECK(msg.get<uint32_t>() >= 82000);
		msg.reset();
		CHECK(packet->setBufferPosition(0));
		ProtocolGamePingTelemetryTestAccess::stop(p);
		ProtocolGamePingTelemetryTestAccess::receive(p, packet, arrival);
		CHECK(body(p).empty());
		ProtocolGamePingTelemetryTestAccess::setPlayer(p, nullptr, false, false);
		ProtocolGamePingTelemetryTestAccess::receive(p, packet, arrival);
		CHECK(body(p).empty());
		player->setParent(nullptr);
	}
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
