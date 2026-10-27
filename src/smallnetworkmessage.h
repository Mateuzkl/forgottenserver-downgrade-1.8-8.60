// Small outgoing bodies only. Incoming framing and large packets keep NetworkMessage.
#ifndef FS_SMALLNETWORKMESSAGE_H
#define FS_SMALLNETWORKMESSAGE_H

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include "networkmessage.h"
#include "position.h"

#include <simdutf.h>

template <size_t Capacity>
class SmallNetworkMessage final
{
	static_assert(Capacity > 0 && Capacity <= NetworkMessage::MAX_BODY_LENGTH);
public:
	void addByte(uint8_t value) { add(value); }

	template <typename T>
	void add(T value)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		if (!canAdd(sizeof(T))) { failed = true; return; }
		std::memcpy(buffer.data() + length, &value, sizeof(T));
		length += sizeof(T);
	}

	void addPosition(const Position& position)
	{
		add<uint16_t>(position.x);
		add<uint16_t>(position.y);
		addByte(position.z);
	}

	void addString(std::string_view value)
	{
		// Keep NetworkMessage's UTF-8 -> Latin-1 encoding and empty-string
		// fallback for unrepresentable/invalid input. Never emit a partial body.
		const auto stringLength = simdutf::latin1_length_from_utf8(value.data(), value.size());
		// UTF-8 input length is also a conservative bound for partial converter
		// output on invalid input. Callers use the large sender above this bound.
		if (stringLength > NetworkMessage::MAX_STRING_LENGTH || !canAdd(value.size() + sizeof(uint16_t))) {
			add<uint16_t>(0);
			return;
		}
		const auto start = length;
		add<uint16_t>(static_cast<uint16_t>(stringLength));
		const auto written = simdutf::convert_utf8_to_latin1(
		    value.data(), value.size(), reinterpret_cast<char*>(buffer.data() + length));
		if (written != stringLength) {
			length = start;
			add<uint16_t>(0);
			return;
		}
		length += written;
	}

	[[nodiscard]] std::span<const uint8_t> bytes() const
	{
		return failed ? std::span<const uint8_t>{} : std::span<const uint8_t>{buffer.data(), length};
	}

private:
	bool canAdd(size_t size) const { return !failed && size <= Capacity - length; }
	std::array<uint8_t, Capacity> buffer{};
	size_t length = 0;
	bool failed = false;
};

#endif
