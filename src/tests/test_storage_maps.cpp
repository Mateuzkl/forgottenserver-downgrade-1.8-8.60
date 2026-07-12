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

TEST_CASE(bestiary_kills_are_counted_in_memory)
{
	Player player(nullptr);

	CHECK(player.getBestiaryKillCount(35) == 0);
	CHECK(player.addBestiaryKillCount(35) == 1);
	CHECK(player.addBestiaryKillCount(35, 4) == 5);
	CHECK(player.getBestiaryKillCount(35) == 5);
	CHECK(player.addBestiaryKillCount(0, 10) == 0);
}

TEST_CASE(bestiary_kills_saturate_without_overflow)
{
	Player player(nullptr);

	CHECK(player.addBestiaryKillCount(434, std::numeric_limits<uint32_t>::max()) ==
	      std::numeric_limits<uint32_t>::max());
	CHECK(player.addBestiaryKillCount(434, 1) == std::numeric_limits<uint32_t>::max());
}

TFS_TEST_MAIN()
