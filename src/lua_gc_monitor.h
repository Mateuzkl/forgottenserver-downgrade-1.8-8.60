// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_LUA_GC_MONITOR_H
#define FS_LUA_GC_MONITOR_H

#include "enums.h"

#include <string>
#include <string_view>

struct lua_State;

class LuaGcMonitor
{
public:
	static void configure(lua_State* L);
	static void fullCollect(lua_State* L, std::string_view reason = {});
	static void step(lua_State* L);
	static size_t getMemoryKb(lua_State* L);
	static double getMemoryMb(lua_State* L);

	static void logIfNeeded(lua_State* L);

	static std::string getModeName(int mode);

private:
	LuaGcMonitor() = delete;
};

#endif // FS_LUA_GC_MONITOR_H
