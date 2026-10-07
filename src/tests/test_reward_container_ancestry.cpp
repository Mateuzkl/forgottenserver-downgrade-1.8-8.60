#include "../otpch.h"

#include "test_support.h"

#include "container.h"
#include "rewardchest.h"

#include <memory>

namespace {

constexpr uint16_t TEST_CONTAINER_ID = 1987;

std::shared_ptr<Container> makeNestedContainer(Container& parent)
{
	auto nested = Item::make<Container>(TEST_CONTAINER_ID, 20);
	parent.internalAddThing(nested.get());
	return nested;
}

TEST_CASE(detects_nested_item_reward_container)
{
	auto rewardContainer = Item::make<Container>(ITEM_REWARD_CONTAINER, 20);
	auto nested = makeNestedContainer(*rewardContainer);

	CHECK(isInsideRewardContainer(nested.get()));
}

TEST_CASE(detects_nested_reward_chest)
{
	auto rewardChest = Item::make<RewardChest>(ITEM_REWARD_CHEST);
	auto nested = makeNestedContainer(*rewardChest);

	CHECK(isInsideRewardContainer(nested.get()));
}

TEST_CASE(detects_nested_reward_corpse)
{
	auto rewardCorpse = Item::make<Container>(TEST_CONTAINER_ID, 20);
	auto rewardMarker = Item::make<Container>(ITEM_REWARD_CONTAINER, 20);
	rewardCorpse->internalAddThing(rewardMarker.get());
	auto nested = makeNestedContainer(*rewardCorpse);

	CHECK(rewardCorpse->isRewardCorpse());
	CHECK(isInsideRewardContainer(nested.get()));
}

TEST_CASE(allows_nested_regular_container)
{
	auto regularContainer = Item::make<Container>(TEST_CONTAINER_ID, 20);
	auto nested = makeNestedContainer(*regularContainer);

	CHECK(!isInsideRewardContainer(nested.get()));
}

} // namespace

TFS_TEST_MAIN()
