// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "server_minimap.h"

#include "game.h"
#include "item.h"
#include "items.h"
#include "logger.h"
#include "map.h"
#include "thread_pool.h"
#include "tile.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <zlib.h>

extern Game g_game;

namespace {

constexpr uint32_t OTMM_SIGNATURE = 0x4D4d544F;
constexpr uint16_t OTMM_VERSION = 1;
constexpr uint8_t MMBLOCK_SIZE = 64;
constexpr uint8_t MINIMAP_TILE_WAS_SEEN = 1;
constexpr uint8_t MINIMAP_TILE_NOT_PATHABLE = 2;
constexpr uint8_t MINIMAP_TILE_NOT_WALKABLE = 4;
constexpr uint32_t MINIMAP_WORLD_SIZE = 65536;
constexpr uint32_t MINIMAP_BLOCKS_PER_AXIS = MINIMAP_WORLD_SIZE / MMBLOCK_SIZE;

#pragma pack(push, 1)
struct MinimapTile
{
	uint8_t flags = 0;
	uint8_t color = 255;
	uint8_t speed = 10;

	bool operator==(const MinimapTile&) const = default;
};
#pragma pack(pop)

struct MinimapBlock
{
	std::array<MinimapTile, MMBLOCK_SIZE * MMBLOCK_SIZE> tiles{};
};

struct SparseMinimapTile
{
	uint16_t index = 0;
	MinimapTile tile;
};

using BlockMap = std::map<uint32_t, std::vector<SparseMinimapTile>>;
using WorldBlocks = std::array<BlockMap, MAP_MAX_LAYERS>;

struct CapturedMap
{
	WorldBlocks blocks;
	size_t tileCount = 0;
};

struct BuildResult
{
	std::string data;
	size_t blockCount = 0;
};

std::mutex snapshotMutex;
ServerMinimap::Snapshot cachedSnapshot;

void addU8(std::string& out, uint8_t value) { out.push_back(static_cast<char>(value)); }

void addU16(std::string& out, uint16_t value)
{
	out.push_back(static_cast<char>(value & 0xFF));
	out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void addU32(std::string& out, uint32_t value)
{
	addU16(out, static_cast<uint16_t>(value & 0xFFFF));
	addU16(out, static_cast<uint16_t>((value >> 16) & 0xFFFF));
}

void writeU16At(std::string& out, size_t offset, uint16_t value)
{
	out[offset] = static_cast<char>(value & 0xFF);
	out[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
}

uint32_t getBlockIndex(const Position& pos)
{
	return ((pos.y / MMBLOCK_SIZE) * MINIMAP_BLOCKS_PER_AXIS) + (pos.x / MMBLOCK_SIZE);
}

size_t getTileIndex(const Position& pos) { return ((pos.y % MMBLOCK_SIZE) * MMBLOCK_SIZE) + (pos.x % MMBLOCK_SIZE); }

Position getBlockPosition(uint32_t index, uint8_t z)
{
	return Position((index % MINIMAP_BLOCKS_PER_AXIS) * MMBLOCK_SIZE, (index / MINIMAP_BLOCKS_PER_AXIS) * MMBLOCK_SIZE,
	                z);
}

uint8_t getItemMinimapColor(const Item* item)
{
	if (!item) {
		return 0;
	}
	return Item::items[item->getID()].minimapColor;
}

uint8_t getTileColor(const Tile& tile)
{
	uint8_t color = 255;
	if (const uint8_t groundColor = getItemMinimapColor(tile.getGround()); groundColor != 0) {
		color = groundColor;
	}

	const TileItemVector* items = tile.getItemList();
	if (!items) {
		return color;
	}

	for (const auto& item : *items) {
		const ItemType& type = Item::items[item->getID()];
		if (!type.alwaysOnTop) {
			break;
		}
		if (type.minimapColor != 0) {
			color = type.minimapColor;
		}
	}
	return color;
}

MinimapTile makeMinimapTile(const Tile& tile)
{
	MinimapTile result;
	result.flags = MINIMAP_TILE_WAS_SEEN;
	result.color = getTileColor(tile);

	uint16_t speed = 100;
	if (const Item* ground = tile.getGround()) {
		speed = Item::items[ground->getID()].speed;
		if (speed == 0) {
			speed = 100;
		}
	} else {
		result.flags |= MINIMAP_TILE_NOT_WALKABLE;
	}
	result.speed = static_cast<uint8_t>(std::min<uint16_t>((speed + 9) / 10, 255));

	if (tile.hasFlag(TILESTATE_BLOCKSOLID | TILESTATE_IMMOVABLEBLOCKSOLID)) {
		result.flags |= MINIMAP_TILE_NOT_WALKABLE;
	}
	if (tile.hasFlag(TILESTATE_BLOCKPATH | TILESTATE_IMMOVABLEBLOCKPATH | TILESTATE_NOFIELDBLOCKPATH |
	                 TILESTATE_IMMOVABLENOFIELDBLOCKPATH)) {
		result.flags |= MINIMAP_TILE_NOT_PATHABLE;
	}
	return result;
}

const ItemType* getBasicItemType(const std::shared_ptr<BasicItem>& item)
{
	if (!item || item->id == 0 || item->id >= Item::items.size()) {
		return nullptr;
	}
	return &Item::items[item->id];
}

MinimapTile makeMinimapTile(const BasicTile& tile)
{
	MinimapTile result;
	result.flags = MINIMAP_TILE_WAS_SEEN;

	const ItemType* groundType = getBasicItemType(tile.ground);
	uint16_t speed = 100;
	if (groundType) {
		if (groundType->minimapColor != 0) {
			result.color = groundType->minimapColor;
		}
		if (groundType->speed != 0) {
			speed = groundType->speed;
		}
	} else {
		result.flags |= MINIMAP_TILE_NOT_WALKABLE;
	}

	bool blockSolid = groundType && groundType->blockSolid;
	bool blockPath = groundType && groundType->blockPathFind;
	for (const auto& item : tile.items) {
		const ItemType* type = getBasicItemType(item);
		if (!type) {
			continue;
		}
		blockSolid = blockSolid || type->blockSolid;
		blockPath = blockPath || type->blockPathFind;
		if (type->alwaysOnTop && type->minimapColor != 0) {
			result.color = type->minimapColor;
		}
	}

	result.speed = static_cast<uint8_t>(std::min<uint16_t>((speed + 9) / 10, 255));
	if (blockSolid) {
		result.flags |= MINIMAP_TILE_NOT_WALKABLE;
	}
	if (blockPath) {
		result.flags |= MINIMAP_TILE_NOT_PATHABLE;
	}
	return result;
}

CapturedMap captureMap()
{
	CapturedMap captured;

	g_game.map.forEachStaticTile([&](const Position& pos, const Tile* tile, const BasicTile* basicTile) {
		const MinimapTile minimapTile = tile ? makeMinimapTile(*tile) : makeMinimapTile(*basicTile);
		captured.blocks[pos.z][getBlockIndex(pos)].push_back({static_cast<uint16_t>(getTileIndex(pos)), minimapTile});
		++captured.tileCount;
	});
	return captured;
}

BuildResult buildOtmm(CapturedMap&& captured, size_t maxBytes)
{
	BuildResult result;

	addU32(result.data, OTMM_SIGNATURE);
	addU16(result.data, 0);
	addU16(result.data, OTMM_VERSION);
	addU32(result.data, 0);

	constexpr std::string_view description = "OTMM 1.0";
	addU16(result.data, static_cast<uint16_t>(description.size()));
	result.data.append(description);
	writeU16At(result.data, 4, static_cast<uint16_t>(result.data.size()));

	constexpr size_t blockSize = MMBLOCK_SIZE * MMBLOCK_SIZE * sizeof(MinimapTile);
	std::vector<Bytef> compressed(compressBound(blockSize));

	for (uint8_t z = 0; z < MAP_MAX_LAYERS; ++z) {
		for (const auto& [index, sparseTiles] : captured.blocks[z]) {
			MinimapBlock block;
			for (const SparseMinimapTile& sparseTile : sparseTiles) {
				block.tiles[sparseTile.index] = sparseTile.tile;
			}

			uLongf compressedLength = static_cast<uLongf>(compressed.size());
			const int status = compress2(compressed.data(), &compressedLength,
			                             reinterpret_cast<const Bytef*>(block.tiles.data()), blockSize, 3);
			if (status != Z_OK || compressedLength > std::numeric_limits<uint16_t>::max()) {
				return {};
			}

			const size_t recordSize = sizeof(uint16_t) * 3 + sizeof(uint8_t) + compressedLength;
			if (result.data.size() + recordSize + 5 > maxBytes) {
				return {};
			}

			const Position blockPos = getBlockPosition(index, z);
			addU16(result.data, blockPos.x);
			addU16(result.data, blockPos.y);
			addU8(result.data, blockPos.z);
			addU16(result.data, static_cast<uint16_t>(compressedLength));
			result.data.append(reinterpret_cast<const char*>(compressed.data()), static_cast<size_t>(compressedLength));
			++result.blockCount;
		}
	}

	addU16(result.data, 0xFFFF);
	addU16(result.data, 0xFFFF);
	addU8(result.data, 0xFF);
	return result;
}

} // namespace

namespace ServerMinimap {

std::string contentVersion(std::string_view data)
{
	// FNV-1a is used as a compact cache fingerprint, not as a security primitive.
	uint64_t hash = 14695981039346656037ULL;
	for (const unsigned char byte : data) {
		hash ^= byte;
		hash *= 1099511628211ULL;
	}
	return fmt::format("{:016x}-{}", hash, data.size());
}

std::vector<TransferChunk> splitForTransfer(std::string_view data)
{
	std::vector<TransferChunk> chunks;
	if (data.empty()) {
		return chunks;
	}

	chunks.reserve((data.size() + CHUNK_PAYLOAD_SIZE - 1) / CHUNK_PAYLOAD_SIZE);
	if (data.size() <= CHUNK_PAYLOAD_SIZE) {
		chunks.push_back({'O', data});
		return chunks;
	}

	for (size_t offset = 0; offset < data.size(); offset += CHUNK_PAYLOAD_SIZE) {
		const size_t length = std::min(CHUNK_PAYLOAD_SIZE, data.size() - offset);
		const char marker = offset == 0 ? 'S' : (offset + length == data.size() ? 'E' : 'P');
		chunks.push_back({marker, data.substr(offset, length)});
	}
	return chunks;
}

bool prepare(size_t maxBytes)
{
	if (maxBytes == 0) {
		LOG_ERROR("[ServerMinimap] The configured size limit is zero.");
		return false;
	}
	if (!g_threadPool.isRunning()) {
		LOG_ERROR("[ServerMinimap] The worker pool is not running.");
		return false;
	}

	CapturedMap captured = captureMap();
	if (captured.tileCount == 0) {
		LOG_ERROR("[ServerMinimap] The loaded map does not contain any static tiles.");
		return false;
	}
	const size_t tileCount = captured.tileCount;
	reset();

	g_threadPool.detach_task([captured = std::move(captured), maxBytes, tileCount]() mutable {
		BuildResult result = buildOtmm(std::move(captured), maxBytes);
		if (result.data.empty() || result.blockCount == 0) {
			LOG_ERROR("[ServerMinimap] Failed to build a non-empty minimap within the configured size limit.");
			return;
		}

		auto otmm = std::make_shared<const std::string>(std::move(result.data));
		Snapshot snapshot;
		snapshot.metadata.version = contentVersion(*otmm);
		snapshot.metadata.size = otmm->size();
		snapshot.otmm = std::move(otmm);

		{
			std::lock_guard lock(snapshotMutex);
			cachedSnapshot = snapshot;
		}

		LOG_INFO("[ServerMinimap] Prepared {} tiles in {} blocks ({} bytes).", tileCount, result.blockCount,
		         snapshot.metadata.size);
	});
	LOG_INFO("[ServerMinimap] Captured {} static tiles; OTMM compression is running in the background.", tileCount);
	return true;
}

Snapshot getSnapshot()
{
	std::lock_guard lock(snapshotMutex);
	return cachedSnapshot;
}

void reset()
{
	std::lock_guard lock(snapshotMutex);
	cachedSnapshot = {};
}

} // namespace ServerMinimap
