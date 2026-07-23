// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_PACKET_BACKLOG_H
#define FS_PACKET_BACKLOG_H

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>

namespace tfs::net {

// Preserve the legacy dispatcher deadline only for gameplay actions that
// become unsafe or confusing when replayed after a long dispatcher stall.
constexpr bool shouldExpireQueuedGamePacket(uint8_t opcode) noexcept
{
	switch (opcode) {
		case 0x6F: // turn north
		case 0x70: // turn east
		case 0x71: // turn south
		case 0x72: // turn west
		case 0x77: // hotkey equip
		case 0x78: // move item
		case 0x79: // look in shop
		case 0x7A: // buy
		case 0x7B: // sell
		case 0x7E: // look in trade
		case 0x82: // use item
		case 0x83: // use item on position
		case 0x84: // use item on creature
		case 0x85: // rotate item
		case 0x8B: // wrap item
		case 0x8C: // look at
		case 0x8D: // look in battle list
		case 0xCB: // browse field
		case 0xCC: // seek in container
			return true;
		default:
			return false;
	}
}

class PacketBacklog
{
private:
	struct State
	{
		explicit State(uint32_t packetLimit) : limit(packetLimit) {}

		const uint32_t limit;
		std::atomic<uint32_t> pending{0};
		std::atomic<bool> floodTriggered{false};
#ifdef PROTOCOLGAME_BACKLOG_DIAGNOSTICS
		std::atomic<uint32_t> peak{0};
		std::atomic<uint64_t> rejected{0};
#endif
	};

public:
	static constexpr uint32_t DEFAULT_LIMIT = 128;

	class Ticket
	{
	public:
		Ticket() = default;
		Ticket(const Ticket&) = delete;
		Ticket& operator=(const Ticket&) = delete;
		Ticket(Ticket&& other) noexcept : state(std::exchange(other.state, {})) {}
		Ticket& operator=(Ticket&& other) noexcept
		{
			if (this != &other) {
				release();
				state = std::exchange(other.state, {});
			}
			return *this;
		}

		~Ticket() { release(); }

		explicit operator bool() const noexcept { return static_cast<bool>(state); }

	private:
		friend class PacketBacklog;
		explicit Ticket(std::shared_ptr<State> state) : state(std::move(state)) {}

		void release() noexcept
		{
			if (!state) {
				return;
			}

			const uint32_t previous = state->pending.fetch_sub(1, std::memory_order_acq_rel);
			assert(previous > 0 && "Packet backlog counter underflow");
			(void)previous;
			state.reset();
		}

		std::shared_ptr<State> state;
	};

	struct Admission
	{
		Ticket ticket;
		uint32_t observedPending = 0;
		uint32_t newPeak = 0;
		bool requestDisconnect = false;

		explicit operator bool() const noexcept { return static_cast<bool>(ticket); }
	};

	explicit PacketBacklog(uint32_t limit = DEFAULT_LIMIT) : state(std::make_shared<State>(limit))
	{
		assert(limit > 0 && "Packet backlog limit must be positive");
	}

	Admission tryAcquire() noexcept
	{
		Admission admission;
		if (state->floodTriggered.load(std::memory_order_acquire)) {
			recordRejected();
			return admission;
		}

		const uint32_t pending = state->pending.fetch_add(1, std::memory_order_acq_rel) + 1;
		admission.observedPending = pending;
		if (pending > state->limit || state->floodTriggered.load(std::memory_order_acquire)) {
			state->pending.fetch_sub(1, std::memory_order_acq_rel);
			recordRejected();

			bool expected = false;
			admission.requestDisconnect = pending > state->limit && state->floodTriggered.compare_exchange_strong(
			    expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
			return admission;
		}

#ifdef PROTOCOLGAME_BACKLOG_DIAGNOSTICS
		uint32_t peak = state->peak.load(std::memory_order_relaxed);
		while (pending > peak && !state->peak.compare_exchange_weak(
		                             peak, pending, std::memory_order_relaxed, std::memory_order_relaxed)) {
		}
		if (pending > peak) {
			admission.newPeak = pending;
		}
#endif

		admission.ticket = Ticket(state);
		return admission;
	}

	uint32_t current() const noexcept { return state->pending.load(std::memory_order_acquire); }
	uint32_t limit() const noexcept { return state->limit; }
	bool floodTriggered() const noexcept { return state->floodTriggered.load(std::memory_order_acquire); }

#ifdef PROTOCOLGAME_BACKLOG_DIAGNOSTICS
	uint32_t peak() const noexcept { return state->peak.load(std::memory_order_relaxed); }
	uint64_t rejected() const noexcept { return state->rejected.load(std::memory_order_relaxed); }
#endif

private:
	void recordRejected() noexcept
	{
#ifdef PROTOCOLGAME_BACKLOG_DIAGNOSTICS
		state->rejected.fetch_add(1, std::memory_order_relaxed);
#endif
	}

	std::shared_ptr<State> state;
};

} // namespace tfs::net

#endif // FS_PACKET_BACKLOG_H
