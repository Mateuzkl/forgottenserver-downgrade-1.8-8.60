// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "server.h"

#include "ban.h"
#include "configmanager.h"
#include "outputmessage.h"
#include "scheduler.h"
#include "logger.h"
#include <fmt/format.h>

#include "tools.h"

namespace {
struct ConnectBlock
{
	uint64_t lastAttempt;
	uint64_t blockTime = 0;
	uint32_t count = 1;
	uint32_t totalBlocks = 0;  // escalation counter
};

// Cleanup stale entries periodically to prevent memory growth
void cleanupConnectMap(std::unordered_map<uint32_t, ConnectBlock>& ipConnectMap, uint64_t currentTime)
{
	static uint64_t lastCleanup = 0;
	if (currentTime - lastCleanup < 60000) { // every 60 seconds
		return;
	}
	lastCleanup = currentTime;

	std::erase_if(ipConnectMap, [currentTime](const auto& entry) {
		return currentTime - entry.second.lastAttempt > 300000;
	});
}

bool acceptConnection(const uint32_t clientIP)
{
	static std::recursive_mutex mu;
	std::scoped_lock lock{mu};

	uint64_t currentTime = OTSYS_TIME();

	static std::unordered_map<uint32_t, ConnectBlock> ipConnectMap;

	// Periodic cleanup of stale entries
	cleanupConnectMap(ipConnectMap, currentTime);

	auto it = ipConnectMap.find(clientIP);
	if (it == ipConnectMap.end()) {
		ipConnectMap.emplace(clientIP, ConnectBlock{.lastAttempt = currentTime});
		return true;
	}

	ConnectBlock& connectBlock = it->second;
	if (connectBlock.blockTime > currentTime) {
		// Escalate block time for repeat offenders (DDoS protection)
		connectBlock.blockTime += 500;
		return false;
	}

	int64_t timeDiff = currentTime - connectBlock.lastAttempt;
	connectBlock.lastAttempt = currentTime;
	if (timeDiff <= 5000) {
		if (++connectBlock.count > static_cast<uint32_t>(getInteger(ConfigManager::CONNECTION_RATE_LIMIT_ALLOWED))) {
			connectBlock.count = 0;
			connectBlock.totalBlocks++;

			// Escalating block: repeat offenders get longer bans
			uint64_t blockDuration = 3000;
			if (connectBlock.totalBlocks >= 10) {
				blockDuration = 300000; // 5 minutes
			} else if (connectBlock.totalBlocks >= 5) {
				blockDuration = 60000;  // 1 minute
			} else if (connectBlock.totalBlocks >= 3) {
				blockDuration = 15000;  // 15 seconds
			}

			if (timeDiff <= getInteger(ConfigManager::CONNECTION_RATE_LIMIT_MS)) {
				connectBlock.blockTime = currentTime + blockDuration;
				return false;
			}
		}
	} else {
		connectBlock.count = 1;
		// Slowly decay totalBlocks when the IP behaves
		if (timeDiff > 60000 && connectBlock.totalBlocks > 0) {
			connectBlock.totalBlocks--;
		}
	}
	return true;
}
}

ServiceManager::~ServiceManager() { stop(); }

void ServiceManager::die()
{
	// Release protocols before closing connections — the dispatcher is already
	// shut down so closeLocked's dispatched release() would be lost.
	ConnectionManager::getInstance().releaseAllProtocols();
	ConnectionManager::getInstance().closeAll();
	io_context.stop();
}

void ServiceManager::run()
{
	assert(!running);
	running = true;

	// Spawn additional io_context threads for parallel I/O (XTEA encrypt + async_write).
	// The main thread also runs io_context.run(), so total = networkThreads.
	int networkThreads = std::max(1, static_cast<int>(getInteger(ConfigManager::NETWORK_THREADS)));
	int extraThreads = networkThreads - 1;

	if (extraThreads > 0) {
		ioThreads.reserve(extraThreads);
		for (int i = 0; i < extraThreads; ++i) {
			ioThreads.emplace_back([this]() { io_context.run(); });
		}
	}

	io_context.run();

	// After io_context.stop() is called in die(), all io_context.run() calls return.
	// Now we are back on the main thread (startServer → serviceManager.run()),
	// so it is safe to join the I/O threads without risk of self-join deadlock.
	ioThreads.clear();
}

void ServiceManager::stop()
{
	if (!running) {
		return;
	}

	running = false;

	for (auto& servicePortIt : acceptors) {
		try {
			asio::post(io_context, [servicePort = servicePortIt.second]() { servicePort->onStopServer(); });
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

	auto connection = ConnectionManager::getInstance().createConnection(io_context, shared_from_this());
	if (!connection) {
		// Max connections reached — schedule retry after a brief delay
		auto timer = std::make_shared<asio::steady_timer>(io_context);
		timer->expires_after(std::chrono::milliseconds(100));
		timer->async_wait([timer, thisPtr = shared_from_this()](const asio::error_code&) {
			thisPtr->accept();
		});
		return;
	}

	acceptor->async_accept(connection->getSocket(),
	                       [=, thisPtr = shared_from_this()](const asio::error_code& error) {
		                       thisPtr->onAccept(connection, error);
	                       });
}

void ServicePort::onAccept(Connection_ptr connection, const asio::error_code& error)
{
	if (!error) {
		if (services.empty()) {
			return;
		}

		const auto remote_ip = connection->getIP();
		if (remote_ip != 0 && acceptConnection(remote_ip)) {
			// Check per-IP connection limit
			if (ConnectionManager::getInstance().isMaxConnectionsPerIPReached(remote_ip)) {
				connection->close(Connection::FORCE_CLOSE);
				accept();
				return;
			}

			// Track this IP's connection count
			ConnectionManager::getInstance().trackIPConnection(remote_ip);

			Service_ptr service = services.front();
			if (service->is_single_socket()) {
				connection->accept(service->make_protocol(connection));
			} else {
				connection->accept();
			}
		} else {
			connection->close(Connection::FORCE_CLOSE);
		}

		accept();
	} else if (error != asio::error::operation_aborted) {
		if (!stopped.load() && !pendingStart.exchange(true)) {
			close();
			g_scheduler.addEvent(createSchedulerTask(
			    15000, ([serverPort = this->serverPort, service = std::weak_ptr<ServicePort>(shared_from_this())]() {
				    openAcceptor(service, serverPort);
			    })));
		}
	}
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
	stopped.store(true);
	close();
}

void ServicePort::openAcceptor(std::weak_ptr<ServicePort> weak_service, uint16_t port)
{
	if (auto service = weak_service.lock(); service && !service->stopped.load()) {
		service->open(port);
	}
}

void ServicePort::open(uint16_t port)
{
	if (stopped.load()) {
		return;
	}

	close();

	serverPort = port;
	pendingStart.store(false);

	try {
		if (getBoolean(ConfigManager::BIND_ONLY_GLOBAL_ADDRESS)) {
			acceptor = std::make_unique<asio::ip::tcp::acceptor>(
			    io_context,
			    asio::ip::tcp::endpoint(asio::ip::address(asio::ip::make_address_v4(
			                                       std::string{getString(ConfigManager::IP)})),
			                                   serverPort));
		} else {
			acceptor = std::make_unique<asio::ip::tcp::acceptor>(
			    io_context, asio::ip::tcp::endpoint(
			                    asio::ip::address(asio::ip::address_v4(INADDR_ANY)), serverPort));
		}

		acceptor->set_option(asio::ip::tcp::no_delay(true));

		accept();
	} catch (std::system_error& e) {
		LOG_NETWORK(fmt::format("Error - ServicePort::open: {}", e.what()));

		if (e.code() == asio::error::address_in_use) {
			LOG_ERROR("\x1b[31mATTENTION: network port is already in use. There is likely another TFS process running.\x1b[0m");
			LOG_ERROR("\x1b[31mTo check, run: ps aux | grep ./tfs\x1b[0m");
			LOG_ERROR("\x1b[31mTry this first: kill PID\x1b[0m");
			LOG_ERROR("\x1b[31mIf it does not stop, then use: kill -9 PID\x1b[0m");
			LOG_ERROR("\x1b[31mThen start the server again.\x1b[0m");
		}

		if (!stopped.load()) {
			pendingStart.store(true);
			g_scheduler.addEvent(createSchedulerTask(
			    15000,
			    ([port, service = std::weak_ptr<ServicePort>(shared_from_this())]() { openAcceptor(service, port); })));
		}
	}
}

void ServicePort::close()
{
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
