// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../character_bazaar.h"
#include "../configmanager.h"
#include "../game.h"
#include "../iologindata.h"
#include "../outputmessage.h"
#include "../player.h"
#include "../protocolgame.h"
#include "../protocollogin.h"
#include "../reactor.h"
#include "../tools.h"

#include "protocolgame_owned_fixture.h"
#include "test_support.h"

struct TaskReactorTestAccess
{
	const TaskReactor* previous = TaskReactor::currentReactor;
	TaskReactorTestAccess() { TaskReactor::currentReactor = &g_reactor; }
	~TaskReactorTestAccess() { TaskReactor::currentReactor = previous; }
};

struct AccountManagerSecurityTestAccess
{
	static void dispatch(ProtocolGame& protocol, const std::shared_ptr<Player>& session, NetworkMessage& message)
	{
		protocol.player = session;
		protocol.acceptPackets = true;
		CHECK(message.setBufferPosition(0));
		auto packet = tfs::net::make_network_message(message);
		TaskReactorTestAccess dispatcher;
		protocol.parsePacketOnDispatcher(packet);
		CHECK(!packet->isOverrun());
		CHECK(packet->getBufferPosition() == NetworkMessage::INITIAL_BUFFER_POSITION + packet->getLength());
	}

	static void configureNewAccountSession(Player& player, uint32_t ip)
	{
		player.setAccountManagerMode(ACCOUNT_MANAGER_NEW);
		player.setAccountManagerData(0);
		player.lastIP = ip;
		player.resetTalkState(0, 0);
	}

	static void configureAccountSession(Player& player, uint32_t accountId, uint32_t ip)
	{
		player.setAccountManagerMode(ACCOUNT_MANAGER_ACCOUNT);
		player.setAccountManagerData(accountId);
		player.lastIP = ip;
		player.resetTalkState(0, 0);
		player.setManagerTalkState(1, true);
	}

	static void prepareRecoveryKeyPrompt(Player& player, const std::string& canonicalAccountName)
	{
		player.managerData.accountName = canonicalAccountName;
		player.managerTalkState[1] = true;
		player.managerTalkState[6] = false;
		player.managerTalkState[7] = true;
	}

	static bool verifyRecovery(Player& player, uint32_t accId, std::string_view storedHash, std::string_view key,
	                           uint32_t ip, std::ostringstream& msg)
	{
		return player.verifyAccountRecoveryAttempt(accId, storedHash, key, ip, msg);
	}

	static uint8_t recoveryAttempts(const Player& player) { return player.managerData.recoveryAttempts; }
	static bool talkState(const Player& player, size_t index) { return player.managerTalkState[index]; }
	static const std::string& accountName(const Player& player) { return player.managerData.accountName; }
};

namespace {

// Each test uses a dedicated IPv4 address so singleton LoginAttemptLimiter buckets stay isolated.
constexpr uint32_t IP_IN_SESSION = 0x0A100001;
constexpr uint32_t IP_CANCEL_RESTART = 0x0A100002;
constexpr uint32_t IP_RECONNECT_LOCK = 0x0A100003;
constexpr uint32_t IP_RECONNECT_CUMULATIVE = 0x0A100004;
constexpr uint32_t IP_CROSS_ACCOUNT_RECOVERY = 0x0A100005;
constexpr uint32_t IP_SPRAY_RECONNECT = 0x0A100006;
constexpr uint32_t IP_SUCCESS_RESET = 0x0A100007;
constexpr uint32_t IP_PROTECTED_ACCOUNT = 0x0A100008;

} // namespace

TEST_CASE(account_manager_cross_account_deletion_ownership_guards)
{
	// Account 2 may only delete non-protected characters owned by account 2.
	CHECK(Player::canAccountDeleteCharacter(2, 50, 2));
	CHECK(!Player::canAccountDeleteCharacter(2, 50, 3));
	CHECK(!Player::canAccountDeleteCharacter(2, 50, 0));

	// Protected Account Manager account (id <= 1) and character (guid <= 1) are always rejected.
	CHECK(!Player::canAccountDeleteCharacter(1, 50, 1));
	CHECK(!Player::canAccountDeleteCharacter(0, 50, 0));
	CHECK(!Player::canAccountDeleteCharacter(2, 1, 2));
	CHECK(!Player::canAccountDeleteCharacter(2, 0, 2));

	// Database layer rejects protected IDs before issuing SQL.
	CHECK(!IOLoginData::deletePlayer(1, 2));
	CHECK(!IOLoginData::deletePlayer(0, 2));
	CHECK(!IOLoginData::deletePlayer(50, 1));
	CHECK(!IOLoginData::deletePlayer(50, 0));
	CHECK(!IOLoginData::setPassword(1, "newpassword123"));
	CHECK(!IOLoginData::setAccountEmail(1, "attacker@example.com"));
	CHECK(!IOLoginData::setRecoveryKey(1, "AAAA-BBBB-CCCC-DDDD"));
	CHECK(!IOLoginData::createPlayer(1, "Injected", 1, PLAYERSEX_MALE));
}

TEST_CASE(account_manager_blocks_protected_account_session_and_preserves_delete_flow)
{
	// Protected account (id=1) cannot enter management flows.
	Player protectedSession(nullptr);
	AccountManagerSecurityTestAccess::configureAccountSession(protectedSession, 1, IP_PROTECTED_ACCOUNT);
	protectedSession.manageAccount("delete");
	CHECK(!AccountManagerSecurityTestAccess::talkState(protectedSession, 2));

	// Legitimate account (id=2) keeps normal delete -> character -> no -> cancel state transitions.
	Player playerSession(nullptr);
	AccountManagerSecurityTestAccess::configureAccountSession(playerSession, 2, IP_PROTECTED_ACCOUNT);
	playerSession.manageAccount("delete");
	CHECK(AccountManagerSecurityTestAccess::talkState(playerSession, 2));
	CHECK(!AccountManagerSecurityTestAccess::talkState(playerSession, 1));

	playerSession.manageAccount("Own Character");
	CHECK(AccountManagerSecurityTestAccess::talkState(playerSession, 3));
	CHECK(!AccountManagerSecurityTestAccess::talkState(playerSession, 2));

	// Saying "no" returns to character prompt (talkState[2]), not main menu.
	playerSession.manageAccount("no");
	CHECK(AccountManagerSecurityTestAccess::talkState(playerSession, 2));
	CHECK(!AccountManagerSecurityTestAccess::talkState(playerSession, 3));

	// Saying "cancel" returns to main account menu (talkState[1]).
	playerSession.manageAccount("cancel");
	CHECK(AccountManagerSecurityTestAccess::talkState(playerSession, 1));
	CHECK(!AccountManagerSecurityTestAccess::talkState(playerSession, 2));
}

TEST_CASE(account_manager_recovery_blocks_after_five_failures_and_persists_across_cancel)
{
	const std::string validHash = transformToSHA1Hex("GOOD-1111-2222-3333");
	Player session(nullptr);
	AccountManagerSecurityTestAccess::configureNewAccountSession(session, IP_CANCEL_RESTART);

	// 3 wrong guesses, then user sends "cancel" and restarts "recover".
	AccountManagerSecurityTestAccess::prepareRecoveryKeyPrompt(session, "VictimAccount");
	for (int i = 0; i < 3; ++i) {
		std::ostringstream msg;
		CHECK(!AccountManagerSecurityTestAccess::verifyRecovery(session, 42, validHash, "WRONG-KEY",
		                                                        IP_CANCEL_RESTART, msg));
	}
	CHECK(AccountManagerSecurityTestAccess::recoveryAttempts(session) == 3);

	session.manageAccount("cancel");
	CHECK(AccountManagerSecurityTestAccess::recoveryAttempts(session) == 3);
	CHECK(!AccountManagerSecurityTestAccess::talkState(session, 1));
	CHECK(AccountManagerSecurityTestAccess::accountName(session).empty());

	// Restarting "recover" is still allowed while under 5 attempts, and must NOT zero the counter.
	session.manageAccount("recover");
	CHECK(AccountManagerSecurityTestAccess::talkState(session, 6));
	CHECK(AccountManagerSecurityTestAccess::recoveryAttempts(session) == 3);

	// 2 more wrong guesses hit the 5-attempt cap and lock recovery for the session.
	AccountManagerSecurityTestAccess::prepareRecoveryKeyPrompt(session, "VictimAccount");
	for (int i = 0; i < 2; ++i) {
		std::ostringstream msg;
		CHECK(!AccountManagerSecurityTestAccess::verifyRecovery(session, 42, validHash, "WRONG-KEY",
		                                                        IP_CANCEL_RESTART, msg));
	}
	CHECK(AccountManagerSecurityTestAccess::recoveryAttempts(session) == 5);
	CHECK(!AccountManagerSecurityTestAccess::talkState(session, 7));
	CHECK(!AccountManagerSecurityTestAccess::talkState(session, 1));

	// Neither "cancel" nor "recover" bypasses the limit; even the valid key is now rejected.
	session.manageAccount("cancel");
	CHECK(AccountManagerSecurityTestAccess::recoveryAttempts(session) == 5);
	session.manageAccount("recover");
	CHECK(!AccountManagerSecurityTestAccess::talkState(session, 6));

	AccountManagerSecurityTestAccess::prepareRecoveryKeyPrompt(session, "VictimAccount");
	std::ostringstream lockedMsg;
	CHECK(!AccountManagerSecurityTestAccess::verifyRecovery(session, 42, validHash, "GOOD-1111-2222-3333",
	                                                        IP_CANCEL_RESTART, lockedMsg));
}

TEST_CASE(account_manager_recovery_lockout_persists_across_reconnections)
{
	const std::string validHash = transformToSHA1Hex("REAL-AAAA-BBBB-CCCC");

	// Session 1 exhausts all 5 attempts against "VictimOne".
	{
		Player session1(nullptr);
		AccountManagerSecurityTestAccess::configureNewAccountSession(session1, IP_RECONNECT_LOCK);
		AccountManagerSecurityTestAccess::prepareRecoveryKeyPrompt(session1, "VictimOne");
		for (int i = 0; i < 5; ++i) {
			std::ostringstream msg;
			CHECK(!AccountManagerSecurityTestAccess::verifyRecovery(session1, 100, validHash, "BAD-KEY",
			                                                        IP_RECONNECT_LOCK, msg));
		}
	}

	// Session 2 reconnects with a fresh Player object (recoveryAttempts == 0) from the same IP.
	// Case/whitespace variations resolve to the same normalized bucket and must remain blocked.
	{
		Player session2(nullptr);
		AccountManagerSecurityTestAccess::configureNewAccountSession(session2, IP_RECONNECT_LOCK);
		CHECK(AccountManagerSecurityTestAccess::recoveryAttempts(session2) == 0);
		CHECK(!Player::isAccountRecoveryAllowed(IP_RECONNECT_LOCK, "victimone"));
		CHECK(!Player::isAccountRecoveryAllowed(IP_RECONNECT_LOCK, "  VICTIMONE  "));

		AccountManagerSecurityTestAccess::prepareRecoveryKeyPrompt(session2, "victimone");
		std::ostringstream msg;
		CHECK(!AccountManagerSecurityTestAccess::verifyRecovery(session2, 100, validHash, "REAL-AAAA-BBBB-CCCC",
		                                                        IP_RECONNECT_LOCK, msg));
	}

	// Normal password login bucket for "victimone" is separate from "recover:victimone".
	CHECK(LoginAttemptLimiter::getInstance().allowLogin(IP_RECONNECT_LOCK, "victimone"));
}

TEST_CASE(account_manager_recovery_counts_cumulatively_across_partial_reconnections)
{
	const std::string validHash = transformToSHA1Hex("SECRET-KEY-9999");

	// Attacker tries 3 guesses, disconnects before hitting 5...
	{
		Player session1(nullptr);
		AccountManagerSecurityTestAccess::configureNewAccountSession(session1, IP_RECONNECT_CUMULATIVE);
		AccountManagerSecurityTestAccess::prepareRecoveryKeyPrompt(session1, "TargetAcc");
		for (int i = 0; i < 3; ++i) {
			std::ostringstream msg;
			CHECK(!AccountManagerSecurityTestAccess::verifyRecovery(session1, 200, validHash, "GUESS-1",
			                                                        IP_RECONNECT_CUMULATIVE, msg));
		}
		CHECK(Player::isAccountRecoveryAllowed(IP_RECONNECT_CUMULATIVE, "TargetAcc"));
	}

	// ...reconnects in a new session and tries 2 more wrong keys -> total hits 5 and locks!
	{
		Player session2(nullptr);
		AccountManagerSecurityTestAccess::configureNewAccountSession(session2, IP_RECONNECT_CUMULATIVE);
		AccountManagerSecurityTestAccess::prepareRecoveryKeyPrompt(session2, "TargetAcc");
		for (int i = 0; i < 2; ++i) {
			std::ostringstream msg;
			CHECK(!AccountManagerSecurityTestAccess::verifyRecovery(session2, 200, validHash, "GUESS-2",
			                                                        IP_RECONNECT_CUMULATIVE, msg));
		}
		CHECK(!Player::isAccountRecoveryAllowed(IP_RECONNECT_CUMULATIVE, "TargetAcc"));
	}
}

TEST_CASE(account_manager_recovery_success_isolated_per_account_and_ip_spray_blocked)
{
	const std::string validHash = transformToSHA1Hex("VALID-KEY-7777");

	// 4 failures on "victim", then success on attacker's own account must NOT clear "victim".
	for (int i = 0; i < 4; ++i) {
		Player::recordAccountRecoveryFailure(IP_CROSS_ACCOUNT_RECOVERY, "victim");
	}
	Player::recordAccountRecoverySuccess(IP_CROSS_ACCOUNT_RECOVERY, "attacker");
	Player::recordAccountRecoveryFailure(IP_CROSS_ACCOUNT_RECOVERY, "victim");
	CHECK(!Player::isAccountRecoveryAllowed(IP_CROSS_ACCOUNT_RECOVERY, "victim"));
	CHECK(Player::isAccountRecoveryAllowed(IP_CROSS_ACCOUNT_RECOVERY, "otheruser"));

	// Legitimate user who succeeds on 2nd attempt clears their bucket.
	Player validSession(nullptr);
	AccountManagerSecurityTestAccess::configureNewAccountSession(validSession, IP_SUCCESS_RESET);
	AccountManagerSecurityTestAccess::prepareRecoveryKeyPrompt(validSession, "LegitUser");
	{
		std::ostringstream msg;
		CHECK(!AccountManagerSecurityTestAccess::verifyRecovery(validSession, 300, validHash, "TYPO-KEY",
		                                                        IP_SUCCESS_RESET, msg));
	}
	{
		std::ostringstream msg;
		CHECK(AccountManagerSecurityTestAccess::verifyRecovery(validSession, 300, validHash, "VALID-KEY-7777",
		                                                       IP_SUCCESS_RESET, msg));
		Player::recordAccountRecoverySuccess(IP_SUCCESS_RESET, "LegitUser");
		CHECK(Player::isAccountRecoveryAllowed(IP_SUCCESS_RESET, "LegitUser"));
	}

	// Protected account (id <= 1) is always rejected even with matching hash.
	{
		Player protectedTarget(nullptr);
		AccountManagerSecurityTestAccess::configureNewAccountSession(protectedTarget, IP_IN_SESSION);
		AccountManagerSecurityTestAccess::prepareRecoveryKeyPrompt(protectedTarget, "1");
		std::ostringstream msg;
		CHECK(!AccountManagerSecurityTestAccess::verifyRecovery(protectedTarget, 1, validHash, "VALID-KEY-7777",
		                                                        IP_IN_SESSION, msg));
	}

	// Spraying 20 different accounts across reconnects trips the per-IP guard immediately at "recover".
	for (int i = 0; i < 20; ++i) {
		Player::recordAccountRecoveryFailure(IP_SPRAY_RECONNECT, "spray_acc_" + std::to_string(i));
	}
	Player sprayedSession(nullptr);
	AccountManagerSecurityTestAccess::configureNewAccountSession(sprayedSession, IP_SPRAY_RECONNECT);
	sprayedSession.manageAccount("recover");
	CHECK(!AccountManagerSecurityTestAccess::talkState(sprayedSession, 6));
}

TEST_CASE(account_manager_protocol_opcode_allowlist)
{
	// Only logout, ping, extended ping, and say are allowed for Account Manager sessions.
	CHECK(ProtocolGame::isOpcodeAllowedForAccountManager(0x14));
	CHECK(ProtocolGame::isOpcodeAllowedForAccountManager(0x1E));
	CHECK(ProtocolGame::isOpcodeAllowedForAccountManager(0x40));
	CHECK(ProtocolGame::isOpcodeAllowedForAccountManager(0x96));

	// Sensitive packets (Extended Opcode, Character Bazaar, Store, VIP, Walk, Trade, Attack, Lua handlers) are blocked.
	for (uint8_t blocked : {uint8_t{0x32}, CharacterBazaar::CLIENT_PACKET, uint8_t{0x64}, uint8_t{0x65},
	                        uint8_t{0x78}, uint8_t{0x7D}, uint8_t{0x81}, uint8_t{0xA1}, uint8_t{0xDC},
	                        uint8_t{0xDD}, uint8_t{0xF8}, uint8_t{0xF9}, uint8_t{0xFA}, uint8_t{0xFB},
	                        uint8_t{0xFC}, uint8_t{0xFF}}) {
		CHECK(!ProtocolGame::isOpcodeAllowedForAccountManager(blocked));
	}
}

TEST_CASE(account_manager_dispatches_dialogue_and_discards_other_messages)
{
	auto session = std::make_shared<Player>(nullptr);
	AccountManagerSecurityTestAccess::configureAccountSession(*session, 25, IP_IN_SESSION);
	ProtocolGameOwnedFixture protocolFixture;
	auto& protocol = *protocolFixture.owner;
	protocol.getCurrentBuffer() = std::make_shared<OutputMessage>();
	for (const auto opcode : {0x32, 0x65, 0x78, 0xA1, 0xDC, 0xFF}) {
		NetworkMessage message;
		message.addByte(opcode);
		message.addByte(0);
		AccountManagerSecurityTestAccess::dispatch(protocol, session, message);
	}
	for (const auto type : {TALKTYPE_PRIVATE, TALKTYPE_SAY}) {
		NetworkMessage message;
		message.addByte(0x96);
		message.addByte(type);
		if (type == TALKTYPE_PRIVATE) { message.addString("Fixture"); }
		message.addString("delete");
		AccountManagerSecurityTestAccess::dispatch(protocol, session, message);
		CHECK(AccountManagerSecurityTestAccess::talkState(*session, 2) == (type == TALKTYPE_SAY));
	}
}

TEST_CASE(account_manager_recovery_account_limit_follows_account_and_expires)
{
	const auto previous = ConfigManager::getInteger(ConfigManager::BRUTEFORCE_BLOCK_SECONDS);
	ConfigManager::setInteger(ConfigManager::BRUTEFORCE_BLOCK_SECONDS, 1);
	for (uint32_t ip = 0; ip < 5; ++ip) {
		Player::recordAccountRecoveryFailure(ip, "CanonicalFixture");
	}
	CHECK(!Player::isAccountRecoveryAllowed(0, "canonicalfixture"));
	CHECK(!Player::isAccountRecoveryAllowed(30, " CANONICALFIXTURE "));
	CHECK(Player::isAccountRecoveryAllowed(0, "another-account"));
	CHECK(LoginAttemptLimiter::getInstance().allowLogin(0, "CanonicalFixture"));
	CHECK(LoginAttemptLimiter::getInstance().allowLogin(IP_SPRAY_RECONNECT, ""));
	std::this_thread::sleep_for(std::chrono::milliseconds(1050));
	CHECK(Player::isAccountRecoveryAllowed(30, "CanonicalFixture"));
	ConfigManager::setInteger(ConfigManager::BRUTEFORCE_BLOCK_SECONDS, previous);
}

TEST_CASE(account_manager_replies_are_private_to_session_owner)
{
	ProtocolGameOwnedFixture ownerFixture;
	ProtocolGameOwnedFixture viewerFixture;
	ProtocolGameOwnedFixture spyFixture;
	const auto& owner = ownerFixture.owner;
	const auto& viewer = viewerFixture.owner;
	const auto& spy = spyFixture.owner;
	for (auto& protocol : {owner, viewer, spy}) {
		protocol->getCurrentBuffer() = std::make_shared<OutputMessage>();
	}
	TaskReactorTestAccess dispatcher;
	Player session(owner);
	session.client->addSpectator(viewer);
	session.client->addSpyClient(spy);
	AccountManagerSecurityTestAccess::configureNewAccountSession(session, IP_IN_SESSION);
	session.manageAccount("account");
	CHECK(owner->getCurrentBuffer()->getLength() > 0);
	CHECK(viewer->getCurrentBuffer()->getLength() == 0);
	CHECK(spy->getCurrentBuffer()->getLength() == 0);
}

TFS_TEST_MAIN()
