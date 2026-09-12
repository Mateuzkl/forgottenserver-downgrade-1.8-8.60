// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_STORE_PROTOCOL_H
#define FS_STORE_PROTOCOL_H

#include "networkmessage.h"
#include "store/store_types.h"

#include <cstdint>

/// Named constants for the custom 8.60 store wire protocol.
/// These are consumed by AstraClient / OTClient and MUST NOT change.
namespace StoreProtocol {

enum class ClientOpcode : uint8_t
{
	Transfer = 0xF8,
	History = 0xFA,
	Open = 0xFB,
	Buy = 0xFC,
};

/// The single server → client opcode for all store responses.
inline constexpr uint8_t ServerOpcode = 0xFD;

enum class ResponseType : uint8_t
{
	Error = 0x00,
	Catalog = 0x01,
	Success = 0x02,
	History = 0x03,
};

[[nodiscard]] constexpr StoreHighlightState effectiveHighlightState(StoreHighlightState state,
                                                                    uint32_t validUntilTimestamp,
                                                                    uint32_t nowTimestamp) noexcept
{
	if (storeHighlightHasExpiration(state) && validUntilTimestamp != 0 &&
	    validUntilTimestamp <= nowTimestamp) {
		return StoreHighlightState::None;
	}
	return state;
}

inline void addCategoryHighlight(NetworkMessage& msg, bool enabled, StoreHighlightState state)
{
	if (enabled) {
		msg.addByte(static_cast<uint8_t>(state));
	}
}

inline void addOfferHighlight(NetworkMessage& msg, bool enabled, StoreHighlightState state,
                              uint32_t validUntilTimestamp, uint32_t nowTimestamp)
{
	if (!enabled) {
		return;
	}

	const auto effectiveState = effectiveHighlightState(state, validUntilTimestamp, nowTimestamp);
	msg.addByte(static_cast<uint8_t>(effectiveState));
	if (storeHighlightHasExpiration(effectiveState)) {
		msg.add<uint32_t>(validUntilTimestamp);
	}
}

} // namespace StoreProtocol

#endif // FS_STORE_PROTOCOL_H
