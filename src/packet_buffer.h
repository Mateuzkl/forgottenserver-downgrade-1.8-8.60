// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in LICENSE.

#ifndef FS_PACKET_BUFFER_H
#define FS_PACKET_BUFFER_H

#include "networkmessage.h"
#include "position.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <simdutf.h>

namespace tfs::net {

// A bounded, initialized, write-only plaintext payload. No reserved headers,
// arbitrary seeks, or receive operations. bytes() exposes only the written
// prefix, which must be copied into a recipient-owned OutputMessage before
// this stack object dies. Never hand its storage to an asynchronous write.
template <size_t Capacity>
class PacketBuffer
{
	static_assert(Capacity > 0 && Capacity <= NetworkMessage::MAX_PROTOCOL_BODY_LENGTH);

public:
	void addByte(uint8_t value) noexcept { add(value); }

	template <typename T>
	void add(T value) noexcept
	{
		static_assert(std::is_integral_v<T>);
		if (!canAdd(sizeof(T))) {
			failed = true;
			return;
		}
		// Matches NetworkMessage's existing integer byte order exactly.
		std::memcpy(buffer.data() + length, &value, sizeof(T));
		length += sizeof(T);
	}

	void addPosition(const Position& position) noexcept
	{
		add<uint16_t>(position.x);
		add<uint16_t>(position.y);
		addByte(position.z);
	}

	void addString(std::string_view value) noexcept
	{
		if (!canAdd(sizeof(uint16_t))) {
			failed = true;
			return;
		}
		const auto stringLength = simdutf::latin1_length_from_utf8(value.data(), value.size());
		if (stringLength > NetworkMessage::MAX_STRING_LENGTH || !canAdd(stringLength + sizeof(uint16_t))) {
			add<uint16_t>(0);
			return;
		}
		const auto start = length;
		add<uint16_t>(static_cast<uint16_t>(stringLength));
		const auto written = simdutf::convert_utf8_to_latin1(value.data(), value.size(),
		                                                     reinterpret_cast<char*>(buffer.data() + length));
		if (written != stringLength) {
			length = start;
			add<uint16_t>(0);
			return;
		}
		length += written;
	}

	[[nodiscard]] std::span<const uint8_t> bytes() const noexcept
	{
		return failed ? std::span<const uint8_t>{} : std::span<const uint8_t>{buffer.data(), length};
	}

private:
	bool canAdd(size_t size) const noexcept { return !failed && size <= Capacity - length; }
	std::array<uint8_t, Capacity> buffer{};
	size_t length = 0;
	bool failed = false;
};

} // namespace tfs::net
#endif
