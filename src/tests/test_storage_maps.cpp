#include "../otpch.h"

#include "../creature.h"
#include "../game.h"
#include "../player.h"

#include <absl/container/flat_hash_map.h>
#include "test_support.h"
#include <type_traits>

TEST_CASE(storage_maps_use_flat_hash_map)
{
	static_assert(std::is_same_v<Creature::StorageMap, absl::flat_hash_map<uint32_t, int64_t>>);
	static_assert(std::is_same_v<Game::StorageMap, absl::flat_hash_map<uint32_t, int64_t>>);

	CHECK(true);
}

TEST_CASE(storage_dirty_acknowledgement_preserves_a_newer_write)
{
	Player player(nullptr);
	player.clearStorageDirty();

	constexpr uint32_t key = STORAGE_DAILY_REWARD_INSTANT_TOKENS;
	player.setStorageValue(key, 1);
	const auto snapshot = player.getStorageDirtySnapshot();
	player.setStorageValue(key, 2);

	player.acknowledgeStorageDirty(snapshot);

	CHECK(player.getStorageValue(key).value_or(0) == 2);
	CHECK(player.getModifiedStorageKeys().contains(key));
}

TEST_CASE(selective_storage_acknowledgement_preserves_unrelated_dirty_keys)
{
	Player player(nullptr);
	player.clearStorageDirty();

	player.setStorageValue(STORAGE_DAILY_REWARD_LAST_DAY, 10);
	player.setStorageValue(99999, 20);
	const auto completeSnapshot = player.getStorageDirtySnapshot();

	Player::StorageDirtySnapshot dailySnapshot;
	dailySnapshot.snapshotId = completeSnapshot.snapshotId;
	dailySnapshot.modifiedKeys.insert(STORAGE_DAILY_REWARD_LAST_DAY);
	player.acknowledgeStorageDirty(dailySnapshot);

	CHECK(!player.getModifiedStorageKeys().contains(STORAGE_DAILY_REWARD_LAST_DAY));
	CHECK(player.getModifiedStorageKeys().contains(99999));
}

TFS_TEST_MAIN()
