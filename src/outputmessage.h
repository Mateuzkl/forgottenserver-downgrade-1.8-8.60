// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_OUTPUTMESSAGE_H
#define FS_OUTPUTMESSAGE_H

#include "connection.h"
#include "networkmessage.h"
#include "tools.h"

#include <chrono>

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

	static constexpr MsgSize_t MAX_PAYLOAD_LENGTH = MAX_ENCRYPTED_PAYLOAD_LENGTH;

	[[nodiscard]] uint8_t* getOutputBuffer() noexcept { return &buffer[outputBufferStart]; }
	[[nodiscard]] const uint8_t* getOutputBuffer() const noexcept { return &buffer[outputBufferStart]; }
	[[nodiscard]] MsgSize_t getOutputBufferStart() const noexcept { return outputBufferStart; }

	bool writeMessageLength() noexcept { return add_header(info.length); }

	bool padToXteaBlock() noexcept
	{
		const size_t remainder = info.length % XTEA_MULTIPLE;
		if (remainder == 0) {
			return true;
		}

		const size_t paddingBytes = XTEA_MULTIPLE - remainder;
		const size_t writePos = static_cast<size_t>(outputBufferStart) + static_cast<size_t>(info.length);
		if (writePos > buffer.size() || paddingBytes > buffer.size() - writePos ||
		    paddingBytes > static_cast<size_t>(std::numeric_limits<MsgSize_t>::max() - info.length)) {
			return false;
		}

		std::fill_n(buffer.data() + writePos, paddingBytes, 0x33);
		info.length += static_cast<MsgSize_t>(paddingBytes);
		info.position = static_cast<MsgSize_t>(writePos + paddingBytes);
		return true;
	}

	bool addCryptoHeader(const bool addChecksum) noexcept
	{
		if (addChecksum) {
			if (!add_header(adlerChecksum(&buffer[outputBufferStart], info.length))) {
				return false;
			}
		}
		return writeMessageLength();
	}

	bool append(const NetworkMessage& msg) noexcept
	{
		const auto msgLen = msg.getLength();

		if (outputBufferStart != INITIAL_BUFFER_POSITION || msgLen > MAX_PAYLOAD_LENGTH ||
		    info.length > MAX_PAYLOAD_LENGTH - msgLen ||
		    static_cast<size_t>(info.position) + msgLen > buffer.size()) [[unlikely]] {
			return false;
		}

		std::memcpy(buffer.data() + info.position, msg.getBuffer() + INITIAL_BUFFER_POSITION, msgLen);
		info.length += msgLen;
		info.position += msgLen;
		return true;
	}

	bool append(const OutputMessage_ptr& msg) noexcept
	{
		if (!msg) [[unlikely]] {
			return false;
		}

		const auto msgLen = msg->getLength();

		if (outputBufferStart != INITIAL_BUFFER_POSITION || msgLen > MAX_PAYLOAD_LENGTH ||
		    info.length > MAX_PAYLOAD_LENGTH - msgLen ||
		    static_cast<size_t>(info.position) + msgLen > buffer.size()) [[unlikely]] {
			return false;
		}

		std::memcpy(buffer.data() + info.position, msg->getBuffer() + INITIAL_BUFFER_POSITION, msgLen);
		info.length += msgLen;
		info.position += msgLen;
		return true;
	}

	bool append(std::span<const uint8_t> bytes) noexcept
	{
		if (bytes.empty()) [[unlikely]] {
			return true;
		}

		if (outputBufferStart != INITIAL_BUFFER_POSITION || bytes.size() > MAX_PAYLOAD_LENGTH ||
		    info.length > MAX_PAYLOAD_LENGTH - bytes.size() ||
		    static_cast<size_t>(info.position) + bytes.size() > buffer.size()) [[unlikely]] {
			return false;
		}

		std::memcpy(buffer.data() + info.position, bytes.data(), bytes.size());
		info.length += static_cast<MsgSize_t>(bytes.size());
		info.position += static_cast<MsgSize_t>(bytes.size());
		return true;
	}

private:
	template <typename T>
	bool add_header(T add) noexcept
	{
		static_assert(std::is_trivially_copyable_v<T>,
			"Header type must be trivially copyable");

		if (outputBufferStart < sizeof(T) ||
		    sizeof(T) > static_cast<size_t>(std::numeric_limits<MsgSize_t>::max() - info.length)) [[unlikely]] {
			return false;
		}

		outputBufferStart -= static_cast<MsgSize_t>(sizeof(T));
		std::memcpy(buffer.data() + outputBufferStart, &add, sizeof(T));
		info.length += static_cast<MsgSize_t>(sizeof(T));
		return true;
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

	static void prewarmPool(size_t count);

	static void drainPool();

	void addProtocolToAutosend(Protocol_ptr protocol);

	void removeProtocolFromAutosend(const Protocol_ptr& protocol);
	void flushIfDue() noexcept;
	// Dispatcher-only. A stopped scheduler or exhausted IDs can reject its timer.
	bool needsAutosendRetry() const noexcept { return !sendScheduled; }

private:
	OutputMessagePool()
	{
		bufferedProtocols.reserve(256);
		flushingProtocols.reserve(256);
	}

	void scheduleSendAll(std::chrono::milliseconds delay) noexcept;
	void sendAll() noexcept;

	std::vector<Protocol_ptr> bufferedProtocols;
	// Keep both batches' storage between flushes. A local swapped vector used to
	// free the reserved capacity after every 10 ms batch, then allocate it again.
	std::vector<Protocol_ptr> flushingProtocols;
	std::chrono::steady_clock::time_point firstBufferedAt{};
	bool sendScheduled = false;
	friend struct OutputMessagePoolTestAccess;
};

#endif // FS_OUTPUTMESSAGE_H
