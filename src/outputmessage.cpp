// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "outputmessage.h"

#include "lockfree.h"
#include "protocol.h"
#include "scheduler.h"

extern Scheduler g_scheduler;

namespace {

using namespace std::chrono_literals;

// Retained free blocks, not a limit on outstanding messages. Each slot costs
// about 64 KiB (2048 ~= 128 MiB; 4096 ~= 256 MiB; 8192 ~= 512 MiB).
// Increase only when sustained profiling shows misses after the initial ramp.
inline constexpr uint16_t OUTPUTMESSAGE_FREE_LIST_CAPACITY = 2048;
inline constexpr auto AUTOSEND_DELAY_DEFAULT = 10ms;

} // namespace

void OutputMessagePool::scheduleSendAll(std::chrono::milliseconds delay) noexcept
{
	sendScheduled = true;
	g_scheduler.addEvent(createSchedulerTask(static_cast<int>(delay.count()), []() {
		OutputMessagePool::getInstance().sendAll();
	}));
}

void OutputMessagePool::sendAll() noexcept
{
	// THREAD-SAFETY: Must be called from dispatcher thread only.
	sendScheduled = false;
	if (bufferedProtocols.empty()) [[unlikely]] {
		return;
	}

	std::vector<Protocol_ptr> protocols;
	protocols.swap(bufferedProtocols);

	for (auto& protocol : protocols) {
		protocol->inAutosend = false;
		auto& msg = protocol->getCurrentBuffer();
		if (msg) [[likely]] {
			protocol->send(std::move(msg));
		}
	}

	if (!bufferedProtocols.empty()) [[likely]] {
		scheduleSendAll(AUTOSEND_DELAY_DEFAULT);
	}
}

void OutputMessagePool::addProtocolToAutosend(Protocol_ptr protocol)
{
	// THREAD-SAFETY: Must be called from dispatcher thread only
	// dispatcher thread
	if (protocol->inAutosend) {
		return;
	}

	auto& bufferedProtocol = bufferedProtocols.emplace_back(std::move(protocol));
	bufferedProtocol->inAutosend = true;
	if (!sendScheduled) [[unlikely]] {
		scheduleSendAll(AUTOSEND_DELAY_DEFAULT);
	}
}

void OutputMessagePool::removeProtocolFromAutosend(const Protocol_ptr& protocol)
{
	// THREAD-SAFETY: Must be called from dispatcher thread only
	// dispatcher thread
	auto it = std::find(bufferedProtocols.begin(), bufferedProtocols.end(), protocol);
	if (it != bufferedProtocols.end()) [[likely]] {
		// Swap-and-pop for O(1) removal - does not preserve order
		(*it)->inAutosend = false;
		std::swap(*it, bufferedProtocols.back());
		bufferedProtocols.pop_back();
	}
}

OutputMessage_ptr OutputMessagePool::getOutputMessage()
{
	/**
	 * Uses the fixed lock-free allocator to create messages
	 *
	 * The fixed allocator guarantees:
	 * - Single allocations (n=1) use the lock-free pool
	 * - Block allocations (n>1) use the standard operator new
	 * - Works correctly with std::allocate_shared
	 *
	 * Pool capacity: 2048 messages
	 */
	return std::allocate_shared<OutputMessage>(
	    LockfreePoolingAllocator<void, OUTPUTMESSAGE_FREE_LIST_CAPACITY, true>());
}

#ifdef OUTPUTMESSAGE_POOL_DIAGNOSTICS
OutputMessagePool::Diagnostics OutputMessagePool::getDiagnostics() noexcept
{
	using Counters = LockfreePoolDiagnostics<OUTPUTMESSAGE_FREE_LIST_CAPACITY>;
	return {Counters::hits.load(std::memory_order_relaxed), Counters::misses.load(std::memory_order_relaxed),
	        Counters::inUse.load(std::memory_order_relaxed), Counters::peakInUse.load(std::memory_order_relaxed),
	        Counters::overflowFrees.load(std::memory_order_relaxed)};
}
#endif

void OutputMessagePool::prewarmPool(size_t count)
{
	// Pre-allocate OutputMessage blocks and return them to the lock-free pool.
	// This avoids operator new() overhead during early gameplay.
	count = std::min(count, static_cast<size_t>(OUTPUTMESSAGE_FREE_LIST_CAPACITY));

	std::vector<OutputMessage_ptr> warmup;
	warmup.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		warmup.emplace_back(getOutputMessage());
	}
	// All shared_ptrs go out of scope here, returning blocks to the pool
}

void OutputMessagePool::drainPool()
{
	// Free all pooled memory blocks so valgrind reports zero leaks.
	LockfreePoolRegistry::drainAll();
}
