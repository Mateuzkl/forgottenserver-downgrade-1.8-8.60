#include "../save_journal.h"
#include "../transaction_retry.h"
#include "test_support.h"

namespace {
int begins, commits, rollbacks, calls;
bool beginOK, commitOK, rollbackOK, retry;
struct Transaction
{
	bool begin()
	{
		++begins;
		return beginOK;
	}
	bool commit()
	{
		++commits;
		return commitOK;
	}
	bool rollback()
	{
		++rollbacks;
		retry = false;
		return rollbackOK;
	}
};
void reset()
{
	begins = commits = rollbacks = calls = 0;
	beginOK = commitOK = rollbackOK = true;
	retry = false;
}
template <typename Callback>
bool run(Callback callback)
{
	return tfs::database::runTransaction<Transaction>(callback, [] { return retry; }, [](const char*) {}, 3);
}
} // namespace

TEST_CASE(transaction_retry_success_and_permanent_failure)
{
	reset();
	CHECK(run([] {
		++calls;
		return true;
	}));
	CHECK(begins == 1 && calls == 1 && commits == 1 && rollbacks == 0);
	reset(); // Syntax/schema errors are not retryable.
	CHECK(!run([] {
		++calls;
		return false;
	}));
	CHECK(begins == 1 && calls == 1 && rollbacks == 1);
	reset(); // Disconnected database/BEGIN failure never retries the snapshot.
	beginOK = false;
	CHECK(!run([] {
		++calls;
		return true;
	}));
	CHECK(begins == 1 && calls == 0);
}

TEST_CASE(transaction_retry_preserves_deadlock_before_rollback)
{
	reset();
	CHECK(run([] {
		retry = ++calls == 1;
		return calls > 1;
	}));
	CHECK(begins == 2 && calls == 2 && commits == 1 && rollbacks == 1);
	reset(); // Repeated deadlocks or lock timeouts: exactly three attempts.
	CHECK(!run([] {
		++calls;
		retry = true;
		return false;
	}));
	CHECK(begins == 3 && calls == 3 && rollbacks == 3);
	reset();
	rollbackOK = false;
	CHECK(!run([] {
		++calls;
		retry = true;
		return false;
	}));
	CHECK(begins == 1 && calls == 1);
}

TEST_CASE(transaction_retry_commit_failure_and_exceptions)
{
	reset();
	commitOK = false;
	CHECK(!run([] {
		++calls;
		return true;
	}));
	CHECK(begins == 1 && commits == 1 && rollbacks == 1);
	reset();
	commitOK = false;
	CHECK(!run([] {
		++calls;
		retry = true;
		return true;
	}));
	CHECK(begins == 3 && commits == 3 && rollbacks == 3);
	reset();
	CHECK(!run([]() -> bool {
		++calls;
		throw std::runtime_error("SQL failure");
	}));
	CHECK(begins == 1 && calls == 1 && rollbacks == 1);
}

TEST_CASE(save_journal_roundtrip_and_corruption)
{
	const std::vector<std::string> queries{"UPDATE players SET onlinetime=120 WHERE id=7", std::string("a\0b;c", 5)};
	const auto encoded = tfs::save::encode(7, 42, queries);
	CHECK(encoded);
	CHECK(tfs::save::decode(*encoded, 7, 42) == queries);
	CHECK(!tfs::save::decode(*encoded, 8, 42));
	CHECK(!tfs::save::decode(*encoded, 7, 41));
	for (size_t length = 0; length < encoded->size(); ++length) {
		CHECK(!tfs::save::decode(std::string_view(*encoded).substr(0, length), 7, 42));
	}
	CHECK(!tfs::save::decode(*encoded + "x", 7, 42));
	CHECK(!tfs::save::encode(7, 0, queries));
	CHECK(!tfs::save::encode(7, 42, {}));
}

TFS_TEST_MAIN()
