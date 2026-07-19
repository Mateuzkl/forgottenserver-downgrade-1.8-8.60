// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "server.h"

#include "configmanager.h"
#include "performance_metrics.h"
#include "logger.h"
#include <fmt/format.h>

#include "tools.h"

namespace {
bool shouldLog(std::atomic<uint64_t>& nextLog, uint64_t currentTime, uint64_t interval)
{
	auto expected = nextLog.load(std::memory_order_relaxed);
	while (currentTime >= expected) {
		if (nextLog.compare_exchange_weak(expected, currentTime + interval, std::memory_order_relaxed)) {
			return true;
		}
	}
	return false;
}

std::atomic<uint64_t> nextConnectionLimitLog{0};
std::atomic<uint64_t> nextIpLimitLog{0};
std::atomic<uint64_t> nextRateLimitLog{0};
std::atomic<uint64_t> nextAcceptErrorLog{0};
} // namespace

ServiceManager::~ServiceManager() { stop(); }

bool ServiceManager::hasOpenServices() const
{
	return !acceptors.empty() && std::all_of(acceptors.begin(), acceptors.end(), [](const auto& entry) {
		return entry.second && entry.second->isOpen();
	});
}

void ServiceManager::die()
{
	// Release protocols before closing connections — the dispatcher is already
	// shut down so closeLocked's dispatched release() would be lost.
	ConnectionManager::getInstance().releaseAllProtocols();
	ConnectionManager::getInstance().closeAll();
	workGuard.reset();
	LOG_NETWORK("Stopping network io_context.");
	io_context.stop();
}

void ServiceManager::runIoContext() noexcept
{
	while (!io_context.stopped()) {
		try {
			io_context.run();
			return;
		} catch (const std::exception& e) {
			LOG_ERROR(fmt::format("[Network] Exception in asynchronous handler: {}. Continuing io_context.", e.what()));
		} catch (...) {
			LOG_ERROR("[Network] Unknown exception in asynchronous handler. Continuing io_context.");
		}
	}
}

void ServiceManager::run(std::function<void()> onStarted)
{
	bool expected = false;
	if (!running.compare_exchange_strong(expected, true)) {
		return;
	}

	// Spawn additional io_context threads for parallel I/O (XTEA encrypt + async_write).
	// The main thread also runs io_context.run(), so total = networkThreads.
	int networkThreads = std::max(1, static_cast<int>(getInteger(ConfigManager::NETWORK_THREADS)));
	int extraThreads = networkThreads - 1;

	if (extraThreads > 0) {
		ioThreads.reserve(extraThreads);
		for (int i = 0; i < extraThreads; ++i) {
			ioThreads.emplace_back([this]() { runIoContext(); });
		}
	}
	if (onStarted) {
		onStarted();
	}

	runIoContext();
	running.store(false, std::memory_order_release);

	// After io_context.stop() is called in die(), all io_context.run() calls return.
	// Now we are back on the main thread (startServer → serviceManager.run()),
	// so it is safe to join the I/O threads without risk of self-join deadlock.
	ioThreads.clear();
}

void ServiceManager::stop()
{
	if (!running.exchange(false, std::memory_order_acq_rel)) {
		return;
	}

	for (auto& servicePortIt : acceptors) {
		try {
			servicePortIt.second->onStopServer();
		} catch (std::system_error& e) {
			LOG_NETWORK(fmt::format("Error - ServiceManager::stop: {}", e.what()));
		}
	}

	acceptors.clear();

	death_timer.expires_after(std::chrono::seconds(3));
	death_timer.async_wait([this](const asio::error_code&) { die(); });
}

ServicePort::~ServicePort() { close(); }

bool ServicePort::is_single_socket() const { return !services.empty() && services.front()->is_single_socket(); }

std::string ServicePort::get_protocol_names() const
{
	if (services.empty()) {
		return std::string();
	}

	std::string str = services.front()->get_protocol_name();
	for (size_t i = 1; i < services.size(); ++i) {
		str.push_back(',');
		str.push_back(' ');
		str.append(services[i]->get_protocol_name());
	}
	return str;
}

void ServicePort::accept()
{
	if (stopped.load() || !acceptor || !acceptor->is_open()) {
		return;
	}

	Connection_ptr connection;
	try {
		connection = ConnectionManager::getInstance().createConnection(io_context, shared_from_this());
		if (!connection) {
			const uint64_t currentTime = OTSYS_TIME();
			if (shouldLog(nextConnectionLimitLog, currentTime, 60'000)) {
				LOG_NETWORK(fmt::format("Connection limit reached: active={}, configured={}. Retrying accept.",
				                                ConnectionManager::getInstance().getConnectionCount(),
				                                getInteger(ConfigManager::MAX_CONNECTIONS)));
			}
			scheduleAccept(std::chrono::milliseconds(100));
			return;
		}

		acceptor->async_accept(
			connection->getSocket(),
			asio::bind_executor(strand, [thisPtr = shared_from_this(), connection](const asio::error_code& error) {
				thisPtr->onAccept(connection, error);
			}));
		g_performanceMetrics.recordNetworkAcceptStarted();
	} catch (const std::exception& e) {
		if (connection) {
			connection->close(Connection::FORCE_CLOSE);
		}
		const uint64_t currentTime = OTSYS_TIME();
		if (shouldLog(nextAcceptErrorLog, currentTime, 10'000)) {
			LOG_NETWORK(fmt::format("Failed to start async_accept on port {}: {}", serverPort, e.what()));
		}
		scheduleAccept(std::chrono::milliseconds(250));
	}
}

void ServicePort::onAccept(Connection_ptr connection, const asio::error_code& error)
{
	if (error) {
		connection->close(Connection::FORCE_CLOSE);
		if (error == asio::error::operation_aborted) {
			if (!stopped.load() && !pendingStart.load() && acceptor && acceptor->is_open()) {
				scheduleAccept(std::chrono::milliseconds(0));
			}
			return;
		}

		g_performanceMetrics.recordNetworkAccept(false);
		const uint64_t currentTime = OTSYS_TIME();
		if (shouldLog(nextAcceptErrorLog, currentTime, 10'000)) {
			LOG_NETWORK(fmt::format("async_accept failed on port {}: code={} message='{}' acceptor_open={}",
			                                serverPort, error.value(), error.message(),
			                                acceptor && acceptor->is_open()));
		}

		if (stopped.load()) {
			return;
		}

		const bool needsReopen = error == asio::error::bad_descriptor || error == asio::error::not_socket ||
		                         error == asio::error::invalid_argument || !acceptor || !acceptor->is_open();
		if (needsReopen) {
			scheduleOpen(std::chrono::seconds(1));
		} else {
			scheduleAccept(std::chrono::milliseconds(250));
		}
		return;
	}

	g_performanceMetrics.recordNetworkAccept(true);
	try {
		if (services.empty()) {
			LOG_ERROR(fmt::format("[Network] Accepted a connection on port {} without a registered service.", serverPort));
			connection->close(Connection::FORCE_CLOSE);
			accept();
			return;
		}

		const uint32_t remoteIp = connection->getIP();
		if (remoteIp == 0) {
			connection->close(Connection::FORCE_CLOSE);
			accept();
			return;
		}

		const uint64_t currentTime = OTSYS_TIME();
		const auto rateResult = rateLimiter.check(
			remoteIp, currentTime,
			static_cast<uint32_t>(std::max<int64_t>(0, getInteger(ConfigManager::CONNECTION_RATE_LIMIT_ALLOWED))),
			static_cast<uint64_t>(std::max<int64_t>(0, getInteger(ConfigManager::CONNECTION_RATE_LIMIT_MS))));
		if (!rateResult.allowed) {
			g_performanceMetrics.recordNetworkRateLimitRejection();
			if (rateResult.blockStarted && shouldLog(nextRateLimitLog, currentTime, 10'000)) {
				LOG_NETWORK(fmt::format("Rate limit started for {}: duration_ms={} escalation={}",
				                                convertIPToString(remoteIp), rateResult.blockDuration,
				                                rateResult.totalBlocks));
			}
			connection->close(Connection::FORCE_CLOSE);
			accept();
			return;
		}

		uint32_t ipConnectionCount = 0;
		if (!ConnectionManager::getInstance().trackIPConnection(connection, remoteIp, ipConnectionCount)) {
			g_performanceMetrics.recordNetworkIpLimitRejection();
			if (shouldLog(nextIpLimitLog, currentTime, 60'000)) {
				LOG_NETWORK(fmt::format("Per-IP connection limit reached for {}: active={}, configured={}",
				                                convertIPToString(remoteIp), ipConnectionCount,
				                                getInteger(ConfigManager::MAX_CONNECTIONS_PER_IP)));
			}
			connection->close(Connection::FORCE_CLOSE);
			accept();
			return;
		}

		Service_ptr service = services.front();
		if (service->is_single_socket()) {
			connection->accept(service->make_protocol(connection));
		} else {
			connection->accept();
		}
	} catch (const std::exception& e) {
		LOG_NETWORK(fmt::format("Exception while handling accepted connection on port {}: {}", serverPort, e.what()));
		connection->close(Connection::FORCE_CLOSE);
	} catch (...) {
		LOG_NETWORK(fmt::format("Unknown exception while handling accepted connection on port {}.", serverPort));
		connection->close(Connection::FORCE_CLOSE);
	}

	accept();
}

Protocol_ptr ServicePort::make_protocol(bool checksummed, NetworkMessage& msg, const Connection_ptr& connection) const
{
	uint8_t protocolID = msg.getByte();
	for (auto& service : services) {
		if (protocolID != service->get_protocol_identifier()) {
			continue;
		}

		if ((checksummed && service->is_checksummed()) || !service->is_checksummed()) {
			return service->make_protocol(connection);
		}
	}
	return nullptr;
}

void ServicePort::onStopServer()
{
	asio::dispatch(strand, [thisPtr = shared_from_this()] { thisPtr->stopOnStrand(); });
}

void ServicePort::stopOnStrand()
{
	stopped.store(true);
	close();
}

void ServicePort::scheduleAccept(std::chrono::milliseconds delay)
{
	if (stopped.load()) {
		return;
	}

	retryTimer.expires_after(delay);
	retryTimer.async_wait([service = std::weak_ptr<ServicePort>(shared_from_this())](const asio::error_code& error) {
		if (error == asio::error::operation_aborted) {
			return;
		}
		if (auto servicePort = service.lock(); servicePort && !servicePort->stopped.load()) {
			servicePort->accept();
		}
	});
}

void ServicePort::scheduleOpen(std::chrono::milliseconds delay)
{
	if (stopped.load() || pendingStart.exchange(true)) {
		return;
	}

	close();
	retryTimer.expires_after(delay);
	retryTimer.async_wait(
		[service = std::weak_ptr<ServicePort>(shared_from_this()), port = serverPort](const asio::error_code& error) {
			if (error == asio::error::operation_aborted) {
				return;
			}
			if (auto servicePort = service.lock(); servicePort && !servicePort->stopped.load()) {
				servicePort->open(port, true);
			}
		});
}

bool ServicePort::open(uint16_t port, bool retryOnFailure)
{
	if (stopped.load()) {
		return false;
	}

	close();

	serverPort = port;
	pendingStart.store(false);

	try {
		asio::ip::tcp::endpoint endpoint;
		if (getBoolean(ConfigManager::BIND_ONLY_GLOBAL_ADDRESS)) {
			endpoint = asio::ip::tcp::endpoint(
				asio::ip::address(asio::ip::make_address_v4(std::string{getString(ConfigManager::IP)})), serverPort);
		} else {
			endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), serverPort);
		}

		acceptor = std::make_unique<asio::ip::tcp::acceptor>(strand, endpoint, true);
		acceptor->set_option(asio::ip::tcp::no_delay(true));
		const auto boundEndpoint = acceptor->local_endpoint();
		LOG_NETWORK(fmt::format("Acceptor listening on {}:{} (configured_ip={}, bind_only_global={}).",
		                                boundEndpoint.address().to_string(), boundEndpoint.port(),
		                                getString(ConfigManager::IP),
		                                getBoolean(ConfigManager::BIND_ONLY_GLOBAL_ADDRESS)));

		accept();
		return true;
	} catch (const std::system_error& e) {
		LOG_NETWORK(fmt::format("Error - ServicePort::open: {}", e.what()));

		if (e.code() == asio::error::address_in_use) {
			LOG_ERROR("ATTENTION: network port is already in use. There is likely another TFS process running.");
		}

		if (retryOnFailure) {
			scheduleOpen(std::chrono::seconds(15));
		}
	} catch (const std::exception& e) {
		LOG_NETWORK(fmt::format("Error - ServicePort::open: {}", e.what()));
		if (retryOnFailure) {
			scheduleOpen(std::chrono::seconds(15));
		}
	}
	return false;
}

void ServicePort::close()
{
	asio::error_code timerError;
	retryTimer.cancel(timerError);
	if (acceptor && acceptor->is_open()) {
		asio::error_code error;
		acceptor->close(error);
	}
}

bool ServicePort::add_service(const Service_ptr& new_svc)
{
	if (std::any_of(services.begin(), services.end(), [](const Service_ptr& svc) { return svc->is_single_socket(); })) {
		return false;
	}

	services.push_back(new_svc);
	return true;
}
