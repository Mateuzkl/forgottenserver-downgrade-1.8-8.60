// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "xtea_simd.h"

#include <cassert>
#include <cstring>
#if TFS_XTEA_X86 && defined(_MSC_VER)
#include <intrin.h>
#endif

namespace xtea {

namespace {
Backend detectBackend() noexcept
{
#if TFS_XTEA_X86 && defined(_MSC_VER)
	int info[4];
	__cpuid(info, 0);
	const int maxLeaf = info[0];
	if (maxLeaf < 1) return Backend::Scalar;
	__cpuidex(info, 1, 0);
	const bool sse2 = (info[3] & (1 << 26)) != 0;
	const bool avx = (info[2] & (1 << 28)) != 0;
	const bool osxsave = (info[2] & (1 << 27)) != 0;
	if (sse2 && avx && osxsave && maxLeaf >= 7 && (_xgetbv(0) & 6) == 6) {
		__cpuidex(info, 7, 0);
		if ((info[1] & (1 << 5)) != 0) return Backend::AVX2;
	}
	return sse2 ? Backend::SSE2 : Backend::Scalar;
#elif TFS_XTEA_X86 && (defined(__GNUC__) || defined(__clang__))
	// GCC/Clang's feature initialization includes OSXSAVE/XCR0 usability.
	__builtin_cpu_init();
	if (__builtin_cpu_supports("avx2")) return Backend::AVX2;
	if (__builtin_cpu_supports("sse2")) return Backend::SSE2;
	return Backend::Scalar;
#else
	return Backend::Scalar;
#endif
}

Backend hardwareBackend() noexcept
{
	static const Backend backend = detectBackend();
	return backend;
}

struct Dispatch
{
	Backend backend;
	detail::Function encrypt;
	detail::Function decrypt;
};

const Dispatch& dispatch() noexcept
{
	static const Dispatch chosen = [] {
#ifdef TFS_XTEA_FORCE_SCALAR
		return Dispatch{Backend::Scalar, detail::encryptScalar, detail::decryptScalar};
#else
		switch (hardwareBackend()) {
			case Backend::AVX2:
				return Dispatch{Backend::AVX2, detail::encryptAvx2, detail::decryptAvx2};
			case Backend::SSE2:
				return Dispatch{Backend::SSE2, detail::encryptSse2, detail::decryptSse2};
			default:
				return Dispatch{Backend::Scalar, detail::encryptScalar, detail::decryptScalar};
		}
#endif
	}();
	return chosen;
}
} // namespace

bool detail::supportsBackend(Backend backend) noexcept
{
	return backend == Backend::Scalar || (backend == Backend::SSE2 && hardwareBackend() != Backend::Scalar) ||
	       (backend == Backend::AVX2 && hardwareBackend() == Backend::AVX2);
}

Backend selectedBackend() noexcept { return dispatch().backend; }
std::string_view backendName() noexcept
{
	switch (selectedBackend()) {
		case Backend::AVX2:
			return "AVX2";
		case Backend::SSE2:
			return "SSE2";
		default:
			return "Scalar";
	}
}

void encrypt(uint8_t* data, size_t length, const round_keys& k)
{
	assert(length % 8 == 0);
	if (length < 32)
		detail::encryptScalar(data, length, k);
	else
		dispatch().encrypt(data, length, k);
}

void decrypt(uint8_t* data, size_t length, const round_keys& k)
{
	assert(length % 8 == 0);
	if (length < 32)
		detail::decryptScalar(data, length, k);
	else
		dispatch().decrypt(data, length, k);
}

round_keys expand_key(const key& k)
{
	constexpr uint32_t delta = 0x9E3779B9;
	round_keys expanded;

	for (uint32_t i = 0, sum = 0, next_sum = sum + delta; i < expanded.size();
	     i += 2, sum = next_sum, next_sum += delta) {
		expanded[i] = sum + k[sum & 3];
		expanded[i + 1] = next_sum + k[(next_sum >> 11) & 3];
	}

	return expanded;
}

void detail::encryptScalar(uint8_t* data, size_t length, const round_keys& k)
{
	for (auto i = 0u; i < k.size(); i += 2) {
		for (size_t offset = 0; offset < length; offset += 8) {
			auto it = data + offset;
			uint32_t left, right;
			std::memcpy(&left, it, 4);
			std::memcpy(&right, it + 4, 4);

			left += ((right << 4 ^ right >> 5) + right) ^ k[i];
			right += ((left << 4 ^ left >> 5) + left) ^ k[i + 1];

			std::memcpy(it, &left, 4);
			std::memcpy(it + 4, &right, 4);
		}
	}
}

void detail::decryptScalar(uint8_t* data, size_t length, const round_keys& k)
{
	for (auto i = k.size(); i > 0; i -= 2) {
		for (size_t offset = 0; offset < length; offset += 8) {
			auto it = data + offset;
			uint32_t left, right;
			std::memcpy(&left, it, 4);
			std::memcpy(&right, it + 4, 4);

			right -= ((left << 4 ^ left >> 5) + left) ^ k[i - 1];
			left -= ((right << 4 ^ right >> 5) + right) ^ k[i - 2];

			std::memcpy(it, &left, 4);
			std::memcpy(it + 4, &right, 4);
		}
	}
}

} // namespace xtea
