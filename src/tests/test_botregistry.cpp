#include "../otpch.h"

#include "../botregistry.h"

#include "test_support.h"

namespace {

struct StubPlayer
{
	bool removed = false;
	bool isRemoved() const { return removed; }
};

std::shared_ptr<StubPlayer> makeStub(bool removed = false)
{
	auto stub = std::make_shared<StubPlayer>();
	stub->removed = removed;
	return stub;
}

} // namespace

TEST_CASE(parse_guid_accepts_numeric_ids)
{
	CHECK(tfs::bot::parseGuid("123") == 123U);
	CHECK(tfs::bot::parseGuid("1") == 1U);
	CHECK(tfs::bot::parseGuid("4294967295") == 4294967295U);
}

TEST_CASE(parse_guid_rejects_invalid_input)
{
	CHECK(!tfs::bot::parseGuid(""));
	CHECK(!tfs::bot::parseGuid("0"));
	CHECK(!tfs::bot::parseGuid("abc"));
	CHECK(!tfs::bot::parseGuid("12ab"));
	CHECK(!tfs::bot::parseGuid("-5"));
	CHECK(!tfs::bot::parseGuid("4294967296")); // overflows uint32_t
	CHECK(!tfs::bot::parseGuid(" 123"));       // callers trim before parsing
}

TEST_CASE(registry_insert_rejects_null_zero_and_duplicates)
{
	tfs::bot::Registry<StubPlayer> registry;
	auto player = makeStub();

	CHECK(!registry.insert(0, player));
	CHECK(!registry.insert(1, nullptr));
	CHECK(registry.insert(1, player));
	CHECK(!registry.insert(1, makeStub()));
	CHECK(registry.size() == 1U);
}

TEST_CASE(registry_find_filters_removed_players)
{
	tfs::bot::Registry<StubPlayer> registry;
	auto alive = makeStub();
	auto dead = makeStub(true);

	CHECK(registry.insert(1, alive));
	CHECK(registry.insert(2, dead));

	CHECK(registry.find(1) == alive);
	CHECK(registry.find(2) == nullptr);
	CHECK(registry.contains(1));
	CHECK(!registry.contains(2));
	CHECK(!registry.contains(99));
}

TEST_CASE(registry_erase_is_idempotent)
{
	// The despawn bookkeeping (F3 fix) relies on erase reporting the removal
	// exactly once so the runtime timestamp is written exactly once.
	tfs::bot::Registry<StubPlayer> registry;
	CHECK(registry.insert(7, makeStub()));

	CHECK(registry.erase(7));
	CHECK(!registry.erase(7));
	CHECK(!registry.erase(7));
	CHECK(registry.size() == 0U);
}

TEST_CASE(registry_sweep_returns_swept_guids_and_keeps_live_entries)
{
	tfs::bot::Registry<StubPlayer> registry;
	auto alive = makeStub();
	CHECK(registry.insert(1, alive));
	CHECK(registry.insert(2, makeStub(true)));
	CHECK(registry.insert(3, makeStub(true)));

	auto swept = registry.sweepRemoved();
	CHECK(swept.size() == 2U);
	CHECK((swept[0] == 2U || swept[0] == 3U));
	CHECK((swept[1] == 2U || swept[1] == 3U));
	CHECK(swept[0] != swept[1]);

	CHECK(registry.size() == 1U);
	CHECK(registry.contains(1));
	CHECK(registry.sweepRemoved().empty());
}

TEST_CASE(registry_snapshot_and_guids_skip_removed_entries)
{
	tfs::bot::Registry<StubPlayer> registry;
	auto alive = makeStub();
	CHECK(registry.insert(1, alive));
	CHECK(registry.insert(2, makeStub(true)));

	auto snapshot = registry.snapshot();
	CHECK(snapshot.size() == 1U);
	CHECK(snapshot[0] == alive);

	auto guids = registry.guids();
	CHECK(guids.size() == 1U);
	CHECK(guids[0] == 1U);
}

TFS_TEST_MAIN()
