// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_PLAYER_STASH_H
#define FS_PLAYER_STASH_H

#include "stash.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Authoritative in-memory Supply Stash state for one player.
//
// The stash is logical storage: a count per (itemId, tier), never physical items.
// This class owns that state, is mutated on the game thread, and tracks which rows
// changed so a save can persist just those.
//
// It follows the same dirty/snapshot/acknowledge discipline the storage values
// already use, including per-row revisions. That last part is the reason this is
// not just a map plus a bool: a row modified *after* a snapshot is taken must
// survive the acknowledgement of that snapshot, or a mutation made while the save
// worker was running would be silently dropped.
//
// Deliberately free of Player, Item and Database, so it can be tested on its own.
class PlayerStash
{
public:
	struct Key
	{
		uint16_t itemId = 0;
		uint8_t tier = 0;

		bool operator==(const Key& other) const noexcept
		{
			return itemId == other.itemId && tier == other.tier;
		}
	};

	struct KeyHash
	{
		size_t operator()(const Key& key) const noexcept
		{
			return (static_cast<size_t>(key.itemId) << 8) ^ key.tier;
		}
	};

	using Rows = std::unordered_map<Key, uint32_t, KeyHash>;
	using KeySet = std::unordered_set<Key, KeyHash>;

	struct DirtySnapshot
	{
		uint64_t snapshotId = 0;
		KeySet modifiedRows;
	};

	// Tier is part of the row identity, so (2160, 0) and (2160, 3) are two rows
	// against this cap, matching how the table is keyed.
	static constexpr size_t MAX_UNIQUE_ROWS = 1000;

	// Loads a row as it exists in the database. Does not mark anything dirty:
	// nothing has changed yet at that point.
	void load(uint16_t itemId, uint8_t tier, uint32_t amount);

	// Both return false and change nothing on failure, so a caller that has
	// already removed a physical item can roll back on a false.
	[[nodiscard]] bool add(uint16_t itemId, uint8_t tier, uint32_t amount);
	[[nodiscard]] bool remove(uint16_t itemId, uint8_t tier, uint32_t amount);

	[[nodiscard]] uint32_t getCount(uint16_t itemId, uint8_t tier) const;
	[[nodiscard]] const Rows& getRows() const noexcept { return rows; }
	[[nodiscard]] size_t getUniqueRowCount() const noexcept { return rows.size(); }

	// True when adding this row would introduce a new (itemId, tier) combination
	// and the cap is already reached. Callers check before removing anything.
	[[nodiscard]] bool wouldExceedRowLimit(uint16_t itemId, uint8_t tier) const;

	[[nodiscard]] bool isDirty() const noexcept { return !modifiedRows.empty(); }
	[[nodiscard]] DirtySnapshot getDirtySnapshot() const;
	void acknowledgeDirty(const DirtySnapshot& snapshot);
	void clearDirty();

	// Flattened view for serialisation, sorted so the wire order is stable.
	[[nodiscard]] std::vector<StashRecord> toRecords() const;

private:
	void markDirty(const Key& key);

	Rows rows;
	KeySet modifiedRows;
	std::unordered_map<Key, uint64_t, KeyHash> rowRevisions;
	uint64_t dirtyRevision = 0;
};

#endif // FS_PLAYER_STASH_H
