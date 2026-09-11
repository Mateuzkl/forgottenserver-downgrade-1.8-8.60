// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_STORE_PROTOCOL_H
#define FS_STORE_PROTOCOL_H

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

} // namespace StoreProtocol

#endif // FS_STORE_PROTOCOL_H
