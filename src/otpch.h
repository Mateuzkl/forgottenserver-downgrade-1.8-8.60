// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_OTPCH_H
#define FS_OTPCH_H

// Definitions should be global.
#include "definitions.h"

// System headers required in headers should be included here.
#include "lua.hpp"

#include <absl/container/flat_hash_map.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <asio.hpp>
#include <cassert>
#include <concepts>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fmt/chrono.h>
#include <fmt/color.h>
#include <fmt/format.h>
#include <forward_list>
#include <functional>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mio/mmap.hpp>
#include <mutex>
#include <mysql/mysql.h>
#include <optional>
#include <pugixml.hpp>
#include <random>
#include <set>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <valarray>
#include <variant>
#include <vector>
#include <span>
#include <ranges>
#include <semaphore>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <fstream>
#include <future>
#include <csignal>

#endif // FS_OTPCH_H
