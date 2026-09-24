#include "../otpch.h"

#include "../iomapserialize.h"

#include "test_support.h"

namespace {

ItemType makeFixture(uint16_t id, ItemTypes_t type)
{
	ItemType itemType;
	itemType.id = id;
	itemType.type = type;
	return itemType;
}

} // namespace

TEST_CASE(exact_static_fixture_ids_match)
{
	auto mapItem = makeFixture(100, ITEM_TYPE_NONE);
	auto persistedItem = makeFixture(100, ITEM_TYPE_NONE);
	CHECK(IOMapSerialize::isSamePersistentFixtureFamily(mapItem, persistedItem));
}

TEST_CASE(same_bed_transform_family_matches)
{
	auto freeBed = makeFixture(200, ITEM_TYPE_BED);
	freeBed.transformToOnUse[PLAYERSEX_MALE] = 201;
	freeBed.transformToOnUse[PLAYERSEX_FEMALE] = 202;
	auto occupiedMale = makeFixture(201, ITEM_TYPE_BED);
	occupiedMale.transformToFree = 200;
	auto occupiedFemale = makeFixture(202, ITEM_TYPE_BED);
	occupiedFemale.transformToFree = 200;

	CHECK(IOMapSerialize::isSamePersistentFixtureFamily(freeBed, occupiedMale));
	CHECK(IOMapSerialize::isSamePersistentFixtureFamily(occupiedMale, occupiedFemale));
}

TEST_CASE(different_bed_transform_families_do_not_match)
{
	auto bedA = makeFixture(200, ITEM_TYPE_BED);
	auto bedB = makeFixture(300, ITEM_TYPE_BED);
	CHECK(!IOMapSerialize::isSamePersistentFixtureFamily(bedA, bedB));
}

TEST_CASE(same_door_transform_family_matches)
{
	auto closedDoor = makeFixture(400, ITEM_TYPE_DOOR);
	closedDoor.persistentTransformFamily = 401;
	auto openDoor = makeFixture(401, ITEM_TYPE_DOOR);
	openDoor.persistentTransformFamily = 401;
	CHECK(IOMapSerialize::isSamePersistentFixtureFamily(closedDoor, openDoor));
}

TEST_CASE(different_door_transform_families_do_not_match)
{
	auto doorA = makeFixture(400, ITEM_TYPE_DOOR);
	doorA.persistentTransformFamily = 401;
	auto doorB = makeFixture(500, ITEM_TYPE_DOOR);
	doorB.persistentTransformFamily = 501;
	CHECK(!IOMapSerialize::isSamePersistentFixtureFamily(doorA, doorB));
}

TFS_TEST_MAIN()
