// Copyright 2024 Black Tek Server Authors. All rights reserved.
// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// GPL-2.0; four-block layout adapted from BlackTek-Server 828b89395f28852f.
#include "xtea_simd.h"

#if TFS_XTEA_X86
#include <emmintrin.h>
#if defined(__GNUC__) || defined(__clang__)
#define TFS_SSE2 __attribute__((target("sse2"), noinline))
#else
#define TFS_SSE2 __declspec(noinline)
#endif

namespace xtea::detail {
template <bool Decrypt>
TFS_SSE2 void transformSse2(uint8_t* data, size_t length, const round_keys& k)
{
	while (length >= 32) {
		const auto a =
		    _mm_shuffle_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(data)), _MM_SHUFFLE(3, 1, 2, 0));
		const auto b =
		    _mm_shuffle_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(data + 16)), _MM_SHUFFLE(3, 1, 2, 0));
		auto left = _mm_unpacklo_epi64(a, b);
		auto right = _mm_unpackhi_epi64(a, b);
		for (size_t round = 0; round < k.size(); round += 2) {
			const size_t i = Decrypt ? k.size() - 2 - round : round;
			const auto ka = _mm_set1_epi32(static_cast<int32_t>(k[i]));
			const auto kb = _mm_set1_epi32(static_cast<int32_t>(k[i + 1]));
			if constexpr (Decrypt) {
				right = _mm_sub_epi32(
				    right,
				    _mm_xor_si128(_mm_add_epi32(_mm_xor_si128(_mm_slli_epi32(left, 4), _mm_srli_epi32(left, 5)), left),
				                  kb));
				left = _mm_sub_epi32(
				    left,
				    _mm_xor_si128(
				        _mm_add_epi32(_mm_xor_si128(_mm_slli_epi32(right, 4), _mm_srli_epi32(right, 5)), right), ka));
			} else {
				left = _mm_add_epi32(
				    left,
				    _mm_xor_si128(
				        _mm_add_epi32(_mm_xor_si128(_mm_slli_epi32(right, 4), _mm_srli_epi32(right, 5)), right), ka));
				right = _mm_add_epi32(
				    right,
				    _mm_xor_si128(_mm_add_epi32(_mm_xor_si128(_mm_slli_epi32(left, 4), _mm_srli_epi32(left, 5)), left),
				                  kb));
			}
		}
		_mm_storeu_si128(reinterpret_cast<__m128i*>(data), _mm_unpacklo_epi32(left, right));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(data + 16), _mm_unpackhi_epi32(left, right));
		data += 32;
		length -= 32;
	}
	if constexpr (Decrypt)
		decryptScalar(data, length, k);
	else
		encryptScalar(data, length, k);
}
TFS_SSE2 void encryptSse2(uint8_t* data, size_t length, const round_keys& k) { transformSse2<false>(data, length, k); }
TFS_SSE2 void decryptSse2(uint8_t* data, size_t length, const round_keys& k) { transformSse2<true>(data, length, k); }
} // namespace xtea::detail
#else
namespace xtea::detail {
void encryptSse2(uint8_t* data, size_t length, const round_keys& k) { encryptScalar(data, length, k); }
void decryptSse2(uint8_t* data, size_t length, const round_keys& k) { decryptScalar(data, length, k); }
} // namespace xtea::detail
#endif
