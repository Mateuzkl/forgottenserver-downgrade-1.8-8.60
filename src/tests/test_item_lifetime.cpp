#include "../otpch.h"

#include "../item.h"

#include "test_support.h"
#include <memory>

extern bool isValidItemPointer(Item* item);

TEST_CASE(item_lifetime_registry_tracks_destroyed_item)
{
	Item* rawItem = nullptr;
	{
		auto item = std::make_shared<Item>(0);
		rawItem = item.get();
		CHECK(isValidItemPointer(rawItem));
	}

	CHECK(!isValidItemPointer(rawItem));
}

TFS_TEST_MAIN()
