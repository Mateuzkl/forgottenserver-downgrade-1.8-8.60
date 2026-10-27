#include "../otpch.h"
#include "../outputmessage.h"
#include "../protocol.h"
#include "../scheduler.h"
#include "../tasks.h"
#include "test_support.h"

struct OutputMessagePoolTestAccess
{
	static std::unique_ptr<OutputMessagePool> create() { return std::unique_ptr<OutputMessagePool>(new OutputMessagePool); }
	static void add(OutputMessagePool& pool, const Protocol_ptr& protocol)
	{
		// Exercise real batching without scheduling work on the global reactor.
		pool.sendScheduled = true;
		pool.addProtocolToAutosend(protocol);
	}
	static void flush(OutputMessagePool& pool) { pool.sendAll(); }
	static size_t size(const OutputMessagePool& pool) { return pool.bufferedProtocols.size(); }
	static size_t capacity(const OutputMessagePool& pool) { return pool.bufferedProtocols.capacity(); }
	static bool scheduled(const OutputMessagePool& pool) { return pool.sendScheduled; }
	static void resetSchedule(OutputMessagePool& pool) { pool.sendScheduled = false; }
	static size_t flushingCapacity(const OutputMessagePool& pool) { return pool.flushingProtocols.capacity(); }
	static bool released(const OutputMessagePool& pool)
	{
		return pool.bufferedProtocols.empty() && pool.flushingProtocols.empty();
	}
};

namespace {
class TestProtocol final : public Protocol
{
public:
	TestProtocol() : Protocol(nullptr) {}
	void onRecvFirstMessage(NetworkMessage&) override {}
};
} // namespace

TEST_CASE(autosend_deduplicates_protocol_and_releases_messages_after_flush)
{
	auto pool = OutputMessagePoolTestAccess::create();
	auto protocol = std::make_shared<TestProtocol>();
	std::weak_ptr<OutputMessage> message;
	{
		auto output = std::make_shared<OutputMessage>();
		output->addByte(0xA0);
		message = output;
		protocol->getCurrentBuffer() = std::move(output);
	}
	OutputMessagePoolTestAccess::add(*pool, protocol);
	OutputMessagePoolTestAccess::add(*pool, protocol);
	CHECK(OutputMessagePoolTestAccess::size(*pool) == 1);
	OutputMessagePoolTestAccess::flush(*pool);
	CHECK(!protocol->getCurrentBuffer());
	CHECK(message.expired());
	CHECK(OutputMessagePoolTestAccess::released(*pool));
	// The same protocol must be eligible for the next batch.
	OutputMessagePoolTestAccess::add(*pool, protocol);
	CHECK(OutputMessagePoolTestAccess::size(*pool) == 1);
	pool->removeProtocolFromAutosend(protocol);
	CHECK(OutputMessagePoolTestAccess::released(*pool));
	std::weak_ptr<Protocol> weak = protocol;
	protocol.reset();
	CHECK(weak.expired());
}

TEST_CASE(autosend_reuses_both_batch_allocations_under_repeated_load)
{
	auto pool = OutputMessagePoolTestAccess::create();
	std::vector<Protocol_ptr> clients;
	for (int i = 0; i < 1000; ++i) clients.push_back(std::make_shared<TestProtocol>());
	// Warm both vectors, then require stable capacity for 1,000 more batches.
	for (int batch = 0; batch < 1002; ++batch) {
		for (const auto& protocol : clients) OutputMessagePoolTestAccess::add(*pool, protocol);
		CHECK(OutputMessagePoolTestAccess::size(*pool) == clients.size());
		OutputMessagePoolTestAccess::flush(*pool);
		CHECK(OutputMessagePoolTestAccess::released(*pool));
		if (batch >= 1) {
			CHECK(OutputMessagePoolTestAccess::capacity(*pool) >= clients.size());
			CHECK(OutputMessagePoolTestAccess::flushingCapacity(*pool) >= clients.size());
		}
	}
	std::weak_ptr<Protocol> weak = clients.back();
	clients.clear();
	CHECK(weak.expired());
}

TEST_CASE(autosend_recovers_after_scheduler_rejects_its_timer)
{
	auto& pool = OutputMessagePool::getInstance();
	auto protocol = std::make_shared<TestProtocol>();
	struct Cleanup
	{
		OutputMessagePool& pool;
		Protocol_ptr protocol;
		~Cleanup()
		{
			pool.removeProtocolFromAutosend(protocol);
			OutputMessagePoolTestAccess::resetSchedule(pool);
			g_scheduler.shutdown();
			g_dispatcher.shutdown();
			g_reactor.drain();
		}
	} cleanup{pool, protocol};
	g_dispatcher.start();
	g_scheduler.stop();
	std::exception_ptr failure;
	OutputMessage_ptr first;
	bool completed = false;
	const auto attempt = [&](auto&& action) {
		try { action(); } catch (...) {
			failure = std::current_exception();
			g_dispatcher.shutdown();
		}
	};
	g_dispatcher.addTask([&] { attempt([&] {
		// A stopped scheduler refuses events without dropping critical reactor work.
		CHECK(g_scheduler.addEvent(0, [] {}) == 0);
		first = protocol->getOutputBuffer(1);
		first->addByte(0xAA);
		CHECK(!OutputMessagePoolTestAccess::scheduled(pool));
		g_dispatcher.addTask([&] { attempt([&] {
			// Restart the scheduler, then retry the same unsent buffer.
			g_scheduler.start();
			auto next = protocol->getOutputBuffer(1);
			next->addByte(0xBB);
			CHECK(next == first);
			CHECK(OutputMessagePoolTestAccess::scheduled(pool));
			CHECK(first->getLength() == 2);
			CHECK(first->getBuffer()[NetworkMessage::INITIAL_BUFFER_POSITION] == 0xAA);
			CHECK(first->getBuffer()[NetworkMessage::INITIAL_BUFFER_POSITION + 1] == 0xBB);
			CHECK(g_scheduler.addEvent(20, [&] { attempt([&] {
				CHECK(!protocol->getCurrentBuffer());
				CHECK(OutputMessagePoolTestAccess::released(pool));
				completed = true;
				g_dispatcher.shutdown();
			}); }) != 0);
		}); });
	}); });
	g_reactor.runLoop();
	if (failure) std::rethrow_exception(failure);
	CHECK(completed);
}

TFS_TEST_MAIN()
