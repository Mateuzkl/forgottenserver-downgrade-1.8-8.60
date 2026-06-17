// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "chat.h"
#include "luascript.h"
#include "script.h"
#include "scriptmanager.h"

namespace {
using namespace Lua;

int luaCreateChatChannel(lua_State* L)
{
	// ChatChannel(id, name)
	if (LuaScriptInterface::getScriptEnv()->getScriptInterface() != &g_scripts->getScriptInterface()) {
		reportErrorFunc(L, "ChatChannels can only be registered in the Scripts interface.");
		lua_pushnil(L);
		return 1;
	}

	uint16_t channelId = getInteger<uint16_t>(L, 2);
	const std::string& channelName = getString(L, 3);
	auto channel = std::make_unique<ChatChannel>(channelId, channelName);
	channel->setScriptInterface(LuaScriptInterface::getScriptEnv()->getScriptInterface());
	pushOwnedUserdata<ChatChannel>(L, std::move(channel));
	setMetatable(L, -1, "ChatChannel");
	return 1;
}

int luaChatChannelPublic(lua_State* L)
{
	// chatChannel:public(value)
	if (ChatChannel* channel = getUserdata<ChatChannel>(L, 1)) {
		channel->setPublicChannel(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaChatChannelRegister(lua_State* L)
{
	// chatChannel:register()
	if (getUserdata<ChatChannel>(L, 1)) {
		pushBoolean(L, g_chat->registerLuaChannel(releaseOwnedUserdataPtr<ChatChannel>(L, 1)));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int loadChatChannelCallback(lua_State* L, std::string_view callback)
{
	ChatChannel* channel = getUserdata<ChatChannel>(L, 1);
	if (!channel) {
		lua_pushnil(L);
		return 1;
	}

	LuaScriptInterface* scriptInterface = LuaScriptInterface::getScriptEnv()->getScriptInterface();
	const int32_t event = scriptInterface->getEvent();
	if (event == -1) {
		pushBoolean(L, false);
		return 1;
	}

	if (callback == "canJoin") {
		channel->setCanJoinEvent(event);
	} else if (callback == "onJoin") {
		channel->setOnJoinEvent(event);
	} else if (callback == "onLeave") {
		channel->setOnLeaveEvent(event);
	} else if (callback == "onSpeak") {
		channel->setOnSpeakEvent(event);
	} else {
		pushBoolean(L, false);
		return 1;
	}

	pushBoolean(L, true);
	return 1;
}

int luaChatChannelCanJoin(lua_State* L)
{
	return loadChatChannelCallback(L, "canJoin");
}

int luaChatChannelOnJoin(lua_State* L)
{
	return loadChatChannelCallback(L, "onJoin");
}

int luaChatChannelOnLeave(lua_State* L)
{
	return loadChatChannelCallback(L, "onLeave");
}

int luaChatChannelOnSpeak(lua_State* L)
{
	return loadChatChannelCallback(L, "onSpeak");
}

int luaChatChannelNewIndex(lua_State* L)
{
	// chatChannel.callback = function(...)
	ChatChannel* channel = getUserdata<ChatChannel>(L, 1);
	if (!channel || !isString(L, 2) || !isFunction(L, 3)) {
		return 0;
	}

	const std::string_view callback = getStringView(L, 2);
	if (callback != "canJoin" && callback != "onJoin" && callback != "onLeave" && callback != "onSpeak") {
		reportErrorFunc(L, "Invalid ChatChannel callback name.");
		return 0;
	}

	LuaScriptInterface* scriptInterface = LuaScriptInterface::getScriptEnv()->getScriptInterface();
	lua_pushvalue(L, 3);
	const int32_t event = scriptInterface->getEvent();
	if (event == -1) {
		return 0;
	}

	if (callback == "canJoin") {
		channel->setCanJoinEvent(event);
	} else if (callback == "onJoin") {
		channel->setOnJoinEvent(event);
	} else if (callback == "onLeave") {
		channel->setOnLeaveEvent(event);
	} else if (callback == "onSpeak") {
		channel->setOnSpeakEvent(event);
	}
	return 0;
}

int luaDeleteChatChannel(lua_State* L)
{
	return deleteOwnedUserdata(L);
}

} // namespace

void LuaScriptInterface::registerChatChannel()
{
	registerClass("ChatChannel", "", luaCreateChatChannel);
	registerMetaMethod("ChatChannel", "__gc", luaDeleteChatChannel);
	registerMetaMethod("ChatChannel", "__close", luaDeleteChatChannel);
	registerMetaMethod("ChatChannel", "__newindex", luaChatChannelNewIndex);
	registerMethod("ChatChannel", "delete", luaDeleteChatChannel);

	registerMethod("ChatChannel", "public", luaChatChannelPublic);
	registerMethod("ChatChannel", "register", luaChatChannelRegister);
	registerMethod("ChatChannel", "canJoin", luaChatChannelCanJoin);
	registerMethod("ChatChannel", "onJoin", luaChatChannelOnJoin);
	registerMethod("ChatChannel", "onLeave", luaChatChannelOnLeave);
	registerMethod("ChatChannel", "onSpeak", luaChatChannelOnSpeak);
}
