// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../player_stash.h"

#include "test_support.h"

#include <limits>

TEST_CASE(player_stash_add_and_count)
{
	PlayerStash stash;

	CHECK(stash.add(2160, 0, 30));
	CHECK(stash.getCount(2160, 0) == 30);

	CHECK(stash.add(2160, 0, 12));
	CHECK(stash.getCount(2160, 0) == 42);
}

// Tier is part of the row identity, exactly as the table is keyed. The same
// itemId at two tiers must never be merged into one count.
TEST_CASE(player_stash_keeps_tiers_apart)
{
	PlayerStash stash;

	CHECK(stash.add(2160, 0, 100));
	CHECK(stash.add(2160, 3, 5));

	CHECK(stash.getCount(2160, 0) == 100);
	CHECK(stash.getCount(2160, 3) == 5);
	CHECK(stash.getUniqueRowCount() == 2);
}

TEST_CASE(player_stash_remove_is_guarded)
{
	PlayerStash stash;
	CHECK(stash.add(2160, 0, 50));

	// Asking for more than is stored must change nothing, so a caller can treat
	// false as "nothing happened" and roll back.
	CHECK(!stash.remove(2160, 0, 51));
	CHECK(stash.getCount(2160, 0) == 50);

	CHECK(stash.remove(2160, 0, 20));
	CHECK(stash.getCount(2160, 0) == 30);
}

TEST_CASE(player_stash_removing_everything_drops_the_row)
{
	PlayerStash stash;
	CHECK(stash.add(2160, 0, 10));
	CHECK(stash.getUniqueRowCount() == 1);

	CHECK(stash.remove(2160, 0, 10));
	CHECK(stash.getCount(2160, 0) == 0);
	// An emptied row must not keep occupying a slot against the cap.
	CHECK(stash.getUniqueRowCount() == 0);
}

TEST_CASE(player_stash_rejects_removing_from_an_empty_stash)
{
	PlayerStash stash;
	CHECK(!stash.remove(2160, 0, 1));
}

TEST_CASE(player_stash_rejects_zero_and_invalid_input)
{
	PlayerStash stash;

	CHECK(!stash.add(0, 0, 10));
	CHECK(!stash.add(2160, 0, 0));
	CHECK(!stash.remove(2160, 0, 0));
	CHECK(stash.getUniqueRowCount() == 0);
}

// The amount column is unsigned 32-bit. Wrapping would turn a nearly full stash
// into an empty one, which is the worst possible failure mode here.
TEST_CASE(player_stash_add_does_not_overflow)
{
	PlayerStash stash;
	constexpr uint32_t max = std::numeric_limits<uint32_t>::max();

	CHECK(stash.add(2160, 0, max - 5));
	CHECK(!stash.add(2160, 0, 6));
	CHECK(stash.getCount(2160, 0) == max - 5);

	CHECK(stash.add(2160, 0, 5));
	CHECK(stash.getCount(2160, 0) == max);
}

TEST_CASE(player_stash_enforces_the_unique_row_limit)
{
	PlayerStash stash;

	for (uint16_t i = 0; i < PlayerStash::MAX_UNIQUE_ROWS; ++i) {
		CHECK(stash.add(static_cast<uint16_t>(i + 1), 0, 1));
	}
	CHECK(stash.getUniqueRowCount() == PlayerStash::MAX_UNIQUE_ROWS);

	// A brand new row is refused once the cap is reached...
	CHECK(stash.wouldExceedRowLimit(60000, 0));
	CHECK(!stash.add(60000, 0, 1));

	// ...but topping up a row that already exists is still fine.
	CHECK(!stash.wouldExceedRowLimit(1, 0));
	CHECK(stash.add(1, 0, 10));
	CHECK(stash.getCount(1, 0) == 11);
}

// A new tier of an item already held is a new row, so it counts against the cap.
TEST_CASE(player_stash_row_limit_counts_tiers_separately)
{
	PlayerStash stash;
	CHECK(stash.add(2160, 0, 1));

	CHECK(!stash.wouldExceedRowLimit(2160, 0));
	CHECK(stash.wouldExceedRowLimit(2160, 1) == false); // room is available here
	CHECK(stash.add(2160, 1, 1));
	CHECK(stash.getUniqueRowCount() == 2);
}

TEST_CASE(player_stash_load_does_not_mark_dirty)
{
	PlayerStash stash;

	stash.load(2160, 0, 500);
	stash.load(2148, 2, 25);

	CHECK(stash.getCount(2160, 0) == 500);
	CHECK(stash.getCount(2148, 2) == 25);
	// Loading is what the database already holds, so there is nothing to save.
	CHECK(!stash.isDirty());
}

TEST_CASE(player_stash_mutations_mark_dirty)
{
	PlayerStash stash;
	CHECK(!stash.isDirty());

	CHECK(stash.add(2160, 0, 10));
	CHECK(stash.isDirty());

	stash.clearDirty();
	CHECK(!stash.isDirty());

	CHECK(stash.remove(2160, 0, 1));
	CHECK(stash.isDirty());
}

TEST_CASE(player_stash_failed_mutations_do_not_mark_dirty)
{
	PlayerStash stash;
	stash.load(2160, 0, 5);

	CHECK(!stash.remove(2160, 0, 6));
	CHECK(!stash.add(0, 0, 1));
	CHECK(!stash.isDirty());
}

TEST_CASE(player_stash_acknowledge_clears_only_what_was_saved)
{
	PlayerStash stash;
	CHECK(stash.add(2160, 0, 10));
	CHECK(stash.add(2148, 0, 20));

	const auto snapshot = stash.getDirtySnapshot();
	CHECK(snapshot.modifiedRows.size() == 2);

	stash.acknowledgeDirty(snapshot);
	CHECK(!stash.isDirty());
}

// The reason this class tracks per-row revisions at all: a stow landing while the
// save worker is running must not be acknowledged away by that save.
TEST_CASE(player_stash_keeps_rows_changed_after_the_snapshot)
{
	PlayerStash stash;
	CHECK(stash.add(2160, 0, 10));
	CHECK(stash.add(2148, 0, 20));

	const auto snapshot = stash.getDirtySnapshot();

	// The save is in flight; the player stows more of 2160.
	CHECK(stash.add(2160, 0, 5));

	stash.acknowledgeDirty(snapshot);

	// 2148 was written and is clean; 2160 changed afterwards and is still pending.
	CHECK(stash.isDirty());
	const auto pending = stash.getDirtySnapshot();
	CHECK(pending.modifiedRows.size() == 1);
	CHECK(pending.modifiedRows.contains(PlayerStash::Key{2160, 0}));
	CHECK(stash.getCount(2160, 0) == 15);
}

TEST_CASE(player_stash_records_are_sorted_and_tier_aware)
{
	PlayerStash stash;
	stash.load(2160, 3, 25);
	stash.load(2148, 0, 100);
	stash.load(2160, 0, 500);

	const auto records = stash.toRecords();

	CHECK(records.size() == 3);
	CHECK(records[0].itemId == 2148);
	CHECK(records[0].amount == 100);

	CHECK(records[1].itemId == 2160);
	CHECK(records[1].tier == 0);
	CHECK(records[1].amount == 500);

	CHECK(records[2].itemId == 2160);
	CHECK(records[2].tier == 3);
	CHECK(records[2].amount == 25);
}

TEST_CASE(player_stash_normalizes_tier_consistently)
{
	PlayerStash stash;

	// Above the maximum clamps, and must clamp the same way on read as on write,
	// otherwise a stow and its withdraw would address different rows.
	CHECK(stash.add(2160, Stash::MAX_ITEM_TIER + 5, 7));
	CHECK(stash.getCount(2160, Stash::MAX_ITEM_TIER) == 7);
	CHECK(stash.getUniqueRowCount() == 1);
}

TFS_TEST_MAIN()
