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

// Inline constexpr constants avoid ODR issues and are friendly to headers/optimizations
inline constexpr uint16_t OUTPUTMESSAGE_FREE_LIST_CAPACITY = 2048;
inline constexpr auto OUTPUTMESSAGE_AUTOSEND_DELAY = 10ms;

/**
 * Sends all buffered messages to the protocols
 *
 * @param bufferedProtocols Pointer to the protocol vector (non-null)
 * @note Must be called only from the dispatcher thread
 * @note Marked as noexcept - failures will cause std::terminate
 */
void sendAll(std::vector<Protocol_ptr>* bufferedProtocols) noexcept;

/**
 * Schedules the next automatic sending of messages
 *
 * @param bufferedProtocols Pointer to the protocol vector
 * @note Must be called only from the dispatcher thread
 */
void scheduleSendAll(std::vector<Protocol_ptr>* bufferedProtocols) noexcept
{
	g_scheduler.addEvent(createSchedulerTask(static_cast<int>(OUTPUTMESSAGE_AUTOSEND_DELAY.count()),
	                                         [bufferedProtocols]() { sendAll(bufferedProtocols); }));
}

void sendAll(std::vector<Protocol_ptr>* bufferedProtocols) noexcept
{
	// dispatcher thread
	if (!bufferedProtocols || bufferedProtocols->empty()) [[unlikely]] {
		return;
	}

	// Sends the current buffer of each protocol, if present
	for (auto& protocol : *bufferedProtocols) {
		auto& msg = protocol->getCurrentBuffer();
		if (msg) [[likely]] {
			protocol->send(std::move(msg));
		}
	}

	// Reschedule only if there are still buffered protocols
	if (!bufferedProtocols->empty()) [[likely]] {
		scheduleSendAll(bufferedProtocols);
	}
}
} // namespace

void OutputMessagePool::addProtocolToAutosend(Protocol_ptr protocol)
{
	// THREAD-SAFETY: Must be called from dispatcher thread only
	// dispatcher thread
	if (bufferedProtocols.empty()) [[unlikely]] {
		scheduleSendAll(&bufferedProtocols);
	}
	bufferedProtocols.emplace_back(std::move(protocol));
}

void OutputMessagePool::removeProtocolFromAutosend(const Protocol_ptr& protocol)
{
	// THREAD-SAFETY: Must be called from dispatcher thread only
	// dispatcher thread
	auto it = std::find(bufferedProtocols.begin(), bufferedProtocols.end(), protocol);
	if (it != bufferedProtocols.end()) [[likely]] {
		// Swap-and-pop for O(1) removal - does not preserve order
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
	return std::allocate_shared<OutputMessage>(LockfreePoolingAllocator<void, OUTPUTMESSAGE_FREE_LIST_CAPACITY>());
}
