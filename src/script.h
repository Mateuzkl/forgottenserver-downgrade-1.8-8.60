// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_SCRIPTS_H
#define FS_SCRIPTS_H

#include "enums.h"
#include "luascript.h"

#include <set>
#include <string>

class Scripts
{
public:
	Scripts();
	~Scripts();

	bool loadScripts(const std::string& folderName, bool isLib, bool reload);
	LuaScriptInterface& getScriptInterface() { return scriptInterface; }

	void clearLoadedFiles() { loadedFiles.clear(); }

private:
	LuaScriptInterface scriptInterface;
	std::set<std::string> loadedFiles;
};

#endif
