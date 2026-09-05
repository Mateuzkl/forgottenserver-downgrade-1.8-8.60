// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "packet_size_histogram.h"

#ifdef TFS_PACKET_SIZE_HISTOGRAM

#include "logger.h"

#include <fmt/format.h>

namespace tfs::diagnostics {

std::string PacketSizeHistogram::report(const char* label) const
{
	const uint64_t samples = total.load(std::memory_order_relaxed);
	const uint64_t bytes = totalBytes.load(std::memory_order_relaxed);
	std::array<uint64_t, PACKET_SIZE_BUCKETS.size()> bucketHits;
	for (size_t i = 0; i < PACKET_SIZE_BUCKETS.size(); ++i) {
		bucketHits[i] = buckets[i].load(std::memory_order_relaxed);
	}
	const uint64_t overflowHits = overflow.load(std::memory_order_relaxed);

	uint64_t distributionSamples = overflowHits;
	for (uint64_t hits : bucketHits) {
		distributionSamples += hits;
	}
	if (samples == 0 || distributionSamples == 0) {
		return fmt::format("[packet-size] {}: no samples yet\n", label);
	}

	std::string out = fmt::format("[packet-size] {}: {} packets, {} bytes, mean {:.1f} bytes\n", label, samples, bytes,
	                              static_cast<double>(bytes) / static_cast<double>(samples));

	size_t low = 0;
	uint64_t cumulative = 0;
	for (size_t i = 0; i < PACKET_SIZE_BUCKETS.size(); ++i) {
		const uint64_t hits = bucketHits[i];
		cumulative += hits;
		const double share = 100.0 * static_cast<double>(hits) / static_cast<double>(distributionSamples);
		const double cumulativeShare =
		    100.0 * static_cast<double>(cumulative) / static_cast<double>(distributionSamples);
		out += fmt::format("    {:>6}..{:<6} {:>12} {:>6.2f}%  (cum {:>6.2f}%)\n", low + 1, PACKET_SIZE_BUCKETS[i],
		                   hits, share, cumulativeShare);
		low = PACKET_SIZE_BUCKETS[i];
	}

	cumulative += overflowHits;
	const double overflowShare = 100.0 * static_cast<double>(overflowHits) / static_cast<double>(distributionSamples);
	const double cumulativeShare = 100.0 * static_cast<double>(cumulative) / static_cast<double>(distributionSamples);
	out += fmt::format("    {:>6}+{:<7} {:>12} {:>6.2f}%  (cum {:>6.2f}%)\n", low + 1, "", overflowHits,
	                   overflowShare, cumulativeShare);
	return out;
}

PacketSizeHistogram& outgoingPacketSizes()
{
	static PacketSizeHistogram histogram;
	return histogram;
}

PacketSizeHistogram& incomingPacketSizes()
{
	static PacketSizeHistogram histogram;
	return histogram;
}

void reportPacketSizes()
{
	LOG_INFO(fmt::format("\n{}{}", outgoingPacketSizes().report("xtea::encrypt (outgoing)"),
	                     incomingPacketSizes().report("xtea::decrypt (incoming)")));
}

} // namespace tfs::diagnostics

#endif // TFS_PACKET_SIZE_HISTOGRAM
