// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "signals.h"

#include "actions.h"
#include "configmanager.h"
#include "databasetasks.h"
#include "events.h"
#include "game.h"
#include "globalevent.h"
#include "logger.h"
#include "monster.h"
#include "movement.h"
#include "raids.h"
#include "scheduler.h"
#include "spells.h"
#include "talkaction.h"
#include "tasks.h"
#include "weapons.h"

#include <csignal>
#include <fmt/format.h>

extern Scheduler g_scheduler;
extern DatabaseTasks g_databaseTasks;
extern Dispatcher g_dispatcher;

extern Actions* g_actions;
extern Monsters g_monsters;
extern TalkActions* g_talkActions;
extern MoveEvents* g_moveEvents;
extern Spells* g_spells;
extern Weapons* g_weapons;
extern Game g_game;
extern CreatureEvents* g_creatureEvents;
extern GlobalEvents* g_globalEvents;
extern Events* g_events;
extern Chat* g_chat;
extern LuaEnvironment g_luaEnvironment;

namespace {

[[maybe_unused]] void sigbreakHandler()
{
	// Dispatcher thread
	// Dispatcher thread
	LOG_INFO(">> SIGBREAK received, shutting game server down...");
	g_game.setGameState(GAME_STATE_SHUTDOWN);
}

void sigtermHandler()
{
	// Dispatcher thread
	LOG_INFO(">> SIGTERM received, shutting game server down...");
	LOG_INFO(">> Saving game state before shutdown...");
	g_game.setGameState(GAME_STATE_SHUTDOWN);
}

void sigusr1Handler()
{
	// Dispatcher thread
	LOG_INFO("SIGUSR1 received, saving the game state...");
	g_globalEvents->save();
	g_game.saveGameState();
}

void sighupHandler()
{
	// Dispatcher thread
	LOG_INFO("SIGHUP received, reloading config files...");

	g_actions->reload();
	LOG_INFO("Reloaded actions.");

	ConfigManager::load();
	LOG_INFO("Reloaded config.");

	g_creatureEvents->reload();
	LOG_INFO("Reloaded creature scripts.");

	g_moveEvents->reload();
	LOG_INFO("Reloaded movements.");

	Npcs::reload();
	LOG_INFO("Reloaded npcs.");

	g_game.raids.reload();
	g_game.raids.startup();
	LOG_INFO("Reloaded raids.");

	g_monsters.reload();
	LOG_INFO("Reloaded monsters.");

	g_spells->reload();
	LOG_INFO("Reloaded spells.");

	g_talkActions->reload();
	LOG_INFO("Reloaded talk actions.");

	Item::items.reload();
	LOG_INFO("Reloaded items.");

	g_weapons->reload();
	g_weapons->loadDefaults();
	LOG_INFO("Reloaded weapons.");

	g_globalEvents->reload();
	LOG_INFO("Reloaded globalevents.");

	g_events->load();
	LOG_INFO("Reloaded events.");

	g_chat->load();
	LOG_INFO("Reloaded chatchannels.");

	g_luaEnvironment.loadFile("data/global.lua");
	LOG_INFO("Reloaded global.lua.");

	lua_gc(g_luaEnvironment.getLuaState(), LUA_GCCOLLECT, 0);
}

void sigintHandler()
{
	// Dispatcher thread
	LOG_INFO(">> SIGINT received, shutting game server down...");
	LOG_INFO(">> Saving game state before shutdown...");
	g_game.setGameState(GAME_STATE_SHUTDOWN);
}

// On Windows this function does not need to be signal-safe,
// as it is called in a new thread.
// https://github.com/otland/forgottenserver/pull/2473
void dispatchSignalHandler(int signal)
{
	switch (signal) {
		case SIGINT: // Shuts the server down
			g_dispatcher.addTask(sigintHandler);
			break;
		case SIGTERM: // Shuts the server down
			g_dispatcher.addTask(sigtermHandler);
			break;
#ifndef _WIN32
		case SIGHUP: // Reload config/data
			g_dispatcher.addTask(sighupHandler);
			break;
		case SIGUSR1: // Saves game state
			g_dispatcher.addTask(sigusr1Handler);
			break;
#else
		case SIGBREAK: // Shuts the server down
			g_dispatcher.addTask(sigbreakHandler);
			// hold the thread until other threads end
			g_scheduler.join();
			g_databaseTasks.join();
			g_dispatcher.join();
#ifdef STATS_ENABLED
			g_stats.join();
#endif
			break;
#endif
		default:
			break;
	}
}

} // namespace

Signals::Signals(boost::asio::io_context& service) : set(service)
{
	set.add(SIGINT);
	set.add(SIGTERM);
#ifndef _WIN32
	set.add(SIGUSR1);
	set.add(SIGHUP);
#else
	// This must be a blocking call as Windows calls it in a new thread and terminates
	// the process when the handler returns (or after 5 seconds, whichever is earlier).
	// On Windows it is called in a new thread.
	signal(SIGBREAK, dispatchSignalHandler);
#endif

	asyncWait();
}

void Signals::asyncWait()
{
	set.async_wait([this](const boost::system::error_code& err, int signal) {
		if (err) {
			LOG_ERROR(fmt::format("Signal handling error: {}", err.message()));
			return;
		}
		dispatchSignalHandler(signal);
		asyncWait();
	});
}
