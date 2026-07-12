// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "script.h"

#include "configmanager.h"

#include <fmt/color.h>
#include <fmt/ranges.h>
#include "logger.h"

extern LuaEnvironment g_luaEnvironment;

Scripts::Scripts() : scriptInterface("Scripts Interface") { scriptInterface.initState(); }

Scripts::~Scripts() { scriptInterface.reInitState(); }

void Scripts::clearLoadedFiles(const std::string& folderName)
{
	namespace fs = std::filesystem;

	const auto dir = fs::current_path() / "data" / folderName;
	if (!fs::exists(dir) || !fs::is_directory(dir)) {
		return;
	}

	const std::string canonicalDir = fs::canonical(dir).string();
	const std::string prefix = canonicalDir + std::string(1, fs::path::preferred_separator);
	std::erase_if(loadedFiles, [&prefix](const std::string& loadedFile) {
		return loadedFile.starts_with(prefix);
	});
	discoveredFiles.clear();
}

const std::vector<Scripts::DiscoveredFile>& Scripts::getDiscoveredFiles(const std::filesystem::path& directory)
{
	namespace fs = std::filesystem;

	const std::string key = directory.generic_string();
	if (const auto it = discoveredFiles.find(key); it != discoveredFiles.end()) {
		return it->second;
	}

	auto [cacheIt, inserted] = discoveredFiles.try_emplace(key);
	try {
		auto& files = cacheIt->second;
		for (fs::recursive_directory_iterator entry(directory), end; entry != end; ++entry) {
			if (fs::is_regular_file(*entry) && entry->path().extension() == ".lua") {
				files.push_back({entry->path(), fs::canonical(entry->path()).string()});
			}
		}

		std::sort(files.begin(), files.end(), [](const DiscoveredFile& left, const DiscoveredFile& right) {
			return left.path < right.path;
		});
		return files;
	} catch (...) {
		if (inserted) {
			discoveredFiles.erase(cacheIt);
		}
		throw;
	}
}

bool Scripts::loadScripts(const std::string& folderName, bool isLib, bool reload)
{
	namespace fs = std::filesystem;

	const auto dir = fs::current_path() / "data" / folderName;
	if (!fs::exists(dir) || !fs::is_directory(dir)) {
		LOG_WARN(fmt::format("[Warning - Scripts::loadScripts] Can not load folder '{}'.", folderName));
		return false;
	}

	const auto discoveryDir = folderName.starts_with("scripts/") ? (fs::current_path() / "data" / "scripts") : dir;
	const auto discoveryStart = std::chrono::steady_clock::now();
	const auto& discovered = getDiscoveredFiles(discoveryDir);
	const auto discoveryElapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - discoveryStart).count();

	bool scriptsConsoleLogs = getBoolean(ConfigManager::SCRIPTS_CONSOLE_LOGS);
	std::vector<std::string> disabled = {}, loaded = {}, reloaded = {};
	std::vector<std::pair<fs::path, std::string>> v;
	v.reserve(discovered.size());
	static constexpr std::string_view disable = "#";
	for (const auto& file : discovered) {
		const fs::path relative = file.path.lexically_relative(dir);
		if (relative.empty() || relative.begin()->string() == "..") {
			continue;
		}
		const std::string topLevel = (relative.begin() != relative.end()) ? (*relative.begin()).string() : "";
		if ((topLevel == "lib" && !isLib) || topLevel == "events" ||
		    (topLevel == "chatchannels" && folderName != "scripts/chatchannels")) {
			continue;
		}

		const auto filename = file.path.filename().string();
		if (filename.find(disable) != std::string::npos) {
			if (scriptsConsoleLogs) {
				disabled.push_back(fmt::format("\"{}\"", fmt::format(fg(fmt::color::yellow), "{}",
				                                                  std::string_view(filename.data(), filename.size() - 4))));
			}
			continue;
		}
		if (!loadedFiles.contains(file.canonicalPath)) {
			v.emplace_back(file.path, file.canonicalPath);
		}
	}

	const auto executionStart = std::chrono::steady_clock::now();
	for (auto& [path, canonical] : v) {
		const std::string scriptFile = path.string();
		if (scriptInterface.loadFile(scriptFile) == -1) {
			LOG_ERROR(fmt::format("> {} [error]", path.filename().string()));
			LOG_ERROR(fmt::format("^ {}", scriptInterface.getLastLuaError()));
			continue;
		}

		loadedFiles.insert(std::move(canonical));

		if (scriptsConsoleLogs) {
			const auto& scrName = path.filename().string();
			if (!reload) {
				loaded.push_back(fmt::format(
				    "\"{}\"",
				    fmt::format(fg(fmt::color::green), "{}", std::string_view(scrName.data(), scrName.size() - 4))));
			} else {
				reloaded.push_back(fmt::format(
				    "\"{}\"",
				    fmt::format(fg(fmt::color::green), "{}", std::string_view(scrName.data(), scrName.size() - 4))));
			}
		}
	}
	g_logger().info(">> Script phase '{}': {:.3f} s discovery, {:.3f} s execution ({} files).", folderName,
	                discoveryElapsed, std::chrono::duration<double>(std::chrono::steady_clock::now() - executionStart).count(), v.size());

	if (scriptsConsoleLogs) {
		if (!disabled.empty()) {
			LOG_INFO(fmt::format("{{{}}}", fmt::join(disabled, ", ")));
		}

		if (!loaded.empty()) {
			LOG_INFO(fmt::format("{{{}}}", fmt::join(loaded, ", ")));
		}

		if (!reloaded.empty()) {
			LOG_INFO(fmt::format("{{{}}}", fmt::join(reloaded, ", ")));
		}
	}

	return true;
}
