// Copyright 2024 Black Tek Server Authors. All rights reserved.
// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// GPL-2.0; eight-block layout adapted from BlackTek-Server 828b89395f28852f.
#include "xtea_simd.h"

#if TFS_XTEA_X86
#include <immintrin.h>
#if defined(__GNUC__) || defined(__clang__)
#define TFS_AVX2 __attribute__((target("avx2"), noinline))
#else
#define TFS_AVX2 __declspec(noinline)
#endif

namespace xtea::detail {
template <bool Decrypt>
TFS_AVX2 void transformAvx2(uint8_t* data, size_t length, const round_keys& k)
{
	while (length >= 64) {
		const auto a = _mm256_permute4x64_epi64(
		    _mm256_shuffle_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(data)), _MM_SHUFFLE(3, 1, 2, 0)),
		    _MM_SHUFFLE(3, 1, 2, 0));
		const auto b = _mm256_permute4x64_epi64(
		    _mm256_shuffle_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + 32)),
		                         _MM_SHUFFLE(3, 1, 2, 0)),
		    _MM_SHUFFLE(3, 1, 2, 0));
		auto left = _mm256_permute2x128_si256(a, b, 0x20);
		auto right = _mm256_permute2x128_si256(a, b, 0x31);
		for (size_t round = 0; round < k.size(); round += 2) {
			const size_t i = Decrypt ? k.size() - 2 - round : round;
			const auto ka = _mm256_set1_epi32(static_cast<int32_t>(k[i]));
			const auto kb = _mm256_set1_epi32(static_cast<int32_t>(k[i + 1]));
			if constexpr (Decrypt) {
				right = _mm256_sub_epi32(
				    right, _mm256_xor_si256(
				               _mm256_add_epi32(
				                   _mm256_xor_si256(_mm256_slli_epi32(left, 4), _mm256_srli_epi32(left, 5)), left),
				               kb));
				left = _mm256_sub_epi32(
				    left, _mm256_xor_si256(
				              _mm256_add_epi32(
				                  _mm256_xor_si256(_mm256_slli_epi32(right, 4), _mm256_srli_epi32(right, 5)), right),
				              ka));
			} else {
				left = _mm256_add_epi32(
				    left, _mm256_xor_si256(
				              _mm256_add_epi32(
				                  _mm256_xor_si256(_mm256_slli_epi32(right, 4), _mm256_srli_epi32(right, 5)), right),
				              ka));
				right = _mm256_add_epi32(
				    right, _mm256_xor_si256(
				               _mm256_add_epi32(
				                   _mm256_xor_si256(_mm256_slli_epi32(left, 4), _mm256_srli_epi32(left, 5)), left),
				               kb));
			}
		}
		const auto aout = _mm256_shuffle_epi32(
		    _mm256_permute4x64_epi64(_mm256_permute2x128_si256(left, right, 0x20), _MM_SHUFFLE(3, 1, 2, 0)),
		    _MM_SHUFFLE(3, 1, 2, 0));
		const auto bout = _mm256_shuffle_epi32(
		    _mm256_permute4x64_epi64(_mm256_permute2x128_si256(left, right, 0x31), _MM_SHUFFLE(3, 1, 2, 0)),
		    _MM_SHUFFLE(3, 1, 2, 0));
		_mm256_storeu_si256(reinterpret_cast<__m256i*>(data), aout);
		_mm256_storeu_si256(reinterpret_cast<__m256i*>(data + 32), bout);
		data += 64;
		length -= 64;
	}
	// AVX2-capable CPUs also support SSE2; do not turn a 4-7 block tail scalar.
	if constexpr (Decrypt)
		decryptSse2(data, length, k);
	else
		encryptSse2(data, length, k);
}
TFS_AVX2 void encryptAvx2(uint8_t* data, size_t length, const round_keys& k) { transformAvx2<false>(data, length, k); }
TFS_AVX2 void decryptAvx2(uint8_t* data, size_t length, const round_keys& k) { transformAvx2<true>(data, length, k); }
} // namespace xtea::detail
#else
namespace xtea::detail {
void encryptAvx2(uint8_t* data, size_t length, const round_keys& k) { encryptScalar(data, length, k); }
void decryptAvx2(uint8_t* data, size_t length, const round_keys& k) { decryptScalar(data, length, k); }
} // namespace xtea::detail
#endif
