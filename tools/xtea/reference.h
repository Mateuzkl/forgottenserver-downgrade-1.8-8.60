// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in LICENSE.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Frozen round-major algorithm from PR #318, ff7600fa. Test/benchmark only.
namespace xtea_reference {
using key = std::array<uint32_t, 4>;
using round_keys = std::array<uint32_t, 64>;
inline round_keys expand_key(const key& k)
{
	round_keys out;
	for (uint32_t i = 0, sum = 0; i < 64; i += 2) {
		out[i] = sum + k[sum & 3];
		sum += 0x9E3779B9u;
		out[i + 1] = sum + k[(sum >> 11) & 3];
	}
	return out;
}
inline void encrypt(uint8_t* data, size_t length, const round_keys& k)
{
	for (size_t i = 0; i < k.size(); i += 2) {
		for (size_t offset = 0; offset < length; offset += 8) {
			uint32_t left, right;
			std::memcpy(&left, data + offset, 4);
			std::memcpy(&right, data + offset + 4, 4);
			left += ((right << 4 ^ right >> 5) + right) ^ k[i];
			right += ((left << 4 ^ left >> 5) + left) ^ k[i + 1];
			std::memcpy(data + offset, &left, 4);
			std::memcpy(data + offset + 4, &right, 4);
		}
	}
}
inline void decrypt(uint8_t* data, size_t length, const round_keys& k)
{
	for (size_t i = k.size(); i > 0; i -= 2) {
		for (size_t offset = 0; offset < length; offset += 8) {
			uint32_t left, right;
			std::memcpy(&left, data + offset, 4);
			std::memcpy(&right, data + offset + 4, 4);
			right -= ((left << 4 ^ left >> 5) + left) ^ k[i - 1];
			left -= ((right << 4 ^ right >> 5) + right) ^ k[i - 2];
			std::memcpy(data + offset, &left, 4);
			std::memcpy(data + offset + 4, &right, 4);
		}
	}
}
} // namespace xtea_reference
