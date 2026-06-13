// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "lua_gc_monitor.h"

#include "configmanager.h"
#include "luascript.h"

static std::chrono::steady_clock::time_point lastLogTime;
static bool warnedMemory = false;
static bool criticalWarnedMemory = false;

void LuaGcMonitor::configure(lua_State* L)
{
	if (!L) {
		return;
	}

	const auto mode = ConfigManager::getString(ConfigManager::LUA_GC_MODE);
	int gcMode = LUA_GCGEN;
	if (mode == "incremental") {
		gcMode = LUA_GCINC;
	} else if (mode == "stopped") {
		gcMode = LUA_GCSTOP;
		lua_gc(L, LUA_GCSTOP, 0);
		return;
	}

	lua_gc(L, gcMode, 0);

	if (ConfigManager::getBoolean(ConfigManager::LUA_GC_AUTO_TUNE)) {
		if (gcMode == LUA_GCGEN) {
			lua_gc(L, LUA_GCPARAM, LUA_GCPMINORMUL, 200);
			lua_gc(L, LUA_GCPARAM, LUA_GCPMAJORMINOR, 100);
		} else if (gcMode == LUA_GCINC) {
			lua_gc(L, LUA_GCPARAM, LUA_GCPPAUSE, 100);
			lua_gc(L, LUA_GCPARAM, LUA_GCPSTEPMUL, 200);
		}
	}
}

void LuaGcMonitor::fullCollect(lua_State* L, std::string_view reason)
{
	if (!L) {
		return;
	}

	const auto beforeKb = getMemoryKb(L);
	lua_gc(L, LUA_GCCOLLECT, 0);
	const auto afterKb = getMemoryKb(L);

	if (reason.empty()) {
		LOG_INFO(fmt::format("Lua GC: full collect {} -> {} KB", beforeKb, afterKb));
	} else {
		LOG_INFO(fmt::format("Lua GC: full collect {} -> {} KB ({})", beforeKb, afterKb, reason));
	}
}

void LuaGcMonitor::step(lua_State* L)
{
	if (!L) {
		return;
	}

	lua_gc(L, LUA_GCSTEP, ConfigManager::getInteger(ConfigManager::LUA_GC_STEP_SIZE));
}

size_t LuaGcMonitor::getMemoryKb(lua_State* L)
{
	if (!L) {
		return 0;
	}

	return static_cast<size_t>(lua_gc(L, LUA_GCCOUNT, 0));
}

double LuaGcMonitor::getMemoryMb(lua_State* L)
{
	return static_cast<double>(getMemoryKb(L)) / 1024.0;
}

void LuaGcMonitor::logIfNeeded(lua_State* L)
{
	if (!L || !ConfigManager::getBoolean(ConfigManager::LUA_GC_LOG_ENABLED)) {
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	const auto interval = ConfigManager::getInteger(ConfigManager::LUA_GC_LOG_INTERVAL);
	if (interval > 0 && now - lastLogTime < std::chrono::milliseconds(interval)) {
		return;
	}

	lastLogTime = now;
	const auto memoryKb = getMemoryKb(L);
	const auto memoryMb = getMemoryMb(L);

	const auto warnKb = static_cast<size_t>(ConfigManager::getInteger(ConfigManager::LUA_GC_WARN_MEMORY_KB));
	const auto critKb = static_cast<size_t>(ConfigManager::getInteger(ConfigManager::LUA_GC_CRITICAL_MEMORY_KB));

	const bool underCritical = (critKb == 0 || memoryKb <= critKb);
	const bool overWarn = (warnKb > 0 && memoryKb > warnKb);

	if (!underCritical) {
		if (!criticalWarnedMemory) {
			LOG_WARN(fmt::format("Lua GC: CRITICAL memory usage {:.2f} MB (threshold: {} KB)", memoryMb, critKb));
			criticalWarnedMemory = true;
		}
	} else {
		criticalWarnedMemory = false;
	}

	if (underCritical && overWarn) {
		if (!warnedMemory) {
			LOG_WARN(fmt::format("Lua GC: high memory usage {:.2f} MB (threshold: {} KB)", memoryMb, warnKb));
			warnedMemory = true;
		}
	} else {
		warnedMemory = false;
	}

	if (underCritical && !overWarn) {
		LOG_INFO(fmt::format("Lua GC: memory {:.2f} MB ({} KB)", memoryMb, memoryKb));
	}
}

std::string LuaGcMonitor::getModeName(int mode)
{
	switch (mode) {
		case LUA_GCGEN:
			return "generational";
		case LUA_GCINC:
			return "incremental";
		case LUA_GCSTOP:
			return "stopped";
		default:
			return "unknown";
	}
}
