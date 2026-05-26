// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_BASEEVENTS_H
#define FS_BASEEVENTS_H

#include "luascript.h"

class Event;
using Event_ptr = std::unique_ptr<Event>;

// Non-owning observer pointer. The LuaScriptInterface lifetime is managed by
// the owning event registry, not by Event or CallBack instances. Do not store
// or capture this across async/scheduler boundaries without an external
// lifetime guard.
template <typename T>
using ObserverPtr = T*;

class Event
{
public:
	explicit Event(ObserverPtr<LuaScriptInterface> interface);
	virtual ~Event() = default;

	bool loadCallback();
	bool loadScript(const std::string& scriptFile);

	bool isScripted() const { return scripted; }

	bool scripted = false;
	bool fromLua = false;
	bool fromItem = false;

	int32_t getScriptId() const { return scriptId; }

protected:
	virtual std::string_view getScriptEventName() const = 0;

	int32_t scriptId = 0;
	ObserverPtr<LuaScriptInterface> scriptInterface = nullptr;
};

class BaseEvents
{
public:
	constexpr BaseEvents() = default;
	virtual ~BaseEvents() = default;

	bool loadFromXml();
	bool reload();
	bool isLoaded() const { return loaded; }
	void reInitState(bool fromLua);

private:
	virtual LuaScriptInterface& getScriptInterface() = 0;
	virtual std::string_view getScriptBaseName() const = 0;
	virtual void clear(bool) = 0;

	bool loaded = false;
};

class CallBack
{
public:
	CallBack() = default;

	bool loadCallBack(ObserverPtr<LuaScriptInterface> interface, std::string_view name);
	bool loadCallBack(ObserverPtr<LuaScriptInterface> interface);

protected:
	int32_t scriptId = 0;
	ObserverPtr<LuaScriptInterface> scriptInterface = nullptr;

private:
	bool loaded = false;
};

#endif
