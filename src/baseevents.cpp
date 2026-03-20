// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "baseevents.h"

#include "logger.h"
#include "pugicast.h"
#include "tools.h"

#include <fmt/format.h>

extern LuaEnvironment g_luaEnvironment;

bool BaseEvents::loadFromXml()
{
	if (loaded) {
		LOG_ERROR("[Error - BaseEvents::loadFromXml] It's already loaded.");
		return false;
	}

	auto scriptsName = std::string{getScriptBaseName()};
	std::string basePath = "data/" + scriptsName + "/";
	getScriptInterface().loadFile(basePath + "lib/" + scriptsName + ".lua");

	std::string filename = basePath + scriptsName + ".xml";

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.c_str());
	if (!result) {
		loaded = true;
		return true;
	}

	loaded = true;

	for (const auto& node : doc.child(scriptsName.c_str()).children()) {
		Event_ptr event = getEvent(node.name());
		if (!event) {
			continue;
		}

		if (!event->configureEvent(node)) {
			std::string warningMsg =
			    fmt::format("[Warning - BaseEvents::loadFromXml] Failed to configure event: {}", node.name());
			if (node.attribute("name")) {
				warningMsg += fmt::format(" (name: {})", node.attribute("name").as_string());
			}
			if (node.attribute("class")) {
				warningMsg += fmt::format(" (class: {})", node.attribute("class").as_string());
			}
			if (node.attribute("method")) {
				warningMsg += fmt::format(" (method: {})", node.attribute("method").as_string());
			}
			LOG_WARN(warningMsg);
			continue;
		}

		bool success;

		pugi::xml_attribute scriptAttribute = node.attribute("script");
		if (scriptAttribute) {
			std::string scriptFile = fmt::format("scripts/{}", scriptAttribute.as_string());
			success = event->checkScript(basePath, scriptsName, scriptFile) && event->loadScript(basePath + scriptFile);
			if (node.attribute("function")) {
				event->loadFunction(node.attribute("function"), true);
			}
		} else {
			success = event->loadFunction(node.attribute("function"), false);
		}

		if (success) {
			registerEvent(std::move(event), node);
		}
	}
	return true;
}

bool BaseEvents::reload()
{
	loaded = false;
	clear(false);
	return loadFromXml();
}

void BaseEvents::reInitState(bool fromLua)
{
	if (!fromLua) {
		getScriptInterface().reInitState();
	}
}

Event::Event(LuaScriptInterface* interface) : scriptInterface(interface) {}

bool Event::checkScript(std::string_view basePath, std::string_view scriptsName, std::string_view scriptFile) const
{
	LuaScriptInterface* testInterface = g_luaEnvironment.getTestInterface();
	testInterface->reInitState();

	if (testInterface->loadFile(fmt::format("{}lib/{}.lua", basePath, scriptsName)) == -1) {
		LOG_WARN(fmt::format("[Warning - Event::checkScript] Can not load {} lib/{}.lua", scriptsName, scriptsName));
	}

	if (scriptId != 0) {
		LOG_ERROR(fmt::format("[Failure - Event::checkScript] scriptid = {}", scriptId));
		return false;
	}

	if (testInterface->loadFile(fmt::format("{}{}", basePath, scriptFile)) == -1) {
		LOG_WARN(fmt::format("[Warning - Event::checkScript] Can not load script: {}", scriptFile));
		LOG_WARN(testInterface->getLastLuaError());
		return false;
	}

	int32_t id = testInterface->getEvent(getScriptEventName());
	if (id == -1) {
		LOG_WARN(
		    fmt::format("[Warning - Event::checkScript] Event {} not found. {}", getScriptEventName(), scriptFile));
		return false;
	}
	return true;
}

bool Event::loadScript(std::string_view scriptFile)
{
	if (!scriptInterface || scriptId != 0) {
		LOG_ERROR(fmt::format("Failure: [Event::loadScript] scriptInterface == nullptr. scriptid = {}", scriptId));
		return false;
	}

	if (scriptInterface->loadFile(scriptFile) == -1) {
		LOG_WARN(fmt::format("[Warning - Event::loadScript] Can not load script. {}", scriptFile));
		LOG_WARN(scriptInterface->getLastLuaError());
		return false;
	}

	int32_t id = scriptInterface->getEvent(getScriptEventName());
	if (id == -1) {
		LOG_WARN(fmt::format("[Warning - Event::loadScript] Event {} not found. {}", getScriptEventName(), scriptFile));
		return false;
	}

	scripted = true;
	scriptId = id;
	return true;
}

bool Event::loadCallback()
{
	if (!scriptInterface || scriptId != 0) {
		LOG_ERROR(fmt::format("Failure: [Event::loadCallback] scriptInterface == nullptr. scriptid = {}", scriptId));
		return false;
	}

	int32_t id = scriptInterface->getEvent();
	if (id == -1) {
		LOG_WARN(fmt::format("[Warning - Event::loadCallback] Event {} not found.", getScriptEventName()));
		return false;
	}

	scripted = true;
	scriptId = id;
	return true;
}

bool CallBack::loadCallBack(LuaScriptInterface* interface, std::string_view name)
{
	if (!interface) {
		LOG_ERROR(fmt::format("Failure: [CallBack::loadCallBack] scriptInterface == nullptr"));
		return false;
	}

	scriptInterface = interface;

	int32_t id = scriptInterface->getEvent(name);
	if (id == -1) {
		LOG_WARN(fmt::format("[Warning - CallBack::loadCallBack] Event {} not found.", name));
		return false;
	}

	scriptId = id;
	loaded = true;
	return true;
}

bool CallBack::loadCallBack(LuaScriptInterface* interface)
{
	if (!interface) {
		LOG_ERROR("Failure: [CallBack::loadCallBack] scriptInterface == nullptr");
		return false;
	}

	scriptInterface = interface;

	int32_t id = scriptInterface->getEvent();
	if (id == -1) {
		LOG_WARN("[Warning - CallBack::loadCallBack] Event not found.");
		return false;
	}

	scriptId = id;
	loaded = true;
	return true;
}
