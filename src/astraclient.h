// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_ASTRACLIENT_H
#define FS_ASTRACLIENT_H

#include "xtea.h"

#include <cstdint>
#include <string_view>

namespace AstraClient {

inline constexpr std::string_view LOGIN_MARKER = "A";
inline constexpr std::string_view REQUIRED_MESSAGE = "This server requires AstraClient.";

inline uint32_t rotateLeft(uint32_t value, uint8_t bits)
{
	return (value << bits) | (value >> (32 - bits));
}

inline uint32_t mixSignature(uint32_t hash, uint32_t value)
{
	hash ^= value + 0x9E3779B9 + (hash << 6) + (hash >> 2);
	return rotateLeft(hash, 7) ^ (value >> 3);
}

inline uint32_t generateSignature(uint16_t operatingSystem, uint16_t version, const xtea::key& key,
                                  uint32_t challengeTimestamp = 0, uint8_t challengeRandom = 0)
{
	uint32_t hash = 0xA57AC11E;
	hash = mixSignature(hash, 0x41737472);
	hash = mixSignature(hash, 0x61436C69);
	hash = mixSignature(hash, operatingSystem);
	hash = mixSignature(hash, version);
	for (const uint32_t value : key) {
		hash = mixSignature(hash, value);
	}
	hash = mixSignature(hash, challengeTimestamp);
	hash = mixSignature(hash, challengeRandom);
	return hash ^ 0x4D415354;
}

} // namespace AstraClient

#endif // FS_ASTRACLIENT_H
