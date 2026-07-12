#include "../otpch.h"

#include "../walk_event.h"

#include "test_support.h"

TEST_CASE(walk_event_valid_ticket_executes_once)
{
	WalkEventState state;
	const auto generation = state.beginScheduling();
	CHECK(generation.has_value());
	CHECK(state.commitSchedule(*generation, 10));
	CHECK(state.beginExecution(*generation));
	CHECK(!state.beginExecution(*generation));
	CHECK(state.finishExecution(*generation));
	CHECK(!state.isQueued());
	CHECK(!state.isExecuting());
}

TEST_CASE(walk_event_stale_ticket_does_not_change_replacement)
{
	WalkEventState state;
	const auto first = state.beginScheduling();
	CHECK(first.has_value());
	CHECK(state.commitSchedule(*first, 11));
	const auto cancelled = state.cancel();
	CHECK(cancelled.eventId == 11);

	const auto replacement = state.beginScheduling();
	CHECK(replacement.has_value());
	CHECK(state.commitSchedule(*replacement, 12));
	CHECK(!state.beginExecution(*first));
	CHECK(state.isQueued());
	CHECK(state.getEventId() == 12);
	CHECK(state.getGeneration() == *replacement);
}

TEST_CASE(walk_event_cancelled_execution_cannot_reschedule)
{
	WalkEventState state;
	const auto generation = state.beginScheduling();
	CHECK(generation.has_value());
	CHECK(state.commitSchedule(*generation, 21));
	CHECK(state.beginExecution(*generation));
	const auto cancelled = state.cancel();
	CHECK(cancelled.wasExecuting);
	CHECK(!state.finishExecution(*generation));
	CHECK(!state.isExecuting());
}

TEST_CASE(walk_event_allows_only_one_logical_ticket)
{
	WalkEventState state;
	const auto generation = state.beginScheduling();
	CHECK(generation.has_value());
	CHECK(!state.beginScheduling().has_value());
	CHECK(state.commitSchedule(*generation, 31));
	CHECK(!state.beginScheduling().has_value());
}

TEST_CASE(walk_event_failed_schedule_releases_state)
{
	WalkEventState state;
	const auto failed = state.beginScheduling();
	CHECK(failed.has_value());
	CHECK(!state.commitSchedule(*failed, 0));
	CHECK(state.beginScheduling().has_value());
}

TEST_CASE(walk_event_generation_skips_zero_on_wraparound)
{
	WalkEventState state(std::numeric_limits<uint64_t>::max());
	const auto generation = state.beginScheduling();
	CHECK(generation.has_value());
	CHECK(*generation == 1);
}

TFS_TEST_MAIN()
