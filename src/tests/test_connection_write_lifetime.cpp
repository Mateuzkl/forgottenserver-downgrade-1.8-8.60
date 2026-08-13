// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

// Regression tests for the ownership contract between Connection::messageQueue and
// an in-flight asio::async_write().
//
// The bug these cover: the producer-side overflow path in Connection::send() called
// messageQueue.clear() while an async write was still reading the front message's
// inline buffer. The queue held the only strong reference to that OutputMessage, so
// clearing it freed the buffer under Asio (use-after-free), and the completion then
// ran messageQueue.pop_front() on an already-empty deque.

#include "../otpch.h"

#include "../configmanager.h"
#include "../connection.h"
#include "../outputmessage.h"
#include "../protocol.h"

#include "test_support.h"

// Grants the tests access to Connection internals. Declared as a friend in
// connection.h; see the note there.
struct ConnectionTestAccess
{
	static asio::ip::tcp::socket& socket(Connection& connection) { return connection.socket; }
	static size_t queueSize(Connection& connection) { return connection.messageQueue.size(); }
	static bool isClosed(Connection& connection) { return connection.closed; }
	static void setProtocol(Connection& connection, Protocol_ptr protocol)
	{
		connection.protocol = std::move(protocol);
	}
	static void onWriteOperation(Connection& connection, const asio::error_code& error)
	{
		connection.onWriteOperation(error);
	}
};

namespace {

// Protocol is abstract and its real onSendMessage() rewrites the buffer (length
// header, XTEA, checksum). The tests only care about buffer lifetime, so this keeps
// the payload byte-for-byte and does nothing else.
class NullProtocol final : public Protocol
{
public:
	explicit NullProtocol(Connection_ptr connection) : Protocol(std::move(connection)) {}

	void onRecvFirstMessage(NetworkMessage&) override {}
	void onSendMessage(const OutputMessage_ptr&) const override {}
};

void resetConnectionManager()
{
	ConnectionManager::getInstance().closeAll();
	ConfigManager::setInteger(ConfigManager::MAX_CONNECTIONS, 2000);
	ConfigManager::setInteger(ConfigManager::MAX_CONNECTIONS_PER_IP, 10);
}

OutputMessage_ptr makeMessage(size_t payloadBytes)
{
	auto msg = std::make_shared<OutputMessage>();
	for (size_t i = 0; i < payloadBytes; ++i) {
		msg->addByte(static_cast<uint8_t>(i));
	}
	return msg;
}

// Builds a real, connected TCP pair on the loopback and hands the server end to a
// Connection.
struct ConnectedPair
{
	asio::io_context ioContext;
	asio::ip::tcp::acceptor acceptor{ioContext};
	asio::ip::tcp::socket peer{ioContext};
	Connection_ptr connection;

	ConnectedPair()
	{
		const asio::ip::tcp::endpoint localhost{asio::ip::make_address("127.0.0.1"), 0};
		acceptor.open(localhost.protocol());
		acceptor.bind(localhost);
		acceptor.listen();

		connection = ConnectionManager::getInstance().createConnection(ioContext, {});
		ConnectionTestAccess::setProtocol(*connection, std::make_shared<NullProtocol>(connection));

		acceptor.async_accept(ConnectionTestAccess::socket(*connection), [](const asio::error_code&) {});
		peer.connect(acceptor.local_endpoint());
		ioContext.poll();
		ioContext.restart();

		// Keep the kernel buffers small so saturating them below is cheap.
		ConnectionTestAccess::socket(*connection).set_option(asio::socket_base::send_buffer_size{2048});
		peer.set_option(asio::socket_base::receive_buffer_size{2048});
	}

	// Writes until the socket reports would_block. The peer never reads, so from here
	// any async_write() is guaranteed to stay pending rather than completing inside
	// poll() — which is what makes "a write is in flight" deterministic instead of
	// dependent on however much the kernel felt like buffering.
	void saturateSendBuffer()
	{
		auto& socket = ConnectionTestAccess::socket(*connection);
		socket.non_blocking(true);

		const std::vector<uint8_t> filler(16 * 1024, 0xAB);
		asio::error_code error;
		for (int attempt = 0; attempt < 4096; ++attempt) {
			const size_t written = socket.write_some(asio::buffer(filler), error);
			if (error || written == 0) {
				break;
			}
		}
		CHECK(error == asio::error::would_block);
	}

	~ConnectedPair()
	{
		asio::error_code ignored;
		peer.close(ignored);
		acceptor.close(ignored);
	}
};

} // namespace

// The core regression: overflow-triggered forced close must not free the buffer that
// a pending async_write() is still reading from.
TEST_CASE(test_queue_overflow_does_not_free_in_flight_write_buffer)
{
	resetConnectionManager();
	ConnectedPair pair;
	auto& connection = *pair.connection;
	pair.saturateSendBuffer();

	// Start a write that cannot drain, then drop the test's own reference. From here
	// the only owners are Connection's queue and — after the fix — the completion
	// handler that Asio holds while the write is in flight.
	std::weak_ptr<OutputMessage> inFlight;
	{
		auto msg = makeMessage(4'096);
		inFlight = msg;
		connection.send(msg);
	}
	pair.ioContext.poll();
	pair.ioContext.restart();

	CHECK(!inFlight.expired());
	CHECK(ConnectionTestAccess::queueSize(connection) == 1);

	// Fill the queue to exactly the limit. These never start a write of their own:
	// the first message still holds the write slot.
	while (ConnectionTestAccess::queueSize(connection) < MAX_PENDING_WRITE_MESSAGES) {
		connection.send(makeMessage(16));
	}
	CHECK(ConnectionTestAccess::queueSize(connection) == MAX_PENDING_WRITE_MESSAGES);
	CHECK(!ConnectionTestAccess::isClosed(connection));

	// One more send trips the backpressure limit and force-closes.
	connection.send(makeMessage(16));
	CHECK(ConnectionTestAccess::isClosed(connection));

	// The assertion that fails before the fix: the overflow path used to clear the
	// queue here, dropping the last reference to a buffer Asio was still reading.
	CHECK(!inFlight.expired());

	// Let the cancelled write's completion run. It must pop safely, drain the queue
	// from the completion side, and terminate the connection without UB.
	pair.ioContext.poll();
	pair.ioContext.restart();

	CHECK(ConnectionTestAccess::queueSize(connection) == 0);
	CHECK(ConnectionTestAccess::isClosed(connection));
	CHECK(inFlight.expired()); // released only after the write completed

	resetConnectionManager();
}

// The completion handler owns its message independently of the queue, so it must
// tolerate arriving at an empty deque instead of calling pop_front() on it.
TEST_CASE(test_write_completion_on_empty_queue_is_safe)
{
	resetConnectionManager();
	ConnectedPair pair;
	auto& connection = *pair.connection;

	connection.close(Connection::FORCE_CLOSE);
	CHECK(ConnectionTestAccess::queueSize(connection) == 0);

	// Before the fix this was an unconditional pop_front() on an empty deque.
	ConnectionTestAccess::onWriteOperation(connection, asio::error::operation_aborted);
	CHECK(ConnectionTestAccess::queueSize(connection) == 0);

	ConnectionTestAccess::onWriteOperation(connection, {});
	CHECK(ConnectionTestAccess::queueSize(connection) == 0);
	CHECK(ConnectionTestAccess::isClosed(connection));

	resetConnectionManager();
}

// A forced close while a write is pending must terminate cleanly: the connection is
// released from the manager and no queued message survives the completion.
TEST_CASE(test_forced_close_with_pending_write_terminates_cleanly)
{
	resetConnectionManager();
	auto& manager = ConnectionManager::getInstance();
	ConnectedPair pair;
	auto& connection = *pair.connection;
	CHECK(manager.getConnectionCount() == 1);
	pair.saturateSendBuffer();

	std::weak_ptr<OutputMessage> inFlight;
	{
		auto msg = makeMessage(4'096);
		inFlight = msg;
		connection.send(msg);
	}
	pair.ioContext.poll();
	pair.ioContext.restart();
	CHECK(!inFlight.expired());

	connection.send(makeMessage(32));
	CHECK(ConnectionTestAccess::queueSize(connection) == 2);

	connection.close(Connection::FORCE_CLOSE);
	CHECK(manager.getConnectionCount() == 0);
	CHECK(!inFlight.expired()); // still owned by the pending write

	pair.ioContext.poll();
	pair.ioContext.restart();

	CHECK(ConnectionTestAccess::queueSize(connection) == 0);
	CHECK(inFlight.expired());

	resetConnectionManager();
}

// Sends below the limit must keep working normally: backpressure is a ceiling, not a
// behaviour change for ordinary traffic.
TEST_CASE(test_normal_send_below_limit_is_unaffected)
{
	resetConnectionManager();
	ConnectedPair pair;
	auto& connection = *pair.connection;

	connection.send(makeMessage(8));
	pair.ioContext.poll();
	pair.ioContext.restart();

	// Small write against an empty socket buffer completes immediately and drains.
	CHECK(ConnectionTestAccess::queueSize(connection) == 0);
	CHECK(!ConnectionTestAccess::isClosed(connection));

	connection.close(Connection::FORCE_CLOSE);
	resetConnectionManager();
}

TFS_TEST_MAIN()
