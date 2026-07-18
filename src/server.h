// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_SERVER_H
#define FS_SERVER_H

#include "connection.h"
#include "connection_rate_limiter.h"
#include "signals.h"
#include "logger.h"
#include <fmt/format.h>

#include <atomic>
#include <chrono>

class Protocol;

class ServiceBase
{
public:
	virtual ~ServiceBase() = default;

	virtual bool is_single_socket() const = 0;
	virtual bool is_checksummed() const = 0;
	virtual uint8_t get_protocol_identifier() const = 0;
	virtual const char* get_protocol_name() const = 0;

	virtual Protocol_ptr make_protocol(const Connection_ptr& c) const = 0;
};

template <typename ProtocolType>
class Service final : public ServiceBase
{
public:
	bool is_single_socket() const override { return ProtocolType::server_sends_first; }
	bool is_checksummed() const override { return ProtocolType::use_checksum; }
	uint8_t get_protocol_identifier() const override { return ProtocolType::protocol_identifier; }
	const char* get_protocol_name() const override { return ProtocolType::protocol_name(); }

	Protocol_ptr make_protocol(const Connection_ptr& c) const override { return std::make_shared<ProtocolType>(c); }
};

class ServicePort : public std::enable_shared_from_this<ServicePort>
{
public:
	ServicePort(asio::io_context& io_context, tfs::net::ConnectionRateLimiter& rateLimiter) :
	    io_context(io_context), strand(asio::make_strand(io_context)), retryTimer(strand), rateLimiter(rateLimiter)
	{}
	~ServicePort();

	// non-copyable
	ServicePort(const ServicePort&) = delete;
	ServicePort& operator=(const ServicePort&) = delete;

	void open(uint16_t port);
	void close();
	bool is_single_socket() const;
	std::string get_protocol_names() const;

	bool add_service(const Service_ptr& new_svc);
	Protocol_ptr make_protocol(bool checksummed, NetworkMessage& msg, const Connection_ptr& connection) const;

	void onStopServer();
	void onAccept(Connection_ptr connection, const asio::error_code& error);

private:
	void accept();
	void scheduleAccept(std::chrono::milliseconds delay);
	void scheduleOpen(std::chrono::milliseconds delay);
	void stopOnStrand();

	asio::io_context& io_context;
	asio::strand<asio::io_context::executor_type> strand;
	asio::steady_timer retryTimer;
	tfs::net::ConnectionRateLimiter& rateLimiter;
	std::unique_ptr<asio::ip::tcp::acceptor> acceptor;
	std::vector<Service_ptr> services;

	uint16_t serverPort = 0;
	std::atomic_bool pendingStart = false;
	std::atomic_bool stopped = false;
};

class ServiceManager
{
public:
	ServiceManager() = default;
	~ServiceManager();

	// non-copyable
	ServiceManager(const ServiceManager&) = delete;
	ServiceManager& operator=(const ServiceManager&) = delete;

	void run();
	void stop();

	template <typename ProtocolType>
	bool add(uint16_t port);

	bool is_running() const { return acceptors.empty() == false; }

private:
	void die();
	void runIoContext() noexcept;

	asio::io_context io_context;
	tfs::net::ConnectionRateLimiter connectionRateLimiter;
	std::unordered_map<uint16_t, ServicePort_ptr> acceptors;
	Signals signals{io_context};
	asio::steady_timer death_timer{io_context};
	asio::executor_work_guard<asio::io_context::executor_type> workGuard{asio::make_work_guard(io_context)};
	std::vector<std::jthread> ioThreads;
	std::atomic_bool running{false};
};

template <typename ProtocolType>
bool ServiceManager::add(uint16_t port)
{
	if (port == 0) {
		LOG_ERROR(fmt::format("ERROR: No port provided for service {}. Service disabled.", ProtocolType::protocol_name()));
		return false;
	}

	ServicePort_ptr service_port;

	auto foundServicePort = acceptors.find(port);

	if (foundServicePort == acceptors.end()) {
		service_port = std::make_shared<ServicePort>(io_context, connectionRateLimiter);
		service_port->open(port);
		acceptors[port] = service_port;
	} else {
		service_port = foundServicePort->second;

		if (service_port->is_single_socket() || ProtocolType::server_sends_first) {
			LOG_ERROR(fmt::format("ERROR: {} and {} cannot use the same port {}.", ProtocolType::protocol_name(), service_port->get_protocol_names(), port));
			return false;
		}
	}

	return service_port->add_service(std::make_shared<Service<ProtocolType>>());
}

#endif
