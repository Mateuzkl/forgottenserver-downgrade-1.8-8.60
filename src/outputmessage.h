// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_OUTPUTMESSAGE_H
#define FS_OUTPUTMESSAGE_H

#include "connection.h"
#include "networkmessage.h"
#include "tools.h"

class OutputMessage : public NetworkMessage
{
public:
	OutputMessage() = default;
	~OutputMessage() = default;

	// non-copyable
	OutputMessage(const OutputMessage&) = delete;
	OutputMessage& operator=(const OutputMessage&) = delete;

	// movable (allows efficient ownership transfer)
	OutputMessage(OutputMessage&&) noexcept = default;
	OutputMessage& operator=(OutputMessage&&) noexcept = default;

	[[nodiscard]] uint8_t* getOutputBuffer() noexcept { return &buffer[outputBufferStart]; }

	void writeMessageLength() noexcept { add_header(info.length); }

	void addCryptoHeader(const bool addChecksum) noexcept
	{
		if (addChecksum) {
			add_header(adlerChecksum(&buffer[outputBufferStart], info.length));
		}
		writeMessageLength();
	}

	void append(const NetworkMessage& msg) noexcept
	{
		const auto msgLen = msg.getLength();

		if (info.position + msgLen > buffer.size()) [[unlikely]] {
			// In debug, assert fails. In release, behavior is undefined.
			assert(false && "Buffer overflow in OutputMessage::append");
			return;
		}

		std::memcpy(buffer.data() + info.position, msg.getBuffer() + 8, msgLen);
		info.length += msgLen;
		info.position += msgLen;
	}

	void append(const OutputMessage_ptr& msg) noexcept
	{
		const auto msgLen = msg->getLength();

		if (info.position + msgLen > buffer.size()) [[unlikely]] {
			assert(false && "Buffer overflow in OutputMessage::append");
			return;
		}

		std::memcpy(buffer.data() + info.position, msg->getBuffer() + 8, msgLen);
		info.length += msgLen;
		info.position += msgLen;
	}

	void append(std::span<const uint8_t> bytes) noexcept
	{
		if (bytes.empty()) [[unlikely]] {
			return;
		}

		if (info.position + bytes.size() > buffer.size()) [[unlikely]] {
			assert(false && "Buffer overflow in OutputMessage::append");
			return;
		}

		std::memcpy(buffer.data() + info.position, bytes.data(), bytes.size());
		info.length += static_cast<MsgSize_t>(bytes.size());
		info.position += static_cast<MsgSize_t>(bytes.size());
	}

private:
	template <typename T>
	void add_header(T add) noexcept
	{
		static_assert(std::is_trivially_copyable_v<T>, "Header type must be trivially copyable");

		if (outputBufferStart < sizeof(T)) [[unlikely]] {
			assert(false && "Not enough space for header");
			return;
		}

		outputBufferStart -= static_cast<MsgSize_t>(sizeof(T));
		std::memcpy(buffer.data() + outputBufferStart, &add, sizeof(T));
		info.length += static_cast<MsgSize_t>(sizeof(T));
	}

	MsgSize_t outputBufferStart = INITIAL_BUFFER_POSITION;
};

class OutputMessagePool
{
public:
	// non-copyable
	OutputMessagePool(const OutputMessagePool&) = delete;
	OutputMessagePool& operator=(const OutputMessagePool&) = delete;

	static OutputMessagePool& getInstance()
	{
		static OutputMessagePool instance;
		return instance;
	}

	static OutputMessage_ptr getOutputMessage();

	void addProtocolToAutosend(Protocol_ptr protocol);

	void removeProtocolFromAutosend(const Protocol_ptr& protocol);

private:
	OutputMessagePool() = default;

	std::vector<Protocol_ptr> bufferedProtocols;
};

#endif // FS_OUTPUTMESSAGE_H
