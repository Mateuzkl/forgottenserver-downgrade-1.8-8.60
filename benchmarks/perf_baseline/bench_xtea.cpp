// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
//
// Baseline measurement for the XTEA hot path (perf/profile-baseline).
//
// Compares four implementations and proves they all produce the same ciphertext
// before reporting any timing:
//
//   tfs      - the loop currently in src/xtea.cpp, compiled from that exact file
//   bt-sc    - BlackTek's scalar path (block outer, rounds inner)
//   bt-sse2  - BlackTek's SSE2 kernel, 4 blocks at a time
//   bt-avx2  - BlackTek's AVX2 kernel, 8 blocks at a time
//
// The SIMD kernels are transcribed from Black-Tek/BlackTek-Server src/xtea.cpp
// so the comparison runs in one process on one machine. Nothing here is linked
// into the server; this binary exists only to decide whether a port is worth it.
//
// Build and run with benchmarks/perf_baseline/run.sh - it sweeps the flag sets
// that change the answer.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <immintrin.h>

#include "xtea.h"

namespace {

using round_keys = xtea::round_keys;

// ---------------------------------------------------------------- BlackTek --

void bt_scalar_encrypt_block(uint32_t& left, uint32_t& right, const round_keys& k)
{
	for (int32_t i = 0; i < static_cast<int32_t>(k.size()); i += 2) {
		left += ((right << 4 ^ right >> 5) + right) ^ k[i];
		right += ((left << 4 ^ left >> 5) + left) ^ k[i + 1];
	}
}

void bt_scalar_encrypt(uint8_t* data, size_t length, const round_keys& k)
{
	for (size_t blocks = length / 8; blocks > 0; --blocks, data += 8) {
		uint32_t left, right;
		std::memcpy(&left, data, 4);
		std::memcpy(&right, data + 4, 4);
		bt_scalar_encrypt_block(left, right, k);
		std::memcpy(data, &left, 4);
		std::memcpy(data + 4, &right, 4);
	}
}

__attribute__((target("sse2"))) void bt_sse2_encrypt4(uint8_t* data, const round_keys& k)
{
	__m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data));
	__m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + 16));

	__m128i v0s = _mm_shuffle_epi32(v0, _MM_SHUFFLE(3, 1, 2, 0));
	__m128i v1s = _mm_shuffle_epi32(v1, _MM_SHUFFLE(3, 1, 2, 0));

	__m128i lefts = _mm_unpacklo_epi64(v0s, v1s);
	__m128i rights = _mm_unpackhi_epi64(v0s, v1s);

	for (int32_t i = 0; i < static_cast<int32_t>(k.size()); i += 2) {
		__m128i keyA = _mm_set1_epi32(static_cast<int32_t>(k[i]));
		__m128i keyB = _mm_set1_epi32(static_cast<int32_t>(k[i + 1]));

		__m128i newLefts = _mm_add_epi32(
		    lefts, _mm_xor_si128(_mm_add_epi32(_mm_xor_si128(_mm_slli_epi32(rights, 4), _mm_srli_epi32(rights, 5)),
		                                       rights),
		                         keyA));
		__m128i newRights = _mm_add_epi32(
		    rights, _mm_xor_si128(_mm_add_epi32(_mm_xor_si128(_mm_slli_epi32(newLefts, 4), _mm_srli_epi32(newLefts, 5)),
		                                        newLefts),
		                          keyB));
		lefts = newLefts;
		rights = newRights;
	}

	_mm_storeu_si128(reinterpret_cast<__m128i*>(data), _mm_unpacklo_epi32(lefts, rights));
	_mm_storeu_si128(reinterpret_cast<__m128i*>(data + 16), _mm_unpackhi_epi32(lefts, rights));
}

__attribute__((target("avx2"))) void bt_avx2_encrypt8(uint8_t* data, const round_keys& k)
{
	__m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data));
	__m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + 32));

	__m256i v0s = _mm256_shuffle_epi32(v0, _MM_SHUFFLE(3, 1, 2, 0));
	__m256i v1s = _mm256_shuffle_epi32(v1, _MM_SHUFFLE(3, 1, 2, 0));
	__m256i v0p = _mm256_permute4x64_epi64(v0s, _MM_SHUFFLE(3, 1, 2, 0));
	__m256i v1p = _mm256_permute4x64_epi64(v1s, _MM_SHUFFLE(3, 1, 2, 0));

	__m256i lefts = _mm256_permute2x128_si256(v0p, v1p, 0x20);
	__m256i rights = _mm256_permute2x128_si256(v0p, v1p, 0x31);

	for (int32_t i = 0; i < static_cast<int32_t>(k.size()); i += 2) {
		__m256i keyA = _mm256_set1_epi32(static_cast<int32_t>(k[i]));
		__m256i keyB = _mm256_set1_epi32(static_cast<int32_t>(k[i + 1]));

		__m256i newLefts = _mm256_add_epi32(
		    lefts, _mm256_xor_si256(_mm256_add_epi32(_mm256_xor_si256(_mm256_slli_epi32(rights, 4),
		                                                             _mm256_srli_epi32(rights, 5)),
		                                             rights),
		                            keyA));
		__m256i newRights = _mm256_add_epi32(
		    rights, _mm256_xor_si256(_mm256_add_epi32(_mm256_xor_si256(_mm256_slli_epi32(newLefts, 4),
		                                                              _mm256_srli_epi32(newLefts, 5)),
		                                              newLefts),
		                             keyB));
		lefts = newLefts;
		rights = newRights;
	}

	__m256i outV0p = _mm256_permute2x128_si256(lefts, rights, 0x20);
	__m256i outV1p = _mm256_permute2x128_si256(lefts, rights, 0x31);
	__m256i outV0s = _mm256_permute4x64_epi64(outV0p, _MM_SHUFFLE(3, 1, 2, 0));
	__m256i outV1s = _mm256_permute4x64_epi64(outV1p, _MM_SHUFFLE(3, 1, 2, 0));

	_mm256_storeu_si256(reinterpret_cast<__m256i*>(data), _mm256_shuffle_epi32(outV0s, _MM_SHUFFLE(3, 1, 2, 0)));
	_mm256_storeu_si256(reinterpret_cast<__m256i*>(data + 32), _mm256_shuffle_epi32(outV1s, _MM_SHUFFLE(3, 1, 2, 0)));
}

// Mirrors BlackTek's dispatch: AVX2 for whole 8-block groups, then SSE2 for
// whole 4-block groups, then scalar for the tail.
void bt_sse2_encrypt(uint8_t* data, size_t length, const round_keys& k)
{
	size_t blocks = length / 8;
	while (blocks >= 4) {
		bt_sse2_encrypt4(data, k);
		data += 32;
		blocks -= 4;
	}
	bt_scalar_encrypt(data, blocks * 8, k);
}

void bt_avx2_encrypt(uint8_t* data, size_t length, const round_keys& k)
{
	size_t blocks = length / 8;
	while (blocks >= 8) {
		bt_avx2_encrypt8(data, k);
		data += 64;
		blocks -= 8;
	}
	while (blocks >= 4) {
		bt_sse2_encrypt4(data, k);
		data += 32;
		blocks -= 4;
	}
	bt_scalar_encrypt(data, blocks * 8, k);
}

// ------------------------------------------------------------------ harness --

using EncryptFn = void (*)(uint8_t*, size_t, const round_keys&);

struct Impl
{
	const char* name;
	EncryptFn fn;
};

double timeNs(EncryptFn fn, std::vector<uint8_t>& buffer, const round_keys& k, size_t iterations)
{
	const auto start = std::chrono::steady_clock::now();
	for (size_t i = 0; i < iterations; ++i) {
		fn(buffer.data(), buffer.size(), k);
	}
	const auto end = std::chrono::steady_clock::now();
	// Reading the buffer keeps the optimiser from discarding the whole loop.
	asm volatile("" : : "r"(buffer.data()) : "memory");
	return std::chrono::duration<double, std::nano>(end - start).count() / static_cast<double>(iterations);
}

bool checkEquivalence(const std::vector<size_t>& sizes, const round_keys& k, const std::vector<Impl>& impls)
{
	std::mt19937 rng{20260817};
	bool ok = true;

	for (size_t size : sizes) {
		std::vector<uint8_t> seed(size);
		for (auto& byte : seed) {
			byte = static_cast<uint8_t>(rng());
		}

		std::vector<uint8_t> reference = seed;
		impls.front().fn(reference.data(), reference.size(), k);

		for (size_t i = 1; i < impls.size(); ++i) {
			std::vector<uint8_t> candidate = seed;
			impls[i].fn(candidate.data(), candidate.size(), k);
			if (candidate != reference) {
				std::printf("  MISMATCH  size=%-6zu %s != %s\n", size, impls[i].name, impls.front().name);
				ok = false;
			}
		}

		// The current implementation must also round-trip through decrypt.
		std::vector<uint8_t> roundTrip = reference;
		xtea::decrypt(roundTrip.data(), roundTrip.size(), k);
		if (roundTrip != seed) {
			std::printf("  ROUND-TRIP FAILED size=%zu\n", size);
			ok = false;
		}
	}
	return ok;
}

} // namespace

int main()
{
	const xtea::key rawKey = {0x1F2E3D4Cu, 0x5B6A7988u, 0x97A6B5C4u, 0xD3E2F100u};
	const round_keys k = xtea::expand_key(rawKey);

	std::vector<Impl> impls = {
	    {"tfs", &xtea::encrypt},
	    {"bt-scalar", &bt_scalar_encrypt},
	    {"bt-sse2", &bt_sse2_encrypt},
	};
	__builtin_cpu_init();
	if (__builtin_cpu_supports("avx2")) {
		impls.push_back({"bt-avx2", &bt_avx2_encrypt});
	} else {
		std::printf("SKIP bt-avx2: AVX2 is unavailable\n");
	}

	// Multiples of 8 only: XTEA operates on 8-byte blocks and the protocol pads
	// to that boundary before calling encrypt.
	const std::vector<size_t> sizes = {8, 16, 24, 32, 40, 48, 56, 64, 128, 256, 512, 1024, 4096, 16384};

	std::printf("== equivalence ==\n");
	if (!checkEquivalence(sizes, k, impls)) {
		std::printf("EQUIVALENCE FAILED - timings below would be meaningless\n");
		return 1;
	}
	std::printf("  all %zu implementations agree on %zu sizes, decrypt round-trips\n\n", impls.size(), sizes.size());

	std::printf("== encrypt, ns per call, best of 5, lower is better ==\n");
	std::printf("%8s", "bytes");
	for (const Impl& impl : impls) {
		std::printf(" %12s", impl.name);
	}
	std::printf("   %s\n", "winner");

	for (size_t size : sizes) {
		const size_t iterations = std::max<size_t>(2000, (1u << 22) / size);
		std::vector<double> best(impls.size(), 1e30);

		for (int round = 0; round < 5; ++round) {
			for (size_t i = 0; i < impls.size(); ++i) {
				std::vector<uint8_t> buffer(size, static_cast<uint8_t>(0xA5));
				best[i] = std::min(best[i], timeNs(impls[i].fn, buffer, k, iterations));
			}
		}

		const size_t winner = static_cast<size_t>(std::min_element(best.begin(), best.end()) - best.begin());
		const double ratio = best[0] / best[winner];

		std::printf("%8zu", size);
		for (double timing : best) {
			std::printf(" %12.1f", timing);
		}
		std::printf("   %s", impls[winner].name);
		if (winner == 0) {
			std::printf(" (current is fastest)\n");
		} else {
			std::printf(" %.2fx vs tfs\n", ratio);
		}
	}

	return 0;
}
