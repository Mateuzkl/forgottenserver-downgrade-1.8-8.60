// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_XTEA_H
#define FS_XTEA_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace xtea {

using key = std::array<uint32_t, 4>;
using round_keys = std::array<uint32_t, 64>;

enum class Backend : uint8_t
{
	Scalar,
	SSE2,
	AVX2
};

Backend selectedBackend() noexcept;
std::string_view backendName() noexcept;

round_keys expand_key(const key& k);
// In-place, unaligned-safe; length must be a multiple of eight (zero is valid).
void encrypt(uint8_t* data, size_t length, const round_keys& k);
void decrypt(uint8_t* data, size_t length, const round_keys& k);

} // namespace xtea

#endif // FS_XTEA_H
