// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_MAP_CONSTANTS_H
#define FS_MAP_CONSTANTS_H

#include <cstdint>

namespace MapConstants {
inline constexpr int32_t maxViewportX = 11; // min value: maxClientViewportX + 1
inline constexpr int32_t maxViewportY = 11; // min value: maxClientViewportY + 1
inline constexpr int32_t maxClientViewportX = 8;
inline constexpr int32_t maxClientViewportY = 6;
}

#endif
