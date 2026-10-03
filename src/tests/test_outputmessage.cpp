#include "../otpch.h"

#include "../../tools/xtea/reference.h"
#include "../const.h"
#include "../outputmessage.h"
#include "../protocol.h"
#include "../xtea_simd.h"
#include "test_support.h"

namespace {

constexpr xtea::key TEST_KEY{0x01020304, 0x05060708, 0x090A0B0C, 0x0D0E0F10};

class TestProtocol final : public Protocol
{
public:
	TestProtocol(bool encrypted, bool checksum, bool raw = false) : Protocol(nullptr)
	{
		if (encrypted) {
			setXTEAKey(TEST_KEY);
			enableXTEAEncryption();
		}
		if (!checksum) {
			disableChecksum();
		}
		setRawMessages(raw);
	}
	void onRecvFirstMessage(NetworkMessage&) override {}
	void parsePacket(NetworkMessage& msg) override
	{
		received.assign(msg.getBuffer() + msg.getBufferPosition(),
		                msg.getBuffer() + msg.getBufferPosition() + msg.getLength());
	}
	std::vector<uint8_t> received;
};

template <typename T>
T read(const uint8_t* bytes)
{
	T value;
	std::memcpy(&value, bytes, sizeof(value));
	return value;
}

OutputMessage_ptr messageWithStorage(uint8_t fill)
{
	auto message = OutputMessagePool::getOutputMessage();
	// Simulate old pooled packet contents, not implicit constructor zeroing.
	std::fill_n(message->getBuffer(), NETWORKMESSAGE_MAXSIZE, fill);
	return message;
}

std::vector<uint8_t> wireBytes(const OutputMessage_ptr& message)
{
	return {message->getOutputBuffer(), message->getOutputBuffer() + message->getLength()};
}

} // namespace

TEST_CASE(output_headers_checksum_and_padding_ignore_old_storage)
{
	for (size_t length : {size_t{0}, size_t{1}, size_t{6}, size_t{7}, size_t{8}, size_t{15}, size_t{64}, size_t{8192},
	                      size_t{NetworkMessage::MAX_PROTOCOL_BODY_LENGTH}}) {
		std::vector<uint8_t> payload(length);
		for (size_t i = 0; i < length; ++i) {
			payload[i] = static_cast<uint8_t>(i * 17 + 3);
		}
		for (bool encrypted : {false, true}) {
			for (bool checksum : {false, true}) {
				auto clean = messageWithStorage(0);
				auto dirty = messageWithStorage(0xA5);
				clean->append(payload);
				dirty->append(payload);
				TestProtocol protocol(encrypted, checksum);
				protocol.onSendMessage(clean);
				protocol.onSendMessage(dirty);
				const auto wire = wireBytes(dirty);
				if (wire != wireBytes(clean)) {
					std::cerr << "storage mismatch: payload=" << length << " encrypted=" << encrypted
					          << " checksum=" << checksum << '\n';
				}
				CHECK(wire == wireBytes(clean));
				CHECK(read<uint16_t>(wire.data()) == wire.size() - 2);
				if (!encrypted) {
					CHECK(wire.size() == payload.size() + 2);
					CHECK(std::equal(payload.begin(), payload.end(), wire.begin() + 2));
					continue;
				}
				const size_t cryptoStart = checksum ? 6 : 2;
				if (checksum) {
					CHECK(read<uint32_t>(wire.data() + 2) ==
					      adlerChecksum(wire.data() + cryptoStart, wire.size() - cryptoStart));
				}
				std::vector<uint8_t> plain(wire.begin() + cryptoStart, wire.end());
				CHECK(plain.size() % 8 == 0);
				xtea::decrypt(plain.data(), plain.size(), xtea::expand_key(TEST_KEY));
				CHECK(read<uint16_t>(plain.data()) == length);
				CHECK(std::equal(payload.begin(), payload.end(), plain.begin() + 2));
				CHECK(std::all_of(plain.begin() + 2 + length, plain.end(), [](uint8_t byte) { return byte == 0x33; }));
			}
		}
	}
}

TEST_CASE(output_and_incoming_crypto_match_frozen_pr318_wire_bytes)
{
	std::vector<std::vector<uint8_t>> payloads;
	for (size_t length : {size_t{1}, size_t{6}, size_t{7}, size_t{8}, size_t{15}, size_t{64}, size_t{8192},
	                      size_t{NetworkMessage::MAX_PROTOCOL_BODY_LENGTH}}) {
		std::vector<uint8_t> payload(length);
		for (size_t i = 0; i < length; ++i) payload[i] = static_cast<uint8_t>(i * 17 + 3);
		payloads.push_back(std::move(payload));
	}
	NetworkMessage combat;
	combat.addByte(0x8C); // Creature health, production serializer primitives.
	combat.add<uint32_t>(0x40000001);
	combat.addByte(75);
	payloads.emplace_back(combat.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION,
	                      combat.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION + combat.getLength());
	NetworkMessage text;
	text.addByte(0xB4);
	text.addByte(MESSAGE_EVENT_ADVANCE);
	text.addString("You lose 25 hitpoints.");
	payloads.emplace_back(text.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION,
	                      text.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION + text.getLength());
	for (const auto& payload : payloads) {
		const size_t length = payload.size();
		std::vector<uint8_t> crypto(2 + length);
		const auto innerLength = static_cast<uint16_t>(length);
		std::memcpy(crypto.data(), &innerLength, 2);
		std::copy(payload.begin(), payload.end(), crypto.begin() + 2);
		crypto.resize((crypto.size() + 7) & ~size_t{7}, 0x33);
		xtea_reference::encrypt(crypto.data(), crypto.size(), xtea_reference::expand_key(TEST_KEY));
		for (bool checksum : {false, true}) {
			const size_t header = checksum ? 6 : 2;
			std::vector<uint8_t> expected(header + crypto.size());
			const auto outerLength = static_cast<uint16_t>(expected.size() - 2);
			std::memcpy(expected.data(), &outerLength, 2);
			if (checksum) {
				const auto adler = adlerChecksum(crypto.data(), crypto.size());
				std::memcpy(expected.data() + 2, &adler, 4);
			}
			std::copy(crypto.begin(), crypto.end(), expected.begin() + header);
			auto message = messageWithStorage(0xA5);
			message->append(payload);
			TestProtocol protocol(true, checksum);
			protocol.onSendMessage(message);
			CHECK(wireBytes(message) == expected);
		}
		// Simulate Connection after checksum/header parsing: crypto starts at 6.
		NetworkMessage incoming;
		incoming.skipBytes(-2);
		incoming.setLength(static_cast<uint16_t>(crypto.size() + 6));
		std::copy(crypto.begin(), crypto.end(), incoming.getBuffer() + 6);
		TestProtocol receiver(true, true);
		receiver.onRecvMessage(incoming);
		CHECK(receiver.received == payload);
		CHECK(!incoming.isOverrun());
		for (auto backend : {xtea::Backend::SSE2, xtea::Backend::AVX2}) {
			if (!xtea::detail::supportsBackend(backend)) continue;
			std::vector<uint8_t> backendCrypto(2 + length);
			std::memcpy(backendCrypto.data(), &innerLength, 2);
			std::copy(payload.begin(), payload.end(), backendCrypto.begin() + 2);
			backendCrypto.resize((backendCrypto.size() + 7) & ~size_t{7}, 0x33);
			auto encrypt = backend == xtea::Backend::SSE2 ? xtea::detail::encryptSse2 : xtea::detail::encryptAvx2;
			encrypt(backendCrypto.data(), backendCrypto.size(), xtea::expand_key(TEST_KEY));
			CHECK(backendCrypto == crypto);
		}
	}
}

TEST_CASE(output_login_challenge_fills_the_reserved_checksum)
{
	auto message = messageWithStorage(0xA5);
	message->skipBytes(sizeof(uint32_t));
	message->add<uint16_t>(6);
	message->addByte(0x1F);
	message->add<uint32_t>(0x12345678);
	message->addByte(0x42);
	message->skipBytes(-12);
	message->add<uint32_t>(adlerChecksum(message->getOutputBuffer() + 4, 8));
	TestProtocol protocol(false, true);
	protocol.onSendMessage(message);
	const auto wire = wireBytes(message);
	const std::vector<uint8_t> body{6, 0, 0x1F, 0x78, 0x56, 0x34, 0x12, 0x42};
	CHECK(wire.size() == 14);
	CHECK(read<uint16_t>(wire.data()) == 12);
	CHECK(read<uint32_t>(wire.data() + 2) == adlerChecksum(body.data(), body.size()));
	CHECK(std::equal(body.begin(), body.end(), wire.begin() + 6));
}

TEST_CASE(output_append_and_string_fallback_keep_only_written_bytes)
{
	NetworkMessage source;
	source.addByte(0xAA);
	source.add<uint16_t>(0x1234);
	auto appended = messageWithStorage(0xBB);
	appended->append(source);
	appended->addString(std::string("\xF0\x9F\x98\x80", 4));
	appended->addString(std::string(8193, 'x'));
	appended->addByte(0xCC);
	auto destination = messageWithStorage(0xDD);
	destination->append(appended);
	TestProtocol protocol(false, false, true);
	protocol.onSendMessage(destination);
	CHECK(wireBytes(destination) == std::vector<uint8_t>({0xAA, 0x34, 0x12, 0, 0, 0, 0, 0xCC}));
}

TEST_CASE(output_pool_reconstruction_resets_state_but_incoming_messages_stay_zeroed)
{
	{
		auto message = messageWithStorage(0xA5);
		message->addByte(0xAA);
		message->writeMessageLength();
		(void)message->get<uint32_t>();
		CHECK(message->isOverrun());
	}
	auto reused = OutputMessagePool::getOutputMessage();
	CHECK(reused->getLength() == 0);
	CHECK(reused->getBufferPosition() == NetworkMessage::INITIAL_BUFFER_POSITION);
	CHECK(!reused->isOverrun());
	CHECK(reused->getOutputBuffer() == reused->getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION);
	reused->addByte(0xEE);
	reused->writeMessageLength();
	CHECK(wireBytes(reused) == std::vector<uint8_t>({1, 0, 0xEE}));
	NetworkMessage incoming;
	CHECK(std::all_of(incoming.getBuffer(), incoming.getBuffer() + NETWORKMESSAGE_MAXSIZE,
	                  [](uint8_t byte) { return byte == 0; }));
}

TEST_CASE(output_fixed_wire_fixture)
{
	auto message = OutputMessagePool::getOutputMessage();
	message->addByte(0xA4);
	message->addByte(7);
	message->add<uint32_t>(2000);
	TestProtocol protocol(true, true);
	protocol.onSendMessage(message);
	// Captured from the unmodified protocol 8.60 serializer before this port.
	CHECK(wireBytes(message) ==
	      std::vector<uint8_t>({0x0C, 0, 0x23, 0x04, 0xB4, 0x14, 0xC5, 0x92, 0xB5, 0x98, 0x5C, 0x28, 0x6E, 0x8C}));
	std::cout << "protocol860_fixture=";
	for (uint8_t byte : wireBytes(message)) {
		std::cout << fmt::format("{:02x}", byte);
	}
	std::cout << '\n';
}

#ifdef OUTPUTMESSAGE_POOL_DIAGNOSTICS
TEST_CASE(output_pool_diagnostics_exclude_incoming_allocations)
{
	const auto before = OutputMessagePool::getDiagnostics();
	auto first = OutputMessagePool::getOutputMessage();
	auto second = OutputMessagePool::getOutputMessage();
	auto incoming = tfs::net::make_network_message();
	const auto active = OutputMessagePool::getDiagnostics();
	CHECK(active.hits + active.misses == before.hits + before.misses + 2);
	CHECK(active.inUse == before.inUse + 2);
	CHECK(active.peakInUse >= active.inUse);
	first.reset();
	second.reset();
	CHECK(OutputMessagePool::getDiagnostics().inUse == before.inUse);
}
#endif

TFS_TEST_MAIN()
