#include "../otpch.h"

#include "../benchs/small_packet_probe.h"
#include "../networkmessage.h"
#include "../outputmessage.h"
#include "../protocol.h"
#include "test_support.h"

namespace {

class WireProtocol final : public Protocol
{
public:
	WireProtocol(bool encrypted, bool checksum) : Protocol(nullptr)
	{
		if (encrypted) {
			setXTEAKey({0x12345678, 0x87654321, 0xAABBCCDD, 0x10203040});
			enableXTEAEncryption();
		}
		if (!checksum) disableChecksum();
	}
	void onRecvFirstMessage(NetworkMessage&) override {}
};

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

TFS_TEST_MAIN()
