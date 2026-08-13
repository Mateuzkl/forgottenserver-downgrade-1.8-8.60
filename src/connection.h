// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_CONNECTION_H
#define FS_CONNECTION_H

#include "networkmessage.h"

inline constexpr int32_t CONNECTION_WRITE_TIMEOUT = 30;
inline constexpr int32_t CONNECTION_READ_TIMEOUT = 30;

// Upper bound on writes queued for a single connection.
//
// OutputMessage embeds its NETWORKMESSAGE_MAXSIZE buffer inline, so every queued
// message costs ~64 KB of real memory regardless of how few bytes it carries. An
// unbounded queue therefore lets one peer that stops reading (TCP zero window,
// while keeping the socket open) pin memory without limit as the server keeps
// producing updates for it.
//
// 128 messages caps that at roughly 8 MB per connection, which is far above any
// legitimate burst — a full login sends a few dozen — so hitting it means the
// peer is dead or hostile and the connection should go.
inline constexpr size_t MAX_PENDING_WRITE_MESSAGES = 128;

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

	size_t getConnectionCount() const;
	uint32_t getConnectionCountForIP(uint32_t ip) const;
	bool trackIPConnection(const Connection_ptr& connection, uint32_t ip, uint32_t& currentCount);

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

private:
	void parseHeader(const asio::error_code& error);
	void parsePacket(const asio::error_code& error);

	void onWriteOperation(const asio::error_code& error);

	static void handleTimeout(ConnectionWeak_ptr connectionWeak, const asio::error_code& error);

	void closeSocket();
	void closeLocked(bool force);
	void internalSend(OutputMessage_ptr msg);
	uint32_t getIPLocked();

	asio::ip::tcp::socket& getSocket() { return socket; }
	friend class ServicePort;

	// Test seam. The write-lifetime regression tests need a genuinely connected
	// socket and direct sight of messageQueue to reproduce "async write pending while
	// the queue is emptied"; nothing else can observe that state. Declaration only —
	// the type is defined in src/tests and never exists in the server binary.
	friend struct ConnectionTestAccess;

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
	uint32_t trackedIp = 0;
	uint32_t cachedPeerIp = 0;

	bool closed = false;
	bool receivedFirst = false;
};

#endif
