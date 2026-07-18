// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "startup_progress.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fmt/format.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
bool stdoutIsTerminal()
{
#ifdef _WIN32
	return _isatty(_fileno(stdout)) != 0;
#else
	return isatty(fileno(stdout)) != 0;
#endif
}

#ifdef _WIN32
bool enableVirtualTerminalProcessing()
{
	HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
	if (output == nullptr || output == INVALID_HANDLE_VALUE) {
		return false;
	}

	DWORD mode = 0;
	if (GetConsoleMode(output, &mode) == 0) {
		return false;
	}
	return SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}
#endif
} // namespace

void StartupProgress::configure(bool progressBar, bool detailedLogs, bool consoleColors)
{
	std::scoped_lock lock(mutex);
	displayEnabled = progressBar;
	detailed = detailedLogs;
	terminal = stdoutIsTerminal();
#ifdef _WIN32
	colors = consoleColors && terminal && enableVirtualTerminalProcessing();
#else
	colors = consoleColors && terminal;
#endif
	active = true;
	failed = false;
	currentStage = -1;
	currentFraction = 0.0;
	completedWeight = 0.0;
	visualPhase = VisualPhase::SCRIPTS;
	lastVisualPercent = -1;
	consoleLineOpen = false;
	startupStart = std::chrono::steady_clock::now();
	for (auto& stage : stages) {
		stage.seconds = 0.0;
		stage.completed = false;
	}
}

void StartupProgress::start()
{
	std::scoped_lock lock(mutex);
	if (!active || failed) {
		return;
	}
	startupStart = std::chrono::steady_clock::now();
}

void StartupProgress::begin(StartupStage stage, std::string_view)
{
	std::scoped_lock lock(mutex);
	if (!active || failed) {
		return;
	}

	const int nextStage = static_cast<int>(stage);
	if (nextStage < 0 || nextStage >= static_cast<int>(stages.size())) {
		return;
	}

	if (currentStage >= 0 && currentStage != nextStage) {
		completeCurrentLocked({});
	}
	currentStage = nextStage;
	currentFraction = 0.0;
	stageStart = std::chrono::steady_clock::now();
	if (stage == StartupStage::SCRIPT_SYSTEMS) {
		startVisualPhaseLocked(VisualPhase::SCRIPTS);
	} else if (stage == StartupStage::MAP_DATA) {
		startVisualPhaseLocked(VisualPhase::MAP);
	} else if (isVisualStageLocked()) {
		renderVisualLocked();
	}
}

void StartupProgress::update(size_t completed, size_t total, std::string_view detail)
{
	updateFraction(total == 0 ? 1.0 : static_cast<double>(std::min(completed, total)) / static_cast<double>(total), detail);
}

void StartupProgress::updateFraction(double fraction, std::string_view)
{
	std::scoped_lock lock(mutex);
	if (!active || failed || currentStage < 0) {
		return;
	}

	currentFraction = std::max(currentFraction, std::clamp(fraction, 0.0, 1.0));
	renderVisualLocked();
}

void StartupProgress::complete(std::string_view detail)
{
	std::scoped_lock lock(mutex);
	if (!active || failed || currentStage < 0) {
		return;
	}
	completeCurrentLocked(detail);
}

void StartupProgress::completeCurrentLocked(std::string_view)
{
	auto& stage = stages[static_cast<size_t>(currentStage)];
	if (!stage.completed) {
		stage.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - stageStart).count();
		stage.completed = true;
		completedWeight += stage.weight;
	}
	currentFraction = 0.0;
	renderVisualLocked();
	currentStage = -1;
}

void StartupProgress::finish()
{
	std::scoped_lock lock(mutex);
	if (!active || failed) {
		return;
	}
	if (currentStage >= 0) {
		completeCurrentLocked("ready");
	}
	completedWeight = 100.0;
	active = false;
}

void StartupProgress::fail(std::string_view)
{
	std::scoped_lock lock(mutex);
	if (!active || failed) {
		return;
	}
	failed = true;
	if (displayEnabled && terminal && consoleLineOpen) {
		fmt::print("\n");
		std::fflush(stdout);
		consoleLineOpen = false;
	}
	active = false;
}

void StartupProgress::startVisualPhaseLocked(VisualPhase phase)
{
	visualPhase = phase;
	lastVisualPercent = -1;
	if (!displayEnabled) {
		return;
	}

	if (terminal && consoleLineOpen) {
		fmt::print("\n");
		consoleLineOpen = false;
	}
	if (phase == VisualPhase::MAP) {
		fmt::print("\n");
	}

	if (phase == VisualPhase::SCRIPTS) {
		fmt::print("    ⚙  Loading scripts\n");
	} else {
		fmt::print("    ⚙  Loading map\n");
	}
	renderVisualLocked(true);
}

void StartupProgress::renderVisualLocked(bool force)
{
	if (!displayEnabled || !isVisualStageLocked()) {
		return;
	}

	const int percent = static_cast<int>(std::floor(visualPercentageLocked() + 0.0001));
	if (!force && percent == lastVisualPercent) {
		return;
	}
	lastVisualPercent = percent;

	constexpr size_t barWidth = 32;
	const size_t filled = std::min(barWidth, static_cast<size_t>((percent * static_cast<int>(barWidth)) / 100));
	std::string bar;
	bar.reserve(barWidth * 3);
	for (size_t i = 0; i < filled; ++i) {
		bar.append("█");
	}
	for (size_t i = filled; i < barWidth; ++i) {
		bar.append("░");
	}
	const std::string_view completed = percent == 100 ? " ✔" : "";

	if (terminal) {
		fmt::print("\r    [{}] {:3}%{}", bar, percent, completed);
		consoleLineOpen = percent != 100;
		if (!consoleLineOpen) {
			fmt::print("{}", visualPhase == VisualPhase::MAP ? "\n\n" : "\n");
		}
		std::fflush(stdout);
		return;
	}

	fmt::print("    [{}] {:3}%{}{}", bar, percent, completed,
	           percent == 100 && visualPhase == VisualPhase::MAP ? "\n\n" : "\n");
	std::fflush(stdout);
}

bool StartupProgress::isVisualStageLocked() const
{
	if (currentStage < 0) {
		return false;
	}

	const auto stage = static_cast<StartupStage>(currentStage);
	if (visualPhase == VisualPhase::SCRIPTS) {
		return stage >= StartupStage::SCRIPT_SYSTEMS && stage <= StartupStage::NPC_SCRIPTS;
	}
	return stage >= StartupStage::MAP_DATA && stage <= StartupStage::MAP_AUXILIARY;
}

double StartupProgress::visualPercentageLocked() const
{
	const auto contribution = [this](StartupStage first, StartupStage last) {
		double total = 0.0;
		double completed = 0.0;
		for (size_t i = static_cast<size_t>(first); i <= static_cast<size_t>(last); ++i) {
			const auto& stage = stages[i];
			total += stage.weight;
			if (stage.completed) {
				completed += stage.weight;
			} else if (currentStage == static_cast<int>(i)) {
				completed += stage.weight * currentFraction;
			}
		}
		return total == 0.0 ? 100.0 : (completed * 100.0) / total;
	};

	if (visualPhase == VisualPhase::SCRIPTS) {
		return contribution(StartupStage::SCRIPT_SYSTEMS, StartupStage::NPC_SCRIPTS);
	}
	return contribution(StartupStage::MAP_DATA, StartupStage::MAP_AUXILIARY);
}

double StartupProgress::percentageLocked() const
{
	double value = completedWeight;
	if (currentStage >= 0 && !stages[static_cast<size_t>(currentStage)].completed) {
		value += stages[static_cast<size_t>(currentStage)].weight * currentFraction;
	}
	return std::clamp(value, 0.0, 100.0);
}

bool StartupProgress::isActive() const
{
	std::scoped_lock lock(mutex);
	return active;
}

bool StartupProgress::isCurrentStage(StartupStage stage) const
{
	std::scoped_lock lock(mutex);
	return active && !failed && currentStage == static_cast<int>(stage);
}

bool StartupProgress::detailedLogs() const
{
	std::scoped_lock lock(mutex);
	return detailed;
}

bool StartupProgress::colorsEnabled() const
{
	std::scoped_lock lock(mutex);
	return colors;
}

bool StartupProgress::outputIsTerminal() const
{
	std::scoped_lock lock(mutex);
	return terminal;
}

int StartupProgress::percentage() const
{
	std::scoped_lock lock(mutex);
	return static_cast<int>(std::floor(percentageLocked() + 0.0001));
}

double StartupProgress::elapsedSeconds() const
{
	std::scoped_lock lock(mutex);
	if (startupStart == std::chrono::steady_clock::time_point{}) {
		return 0.0;
	}
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - startupStart).count();
}

std::array<StartupStageSnapshot, static_cast<size_t>(StartupStage::COUNT)> StartupProgress::snapshot() const
{
	std::scoped_lock lock(mutex);
	std::array<StartupStageSnapshot, static_cast<size_t>(StartupStage::COUNT)> result{};
	for (size_t i = 0; i < stages.size(); ++i) {
		result[i] = {stages[i].name, stages[i].weight, stages[i].seconds, stages[i].completed};
	}
	return result;
}

StartupProgress& startupProgress()
{
	static StartupProgress instance;
	return instance;
}
