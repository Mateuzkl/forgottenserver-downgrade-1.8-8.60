// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in LICENSE.

#include "../spectators.h"
#include "test_support.h"

#include <cstdlib>
#include <new>
#include <optional>

namespace {
thread_local bool countAllocations = false;
thread_local size_t allocationCalls = 0;

struct AllocationWindow
{
	AllocationWindow()
	{
		allocationCalls = 0;
		countAllocations = true;
	}
	~AllocationWindow() { countAllocations = false; }
};
} // namespace

void* operator new(size_t bytes)
{
	if (void* memory = std::malloc(bytes == 0 ? 1 : bytes)) {
		if (countAllocations) {
			++allocationCalls;
		}
		return memory;
	}
	throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }

TEST_CASE(empty_spectator_queries_and_empty_merges_do_not_allocate)
{
	size_t calls = 0;
	{
		AllocationWindow window;
		for (size_t iteration = 0; iteration < 10000; ++iteration) {
			SpectatorVec spectators;
			SpectatorVec empty;
			spectators.emplace_back(nullptr);
			spectators.addSpectators(empty);
			CHECK(spectators.empty());
		}
		calls = allocationCalls;
	}
	std::cout << "empty_queries_and_merges_allocations=" << calls << '\n';
	CHECK(calls == 0);
}

TEST_CASE(first_insertion_keeps_the_existing_batch_capacity)
{
	// Only shared ownership and identity are used here; no Creature is read.
	auto storage = std::make_shared<int>(42);
	std::shared_ptr<Creature> spectator(storage, reinterpret_cast<Creature*>(storage.get()));
	size_t calls = 0;
	std::optional<SpectatorVec> spectators;
	{
		AllocationWindow window;
		spectators.emplace();
		for (size_t index = 0; index < 32; ++index) {
			spectators->emplace_back(spectator);
		}
		calls = allocationCalls;
	}
	std::cout << "construct_and_insert_32_allocations=" << calls << '\n';
	CHECK(spectators->size() == 32);
	CHECK(calls == 1);

	std::optional<SpectatorVec> destination;
	{
		AllocationWindow window;
		destination.emplace();
		destination->addSpectators(*spectators);
		calls = allocationCalls;
	}
	std::cout << "construct_and_merge_32_allocations=" << calls << '\n';
	CHECK(destination->size() == 1);
	CHECK((*destination)[0] == spectator);
	CHECK(calls == 1);
}

TFS_TEST_MAIN()
