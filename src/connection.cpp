// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "connection.h"

#include "configmanager.h"
#include "outputmessage.h"
#include "protocol.h"
#include "scheduler.h"
#include "server.h"
#include "logger.h"
#include <fmt/format.h>

Connection_ptr ConnectionManager::createConnection(asio::io_context& io_context,
                                                   ConstServicePort_ptr servicePort)
{
	std::scoped_lock lockClass(connectionManagerLock);

	if (connections.size() >= static_cast<size_t>(getInteger(ConfigManager::MAX_CONNECTIONS))) {
		LOG_NETWORK(fmt::format("Max connections ({}) reached. Rejecting new connection.", getInteger(ConfigManager::MAX_CONNECTIONS)));
		return nullptr;
	}

	auto connection = std::make_shared<Connection>(io_context, servicePort);
	connections.insert(connection);
	return connection;
}

void ConnectionManager::releaseConnection(const Connection_ptr& connection)
{
	std::scoped_lock lockClass(connectionManagerLock);

	// Decrement IP counter
	uint32_t ip = connection->getLastIp();
	if (ip != 0) {
		auto it = ipConnectionCount.find(ip);
		if (it != ipConnectionCount.end()) {
			if (it->second <= 1) {
				ipConnectionCount.erase(it);
			} else {
				it->second--;
			}
		}
	}

	connections.erase(connection);
}

void ConnectionManager::closeAll()
{
	std::scoped_lock lockClass(connectionManagerLock);

	for (const auto& connection : connections) {
		try {
			asio::error_code error;
			connection->socket.shutdown(asio::ip::tcp::socket::shutdown_both, error);
			connection->socket.close(error);
		} catch (std::system_error&) {
		}
	}
	connections.clear();
	ipConnectionCount.clear();
}

void ConnectionManager::releaseAllProtocols()
{
	std::scoped_lock lockClass(connectionManagerLock);

	for (const auto& connection : connections) {
		if (connection->protocol) {
			connection->protocol->release();
			connection->protocol.reset();
		}
	}
}

bool ConnectionManager::isMaxConnectionsReached()
{
	std::scoped_lock lockClass(connectionManagerLock);
	return connections.size() >= static_cast<size_t>(getInteger(ConfigManager::MAX_CONNECTIONS));
}

bool ConnectionManager::isMaxConnectionsPerIPReached(uint32_t ip)
{
	std::scoped_lock lockClass(connectionManagerLock);
	auto it = ipConnectionCount.find(ip);
	if (it == ipConnectionCount.end()) {
		return false;
	}

	uint32_t limit = static_cast<uint32_t>(getInteger(ConfigManager::MAX_CONNECTIONS_PER_IP));
	if (limit == 0) {
		return false;
	}

	return it->second >= limit;
}

size_t ConnectionManager::getConnectionCount() const
{
	std::scoped_lock lockClass(connectionManagerLock);
	return connections.size();
}

void ConnectionManager::trackIPConnection(uint32_t ip)
{
	if (ip == 0) return;
	std::scoped_lock lockClass(connectionManagerLock);
	ipConnectionCount[ip]++;
}

// Connection

void Connection::close(bool force)
{
	std::scoped_lock lockClass(connectionLock);
	closeLocked(force);
}

void Connection::closeLocked(bool force)
{
	// Must be called with connectionLock held.
	if (closed) {
		return;
	}
	closed = true;

	ConnectionManager::getInstance().releaseConnection(shared_from_this());

	if (protocol) {
		g_dispatcher.addTask([protocol = protocol]() { protocol->release(); });
	}

	if (messageQueue.empty() || force) {
		closeSocket();
	} else {
		// will be closed by the destructor or onWriteOperation
	}
}

void Connection::closeSocket()
{
	if (socket.is_open()) {
		try {
			readTimer.cancel();
			writeTimer.cancel();
			asio::error_code error;
			socket.shutdown(asio::ip::tcp::socket::shutdown_both, error);
			socket.close(error);
		} catch (std::system_error& e) {
			LOG_NETWORK(fmt::format("Error - Connection::closeSocket: {}", e.what()));
		}
	}
}

Connection::~Connection() { closeSocket(); }

void Connection::accept(Protocol_ptr protocol)
{
	this->protocol = protocol;
	g_dispatcher.addTask([protocol]() { protocol->onConnect(); });

	accept();
}

void Connection::accept()
{
	std::scoped_lock lockClass(connectionLock);
	try {
		readTimer.expires_after(std::chrono::seconds(CONNECTION_READ_TIMEOUT));
		readTimer.async_wait(
		    [thisPtr = std::weak_ptr<Connection>(shared_from_this())](const asio::error_code& error) {
			    Connection::handleTimeout(thisPtr, error);
		    });

		// Read size of the first packet
		asio::async_read(
		    socket, asio::buffer(msg.getBuffer(), NetworkMessage::HEADER_LENGTH),
		    [thisPtr = shared_from_this()](const asio::error_code& error, auto /*bytes_transferred*/) {
			    thisPtr->parseHeader(error);
		    });
	} catch (std::system_error& e) {
		LOG_NETWORK(fmt::format("Error - Connection::accept: {}", e.what()));
		closeLocked(FORCE_CLOSE);
	}
}

void Connection::parseHeader(const asio::error_code& error)
{
	std::scoped_lock lockClass(connectionLock);
	readTimer.cancel();

	if (error) {
		closeLocked(FORCE_CLOSE);
		return;
	} else if (closed) {
		return;
	}

	uint32_t timePassed = std::max<uint32_t>(1, (time(nullptr) - timeConnected) + 1);
	if ((++packetsSent / timePassed) > getInteger(ConfigManager::MAX_PACKETS_PER_SECOND)) {
		LOG_NETWORK(fmt::format("{} disconnected for exceeding packet per second limit.", convertIPToString(getIPLocked())));
		closeLocked(false);
		return;
	}

	if (timePassed > 2) {
		timeConnected = time(nullptr);
		packetsSent = 0;
	}

	uint16_t size = msg.getLengthHeader();
	if (size == 0 || size >= NETWORKMESSAGE_MAXSIZE - 16) {
		closeLocked(FORCE_CLOSE);
		return;
	}

	try {
		readTimer.expires_after(std::chrono::seconds(CONNECTION_READ_TIMEOUT));
		readTimer.async_wait(
		    [thisPtr = std::weak_ptr<Connection>(shared_from_this())](const asio::error_code& error) {
			    Connection::handleTimeout(thisPtr, error);
		    });

		// Read packet content
		msg.setLength(size + NetworkMessage::HEADER_LENGTH);
		asio::async_read(
		    socket, asio::buffer(msg.getBodyBuffer(), size),
		    [thisPtr = shared_from_this()](const asio::error_code& error, auto /*bytes_transferred*/) {
			    thisPtr->parsePacket(error);
		    });
	} catch (std::system_error& e) {
		LOG_NETWORK(fmt::format("Error - Connection::parseHeader: {}", e.what()));
		closeLocked(FORCE_CLOSE);
	}
}

void Connection::parsePacket(const asio::error_code& error)
{
	std::scoped_lock lockClass(connectionLock);
	readTimer.cancel();

	if (error) {
		closeLocked(FORCE_CLOSE);
		return;
	} else if (closed) {
		return;
	}

	// Check packet checksum
	uint32_t checksum;
	int32_t len = msg.getLength() - msg.getBufferPosition() - NetworkMessage::CHECKSUM_LENGTH;
	if (len > 0) {
		checksum = adlerChecksum(msg.getBuffer() + msg.getBufferPosition() + NetworkMessage::CHECKSUM_LENGTH, len);
	} else {
		checksum = 0;
	}

	uint32_t recvChecksum = msg.get<uint32_t>();
	if (recvChecksum != checksum) {
		// it might not have been the checksum, step back
		msg.skipBytes(-NetworkMessage::CHECKSUM_LENGTH);
	}

	if (!receivedFirst) {
		// First message received
		receivedFirst = true;
		lastIp = getIPLocked();

		if (!protocol) {
			// Game protocol has already been created at this point
			protocol = service_port->make_protocol(recvChecksum == checksum, msg, shared_from_this());
			if (!protocol) {
				closeLocked(FORCE_CLOSE);
				return;
			}
		} else {
			msg.skipBytes(1); // Skip protocol ID
		}

		protocol->onRecvFirstMessage(msg);
	} else {
		protocol->onRecvMessage(msg); // Send the packet to the current protocol
	}

	try {
		readTimer.expires_after(std::chrono::seconds(CONNECTION_READ_TIMEOUT));
		readTimer.async_wait(
		    [thisPtr = std::weak_ptr<Connection>(shared_from_this())](const asio::error_code& error) {
			    Connection::handleTimeout(thisPtr, error);
		    });

		// Wait to the next packet
		asio::async_read(
		    socket, asio::buffer(msg.getBuffer(), NetworkMessage::HEADER_LENGTH),
		    [thisPtr = shared_from_this()](const asio::error_code& error, auto /*bytes_transferred*/) {
			    thisPtr->parseHeader(error);
		    });
	} catch (std::system_error& e) {
		LOG_NETWORK(fmt::format("Error - Connection::parsePacket: {}", e.what()));
		closeLocked(FORCE_CLOSE);
	}
}

void Connection::send(const OutputMessage_ptr& msg)
{
	std::scoped_lock lockClass(connectionLock);
	if (closed) {
		return;
	}

	bool noPendingWrite = messageQueue.empty();
	messageQueue.emplace_back(msg);
	if (noPendingWrite) {
		try {
			asio::post(socket.get_executor(),
			                  [thisPtr = shared_from_this(), msg] { thisPtr->internalSend(msg); });
		} catch (const std::system_error& e) {
			LOG_NETWORK(fmt::format("Error - Connection::send: {}", e.what()));
			messageQueue.clear();
			closeLocked(FORCE_CLOSE);
		}
	}
}

void Connection::internalSend(const OutputMessage_ptr& msg)
{
	protocol->onSendMessage(msg);
	try {
		writeTimer.expires_after(std::chrono::seconds(CONNECTION_WRITE_TIMEOUT));
		writeTimer.async_wait(
		    [thisPtr = std::weak_ptr<Connection>(shared_from_this())](const asio::error_code& error) {
			    Connection::handleTimeout(thisPtr, error);
		    });

		asio::async_write(
		    socket, asio::buffer(msg->getOutputBuffer(), msg->getLength()),
		    [thisPtr = shared_from_this()](const asio::error_code& error, auto /*bytes_transferred*/) {
			    thisPtr->onWriteOperation(error);
		    });
	} catch (std::system_error& e) {
		LOG_NETWORK(fmt::format("Error - Connection::internalSend: {}", e.what()));
		close(FORCE_CLOSE);
	}
}

uint32_t Connection::getIP()
{
	std::scoped_lock lockClass(connectionLock);
	return getIPLocked();
}

uint32_t Connection::getIPLocked()
{
	// Must be called with connectionLock held, or from a safe context.
	asio::error_code error;
	const asio::ip::tcp::endpoint endpoint = socket.remote_endpoint(error);
	if (error) {
		return 0;
	}

	return htonl(endpoint.address().to_v4().to_uint());
}

void Connection::onWriteOperation(const asio::error_code& error)
{
	std::scoped_lock lockClass(connectionLock);
	writeTimer.cancel();
	messageQueue.pop_front();

	if (error) {
		messageQueue.clear();
		closeLocked(FORCE_CLOSE);
		return;
	}

	if (!messageQueue.empty()) {
		internalSend(messageQueue.front());
	} else if (closed) {
		closeSocket();
	}
}

void Connection::handleTimeout(ConnectionWeak_ptr connectionWeak, const asio::error_code& error)
{
	if (error == asio::error::operation_aborted) {
		// The timer has been manually canceled
		return;
	}

	if (auto connection = connectionWeak.lock()) {
		connection->close(FORCE_CLOSE);
	}
}
