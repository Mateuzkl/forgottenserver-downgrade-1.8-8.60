#ifndef FS_ASTRA_PING_H
#define FS_ASTRA_PING_H

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

namespace AstraClient {
using PingClock = std::chrono::steady_clock;

inline uint32_t pingQueueMicros(PingClock::time_point received, PingClock::time_point dispatched)
{
	if (dispatched <= received) {
		return 0;
	}
	const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(dispatched - received).count();
	return static_cast<uint32_t>(std::min<int64_t>(micros, std::numeric_limits<uint32_t>::max()));
}
} // namespace AstraClient
#endif
