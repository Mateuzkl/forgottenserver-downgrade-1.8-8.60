// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "player_stash.h"

#include <algorithm>
#include <limits>

void PlayerStash::load(uint16_t itemId, uint8_t tier, uint32_t amount)
{
	if (itemId == 0 || amount == 0) {
		return;
	}

	rows[Key{itemId, Stash::normalizeTier(tier)}] = amount;
}

bool PlayerStash::add(uint16_t itemId, uint8_t tier, uint32_t amount)
{
	if (itemId == 0 || amount == 0) {
		return false;
	}

	const Key key{itemId, Stash::normalizeTier(tier)};
	const auto it = rows.find(key);
	const uint32_t current = it != rows.end() ? it->second : 0;

	// A row that does not exist yet costs one against the cap. Checked before any
	// mutation so a rejected stow leaves the physical item where it was.
	if (current == 0 && rows.size() >= MAX_UNIQUE_ROWS) {
		return false;
	}

	// The column is unsigned 32-bit; wrapping here would turn a huge stash into an
	// empty one.
	if (amount > std::numeric_limits<uint32_t>::max() - current) {
		return false;
	}

	rows[key] = current + amount;
	markDirty(key);
	return true;
}

bool PlayerStash::remove(uint16_t itemId, uint8_t tier, uint32_t amount)
{
	if (itemId == 0 || amount == 0) {
		return false;
	}

	const Key key{itemId, Stash::normalizeTier(tier)};
	const auto it = rows.find(key);
	if (it == rows.end() || it->second < amount) {
		return false;
	}

	it->second -= amount;
	if (it->second == 0) {
		// Drop the emptied row rather than keeping a zero, so the row cap reflects
		// what is actually stored.
		rows.erase(it);
	}

	markDirty(key);
	return true;
}

uint32_t PlayerStash::getCount(uint16_t itemId, uint8_t tier) const
{
	const auto it = rows.find(Key{itemId, Stash::normalizeTier(tier)});
	return it != rows.end() ? it->second : 0;
}

bool PlayerStash::wouldExceedRowLimit(uint16_t itemId, uint8_t tier) const
{
	if (itemId == 0) {
		return false;
	}

	if (rows.contains(Key{itemId, Stash::normalizeTier(tier)})) {
		return false;
	}

	return rows.size() >= MAX_UNIQUE_ROWS;
}

void PlayerStash::markDirty(const Key& key)
{
	modifiedRows.insert(key);
	rowRevisions[key] = ++dirtyRevision;
}

PlayerStash::DirtySnapshot PlayerStash::getDirtySnapshot() const
{
	return DirtySnapshot{dirtyRevision, modifiedRows};
}

void PlayerStash::acknowledgeDirty(const DirtySnapshot& snapshot)
{
	for (const auto& key : snapshot.modifiedRows) {
		const auto revisionIt = rowRevisions.find(key);

		// Changed again after the snapshot was taken, so it is still dirty and the
		// next save has to pick it up. Without this, a stow landing while the save
		// worker ran would be acknowledged away and lost.
		if (revisionIt != rowRevisions.end() && revisionIt->second > snapshot.snapshotId) {
			continue;
		}

		modifiedRows.erase(key);
		if (revisionIt != rowRevisions.end()) {
			rowRevisions.erase(revisionIt);
		}
	}
}

void PlayerStash::clearDirty()
{
	modifiedRows.clear();
	rowRevisions.clear();
}

std::vector<StashRecord> PlayerStash::toRecords() const
{
	std::vector<StashRecord> records;
	records.reserve(rows.size());
	for (const auto& [key, amount] : rows) {
		records.emplace_back(StashRecord{key.itemId, key.tier, amount});
	}

	std::sort(records.begin(), records.end(), [](const StashRecord& lhs, const StashRecord& rhs) {
		if (lhs.itemId != rhs.itemId) {
			return lhs.itemId < rhs.itemId;
		}
		return lhs.tier < rhs.tier;
	});
	return records;
}
