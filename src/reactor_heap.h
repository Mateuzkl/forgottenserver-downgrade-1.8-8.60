// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_REACTOR_HEAP_H
#define FS_REACTOR_HEAP_H

#include <bit>
#include <cstddef>

namespace reactor_detail {

// Compare incremental worst-case work k * h with linear work N + k. For
// 0 < k < N, bit_width(N) bounds the height of the final heap. Division avoids
// overflow in both the product and N + k; no absolute batch-size cutoff.
constexpr bool shouldRebuildHeap(size_t existing, size_t incoming) noexcept
{
	if (incoming == 0) return false;
	if (existing == 0 || incoming >= existing) return true;
	const auto levels = std::bit_width(existing) - 1;
	return levels > 0 && incoming > existing / levels;
}

} // namespace reactor_detail

#endif // FS_REACTOR_HEAP_H
