// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_PACKET_SIZE_HISTOGRAM_H
#define FS_PACKET_SIZE_HISTOGRAM_H

// Opt-in instrumentation for the perf/profile-baseline work.
//
// Whether a SIMD XTEA port is worth anything depends entirely on how large the
// buffers handed to xtea::encrypt actually are: benchmarks/perf_baseline shows
// the vectorised kernels winning by 2.4x at 64 bytes, tying at 256, and losing
// below 48. Guessing that distribution is how this plan mispriotised once
// already, so measure it on a real server instead.
//
// Enable with:
//     cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_PACKET_SIZE_HISTOGRAM=ON
//
// With the option off, TFS_PACKET_SIZE_HISTOGRAM is undefined and every call
// below compiles to nothing - no counters, no branches, no atomics in the
// Release binary.

#ifdef TFS_PACKET_SIZE_HISTOGRAM

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace tfs::diagnostics {

// Bucket edges chosen to match the sizes benchmarked in
// benchmarks/perf_baseline/bench_xtea.cpp, because the decision is read off
// exactly these boundaries.
inline constexpr std::array<size_t, 11> PACKET_SIZE_BUCKETS = {8,   16,  24,  32,   48,  64,
                                                               128, 256, 512, 1024, 4096};

class PacketSizeHistogram
{
public:
	// Called from the IO threads, so the counters are atomic. Relaxed ordering
	// is enough: nothing else is published through them and the totals are only
	// read for reporting.
	uint64_t record(size_t length) noexcept
	{
		const uint64_t count = total.fetch_add(1, std::memory_order_relaxed) + 1;
		totalBytes.fetch_add(length, std::memory_order_relaxed);

		for (size_t i = 0; i < PACKET_SIZE_BUCKETS.size(); ++i) {
			if (length <= PACKET_SIZE_BUCKETS[i]) {
				buckets[i].fetch_add(1, std::memory_order_relaxed);
				return count;
			}
		}
		overflow.fetch_add(1, std::memory_order_relaxed);
		return count;
	}

	// Renders the distribution as a single multi-line string. Kept out of the
	// header body so this stays cheap to include.
	std::string report(const char* label) const;

	uint64_t count() const noexcept { return total.load(std::memory_order_relaxed); }

private:
	std::array<std::atomic<uint64_t>, PACKET_SIZE_BUCKETS.size()> buckets = {};
	std::atomic<uint64_t> overflow = 0;
	std::atomic<uint64_t> total = 0;
	std::atomic<uint64_t> totalBytes = 0;
};

// One histogram per direction: outgoing buffers are padded to a multiple of 8
// before encryption, incoming ones arrive already aligned, and the two
// distributions are not the same shape.
PacketSizeHistogram& outgoingPacketSizes();
PacketSizeHistogram& incomingPacketSizes();

// Logs both distributions. Call it from wherever a report is convenient; the
// XTEA call sites also auto-report every REPORT_INTERVAL packets so a plain
// `./tfs` run under load produces data without any further wiring.
void reportPacketSizes();

inline constexpr uint64_t REPORT_INTERVAL = 100000;

} // namespace tfs::diagnostics

#define TFS_RECORD_OUTGOING_PACKET_SIZE(length)                                        \
	do {                                                                               \
		auto& histogram = ::tfs::diagnostics::outgoingPacketSizes();                   \
		const uint64_t count = histogram.record(length);                               \
		if (count % ::tfs::diagnostics::REPORT_INTERVAL == 0) {                        \
			::tfs::diagnostics::reportPacketSizes();                                   \
		}                                                                              \
	} while (false)

#define TFS_RECORD_INCOMING_PACKET_SIZE(length) ::tfs::diagnostics::incomingPacketSizes().record(length)

#else

#define TFS_RECORD_OUTGOING_PACKET_SIZE(length) ((void)0)
#define TFS_RECORD_INCOMING_PACKET_SIZE(length) ((void)0)

#endif // TFS_PACKET_SIZE_HISTOGRAM

#endif // FS_PACKET_SIZE_HISTOGRAM_H
