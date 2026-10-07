// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in LICENSE.
#pragma once

#include "xtea.h"

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#define TFS_XTEA_X86 1
#else
#define TFS_XTEA_X86 0
#endif

// Internal kernels and read-only test/benchmark access. No runtime setter.
// Call SIMD kernels only after supportsBackend() returns true.
namespace xtea::detail {
using Function = void (*)(uint8_t*, size_t, const round_keys&);
bool supportsBackend(Backend backend) noexcept;
void encryptScalar(uint8_t* data, size_t length, const round_keys& k);
void decryptScalar(uint8_t* data, size_t length, const round_keys& k);
void encryptSse2(uint8_t* data, size_t length, const round_keys& k);
void decryptSse2(uint8_t* data, size_t length, const round_keys& k);
void encryptAvx2(uint8_t* data, size_t length, const round_keys& k);
void decryptAvx2(uint8_t* data, size_t length, const round_keys& k);
} // namespace xtea::detail
