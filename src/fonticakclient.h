#ifndef FS_FONTICAKCLIENT_H
#define FS_FONTICAKCLIENT_H

#include "xtea.h"

#include <cstdint>
#include <string_view>

namespace FonticakClient {

inline constexpr std::string_view LOGIN_MARKER = "F";
inline constexpr std::string_view STORE_HIGHLIGHTS_MARKER = "FonticakStoreHighlights";
inline constexpr std::string_view REQUIRED_MESSAGE = "This server requires OTC-Fonticak.";

inline uint32_t rotateLeft(uint32_t value, uint8_t bits)
{
	return (value << bits) | (value >> (32 - bits));
}

inline uint32_t mixSignature(uint32_t hash, uint32_t value)
{
	hash ^= value + 0x9E3779B9 + (hash << 6) + (hash >> 2);
	return rotateLeft(hash, 7) ^ (value >> 3);
}

inline uint32_t generateSignature(uint16_t operatingSystem, uint16_t version, const xtea::key& key,
                                  uint32_t challengeTimestamp = 0, uint8_t challengeRandom = 0)
{
	uint32_t hash = 0xF04E71CA;
	hash = mixSignature(hash, 0x466F6E74); // "Font"
	hash = mixSignature(hash, 0x6963616B); // "icak"
	hash = mixSignature(hash, operatingSystem);
	hash = mixSignature(hash, version);
	for (const uint32_t value : key) {
		hash = mixSignature(hash, value);
	}
	hash = mixSignature(hash, challengeTimestamp);
	hash = mixSignature(hash, challengeRandom);
	return hash ^ 0x464F4E54; // XOR "FONT"
}

} // namespace FonticakClient

#endif // FS_FONTICAKCLIENT_H
