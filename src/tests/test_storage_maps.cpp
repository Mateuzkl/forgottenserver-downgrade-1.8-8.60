#include "../otpch.h"

#include "../creature.h"
#include "../game.h"

#include <absl/container/flat_hash_map.h>
#include "test_support.h"
#include <type_traits>

TEST_CASE(storage_maps_use_flat_hash_map)
{
	static_assert(std::is_same_v<Creature::StorageMap, absl::flat_hash_map<uint32_t, int64_t>>);
	static_assert(std::is_same_v<Game::StorageMap, absl::flat_hash_map<uint32_t, int64_t>>);

	CHECK(true);
}

TFS_TEST_MAIN()
