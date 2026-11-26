// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "protocol.h"

#include "logger.h"
#include "outputmessage.h"
#include "rsa.h"
#include "tasks.h"
#include "xtea.h"

#include <cstring>

namespace {

bool XTEA_encrypt(OutputMessage& msg, const xtea::round_keys& key)
{
	// The encrypted message (2-byte inner length + payload + padding) must be a
	// non-zero multiple of 8 bytes and fit strictly within the inline buffer.
	if (!msg.padToXteaBlock()) {
		return false;
	}

	const size_t length = msg.getLength();
	if (length == 0 || (length % NetworkMessage::XTEA_MULTIPLE) != 0) {
		return false;
	}

	uint8_t* buffer = msg.getOutputBuffer();
	xtea::encrypt(buffer, length, key);
	return true;
}

bool XTEA_decrypt(NetworkMessage& msg, const xtea::round_keys& key)
{
	const uint16_t position = msg.getBufferPosition();
	const uint16_t length = msg.getLength();
	if (position > length || length > NETWORKMESSAGE_MAXSIZE) {
		return false;
	}

	const size_t encryptedLength = static_cast<size_t>(length) - static_cast<size_t>(position);
	if (encryptedLength < NetworkMessage::XTEA_MULTIPLE ||
	    (encryptedLength % NetworkMessage::XTEA_MULTIPLE) != 0) {
		return false;
	}

	uint8_t* buffer = msg.getBuffer() + position;
	xtea::decrypt(buffer, encryptedLength, key);

	const uint16_t innerLength = msg.get<uint16_t>();
	if (innerLength > encryptedLength - NetworkMessage::HEADER_LENGTH) {
		return false;
	}

	msg.setPayloadLength(innerLength);
	return true;
}

} // namespace

void Protocol::onSendMessage(const OutputMessage_ptr& msg) const
{
	if (!msg || rawMessages) {
		return;
	}

	if (encryptionEnabled) {
		const size_t requiredHeaderSpace =
		    static_cast<size_t>(NetworkMessage::HEADER_LENGTH) +
		    (checksumEnabled ? static_cast<size_t>(NetworkMessage::CHECKSUM_LENGTH) : 0) +
		    static_cast<size_t>(NetworkMessage::HEADER_LENGTH);
		const size_t maxPayloadLength = std::min<size_t>(
		    getMaxOutgoingPayloadLength(), static_cast<size_t>(OutputMessage::MAX_PAYLOAD_LENGTH));
		if (msg->getLength() > maxPayloadLength ||
		    msg->getOutputBufferStart() < requiredHeaderSpace) {
			LOG_ERROR("[Protocol] Refusing to frame oversized or misaligned encrypted OutputMessage (len={}, max={}, start={}).",
			          msg->getLength(), maxPayloadLength, msg->getOutputBufferStart());
			msg->setLength(0);
			return;
		}
	}

	if (!msg->writeMessageLength()) {
		LOG_ERROR("[Protocol] Failed to write message length header (len={}, start={}).",
		          msg->getLength(), msg->getOutputBufferStart());
		msg->setLength(0);
		return;
	}

	if (encryptionEnabled) {
		if (!XTEA_encrypt(*msg, key) || !msg->addCryptoHeader(checksumEnabled)) {
			LOG_ERROR("[Protocol] Failed to pad/encrypt outgoing frame (len={}, start={}).",
			          msg->getLength(), msg->getOutputBufferStart());
			msg->setLength(0);
			return;
		}
	}
}

bool Protocol::validateOutgoingFrame(const OutputMessage& msg) const noexcept
{
	const size_t totalLength = msg.getLength();
	const size_t start = msg.getOutputBufferStart();
	const size_t maxWireLength = std::min<size_t>(getMaxOutgoingWireLength(), NETWORKMESSAGE_MAXSIZE);
	if (totalLength == 0 || start > NETWORKMESSAGE_MAXSIZE || totalLength > NETWORKMESSAGE_MAXSIZE - start ||
	    totalLength > maxWireLength) {
		return false;
	}

	if (rawMessages || !encryptionEnabled) {
		return true;
	}

	const size_t prefixLength =
	    static_cast<size_t>(NetworkMessage::HEADER_LENGTH) +
	    (checksumEnabled ? static_cast<size_t>(NetworkMessage::CHECKSUM_LENGTH) : 0);
	if (totalLength < prefixLength + static_cast<size_t>(NetworkMessage::XTEA_MULTIPLE)) {
		return false;
	}

	uint16_t wireFrameLength = 0;
	std::memcpy(&wireFrameLength, msg.getOutputBuffer(), sizeof(wireFrameLength));
	if (static_cast<size_t>(wireFrameLength) + static_cast<size_t>(NetworkMessage::HEADER_LENGTH) != totalLength) {
		return false;
	}

	const size_t encryptedLength = totalLength - prefixLength;
	return (encryptedLength % static_cast<size_t>(NetworkMessage::XTEA_MULTIPLE)) == 0;
}

void Protocol::onRecvMessage(NetworkMessage& msg)
{
	if (encryptionEnabled && !XTEA_decrypt(msg, key)) {
		return;
	}

	parsePacket(msg);
}

OutputMessage_ptr Protocol::getOutputBuffer(int32_t size)
{
	assert(g_dispatcher.isDispatcherThread() && "Protocol::getOutputBuffer must run on the dispatcher thread");
	const size_t requestedSize = size > 0 ? static_cast<size_t>(size) : 0;
	const size_t maxCoalescedLength = std::min<size_t>(
	    getMaxCoalescedBodyLength(),
	    std::min<size_t>(getMaxOutgoingPayloadLength(), static_cast<size_t>(OutputMessage::MAX_PAYLOAD_LENGTH)));
	if (!outputBuffer) {
		outputBuffer = OutputMessagePool::getOutputMessage();
	} else if (outputBuffer->getLength() > 0 &&
	           (static_cast<size_t>(outputBuffer->getLength()) + requestedSize) > maxCoalescedLength) {
		send(outputBuffer);
		outputBuffer = OutputMessagePool::getOutputMessage();
	}
	auto& pool = OutputMessagePool::getInstance();
	if (!inAutosend || pool.needsAutosendRetry()) {
		// Retry an event rejected under backpressure even when this buffer already
		// existed. Never flush here: the caller has not appended its payload yet.
		pool.addProtocolToAutosend(shared_from_this());
	}
	return outputBuffer;
}

bool Protocol::RSA_decrypt(NetworkMessage& msg)
{
	if (msg.getRemainingBufferLength() != RSA_BUFFER_LENGTH) {
		return false;
	}

	tfs::rsa::decrypt(msg.getRemainingBuffer(), RSA_BUFFER_LENGTH);
	return msg.getByte() == 0;
}

uint32_t Protocol::getIP() const
{
	if (auto connection = getConnection()) {
		return connection->getIP();
	}

	return 0;
}

uint32_t Protocol::getIP(std::string_view s) const
{
	asio::error_code error;
	if (auto ip = asio::ip::make_address_v4(s, error); !error) {
		return htonl(ip.to_uint());
	}

	return 0;
}
