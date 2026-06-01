// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_CONNECTION_H
#define FS_CONNECTION_H

#include "networkmessage.h"

inline constexpr int32_t CONNECTION_WRITE_TIMEOUT = 30;
inline constexpr int32_t CONNECTION_READ_TIMEOUT = 30;

class Protocol;
using Protocol_ptr = std::shared_ptr<Protocol>;
class OutputMessage;
using OutputMessage_ptr = std::shared_ptr<OutputMessage>;
class Connection;
using Connection_ptr = std::shared_ptr<Connection>;
using ConnectionWeak_ptr = std::weak_ptr<Connection>;
class ServiceBase;
using Service_ptr = std::shared_ptr<ServiceBase>;
class ServicePort;
using ServicePort_ptr = std::shared_ptr<ServicePort>;
using ConstServicePort_ptr = std::shared_ptr<const ServicePort>;

class ConnectionManager
{
public:
	static ConnectionManager& getInstance()
	{
		static ConnectionManager instance;
		return instance;
	}

	Connection_ptr createConnection(asio::io_context& io_context, ConstServicePort_ptr servicePort);
	void releaseConnection(const Connection_ptr& connection);
	void closeAll();
	void releaseAllProtocols();

	// Connection limit checks
	bool isMaxConnectionsReached();
	bool isMaxConnectionsPerIPReached(uint32_t ip);
	size_t getConnectionCount() const;
	void trackIPConnection(uint32_t ip);

private:
	ConnectionManager() = default;

	std::unordered_set<Connection_ptr> connections;
	std::unordered_map<uint32_t, uint32_t> ipConnectionCount;
	mutable std::mutex connectionManagerLock;
};

class Connection : public std::enable_shared_from_this<Connection>
{
public:
	// non-copyable
	Connection(const Connection&) = delete;
	Connection& operator=(const Connection&) = delete;

	enum
	{
		FORCE_CLOSE = true
	};

	Connection(asio::io_context& io_context, ConstServicePort_ptr service_port) :
	    strand(asio::make_strand(io_context)),
	    readTimer(strand),
	    writeTimer(strand),
	    service_port(std::move(service_port)),
	    socket(strand),
	    timeConnected(time(nullptr))
	{}
	~Connection();

	friend class ConnectionManager;

	void close(bool force = false);
	// Used by protocols that require server to send first
	void accept(Protocol_ptr protocol);
	void accept();

	void send(const OutputMessage_ptr& msg);

	uint32_t getIP();
	uint32_t getLastIp() const { return lastIp; }

private:
	void parseHeader(const asio::error_code& error);
	void parsePacket(const asio::error_code& error);

	void onWriteOperation(const asio::error_code& error);

	static void handleTimeout(ConnectionWeak_ptr connectionWeak, const asio::error_code& error);

	void closeSocket();
	void closeLocked(bool force);
	void internalSend(const OutputMessage_ptr& msg);
	uint32_t getIPLocked();

	asio::ip::tcp::socket& getSocket() { return socket; }
	friend class ServicePort;

	NetworkMessage msg;

	asio::strand<asio::io_context::executor_type> strand;
	asio::steady_timer readTimer;
	asio::steady_timer writeTimer;

	std::recursive_mutex connectionLock;

	std::deque<OutputMessage_ptr> messageQueue;

	ConstServicePort_ptr service_port;
	Protocol_ptr protocol;

	asio::ip::tcp::socket socket;

	time_t timeConnected;
	uint32_t packetsSent = 0;
	uint32_t lastIp = 0;

	bool closed = false;
	bool receivedFirst = false;
};

#endif
