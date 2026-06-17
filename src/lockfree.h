// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
// Modernized for C++20 standard with bug fix for block allocations

#ifndef FS_LOCKFREE_H
#define FS_LOCKFREE_H

#include <array>
#include <cstddef>
#include <functional>
#include <mutex>
#include <system_error>
#include <vector>

/**
 * Registry of pool drain callbacks.
 * Each LockfreeFreeList instance registers itself here on first use,
 * so drainAll() can free every pooled block at shutdown.
 */
struct LockfreePoolRegistry
{
	static std::vector<std::function<void()>>& drains() {
		static std::vector<std::function<void()>> v;
		return v;
	}
	static void drainAll() {
		for (auto& fn : drains()) fn();
	}
};

/**
 * Lock-free free list wrapper
 * 
 * Provides a singleton free list per (size, capacity) combination
 * This allows different types of the same size to share the same pool
 * 
 * Modernized with [[nodiscard]] and noexcept specifications
 */

template <std::size_t TSize, std::size_t CAPACITY>
struct LockfreeFreeList
{
	class FreeList
	{
	public:
		~FreeList()
		{
			void* p;
			while (pop(p)) {
				operator delete(p);
			}
		}

		bool pop(void*& p) noexcept
		{
			try {
				std::lock_guard lock(mutex);
				if (size == 0) {
					p = nullptr;
					return false;
				}

				p = items[--size];
				return true;
			} catch (const std::system_error&) {
				p = nullptr;
				return false;
			}
		}

		bool bounded_push(void* p) noexcept
		{
			try {
				std::lock_guard lock(mutex);
				if (size >= CAPACITY) {
					return false;
				}

				items[size++] = p;
				return true;
			} catch (const std::system_error&) {
				return false;
			}
		}

	private:
		std::mutex mutex;
		std::array<void*, CAPACITY> items{};
		std::size_t size = 0;
	};

	[[nodiscard]] static FreeList& get() noexcept {
		static FreeList freeList;
		static const bool registered = [] {
			LockfreePoolRegistry::drains().emplace_back([] { drain(); });
			return true;
		}();
		(void)registered;
		return freeList;
	}

	static void drain() noexcept {
		// Use a local stack ref to avoid re-entering get()
		static auto& freeList = get();
		void* p;
		while (freeList.pop(p)) {
			operator delete(p);
		}
	}
};

/**
 * STL-compatible lock-free pooling allocator
 *
 * Reuses memory from a lock-free pool to minimize allocation overhead
 * Falls back to standard allocation when pool is empty/full
 *
 * FIXED: Now properly handles block allocations (n > 1)
 * - Single object allocations (n == 1) use the lock-free pool
 * - Block allocations (n > 1) use standard allocator
 *
 * @tparam T Type to allocate
 * @tparam CAPACITY Maximum number of pooled objects
 */
template <typename T, std::size_t CAPACITY>
class LockfreePoolingAllocator
{
public:
	using value_type = T;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using propagate_on_container_move_assignment = std::true_type;
	using is_always_equal = std::true_type;

	// Rebind is deprecated in C++17 but kept for backward compatibility
	template<typename U>
	struct rebind {
		using other = LockfreePoolingAllocator<U, CAPACITY>;
	};

	constexpr LockfreePoolingAllocator() noexcept = default;
	constexpr LockfreePoolingAllocator(const LockfreePoolingAllocator&) noexcept = default;

	template <typename U>
	constexpr LockfreePoolingAllocator(const LockfreePoolingAllocator<U, CAPACITY>&) noexcept {}

	/**
	 * Allocate memory for n objects of type T
	 *
	 * For single object allocations (n == 1):
	 *   - Attempts to reuse memory from the lock-free pool first
	 *   - Falls back to operator new if pool is empty
	 *
	 * For block allocations (n > 1):
	 *   - Uses standard operator new to allocate contiguous block
	 *   - Pool is not used for block allocations
	 *
	 * @param n Number of objects to allocate
	 * @return Pointer to allocated memory
	 */
	[[nodiscard]] T* allocate(size_type n) const {
		// Block allocations bypass the pool
		if (n != 1) [[unlikely]] {
			return static_cast<T*>(operator new(n * sizeof(T)));
		}

		// Single object allocation - try to reuse from pool
		auto& freeList = LockfreeFreeList<sizeof(T), CAPACITY>::get();
		void* p;

		if (freeList.pop(p)) [[likely]] {
			// Successfully reused memory from pool
			return static_cast<T*>(p);
		}

		// Intentional raw storage allocation: STL allocators must return
		// unconstructed memory; object lifetime is managed by the container.
		return static_cast<T*>(operator new(sizeof(T)));
	}

	/**
	 * Deallocate memory for n objects of type T
	 *
	 * For single object deallocations (n == 1):
	 *   - Attempts to return memory to the lock-free pool first
	 *   - Falls back to operator delete if pool is full
	 *
	 * For block deallocations (n > 1):
	 *   - Uses standard operator delete
	 *   - Pool is not used for block deallocations
	 *
	 * Note: Destructor must already be called before this point
	 *
	 * @param p Pointer to memory to deallocate
	 * @param n Number of objects being deallocated
	 */
	void deallocate(T* p, size_type n) const noexcept {
		// Block deallocations bypass the pool
		if (n != 1) [[unlikely]] {
			operator delete(p);
			return;
		}

		// Single object deallocation - try to return to pool
		auto& freeList = LockfreeFreeList<sizeof(T), CAPACITY>::get();

		if (freeList.bounded_push(p)) [[likely]] {
			// Successfully returned memory to pool
			return;
		}

		// Pool is full, release raw storage. The container already called the
		// destructor before returning memory to the allocator.
		operator delete(p);
	}

	// Allocators are stateless and always equal
	// Using defaulted equality operator for C++20
	constexpr bool operator==(const LockfreePoolingAllocator&) const noexcept = default;
};

#endif // FS_LOCKFREE_H
