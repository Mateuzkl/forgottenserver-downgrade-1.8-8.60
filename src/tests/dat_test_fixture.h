#ifndef FS_DAT_TEST_FIXTURE_H
#define FS_DAT_TEST_FIXTURE_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tfs::tests::dat {

constexpr uint32_t TIBIA_860_DAT_SIGNATURE = 0x4C2C7993;
constexpr uint16_t FIRST_DAT_ITEM_ID = 100;

struct VisualCounts {
	uint16_t outfits = 0;
	uint16_t effects = 0;
	uint16_t distanceEffects = 0;
};

inline void appendU16(std::vector<uint8_t>& bytes, uint16_t value)
{
	bytes.push_back(static_cast<uint8_t>(value));
	bytes.push_back(static_cast<uint8_t>(value >> 8));
}

inline void appendU32(std::vector<uint8_t>& bytes, uint32_t value)
{
	bytes.push_back(static_cast<uint8_t>(value));
	bytes.push_back(static_cast<uint8_t>(value >> 8));
	bytes.push_back(static_cast<uint8_t>(value >> 16));
	bytes.push_back(static_cast<uint8_t>(value >> 24));
}

inline void appendThing(std::vector<uint8_t>& bytes, size_t spriteIdBytes, bool unmoveable = false)
{
	if (unmoveable) {
		bytes.push_back(13); // IsUnmoveable
	}
	bytes.push_back(255);                             // LastFlag
	bytes.insert(bytes.end(), {1, 1, 1, 1, 1, 1, 1}); // width, height, layers, patterns, frames
	if (spriteIdBytes == sizeof(uint16_t)) {
		appendU16(bytes, 1);
	} else {
		appendU32(bytes, 1);
	}
}

inline std::vector<uint8_t> makeDat(uint16_t itemCount = FIRST_DAT_ITEM_ID,
                                    size_t spriteIdBytes = sizeof(uint16_t), bool firstItemUnmoveable = false,
                                    VisualCounts visualCounts = {})
{
	if (itemCount < FIRST_DAT_ITEM_ID) {
		throw std::invalid_argument("DAT test fixture itemCount must be at least 100");
	}
	if (spriteIdBytes != sizeof(uint16_t) && spriteIdBytes != sizeof(uint32_t)) {
		throw std::invalid_argument("DAT test fixture sprite IDs must be uint16 or uint32");
	}

	const size_t recordCount = static_cast<size_t>(itemCount) - FIRST_DAT_ITEM_ID + 1;
	const size_t visualCount =
	    static_cast<size_t>(visualCounts.outfits) + visualCounts.effects + visualCounts.distanceEffects;
	std::vector<uint8_t> bytes;
	bytes.reserve(12 + (recordCount + visualCount) * (8 + spriteIdBytes) + (firstItemUnmoveable ? 1 : 0));
	appendU32(bytes, TIBIA_860_DAT_SIGNATURE);
	appendU16(bytes, itemCount);
	appendU16(bytes, visualCounts.outfits);
	appendU16(bytes, visualCounts.effects);
	appendU16(bytes, visualCounts.distanceEffects);

	for (uint32_t id = FIRST_DAT_ITEM_ID; id <= itemCount; ++id) {
		appendThing(bytes, spriteIdBytes, firstItemUnmoveable && id == FIRST_DAT_ITEM_ID);
	}
	for (size_t index = 0; index < visualCount; ++index) {
		appendThing(bytes, spriteIdBytes);
	}
	return bytes;
}

class TempDatFile
{
public:
	TempDatFile(std::string_view name, const std::vector<uint8_t>& bytes) :
	    path(std::filesystem::temp_directory_path() /
	         (std::string{name} + "_" +
	          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".dat"))
	{
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!stream) {
			throw std::runtime_error("Unable to create temporary DAT test fixture");
		}
	}

	~TempDatFile()
	{
		std::error_code error;
		std::filesystem::remove(path, error);
	}

	TempDatFile(const TempDatFile&) = delete;
	TempDatFile& operator=(const TempDatFile&) = delete;

	std::filesystem::path path;
};

} // namespace tfs::tests::dat

#endif // FS_DAT_TEST_FIXTURE_H
