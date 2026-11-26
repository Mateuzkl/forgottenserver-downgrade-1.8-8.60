#pragma once

#include "../position.h"
#include <array>

// Experimental wire encoding, used ONLY by tests/benchmarks. No production
// sender uses this probe. All bytes are initialized; no shared scratch buffer.
inline std::array<uint8_t, 8> smallEffectPacketProbe(const Position& pos, uint16_t effect)
{
	return {0x83, static_cast<uint8_t>(pos.x), static_cast<uint8_t>(pos.x >> 8),
	        static_cast<uint8_t>(pos.y), static_cast<uint8_t>(pos.y >> 8), pos.z,
	        static_cast<uint8_t>(effect), static_cast<uint8_t>(effect >> 8)};
}
