#include "../otpch.h"

#include "../benchs/small_packet_probe.h"
#include "../networkmessage.h"
#include "../outputmessage.h"
#include "../protocol.h"
#include "../scheduler.h"
#include "../tasks.h"
#include "../tools.h"
#include "test_support.h"

#include <cstring>
#include <vector>

namespace {

class WireProtocol final : public Protocol
{
public:
	WireProtocol(bool encrypted, bool checksum, uint32_t maxCoalesced = NetworkMessage::MAX_PROTOCOL_BODY_LENGTH) :
	    Protocol(nullptr), maxCoalescedBody(maxCoalesced)
	{
		if (encrypted) {
			setXTEAKey({0x12345678, 0x87654321, 0xAABBCCDD, 0x10203040});
			enableXTEAEncryption();
		}
		if (!checksum) disableChecksum();
	}
	void onRecvFirstMessage(NetworkMessage&) override {}
	void parsePacket(NetworkMessage& msg) override
	{
		parsedPayloads.emplace_back(msg.getBuffer() + msg.getBufferPosition(),
		                            msg.getBuffer() + msg.getBufferPosition() + msg.getLength());
	}
	uint32_t getMaxCoalescedBodyLength() const noexcept override { return maxCoalescedBody; }

	std::vector<std::vector<uint8_t>> parsedPayloads;

private:
	uint32_t maxCoalescedBody;
};

void verifyEncryptedRoundTrip(uint16_t payloadLength, bool checksum)
{
	WireProtocol protocol(true, checksum);
	auto output = std::make_shared<OutputMessage>();
	std::vector<uint8_t> payload(payloadLength);
	for (size_t i = 0; i < payload.size(); ++i) {
		payload[i] = static_cast<uint8_t>((i * 31u + 7u) & 0xFFu);
	}
	CHECK(output->append(std::span<const uint8_t>{payload}));
	CHECK(output->getLength() == payloadLength);

	protocol.onSendMessage(output);
	CHECK(protocol.validateOutgoingFrame(*output));

	const size_t totalLen = output->getLength();
	const size_t start = output->getOutputBufferStart();
	CHECK(start + totalLen <= NETWORKMESSAGE_MAXSIZE);

	const uint8_t* wire = output->getOutputBuffer();
	uint16_t outerLen = 0;
	std::memcpy(&outerLen, wire, sizeof(outerLen));
	CHECK(static_cast<size_t>(outerLen) + NetworkMessage::HEADER_LENGTH == totalLen);

	const size_t checksumBytes = checksum ? NetworkMessage::CHECKSUM_LENGTH : 0;
	const size_t encryptedLen = static_cast<size_t>(outerLen) - checksumBytes;
	CHECK(encryptedLen >= NetworkMessage::XTEA_MULTIPLE);
	CHECK((encryptedLen % NetworkMessage::XTEA_MULTIPLE) == 0);

	if (checksum) {
		uint32_t wireChecksum = 0;
		std::memcpy(&wireChecksum, wire + static_cast<size_t>(NetworkMessage::HEADER_LENGTH), sizeof(wireChecksum));
		const uint32_t expectedChecksum = adlerChecksum(
		    wire + static_cast<size_t>(NetworkMessage::HEADER_LENGTH) +
		        static_cast<size_t>(NetworkMessage::CHECKSUM_LENGTH),
		    encryptedLen);
		CHECK(wireChecksum == expectedChecksum);
	}

	// Simulate Connection::parseHeader + Connection::parsePacket + Protocol::onRecvMessage.
	NetworkMessage incoming;
	std::memcpy(incoming.getBuffer(), wire, totalLen);
	incoming.setLength(static_cast<uint16_t>(totalLen));
	(void)incoming.getBodyBuffer(); // sets position = 2
	if (checksum) {
		(void)incoming.get<uint32_t>(); // sets position = 6
	}
	protocol.onRecvMessage(incoming);
	CHECK(protocol.parsedPayloads.size() == 1);
	CHECK(protocol.parsedPayloads.front() == payload);
}

} // namespace

TEST_CASE(small_packet_probe_matches_legacy_bytes_and_encrypted_framing)
{
	for (const auto pos : {Position{0, 0, 0}, Position{65535, 65535, 15}, Position{100, 200, 7}}) {
		for (const uint16_t effect : {0, 1, 255, 256, 65535}) {
			NetworkMessage baseline;
			baseline.addByte(0x83);
			baseline.addPosition(pos);
			baseline.add<uint16_t>(effect);
			const auto bytes = smallEffectPacketProbe(pos, effect);
			CHECK(baseline.getLength() == bytes.size());
			CHECK(std::equal(bytes.begin(), bytes.end(), baseline.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION));
			for (bool encrypted : {false, true}) {
				for (bool checksum : {false, true}) {
					WireProtocol protocol(encrypted, checksum);
					auto legacy = std::make_shared<OutputMessage>();
					auto probe = std::make_shared<OutputMessage>();
					legacy->append(baseline);
					probe->append(std::span<const uint8_t>{bytes});
					protocol.onSendMessage(legacy);
					protocol.onSendMessage(probe);
					CHECK(legacy->getLength() == probe->getLength());
					CHECK(std::memcmp(legacy->getOutputBuffer(), probe->getOutputBuffer(), legacy->getLength()) == 0);
				}
			}
		}
	}
}

TEST_CASE(small_packet_append_owns_bytes_after_stack_writer_expires)
{
	OutputMessage output;
	{
		auto bytes = smallEffectPacketProbe(Position{100, 200, 7}, 65535);
		output.append(std::span<const uint8_t>{bytes});
		bytes.fill(0);
	}
	const std::array<uint8_t, 8> golden{0x83, 100, 0, 200, 0, 7, 255, 255};
	CHECK(std::equal(golden.begin(), golden.end(), output.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION));
}

TEST_CASE(effect_parser_marks_each_truncated_payload_overrun)
{
	const auto bytes = smallEffectPacketProbe(Position{65535, 65535, 15}, 65535);
	for (uint16_t length = 0; length <= bytes.size(); ++length) {
		NetworkMessage input;
		std::memcpy(input.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION, bytes.data(), length);
		input.setLength(length);
		(void)input.getByte();
		(void)input.getPosition();
		(void)input.get<uint16_t>();
		CHECK(input.isOverrun() == (length < bytes.size()));
	}
}

TEST_CASE(xtea_framing_covers_all_alignments_and_maximum_payload_boundaries)
{
	// Small sizes across all 8 XTEA alignments (0..7)
	for (uint16_t len = 0; len <= 32; ++len) {
		verifyEncryptedRoundTrip(len, true);
		verifyEncryptedRoundTrip(len, false);
	}

	// CipSoft 16 KiB coalescing boundary across all alignments
	for (uint16_t len = NetworkMessage::CIPSOFT_MAX_COALESCED_BODY_LENGTH - 16;
	     len <= NetworkMessage::CIPSOFT_MAX_COALESCED_BODY_LENGTH + 16; ++len) {
		verifyEncryptedRoundTrip(len, true);
		verifyEncryptedRoundTrip(len, false);
	}

	// Upper boundary around MAX_PROTOCOL_BODY_LENGTH (65476) through MAX_PAYLOAD_LENGTH (65486),
	// including 65471..65476 which previously caused silent padding loss and invalid frames.
	for (uint16_t len = NetworkMessage::MAX_PROTOCOL_BODY_LENGTH - 16;
	     len <= OutputMessage::MAX_PAYLOAD_LENGTH; ++len) {
		verifyEncryptedRoundTrip(len, true);
		verifyEncryptedRoundTrip(len, false);
	}
}

TEST_CASE(network_message_padding_never_overflows_or_fails_silently)
{
	NetworkMessage msg;
	CHECK(msg.addPaddingBytes(0));
	CHECK(msg.getLength() == 0);
	CHECK(msg.getBufferPosition() == NetworkMessage::INITIAL_BUFFER_POSITION);

	// Advance right up to 7 bytes before NETWORKMESSAGE_MAXSIZE and verify padding succeeds.
	const uint16_t maxPayloadBefore7Pad = NETWORKMESSAGE_MAXSIZE - NetworkMessage::INITIAL_BUFFER_POSITION - 7;
	CHECK(msg.setBufferPosition(maxPayloadBefore7Pad));
	msg.setLength(maxPayloadBefore7Pad);
	CHECK(msg.addPaddingBytes(7));
	CHECK(msg.getLength() == maxPayloadBefore7Pad + 7);
	CHECK(msg.getBufferPosition() == NETWORKMESSAGE_MAXSIZE);

	// One more byte must fail cleanly without mutating length or overflowing the buffer.
	CHECK(!msg.addPaddingBytes(1));
	CHECK(msg.getLength() == maxPayloadBefore7Pad + 7);
	CHECK(msg.getBufferPosition() == NETWORKMESSAGE_MAXSIZE);
}

TEST_CASE(output_message_and_protocol_reject_oversized_or_corrupt_frames)
{
	OutputMessage output;
	std::vector<uint8_t> maxPayload(OutputMessage::MAX_PAYLOAD_LENGTH, 0x42);
	CHECK(output.append(std::span<const uint8_t>{maxPayload}));
	CHECK(output.getLength() == OutputMessage::MAX_PAYLOAD_LENGTH);
	// Even 1 extra byte above MAX_PAYLOAD_LENGTH must be rejected before encryption.
	const std::array<uint8_t, 1> oneByte{0x01};
	CHECK(!output.append(std::span<const uint8_t>{oneByte}));
	CHECK(output.getLength() == OutputMessage::MAX_PAYLOAD_LENGTH);

	// Manually crafted oversized length must be refused by onSendMessage & validateOutgoingFrame.
	WireProtocol protocol(true, true);
	auto oversized = std::make_shared<OutputMessage>();
	oversized->setLength(OutputMessage::MAX_PAYLOAD_LENGTH + 1);
	protocol.onSendMessage(oversized);
	CHECK(oversized->getLength() == 0);
	CHECK(!protocol.validateOutgoingFrame(*oversized));

	// Manually corrupted non-multiple-of-8 frame must fail validateOutgoingFrame.
	auto malformed = std::make_shared<OutputMessage>();
	std::array<uint8_t, 10> body{};
	CHECK(malformed->append(std::span<const uint8_t>{body}));
	protocol.onSendMessage(malformed);
	CHECK(protocol.validateOutgoingFrame(*malformed));
	malformed->setLength(static_cast<uint16_t>(malformed->getLength() - 1));
	CHECK(!protocol.validateOutgoingFrame(*malformed));
}

TEST_CASE(xtea_decrypt_rejects_truncated_and_misaligned_incoming_packets)
{
	WireProtocol protocol(true, true);
	for (uint16_t badLen : {uint16_t{0}, uint16_t{2}, uint16_t{4}, uint16_t{6}, uint16_t{7}, uint16_t{13},
	                        uint16_t{15}, uint16_t{21}}) {
		NetworkMessage incoming;
		incoming.setLength(badLen);
		(void)incoming.getBodyBuffer();
		if (badLen >= 6) {
			(void)incoming.get<uint32_t>();
		}
		protocol.onRecvMessage(incoming);
	}
	CHECK(protocol.parsedPayloads.empty());
}

TEST_CASE(protocol_coalescing_flushes_at_protocol_and_cipsoft_limits)
{
	struct Cleanup
	{
		~Cleanup()
		{
			g_scheduler.shutdown();
			g_dispatcher.shutdown();
			g_reactor.drain();
		}
	} cleanup;

	g_dispatcher.start();
	g_scheduler.start();

	bool verified = false;
	std::exception_ptr failure;
	g_dispatcher.addTask([&] {
		try {
			auto otcProtocol = std::make_shared<WireProtocol>(true, true, NetworkMessage::MAX_PROTOCOL_BODY_LENGTH);
			auto cipProtocol =
			    std::make_shared<WireProtocol>(true, true, NetworkMessage::CIPSOFT_MAX_COALESCED_BODY_LENGTH);

			// Calling getOutputBuffer on an empty buffer with large size must not flush an empty message.
			auto firstCip = cipProtocol->getOutputBuffer(NetworkMessage::CIPSOFT_MAX_COALESCED_BODY_LENGTH);
			std::vector<uint8_t> cipChunk(NetworkMessage::CIPSOFT_MAX_COALESCED_BODY_LENGTH - 1, 0x5A);
			CHECK(firstCip->append(std::span<const uint8_t>{cipChunk}));
			// Next 2 bytes cross the CipSoft coalescing limit -> rotates to a fresh OutputMessage.
			auto nextCip = cipProtocol->getOutputBuffer(2);
			CHECK(nextCip != firstCip);
			CHECK(nextCip->getLength() == 0);
			// The flushed buffer still frames cleanly across XTEA.
			cipProtocol->onSendMessage(firstCip);
			CHECK(cipProtocol->validateOutgoingFrame(*firstCip));

			// Same check at OTC MAX_PROTOCOL_BODY_LENGTH (65476).
			auto firstOtc = otcProtocol->getOutputBuffer(NetworkMessage::MAX_PROTOCOL_BODY_LENGTH);
			std::vector<uint8_t> otcChunk(NetworkMessage::MAX_PROTOCOL_BODY_LENGTH, 0x7E);
			CHECK(firstOtc->append(std::span<const uint8_t>{otcChunk}));
			auto nextOtc = otcProtocol->getOutputBuffer(1);
			CHECK(nextOtc != firstOtc);
			otcProtocol->onSendMessage(firstOtc);
			CHECK(otcProtocol->validateOutgoingFrame(*firstOtc));

			OutputMessagePool::getInstance().removeProtocolFromAutosend(cipProtocol);
			OutputMessagePool::getInstance().removeProtocolFromAutosend(otcProtocol);
			verified = true;
		} catch (...) {
			failure = std::current_exception();
		}
		g_dispatcher.shutdown();
	});

	g_reactor.runLoop();
	if (failure) std::rethrow_exception(failure);
	CHECK(verified);
}

TFS_TEST_MAIN()
