// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "admin_console.h"

#include "game.h"
#include "logger.h"
#include "otserv.h"
#include "tasks.h"
#include "tools.h"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif

namespace {

bool isInteractiveInput()
{
#ifdef _WIN32
	return _isatty(_fileno(stdin)) != 0;
#else
	return isatty(STDIN_FILENO) != 0;
#endif
}

std::string_view gameStateName(GameState_t state)
{
	switch (state) {
		case GAME_STATE_STARTUP:
			return "startup";
		case GAME_STATE_INIT:
			return "initializing";
		case GAME_STATE_NORMAL:
			return "online";
		case GAME_STATE_CLOSED:
			return "closed";
		case GAME_STATE_SHUTDOWN:
			return "shutting down";
		case GAME_STATE_CLOSING:
			return "closing";
		case GAME_STATE_MAINTAIN:
			return "maintenance";
	}
	return "unknown";
}

void printHelp()
{
	LOG_INFO("Console commands: help, status, players, save (S), restart (R), shutdown (Q)");
}

} // namespace

AdminConsole::~AdminConsole()
{
	stop();
}

bool AdminConsole::start()
{
	if (!isInteractiveInput() || running.exchange(true, std::memory_order_acq_rel)) {
		return false;
	}

#ifdef _WIN32
	const HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
	DWORD inputMode = 0;
	if (inputHandle != nullptr && inputHandle != INVALID_HANDLE_VALUE &&
	    GetConsoleMode(inputHandle, &inputMode) != 0) {
		originalInputMode = inputMode;
		const DWORD interactiveMode = inputMode | ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT;
		restoreInputMode = interactiveMode != inputMode && SetConsoleMode(inputHandle, interactiveMode) != 0;
	}
#endif

	inputThread = std::jthread([this](std::stop_token stopToken) { run(stopToken); });
	LOG_INFO("Interactive console enabled. Type 'help' and press ENTER to list commands.");
	return true;
}

void AdminConsole::stop()
{
	[[maybe_unused]] const bool wasRunning = running.exchange(false, std::memory_order_acq_rel);

	if (inputThread.joinable()) {
		inputThread.request_stop();
#ifdef _WIN32
		if (wasRunning) {
			// Queue an empty line so a pending std::getline returns immediately.
			// This also closes the race where cancellation happens just before getline starts.
			const HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
			if (inputHandle != nullptr && inputHandle != INVALID_HANDLE_VALUE) {
				INPUT_RECORD enterRecords[2]{};
				for (INPUT_RECORD& record : enterRecords) {
					record.EventType = KEY_EVENT;
					record.Event.KeyEvent.wRepeatCount = 1;
					record.Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
					record.Event.KeyEvent.uChar.UnicodeChar = L'\r';
				}
				enterRecords[0].Event.KeyEvent.bKeyDown = TRUE;
				enterRecords[1].Event.KeyEvent.bKeyDown = FALSE;
				DWORD recordsWritten = 0;
				if (WriteConsoleInputW(inputHandle, enterRecords, 2, &recordsWritten) == 0 || recordsWritten != 2) {
					CancelSynchronousIo(inputThread.native_handle());
				}
			}
		}
#endif
		inputThread.join();
	}

#ifdef _WIN32
	if (restoreInputMode) {
		const HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
		if (inputHandle != nullptr && inputHandle != INVALID_HANDLE_VALUE) {
			SetConsoleMode(inputHandle, originalInputMode);
		}
		restoreInputMode = false;
	}
#endif
}

void AdminConsole::run(std::stop_token stopToken)
{
#ifdef _WIN32
	const HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
	if (inputHandle == nullptr || inputHandle == INVALID_HANDLE_VALUE) {
		running.store(false, std::memory_order_release);
		return;
	}
#endif

	while (!stopToken.stop_requested()) {
#ifndef _WIN32
		pollfd inputPoll{STDIN_FILENO, POLLIN, 0};
		const int pollResult = poll(&inputPoll, 1, 200);
		if (pollResult < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		if (pollResult == 0) {
			continue;
		}
		if ((inputPoll.revents & (POLLIN | POLLHUP)) == 0) {
			break;
		}
#endif

		std::string command;
		if (!std::getline(std::cin, command)) {
			break;
		}
		if (stopToken.stop_requested()) {
			break;
		}
		processCommand(std::move(command));
	}

	running.store(false, std::memory_order_release);
}

void AdminConsole::processCommand(std::string command) const
{
	trimString(command);
	toLowerCaseString(command);
	if (command.empty()) {
		return;
	}

	if (g_game.getGameState() == GAME_STATE_SHUTDOWN) {
		return;
	}

	if (command == "help" || command == "h") {
		printHelp();
		return;
	}

	if (command == "status") {
		g_dispatcher.addTask([]() {
			const GameState_t state = g_game.getGameState();
			if (state != GAME_STATE_SHUTDOWN) {
				LOG_INFO("Server status: {} | Players online: {}", gameStateName(state), g_game.getPlayersOnline());
			}
		});
		return;
	}

	if (command == "players") {
		g_dispatcher.addTask([]() {
			if (g_game.getGameState() == GAME_STATE_SHUTDOWN) {
				return;
			}

			auto players = g_game.getPlayers();
			std::vector<std::string> names;
			names.reserve(players.size());
			for (const auto& player : players) {
				names.emplace_back(player->getName());
			}
			std::ranges::sort(names);

			std::string playerList;
			for (const std::string& name : names) {
				if (!playerList.empty()) {
					playerList += ", ";
				}
				playerList += name;
			}
			LOG_INFO("Online players ({}): {}", names.size(), playerList.empty() ? "none" : playerList);
		});
		return;
	}

	if (command == "save" || command == "s") {
		g_dispatcher.addTask([]() {
			const GameState_t state = g_game.getGameState();
			if (state != GAME_STATE_NORMAL && state != GAME_STATE_CLOSED) {
				LOG_WARN("Console save ignored while the server is {}.", gameStateName(state));
				return;
			}

			LOG_INFO("Console save requested.");
			g_game.saveGameState();
			LOG_INFO("Console save completed.");
		});
		return;
	}

	if (command == "restart" || command == "r") {
		g_dispatcher.addTask([]() {
			if (g_game.getGameState() == GAME_STATE_SHUTDOWN) {
				return;
			}
			LOG_INFO("Console restart requested. The server will shut down safely and relaunch.");
			requestServerRestart();
			g_game.setGameState(GAME_STATE_SHUTDOWN);
		});
		return;
	}

	if (command == "shutdown" || command == "q") {
		g_dispatcher.addTask([]() {
			if (g_game.getGameState() == GAME_STATE_SHUTDOWN) {
				return;
			}
			LOG_INFO("Console shutdown requested.");
			g_game.setGameState(GAME_STATE_SHUTDOWN);
		});
		return;
	}

	LOG_WARN("Unknown console command '{}'. Type 'help' to list commands.", command);
}
