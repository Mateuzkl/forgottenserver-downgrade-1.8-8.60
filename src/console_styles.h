// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_CONSOLE_STYLES_H
#define FS_CONSOLE_STYLES_H

#include <fmt/color.h>

namespace ConsoleStyle {

inline const auto cyan_b = fmt::fg(fmt::color::cyan) | fmt::emphasis::bold;
inline const auto green_b = fmt::fg(fmt::color::lime_green) | fmt::emphasis::bold;
inline const auto white_b = fmt::fg(fmt::color::white) | fmt::emphasis::bold;
inline const auto gray = fmt::fg(fmt::color::gray);
inline const auto dark_gray = fmt::fg(fmt::color::dim_gray);
inline const auto red_b = fmt::fg(fmt::color::orange_red) | fmt::emphasis::bold;

} // namespace ConsoleStyle

#endif
