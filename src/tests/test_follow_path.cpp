#include "../otpch.h"

#include "../follow_path.h"

#include "test_support.h"

namespace {
constexpr FollowPathKey KEY{{100, 100, 7}, {105, 100, 7}, 42};
}

TEST_CASE(follow_path_reuses_unchanged_success)
{
	FollowPathState state;
	state.recordSuccess(KEY);

	CHECK(state.canReuse(KEY, false, false));
	CHECK(state.canReuse(FollowPathKey{{101, 100, 7}, KEY.target, KEY.targetId}, true, false));
	CHECK(!state.canReuse(KEY, true, true));
}

TEST_CASE(follow_path_recalculates_when_target_changes)
{
	FollowPathState state;
	state.recordSuccess(KEY);

	CHECK(!state.canReuse(FollowPathKey{KEY.start, {106, 100, 7}, KEY.targetId}, true, false));
	CHECK(!state.canReuse(FollowPathKey{KEY.start, KEY.target, 43}, true, false));
}

TEST_CASE(follow_path_allows_only_one_pending_request)
{
	FollowPathState state;
	uint32_t firstToken = 0;
	uint32_t duplicateToken = 0;

	CHECK(state.beginRequest(firstToken));
	CHECK(!state.beginRequest(duplicateToken));
	CHECK(state.isPending());
	CHECK(state.acceptResult(firstToken));
	CHECK(!state.isPending());
}

TEST_CASE(follow_path_abandon_allows_replacement_request)
{
	FollowPathState state;
	uint32_t abandonedToken = 0;
	uint32_t replacementToken = 0;

	CHECK(state.beginRequest(abandonedToken));
	state.abandonRequest(abandonedToken);
	CHECK(!state.isPending());
	CHECK(state.beginRequest(replacementToken));
	CHECK(state.isPending());
	CHECK(state.acceptResult(replacementToken));
}

TEST_CASE(follow_path_rejects_stale_result_after_invalidation)
{
	FollowPathState state;
	uint32_t token = 0;
	CHECK(state.beginRequest(token));

	state.invalidate(true);

	CHECK(!state.acceptResult(token));
	CHECK(!state.isPending());
}

TEST_CASE(follow_path_failure_uses_bounded_exponential_backoff)
{
	FollowPathState state;
	CHECK(state.recordFailure(KEY, 1000) == 250);
	CHECK(!state.retryAllowed(KEY, 1249));
	CHECK(state.retryAllowed(KEY, 1250));
	CHECK(state.recordFailure(KEY, 1250) == 500);
	CHECK(state.recordFailure(KEY, 1750) == 1000);
	CHECK(state.recordFailure(KEY, 2750) == 2000);
	CHECK(state.recordFailure(KEY, 4750) == 2000);
	CHECK(state.getConsecutiveFailures() == 4);
}

TEST_CASE(follow_path_target_or_map_change_resets_backoff)
{
	FollowPathState state;
	state.recordFailure(KEY, 1000);
	state.invalidate(true);

	CHECK(state.getConsecutiveFailures() == 0);
	CHECK(state.retryAllowed(KEY, 1001));
}

TEST_CASE(follow_path_cancel_makes_pending_callback_harmless)
{
	FollowPathState state;
	uint32_t token = 0;
	CHECK(state.beginRequest(token));

	state.cancel();

	CHECK(!state.isPending());
	CHECK(!state.acceptResult(token));
}

TFS_TEST_MAIN()
