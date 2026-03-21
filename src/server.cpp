// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "server.h"

#include "ban.h"
#include "configmanager.h"
#include "logger.h"
#include "outputmessage.h"
#include "scheduler.h"
#include "tools.h"

#include <fmt/format.h>

namespace {
struct ConnectBlock
{
	uint64_t lastAttempt;
	uint64_t blockTime = 0;
	uint32_t count = 1;
	uint32_t totalBlocks = 0; // escalation counter
};

// Cleanup stale entries periodically to prevent memory growth
void cleanupConnectMap(std::map<uint32_t, ConnectBlock>& ipConnectMap, uint64_t currentTime)
{
	static uint64_t lastCleanup = 0;
	if (currentTime - lastCleanup < 60000) { // every 60 seconds
		return;
	}
	lastCleanup = currentTime;

	for (auto it = ipConnectMap.begin(); it != ipConnectMap.end();) {
		// Remove entries idle for more than 5 minutes
		if (currentTime - it->second.lastAttempt > 300000) {
			it = ipConnectMap.erase(it);
		} else {
			++it;
		}
	}
}

bool acceptConnection(const uint32_t clientIP)
{
	static std::recursive_mutex mu;
	std::lock_guard lock{mu};

	uint64_t currentTime = OTSYS_TIME();

	static std::map<uint32_t, ConnectBlock> ipConnectMap;

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
		if (++connectBlock.count > 5) {
			connectBlock.count = 0;
			connectBlock.totalBlocks++;

			// Escalating block: repeat offenders get longer bans
			uint64_t blockDuration = 3000;
			if (connectBlock.totalBlocks >= 10) {
				blockDuration = 300000; // 5 minutes
			} else if (connectBlock.totalBlocks >= 5) {
				blockDuration = 60000; // 1 minute
			} else if (connectBlock.totalBlocks >= 3) {
				blockDuration = 15000; // 15 seconds
			}

			if (timeDiff <= 500) {
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
} // namespace

ServiceManager::~ServiceManager() { stop(); }

void ServiceManager::die()
{
	// Close all connections before stopping the io_context.
	// Without this, Connection destructors run after io_context is destroyed,
	ConnectionManager::getInstance().closeAll();
	io_context.stop();
}

void ServiceManager::run()
{
	assert(!running);
	running = true;
	io_context.run();
}

void ServiceManager::stop()
{
	if (!running) {
		return;
	}

	running = false;

	for (auto& servicePortIt : acceptors) {
		try {
			boost::asio::post(io_context, [servicePort = servicePortIt.second]() { servicePort->onStopServer(); });
		} catch (boost::system::system_error& e) {
			LOG_ERROR(fmt::format("[ServiceManager::stop] Network Error: {}", e.what()));
		}
	}

	acceptors.clear();

	death_timer.expires_after(std::chrono::seconds(3));
	death_timer.async_wait([this](const boost::system::error_code&) { die(); });
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
	if (!acceptor) {
		return;
	}

	auto connection = ConnectionManager::getInstance().createConnection(io_context, shared_from_this());
	if (!connection) {
		// Max connections reached — schedule retry after a brief delay
		auto timer = std::make_shared<boost::asio::steady_timer>(io_context);
		timer->expires_after(std::chrono::milliseconds(100));
		timer->async_wait(
		    [timer, thisPtr = shared_from_this()](const boost::system::error_code&) { thisPtr->accept(); });
		return;
	}

	acceptor->async_accept(connection->getSocket(),
	                       [=, thisPtr = shared_from_this()](const boost::system::error_code& error) {
		                       thisPtr->onAccept(connection, error);
	                       });
}

void ServicePort::onAccept(Connection_ptr connection, const boost::system::error_code& error)
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
	} else if (error != boost::asio::error::operation_aborted) {
		if (!pendingStart) {
			close();
			pendingStart = true;
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

void ServicePort::onStopServer() { close(); }

void ServicePort::openAcceptor(std::weak_ptr<ServicePort> weak_service, uint16_t port)
{
	if (auto service = weak_service.lock()) {
		service->open(port);
	}
}

void ServicePort::open(uint16_t port)
{
	close();

	serverPort = port;
	pendingStart = false;

	try {
		if (getBoolean(ConfigManager::BIND_ONLY_GLOBAL_ADDRESS)) {
			acceptor.reset(new boost::asio::ip::tcp::acceptor(
			    io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::address(boost::asio::ip::make_address_v4(
			                                                   std::string{getString(ConfigManager::IP)})),
			                                               serverPort)));
		} else {
			acceptor.reset(new boost::asio::ip::tcp::acceptor(
			    io_context, boost::asio::ip::tcp::endpoint(
			                    boost::asio::ip::address(boost::asio::ip::address_v4(INADDR_ANY)), serverPort)));
		}

		acceptor->set_option(boost::asio::ip::tcp::no_delay(true));

		accept();
	} catch (boost::system::system_error& e) {
		LOG_ERROR(fmt::format("[ServicePort::open] Error: {}", e.what()));

		pendingStart = true;
		g_scheduler.addEvent(createSchedulerTask(
		    15000,
		    ([port, service = std::weak_ptr<ServicePort>(shared_from_this())]() { openAcceptor(service, port); })));
	}
}

void ServicePort::close()
{
	if (acceptor && acceptor->is_open()) {
		boost::system::error_code error;
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
