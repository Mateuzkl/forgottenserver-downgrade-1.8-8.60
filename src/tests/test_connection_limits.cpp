#include "../otpch.h"

#include "../configmanager.h"
#include "../connection.h"
#include "../connection_rate_limiter.h"
#include "../server.h"

#include "test_support.h"

namespace {

void resetConnectionManager()
{
	ConnectionManager::getInstance().closeAll();
	ConfigManager::setInteger(ConfigManager::MAX_CONNECTIONS, 2000);
	ConfigManager::setInteger(ConfigManager::MAX_CONNECTIONS_PER_IP, 10);
}

} // namespace

TEST_CASE(test_incomplete_connection_releases_ip_slot)
{
	resetConnectionManager();
	asio::io_context ioContext;
	auto& manager = ConnectionManager::getInstance();
	auto connection = manager.createConnection(ioContext, {});
	CHECK(connection != nullptr);

	constexpr uint32_t clientIp = 0x01020304;
	uint32_t currentCount = 0;
	CHECK(manager.trackIPConnection(connection, clientIp, currentCount));
	CHECK(currentCount == 1);
	CHECK(manager.getConnectionCountForIP(clientIp) == 1);

	// No packet was parsed. Closing must still release the slot assigned at accept time.
	connection->close(Connection::FORCE_CLOSE);
	CHECK(manager.getConnectionCountForIP(clientIp) == 0);
	CHECK(manager.getConnectionCount() == 0);
	resetConnectionManager();
}

TEST_CASE(test_ip_limit_check_and_tracking_are_atomic)
{
	resetConnectionManager();
	ConfigManager::setInteger(ConfigManager::MAX_CONNECTIONS_PER_IP, 1);
	asio::io_context ioContext;
	auto& manager = ConnectionManager::getInstance();
	auto first = manager.createConnection(ioContext, {});
	auto second = manager.createConnection(ioContext, {});
	CHECK(first != nullptr);
	CHECK(second != nullptr);

	constexpr uint32_t clientIp = 0x05060708;
	uint32_t currentCount = 0;
	CHECK(manager.trackIPConnection(first, clientIp, currentCount));
	CHECK(currentCount == 1);
	CHECK(!manager.trackIPConnection(second, clientIp, currentCount));
	CHECK(currentCount == 1);

	second->close(Connection::FORCE_CLOSE);
	CHECK(manager.getConnectionCountForIP(clientIp) == 1);
	first->close(Connection::FORCE_CLOSE);
	CHECK(manager.getConnectionCountForIP(clientIp) == 0);
	resetConnectionManager();
}

TEST_CASE(test_zero_global_connection_limit_means_unlimited)
{
	resetConnectionManager();
	ConfigManager::setInteger(ConfigManager::MAX_CONNECTIONS, 0);
	asio::io_context ioContext;
	auto& manager = ConnectionManager::getInstance();
	auto first = manager.createConnection(ioContext, {});
	auto second = manager.createConnection(ioContext, {});
	CHECK(first != nullptr);
	CHECK(second != nullptr);

	first->close(Connection::FORCE_CLOSE);
	second->close(Connection::FORCE_CLOSE);
	resetConnectionManager();
}

TEST_CASE(test_accept_error_releases_pending_connection)
{
	resetConnectionManager();
	asio::io_context ioContext;
	tfs::net::ConnectionRateLimiter limiter;
	auto servicePort = std::make_shared<ServicePort>(ioContext, limiter);
	auto& manager = ConnectionManager::getInstance();
	auto connection = manager.createConnection(ioContext, servicePort);
	CHECK(connection != nullptr);
	CHECK(manager.getConnectionCount() == 1);

	servicePort->onAccept(connection, asio::error::connection_aborted);
	CHECK(manager.getConnectionCount() == 0);
	resetConnectionManager();
}

TEST_CASE(test_rate_limit_deadline_is_not_extended_by_retries)
{
	tfs::net::ConnectionRateLimiter limiter;
	constexpr uint32_t clientIp = 0x090A0B0C;

	CHECK(limiter.check(clientIp, 1'000, 2, 500).allowed);
	CHECK(limiter.check(clientIp, 1'100, 2, 500).allowed);
	const auto blocked = limiter.check(clientIp, 1'200, 2, 500);
	CHECK(!blocked.allowed);
	CHECK(blocked.blockStarted);
	CHECK(blocked.blockDuration == 3'000);

	CHECK(!limiter.check(clientIp, 1'300, 2, 500).allowed);
	CHECK(!limiter.check(clientIp, 4'199, 2, 500).allowed);
	CHECK(limiter.check(clientIp, 4'200, 2, 500).allowed);
}

TEST_CASE(test_rate_limit_escalates_only_when_block_starts)
{
	tfs::net::ConnectionRateLimiter limiter;
	constexpr uint32_t clientIp = 0x0D0E0F10;

	CHECK(limiter.check(clientIp, 1'000, 1, 100).allowed);
	CHECK(limiter.check(clientIp, 1'200, 1, 100).allowed);
	CHECK(limiter.check(clientIp, 1'300, 1, 100).allowed);
	CHECK(limiter.check(clientIp, 1'500, 1, 100).allowed);
	CHECK(limiter.check(clientIp, 1'600, 1, 100).allowed);

	const auto blocked = limiter.check(clientIp, 1'650, 1, 100);
	CHECK(!blocked.allowed);
	CHECK(blocked.blockStarted);
	CHECK(blocked.totalBlocks == 1);
	CHECK(blocked.blockDuration == 3'000);
}

TFS_TEST_MAIN()
