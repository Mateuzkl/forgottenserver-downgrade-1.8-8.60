#include "../otpch.h"

#include "../protocolgame.h"
#include "../protocollogin.h"

#include "test_support.h"

namespace {

LoginAttemptLimiter& limiter() { return LoginAttemptLimiter::getInstance(); }

// The limiter is a singleton, so every case uses its own IP to stay independent.
constexpr uint32_t IP_BYPASS = 0x0A000001;
constexpr uint32_t IP_LOCKOUT = 0x0A000002;
constexpr uint32_t IP_PER_ACCOUNT = 0x0A000003;
constexpr uint32_t IP_SPRAY = 0x0A000004;
constexpr uint32_t IP_CAST = 0x0A000005;
constexpr uint32_t IP_CASE = 0x0A000006;
constexpr uint32_t IP_CAST_GUESS = 0x0A000007;
constexpr uint32_t IP_PENDING = 0x0A000008;

} // namespace

// The bypass this guards against: recordSuccess() used to erase every failure
// recorded for the IP, so anyone holding one valid account could reset the
// counter at will - guess four times against a victim, log into their own
// account, repeat forever.
TEST_CASE(bruteforce_success_on_one_account_does_not_clear_another)
{
	for (int i = 0; i < 4; ++i) {
		limiter().recordFailure(IP_BYPASS, "victim");
	}

	// Attacker logs into an account they legitimately own, from the same IP.
	limiter().recordSuccess(IP_BYPASS, "attacker");

	// The victim's four failures must still stand, so the fifth trips the lockout.
	limiter().recordFailure(IP_BYPASS, "victim");
	CHECK(!limiter().allowLogin(IP_BYPASS, "victim"));
}

TEST_CASE(bruteforce_blocks_after_max_failures_on_one_account)
{
	CHECK(limiter().allowLogin(IP_LOCKOUT, "target"));

	for (int i = 0; i < 4; ++i) {
		limiter().recordFailure(IP_LOCKOUT, "target");
		CHECK(limiter().allowLogin(IP_LOCKOUT, "target"));
	}

	limiter().recordFailure(IP_LOCKOUT, "target");
	CHECK(!limiter().allowLogin(IP_LOCKOUT, "target"));
}

// A lockout is scoped to one account, so it cannot be used to deny service to
// everyone else behind the same address.
TEST_CASE(bruteforce_lockout_does_not_spill_onto_other_accounts)
{
	for (int i = 0; i < 5; ++i) {
		limiter().recordFailure(IP_PER_ACCOUNT, "locked");
	}

	CHECK(!limiter().allowLogin(IP_PER_ACCOUNT, "locked"));
	CHECK(limiter().allowLogin(IP_PER_ACCOUNT, "someone-else"));
}

// Spraying spreads guesses thin enough that no single account reaches the
// per-account threshold, which is what the per-IP counter is there to catch.
//
// Note the tradeoff this encodes deliberately: once the IP threshold is reached,
// even an account that was never touched is refused from that address. That is
// the point - a sprayer must not get a fresh start by picking a new name - but it
// does mean a shared address (NAT, an internet cafe, a school) can be locked out
// by one abusive user behind it. The threshold is set well above what ordinary
// mistyping produces, and is configurable via bruteForceIpFailures, so operators
// who serve large NATs can raise it.
TEST_CASE(bruteforce_spraying_many_accounts_trips_the_ip_guard)
{
	for (int i = 0; i < 20; ++i) {
		limiter().recordFailure(IP_SPRAY, "account" + std::to_string(i));
	}

	// No individual account was guessed more than once, yet the address is done.
	CHECK(!limiter().allowLogin(IP_SPRAY, "account0"));
	CHECK(!limiter().allowLogin(IP_SPRAY, "never-touched"));
}

// A cast (spectator) request carries no account name. It must not be caught by a
// single account's lockout, or watching a stream would break whenever someone on
// the same address fumbled their password.
TEST_CASE(bruteforce_cast_request_is_not_caught_by_an_account_lockout)
{
	for (int i = 0; i < 5; ++i) {
		limiter().recordFailure(IP_CAST, "locked");
	}

	CHECK(!limiter().allowLogin(IP_CAST, "locked"));
	CHECK(limiter().allowLogin(IP_CAST, ""));
}

// Account names arrive as the client typed them, so the key has to be normalized
// or changing capitalisation would hand out a fresh set of attempts each time.
TEST_CASE(bruteforce_account_names_are_normalized)
{
	for (int i = 0; i < 5; ++i) {
		limiter().recordFailure(IP_CASE, "MiXeD");
	}

	CHECK(!limiter().allowLogin(IP_CASE, "mixed"));
	CHECK(!limiter().allowLogin(IP_CASE, "  MIXED  "));
}

// A rejected cast password is recorded with an empty account name, so it feeds
// the per-IP guard without being attributable to any account. Both cast paths -
// the game world spectate() and the login server's cast list - do this.
TEST_CASE(bruteforce_cast_password_failures_feed_the_ip_guard)
{
	CHECK(limiter().allowLogin(IP_CAST_GUESS, ""));

	// The production methods are seams used by ProtocolGame::spectate() and the
	// login-server cast-list path after each one rejects a cast password.
	for (uint32_t i = 0; i < 10; ++i) {
		CHECK(limiter().reserveLogin(IP_CAST_GUESS, ""));
		ProtocolGame::recordRejectedCastPassword(IP_CAST_GUESS);
	}
	CHECK(limiter().allowLogin(IP_CAST_GUESS, ""));

	for (uint32_t i = 0; i < 10; ++i) {
		CHECK(limiter().reserveLogin(IP_CAST_GUESS, ""));
		ProtocolLogin::recordRejectedCastPassword(IP_CAST_GUESS);
	}

	// The address is now refused even for a request carrying no account name.
	CHECK(!limiter().allowLogin(IP_CAST_GUESS, ""));
	CHECK(!limiter().allowLogin(IP_CAST_GUESS, "any-account"));
}

TEST_CASE(bruteforce_pending_attempts_reserve_capacity)
{
	for (uint32_t i = 0; i < 5; ++i) {
		CHECK(limiter().reserveLogin(IP_PENDING, "target"));
	}
	CHECK(!limiter().reserveLogin(IP_PENDING, "target"));

	limiter().releaseReservation(IP_PENDING, "target");
	CHECK(limiter().reserveLogin(IP_PENDING, "target"));
	limiter().commitSuccess(IP_PENDING, "target");
	CHECK(limiter().reserveLogin(IP_PENDING, "target"));
	CHECK(!limiter().reserveLogin(IP_PENDING, "target"));

	for (uint32_t i = 0; i < 5; ++i) {
		limiter().commitFailure(IP_PENDING, "target");
	}
	CHECK(!limiter().allowLogin(IP_PENDING, "target"));
}

TFS_TEST_MAIN()
