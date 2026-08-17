// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
//
// Baseline measurement for the checksum hot path (perf/profile-baseline).
//
// adlerChecksum() in src/tools.cpp is a hand-written Adler-32. zlib is already a
// hard dependency of this project (CMakeLists.txt find_package(ZLIB REQUIRED)),
// so adler32_z() is available at no packaging cost. This binary answers two
// questions before anyone edits tools.cpp:
//
//   1. is the hash byte-identical at every size, including the > MAXSIZE guard?
//   2. where is the crossover, given that zlib loses on very small buffers?
//
// Nothing here is linked into the server.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include <zlib.h>

namespace {

// NETWORKMESSAGE_MAXSIZE from src/networkmessage.h. Duplicated deliberately so
// this benchmark does not drag the engine headers in.
constexpr size_t NETWORKMESSAGE_MAXSIZE = 24590;

// Verbatim copy of src/tools.cpp adlerChecksum() as of this branch.
uint32_t adlerChecksumManual(const uint8_t* data, size_t length)
{
	if (length > NETWORKMESSAGE_MAXSIZE) {
		return 0;
	}

	const uint16_t adler = 65521;

	uint32_t a = 1, b = 0;

	while (length > 0) {
		size_t tmp = length > 5552 ? 5552 : length;
		length -= tmp;

		do {
			a += *data++;
			b += a;
		} while (--tmp);

		a %= adler;
		b %= adler;
	}

	return (b << 16) | a;
}

uint32_t adlerChecksumZlib(const uint8_t* data, size_t length)
{
	if (length > NETWORKMESSAGE_MAXSIZE) {
		return 0;
	}
	return static_cast<uint32_t>(adler32_z(1L, data, length));
}

using ChecksumFn = uint32_t (*)(const uint8_t*, size_t);

double timeNs(ChecksumFn fn, const std::vector<uint8_t>& buffer, size_t iterations)
{
	uint32_t sink = 0;
	const auto start = std::chrono::steady_clock::now();
	for (size_t i = 0; i < iterations; ++i) {
		sink ^= fn(buffer.data(), buffer.size());
	}
	const auto end = std::chrono::steady_clock::now();
	asm volatile("" : : "r"(sink) : "memory");
	return std::chrono::duration<double, std::nano>(end - start).count() / static_cast<double>(iterations);
}

std::vector<uint8_t> makePattern(size_t size, int pattern, std::mt19937& rng)
{
	std::vector<uint8_t> buffer(size);
	for (size_t i = 0; i < size; ++i) {
		switch (pattern) {
			case 0: buffer[i] = 0x00; break;
			case 1: buffer[i] = 0xFF; break;
			case 2: buffer[i] = static_cast<uint8_t>(i); break;
			default: buffer[i] = static_cast<uint8_t>(rng()); break;
		}
	}
	return buffer;
}

} // namespace

int main()
{
	const std::vector<size_t> sizes = {0,   1,   8,    16,   32,   64,                       128,
	                                  256, 512, 1024, 2048, 4096, NETWORKMESSAGE_MAXSIZE,    NETWORKMESSAGE_MAXSIZE + 1};
	const char* patternNames[] = {"zeros", "0xFF", "sequential", "random"};

	std::mt19937 rng{20260817};
	std::printf("== equivalence: manual vs zlib adler32_z ==\n");

	bool ok = true;
	for (size_t size : sizes) {
		for (int pattern = 0; pattern < 4; ++pattern) {
			const std::vector<uint8_t> buffer = makePattern(size, pattern, rng);
			const uint32_t manual = adlerChecksumManual(buffer.data(), buffer.size());
			const uint32_t zlib = adlerChecksumZlib(buffer.data(), buffer.size());
			if (manual != zlib) {
				std::printf("  MISMATCH size=%-6zu %-11s manual=%08x zlib=%08x\n", size, patternNames[pattern],
				            manual, zlib);
				ok = false;
			}
		}
	}

	if (!ok) {
		std::printf("EQUIVALENCE FAILED - do not port\n");
		return 1;
	}
	std::printf("  identical for %zu sizes x 4 patterns, including the > MAXSIZE guard returning 0\n\n",
	            sizes.size());

	std::printf("== checksum, ns per call, best of 5, lower is better ==\n");
	std::printf("%8s %12s %12s %10s   %s\n", "bytes", "manual", "zlib", "speedup", "winner");

	for (size_t size : sizes) {
		if (size == 0 || size > NETWORKMESSAGE_MAXSIZE) {
			continue; // guard-only cases, nothing to time
		}
		const std::vector<uint8_t> buffer = makePattern(size, 3, rng);
		const size_t iterations = std::max<size_t>(5000, (1u << 22) / size);

		double bestManual = 1e30;
		double bestZlib = 1e30;
		for (int round = 0; round < 5; ++round) {
			bestManual = std::min(bestManual, timeNs(&adlerChecksumManual, buffer, iterations));
			bestZlib = std::min(bestZlib, timeNs(&adlerChecksumZlib, buffer, iterations));
		}

		const double speedup = bestManual / bestZlib;
		std::printf("%8zu %12.1f %12.1f %9.2fx   %s\n", size, bestManual, bestZlib, speedup,
		            speedup > 1.0 ? "zlib" : "manual (current)");
	}

	return 0;
}
