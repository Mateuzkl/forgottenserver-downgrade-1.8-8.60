// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_SERVER_MINIMAP_H
#define FS_SERVER_MINIMAP_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ServerMinimap {

inline constexpr uint8_t EXTENDED_OPCODE = 0xE6;
inline constexpr size_t CHUNK_PAYLOAD_SIZE = 8000;

struct Metadata
{
	std::string version;
	size_t size = 0;
	uint32_t checksum = 0;
};

struct Snapshot
{
	std::shared_ptr<const std::string> otmm;
	Metadata metadata;
};

struct TransferChunk
{
	char marker = 0;
	std::string_view payload;
};

// Captures the loaded static map and schedules OTMM compression on the worker
// pool. The size limit is checked before the snapshot becomes visible.
bool prepare(size_t maxBytes);
bool prepareHDRaster(const std::string& fileName, size_t maxBytes);
Snapshot getSnapshot();
Metadata getHDRasterMetadata();
bool readHDRasterChunk(size_t offset, size_t length, std::string& output);
void reset();

// The version contains only a deterministic content fingerprint and byte
// count. It deliberately does not include the map name or filesystem path.
std::string contentVersion(std::string_view data);
std::vector<TransferChunk> splitForTransfer(std::string_view data);

} // namespace ServerMinimap

#endif // FS_SERVER_MINIMAP_H
