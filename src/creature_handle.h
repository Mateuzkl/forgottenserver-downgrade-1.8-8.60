// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_CREATURE_HANDLE_H
#define FS_CREATURE_HANDLE_H

#include <compare>
#include <cstdint>

struct CreatureHandle
{
	uint32_t id = 0;
	uint64_t generation = 0;

	[[nodiscard]] constexpr bool valid() const noexcept { return id != 0 && generation != 0; }
	auto operator<=>(const CreatureHandle&) const = default;
};

#endif // FS_CREATURE_HANDLE_H
