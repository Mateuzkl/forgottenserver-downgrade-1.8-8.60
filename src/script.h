// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_SCRIPTS_H
#define FS_SCRIPTS_H

#include "enums.h"
#include "luascript.h"

#include <filesystem>
#include <unordered_map>
#include <unordered_set>

class Scripts
{
public:
	Scripts();
	~Scripts();

	bool loadScripts(const std::string& folderName, bool isLib, bool reload);
	LuaScriptInterface& getScriptInterface() { return scriptInterface; }
	bool isFileLoaded(const std::filesystem::path& file) const;
	size_t getLoadedFileCount() const { return loadedFiles.size(); }

	void clearLoadedFiles()
	{
		loadedFiles.clear();
		discoveredFiles.clear();
	}
	void clearLoadedFiles(const std::string& folderName);

private:
	struct DiscoveredFile {
		std::filesystem::path path;
		std::string canonicalPath;
	};

	const std::vector<DiscoveredFile>& getDiscoveredFiles(const std::filesystem::path& directory);

	LuaScriptInterface scriptInterface;
	std::unordered_set<std::string> loadedFiles;
	std::unordered_map<std::string, std::vector<DiscoveredFile>> discoveredFiles;
};

#endif
