// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.
// Modernized for C++20 standard with bug fix for block allocations

#ifndef FS_LOCKFREE_H
#define FS_LOCKFREE_H

#include <boost/lockfree/stack.hpp>
#include <cstddef>

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
	using FreeList = boost::lockfree::stack<void*, boost::lockfree::capacity<CAPACITY>>;

	[[nodiscard]] static FreeList& get() noexcept
	{
		static FreeList freeList;
		return freeList;
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
	template <typename U>
	struct rebind
	{
		using other = LockfreePoolingAllocator<U, CAPACITY>;
	};

	constexpr LockfreePoolingAllocator() noexcept = default;
	constexpr LockfreePoolingAllocator(const LockfreePoolingAllocator&) noexcept = default;

	template <typename U>
	constexpr LockfreePoolingAllocator(const LockfreePoolingAllocator<U, CAPACITY>&) noexcept
	{}

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
	[[nodiscard]] T* allocate(size_type n) const
	{
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

		// Pool is empty, allocate new memory without calling constructor
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
	void deallocate(T* p, size_type n) const noexcept
	{
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

		// Pool is full, release memory without calling destructor
		// (destructor has already been called at this point)
		operator delete(p);
	}

	// Allocators are stateless and always equal
	// Using defaulted equality operator for C++20
	constexpr bool operator==(const LockfreePoolingAllocator&) const noexcept = default;
};

#endif // FS_LOCKFREE_H
