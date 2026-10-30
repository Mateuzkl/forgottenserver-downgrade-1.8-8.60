#include "../otpch.h"

#include "../weapons.h"

#include "test_support.h"

TEST_CASE(melee_damage_does_not_roll_near_zero)
{
	for (int32_t sample = 0; sample < 1'000; ++sample) {
		const int32_t damage = Weapons::getStablePlayerDamage(0, 100);
		CHECK(damage >= 43);
		CHECK(damage <= 58);
	}
}

TEST_CASE(distance_damage_stays_close_to_its_average)
{
	for (int32_t sample = 0; sample < 1'000; ++sample) {
		const int32_t damage = Weapons::getStablePlayerDamage(40, 100);
		CHECK(damage >= 60);
		CHECK(damage <= 81);
	}
}

TEST_CASE(stable_damage_preserves_fixed_values)
{
	CHECK(Weapons::getStablePlayerDamage(75, 75) == 75);
	CHECK(Weapons::getStablePlayerDamage(0, 0) == 0);
}

TFS_TEST_MAIN()
