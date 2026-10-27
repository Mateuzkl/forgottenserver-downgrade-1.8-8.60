#include "../otpch.h"

#include "../game.h"
#include "../performance_metrics.h"
#include "../player.h"
#include "../smallnetworkmessage.h"
#include "../tile.h"
#include "protocolgame_audit_support.h"
#include "test_support.h"

namespace {
const Position origin{1400, 1400, 7};
const Position destination{1401, 1402, 7};

struct SenderFixture
{
	std::shared_ptr<ProtocolGame> protocol = std::make_shared<ProtocolGame>(nullptr);
	std::shared_ptr<Player> player = std::make_shared<Player>(nullptr);

	SenderFixture()
	{
		player->setGroup(std::make_shared<Group>());
		player->setID();
		player->setMaxHealth(1000);
		player->setHealth(333);
		if (!g_game.map.getTile(origin)) {
			g_game.map.setTile(origin.x, origin.y, origin.z,
			                   std::make_unique<StaticTile>(origin.x, origin.y, origin.z));
			g_game.map.getTile(origin)->setGround(std::make_shared<Item>(0));
		}
		g_game.map.getTile(origin)->internalAddThing(player.get());
		ProtocolGameAuditTestAccess::bind(*protocol, player);
	}
	~SenderFixture()
	{
		OutputMessagePool::getInstance().removeProtocolFromAutosend(protocol);
		ProtocolGameAuditTestAccess::bind(*protocol, nullptr);
		player->getTile()->removeThing(player.get(), 0);
		player->setParent(nullptr);
	}
	std::vector<uint8_t> body() const
	{
		const auto& output = protocol->getCurrentBuffer();
		CHECK(output != nullptr);
		const size_t length = output->getLength();
		CHECK(length <= NetworkMessage::MAX_PROTOCOL_BODY_LENGTH);
		std::vector<uint8_t> result(length);
		if (length != 0) {
			std::memcpy(result.data(), output->getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION, length);
		}
		return result;
	}
	void reset() { protocol->getCurrentBuffer()->reset(); }
};

template <typename Function>
void onDispatcher(Function function)
{
	g_dispatcher.start();
	std::exception_ptr failure;
	bool ran = false;
	g_dispatcher.addTask([&] {
		try { function(); } catch (...) { failure = std::current_exception(); }
		ran = true;
		g_dispatcher.shutdown();
	});
	g_reactor.runLoop();
	CHECK(ran);
	if (failure) std::rethrow_exception(failure);
}

std::vector<uint8_t> body(const NetworkMessage& message)
{
	const size_t length = message.getLength();
	CHECK(length <= NetworkMessage::MAX_PROTOCOL_BODY_LENGTH);
	std::vector<uint8_t> result(length);
	if (length != 0) {
		std::memcpy(result.data(), message.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION, length);
	}
	return result;
}
}

TEST_CASE(combat_fixed_packets_keep_the_existing_wire_bytes)
{
	onDispatcher([] {
		SenderFixture f;
		ProtocolGameAuditTestAccess::effect(*f.protocol, origin, 0xabcd);
		CHECK(f.body() == (std::vector<uint8_t>{0x83,0x78,0x05,0x78,0x05,7,0xcd,0xab}));
		f.reset();
		ProtocolGameAuditTestAccess::distance(*f.protocol, origin, destination, 0xabcd);
		CHECK(f.body() == (std::vector<uint8_t>{0x85,0x78,0x05,0x78,0x05,7,0x79,0x05,0x7a,0x05,7,0xcd,0xab}));
		f.reset();
		ProtocolGameAuditTestAccess::health(*f.protocol, f.player.get());
		NetworkMessage expected;
		expected.addByte(0x8c);
		expected.add<uint32_t>(f.player->getID());
		expected.addByte(34);
		CHECK(f.body() == body(expected));
		f.reset();
		f.player->setHiddenHealth(true);
		ProtocolGameAuditTestAccess::health(*f.protocol, f.player.get());
		CHECK(f.body().size() == 6 && f.body().back() == 0);
	});
}

TEST_CASE(small_outgoing_body_fails_closed_on_overflow)
{
	SmallNetworkMessage<2> message;
	message.add<uint16_t>(0xabcd);
	CHECK(message.bytes().size() == 2);
	CHECK(message.bytes()[0] == 0xcd && message.bytes()[1] == 0xab);
	message.addByte(0xff);
	CHECK(message.bytes().empty());
	message.add<uint32_t>(1);
	CHECK(message.bytes().empty());
	SmallNetworkMessage<1> truncated;
	truncated.addString("abc");
	CHECK(truncated.bytes().empty());
}

TEST_CASE(combat_text_preserves_encoding_boundaries_and_both_overloads)
{
	onDispatcher([] {
		SenderFixture f;
		for (const std::string& text : {std::string{}, std::string{"Damage 123456"},
		     std::string{"A\xc3\xa7\xc3\xa3o: cura e b\xc3\xb4nus"}, std::string{"\xf0\x9f\x90\x89"},
		     std::string{"\xff"}, std::string(256, 'x'), std::string(257, 'y'),
		     std::string(8192, 'z'), std::string(8193, 'w')}) {
			NetworkMessage expected;
			expected.addByte(0xb4);
			expected.addByte(MESSAGE_STATUS_DEFAULT);
			expected.addString(text);
			f.reset();
			ProtocolGameAuditTestAccess::text(*f.protocol, MESSAGE_STATUS_DEFAULT, text);
			CHECK(f.body() == body(expected));
			f.reset();
			TextMessage message;
			message.type = MESSAGE_STATUS_DEFAULT;
			message.text = text;
			ProtocolGameAuditTestAccess::text(*f.protocol, message);
			CHECK(f.body() == body(expected));
			f.reset();
			expected.reset();
			expected.addByte(0x84);
			expected.addPosition(origin);
			expected.addByte(TEXTCOLOR_RED);
			expected.addString(text);
			ProtocolGameAuditTestAccess::animated(*f.protocol, text, origin, TEXTCOLOR_RED);
			CHECK(f.body() == body(expected));
		}
	});
}

TEST_CASE(combat_effect_and_animated_text_keep_visibility_filters)
{
	onDispatcher([] {
		SenderFixture f;
		const Position outsideViewport{1500,1500,7};
		ProtocolGameAuditTestAccess::effect(*f.protocol, outsideViewport, 1);
		ProtocolGameAuditTestAccess::animated(*f.protocol, "100", outsideViewport, TEXTCOLOR_RED);
		CHECK(f.body().empty());
		// A visible position without ground still cannot receive a magic effect.
		ProtocolGameAuditTestAccess::effect(*f.protocol, destination, 1);
		CHECK(f.body().empty());
	});
}

TEST_CASE(small_combat_packets_keep_traffic_accounting)
{
	onDispatcher([] {
		struct MetricsScope {
			bool previous = g_performanceMetrics.isEnabled();
			MetricsScope() { g_performanceMetrics.setEnabled(true); }
			~MetricsScope() { g_performanceMetrics.setEnabled(previous); }
		} metricsScope;
		SenderFixture f;
		(void)g_performanceMetrics.takeNetworkTrafficSnapshot();
		ProtocolGameAuditTestAccess::effect(*f.protocol, origin, 300);
		ProtocolGameAuditTestAccess::health(*f.protocol, f.player.get());
		ProtocolGameAuditTestAccess::text(*f.protocol, MESSAGE_STATUS_DEFAULT, "hit");
		const auto traffic = g_performanceMetrics.takeNetworkTrafficSnapshot();
		CHECK(traffic.outgoing[0x83].messages == 1 && traffic.outgoing[0x83].bytes == 8);
		CHECK(traffic.outgoing[0x8c].messages == 1 && traffic.outgoing[0x8c].bytes == 6);
		CHECK(traffic.outgoing[0xb4].messages == 1 && traffic.outgoing[0xb4].bytes == 7);
		CHECK(f.body().size() == 21);
	});
}

TEST_CASE(combat_sender_optional_700_recipient_cpu_measurement)
{
	if (!std::getenv("TFS_COMBAT_SENDER_BENCH")) return;
	onDispatcher([] {
		std::vector<std::unique_ptr<SenderFixture>> recipients;
		for (size_t i = 0; i < 700; ++i) recipients.push_back(std::make_unique<SenderFixture>());
		const std::string text = "Patient loses 1234 hitpoints due to an attack by a demon.";
		std::vector<double> measurements;
		for (size_t sample = 0; sample < 3; ++sample) {
			for (auto& recipient : recipients) recipient->reset();
			const auto started = std::chrono::steady_clock::now();
			for (size_t round = 0; round < 100; ++round) {
				for (auto& recipient : recipients) {
					auto& protocol = *recipient->protocol;
					ProtocolGameAuditTestAccess::effect(protocol, origin, 300);
					ProtocolGameAuditTestAccess::health(protocol, recipient->player.get());
					ProtocolGameAuditTestAccess::animated(protocol, "-1234", origin, TEXTCOLOR_RED);
					ProtocolGameAuditTestAccess::text(protocol, MESSAGE_STATUS_DEFAULT, text);
					ProtocolGameAuditTestAccess::distance(protocol, origin, destination, 300);
				}
			}
			measurements.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
			CHECK(recipients.front()->body().size() == 100 * (8+6+14+4+text.size()+13));
		}
		std::sort(measurements.begin(), measurements.end());
		std::cout << "COMBAT_SENDERS recipients=700 rounds=100 messages=350000 median_ms=" << measurements[1]
		          << " min_ms=" << measurements.front() << " max_ms=" << measurements.back() << '\n';
	});
}

TFS_TEST_MAIN()
