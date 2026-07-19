// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_STARTUP_PROGRESS_H
#define FS_STARTUP_PROGRESS_H

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

enum class StartupStage : uint8_t {
	CONFIGURATION,
	RUNTIME,
	DATABASE,
	GAME_DATA,
	SCRIPT_SYSTEMS,
	SPELL_SCRIPTS,
	MONSTER_SCRIPTS,
	LUA_SCRIPTS,
	NPC_SCRIPTS,
	FINAL_GAME_DATA,
	MAP_DATA,
	MAP_AUXILIARY,
	GAME_INITIALIZATION,
	SERVICES,
	COUNT
};

struct StartupStageSnapshot {
	std::string_view name;
	uint8_t weight;
	double seconds;
	bool completed;
};

class StartupProgress final
{
public:
	void configure(bool progressBar, bool detailedLogs, bool consoleColors);
	void start();
	void begin(StartupStage stage, std::string_view detail = {});
	void update(size_t completed, size_t total, std::string_view detail = {});
	void updateFraction(double fraction, std::string_view detail = {});
	void complete(std::string_view detail = {});
	void finish();
	void fail(std::string_view reason);

	bool isActive() const;
	bool isCurrentStage(StartupStage stage) const;
	bool detailedLogs() const;
	bool colorsEnabled() const;
	bool outputIsTerminal() const;
	int percentage() const;
	double elapsedSeconds() const;
	std::array<StartupStageSnapshot, static_cast<size_t>(StartupStage::COUNT)> snapshot() const;

private:
	enum class VisualPhase : uint8_t {
		SCRIPTS,
		MAP,
	};

	struct StageState {
		std::string_view name;
		uint8_t weight;
		double seconds = 0.0;
		bool completed = false;
	};

	void completeCurrentLocked(std::string_view detail);
	void startVisualPhaseLocked(VisualPhase phase);
	void renderVisualLocked(bool force = false);
	bool isVisualStageLocked() const;
	double visualPercentageLocked() const;
	double percentageLocked() const;

	mutable std::mutex mutex;
	std::array<StageState, static_cast<size_t>(StartupStage::COUNT)> stages{{
	    {"Loading configuration", 5},
	    {"Preparing runtime", 4},
	    {"Connecting database", 12},
	    {"Loading game data", 12},
	    {"Loading script systems", 8},
	    {"Loading spell scripts", 5},
	    {"Loading monster scripts", 5},
	    {"Loading Lua scripts", 5},
	    {"Loading NPC scripts", 3},
	    {"Finalizing game data", 2},
	    {"Loading OTBM map data", 20},
	    {"Loading houses and spawns", 7},
	    {"Initializing game state", 7},
	    {"Starting network services", 5},
	}};

	bool active = false;
	bool displayEnabled = true;
	bool detailed = false;
	bool terminal = false;
	bool colors = false;
	bool failed = false;
	int currentStage = -1;
	double currentFraction = 0.0;
	double completedWeight = 0.0;
	VisualPhase visualPhase = VisualPhase::SCRIPTS;
	int lastVisualPercent = -1;
	bool consoleLineOpen = false;
	std::chrono::steady_clock::time_point startupStart;
	std::chrono::steady_clock::time_point stageStart;
};

StartupProgress& startupProgress();

#endif // FS_STARTUP_PROGRESS_H
