// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "chat.h"

#include "game.h"
#include "pugicast.h"
#include "scheduler.h"
#include "script.h"
#include "scriptmanager.h"
#include "logger.h"
#include <fmt/format.h>

extern Game g_game;

bool PrivateChatChannel::isInvited(uint32_t guid) const
{
	if (guid == getOwner()) {
		return true;
	}
	return invites.contains(guid);
}

bool PrivateChatChannel::removeInvite(uint32_t guid) { return invites.erase(guid) != 0; }

void PrivateChatChannel::invitePlayer(const Player& player, Player& invitePlayer)
{
	auto result = invites.emplace(invitePlayer.getGUID(), invitePlayer.getGUID());
	if (!result.second) {
		return;
	}

	invitePlayer.sendTextMessage(MESSAGE_INFO_DESCR,
	                             fmt::format("{:s} invites you to {:s} private chat channel.", player.getName(),
	                                         player.getSex() == PLAYERSEX_FEMALE ? "her" : "his"));

	player.sendTextMessage(MESSAGE_INFO_DESCR, fmt::format("{:s} has been invited.", invitePlayer.getName()));
}

void PrivateChatChannel::excludePlayer(const Player& player, Player& excludePlayer)
{
	if (!removeInvite(excludePlayer.getGUID())) {
		return;
	}

	removeUser(excludePlayer);

	player.sendTextMessage(MESSAGE_INFO_DESCR, fmt::format("{:s} has been excluded.", excludePlayer.getName()));

	excludePlayer.sendClosePrivate(id);
}

void PrivateChatChannel::closeChannel()
{
	std::erase_if(users, [this](const auto& entry) {
		if (auto target = entry.second.lock()) {
			target->sendClosePrivate(id);
			return false;
		}
		return true;
	});
}

bool ChatChannel::addUser(const std::shared_ptr<Player>& player)
{
	if (!player) {
		return false;
	}

	if (users.contains(player->getID())) {
		return false;
	}

	if (!executeCanJoinEvent(*player)) {
		return false;
	}

	// TODO: Move to script when guild channels can be scripted
	if (id == CHANNEL_GUILD) {
		if (const auto& guild = player->getGuild(); guild && !guild->getMotd().empty()) {
			g_scheduler.addEvent(
			    createSchedulerTask(150, [playerID = player->getID()]() { g_game.sendGuildMotd(playerID); }));
		}
	}

	users[player->getID()] = player;
	return true;
}

bool ChatChannel::removeUser(const Player& player)
{
	auto iter = users.find(player.getID());
	if (iter == users.end()) {
		return false;
	}

	users.erase(iter);

	executeOnLeaveEvent(player);
	return true;
}

bool ChatChannel::hasUser(const Player& player) { return users.contains(player.getID()); }

void ChatChannel::sendToAll(std::string_view message, SpeakClasses type)
{
	std::erase_if(users, [this, message, type](const auto& entry) {
		if (auto target = entry.second.lock()) {
			target->sendChannelMessage("", message, type, id);
			return false;
		}
		return true;
	});
}

bool ChatChannel::talk(const Player& fromPlayer, SpeakClasses type, std::string_view text)
{
	if (!users.contains(fromPlayer.getID())) {
		return false;
	}

	std::erase_if(users, [this, &fromPlayer, type, text](const auto& entry) {
		if (auto target = entry.second.lock()) {
			target->sendToChannel(&fromPlayer, type, text, id);
			return false;
		}
		return true;
	});
	return true;
}

bool ChatChannel::executeCanJoinEvent(const Player& player)
{
	if (canJoinEvent == -1) {
		return true;
	}

	// canJoin(player)
	LuaScriptInterface* scriptInterface = this->scriptInterface ? this->scriptInterface : g_chat->getScriptInterface();
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - CanJoinChannelEvent::execute] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(canJoinEvent, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(canJoinEvent);
	Lua::pushUserdata(L, &player);
	Lua::setMetatable(L, -1, "Player");

	return scriptInterface->callFunction(1);
}

void ChatChannel::executeOnJoinEvent(const Player& player)
{
	if (onJoinEvent == -1) {
		return;
	}

	// onJoin(player)
	LuaScriptInterface* scriptInterface = this->scriptInterface ? this->scriptInterface : g_chat->getScriptInterface();
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - OnJoinChannelEvent::execute] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(onJoinEvent, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(onJoinEvent);
	Lua::pushUserdata(L, &player);
	Lua::setMetatable(L, -1, "Player");

	scriptInterface->callVoidFunction(1);
}

bool ChatChannel::executeOnLeaveEvent(const Player& player)
{
	if (onLeaveEvent == -1) {
		return true;
	}

	// onLeave(player)
	LuaScriptInterface* scriptInterface = this->scriptInterface ? this->scriptInterface : g_chat->getScriptInterface();
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - OnLeaveChannelEvent::execute] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(onLeaveEvent, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(onLeaveEvent);
	Lua::pushUserdata(L, &player);
	Lua::setMetatable(L, -1, "Player");

	return scriptInterface->callFunction(1);
}

bool ChatChannel::executeOnSpeakEvent(const Player& player, SpeakClasses& type, std::string_view message)
{
	if (onSpeakEvent == -1) {
		return true;
	}

	// onSpeak(player, type, message)
	LuaScriptInterface* scriptInterface = this->scriptInterface ? this->scriptInterface : g_chat->getScriptInterface();
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - OnSpeakChannelEvent::execute] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(onSpeakEvent, scriptInterface);

	lua_State* L = scriptInterface->getLuaState();

	scriptInterface->pushFunction(onSpeakEvent);
	Lua::pushUserdata(L, &player);
	Lua::setMetatable(L, -1, "Player");

	lua_pushinteger(L, type);
	Lua::pushString(L, message);

	bool result = false;
	int size0 = lua_gettop(L);
	int ret = scriptInterface->protectedCall(L, 3, 1);
	if (ret != 0) {
		LuaScriptInterface::reportError(nullptr, Lua::popString(L));
	} else if (lua_gettop(L) > 0) {
		if (lua_isboolean(L, -1)) {
			result = Lua::getBoolean(L, -1);
		} else if (lua_isinteger(L, -1)) {
			result = true;
			type = Lua::getInteger<SpeakClasses>(L, -1);
		}
		lua_pop(L, 1);
	}

	if ((lua_gettop(L) + 4) != size0) {
		LuaScriptInterface::reportError(nullptr, "Stack size changed!");
	}
	scriptInterface->resetScriptEnv();
	return result;
}

Chat::Chat() : scriptInterface("Chat Interface"), dummyPrivate(CHANNEL_PRIVATE, "Private Chat Channel")
{
	scriptInterface.initState();
}

bool Chat::load()
{
	g_scripts->clearLoadedFiles("scripts/chatchannels");
	clear();
	const bool result = g_scripts->loadScripts("scripts/chatchannels", false, true);
	LOG_INFO(fmt::format("Loaded {:d} chat channels.", normalChannels.size()));
	return result;
}

bool Chat::registerLuaChannel(std::unique_ptr<ChatChannel> channel)
{
	if (!channel) {
		return false;
	}

	const uint16_t channelId = channel->id;
	auto it = normalChannels.find(channelId);
	if (it != normalChannels.end()) {
		LOG_WARN(fmt::format("[Warning - Chat::registerLuaChannel] Duplicate channel id {:d}. Replacing previous channel.", channelId));
		UsersMap tempUserMap = std::move(it->second.users);
		it->second = std::move(*channel);
		usersToRestore[channelId] = std::move(tempUserMap);
	} else {
		it = normalChannels.emplace(channelId, std::move(*channel)).first;
	}

	if (auto restoreIt = usersToRestore.find(it->first); restoreIt != usersToRestore.end()) {
		for (const auto& pair : restoreIt->second) {
			auto restoredPlayer = pair.second.lock();
			if (!restoredPlayer) {
				restoredPlayer = g_game.getCreatureSharedRef<Player>(pair.first);
			}
			if (restoredPlayer) {
				it->second.users[restoredPlayer->getID()] = restoredPlayer;
			}
		}
		usersToRestore.erase(restoreIt);
	}
	return true;
}

void Chat::clear()
{
	usersToRestore.clear();
	for (auto& [channelId, channel] : normalChannels) {
		usersToRestore.emplace(channelId, std::move(channel.users));
	}
	normalChannels.clear();
}

ChatChannel* Chat::createChannel(const Player& player, uint16_t channelId)
{
	if (getChannel(player, channelId)) {
		return nullptr;
	}

	switch (channelId) {
		case CHANNEL_GUILD: {
			if (const auto& guild = player.getGuild()) {
				auto ret = guildChannels.emplace(guild->getId(), ChatChannel(channelId, guild->getName()));
				return &ret.first->second;
			}
			break;
		}

		case CHANNEL_PARTY: {
			Party* party = player.getParty();
			if (party) {
				auto ret = partyChannels.emplace(party, ChatChannel(channelId, "Party"));
				return &ret.first->second;
			}
			break;
		}

		case CHANNEL_PRIVATE: {
			// only 1 private channel for each premium player
			if (!player.isPremium() || getPrivateChannel(player)) {
				return nullptr;
			}

			// find a free private channel slot
			for (uint16_t i = 100; i < 10000; ++i) {
				auto ret = privateChannels.emplace(i, PrivateChatChannel(i, player.getName() + "'s Channel"));
				if (ret.second) { // second is a bool that indicates that a new channel has been placed in the map
					auto& newChannel = (*ret.first).second;
					newChannel.setOwner(player.getGUID());
					return &newChannel;
				}
			}
			break;
		}

		default:
			break;
	}
	return nullptr;
}

bool Chat::deleteChannel(const Player& player, uint16_t channelId)
{
	switch (channelId) {
		case CHANNEL_GUILD: {
			const auto& guild = player.getGuild();
			if (!guild) {
				return false;
			}

			auto it = guildChannels.find(guild->getId());
			if (it == guildChannels.end()) {
				return false;
			}

			guildChannels.erase(it);
			break;
		}

		case CHANNEL_PARTY: {
			Party* party = player.getParty();
			if (!party) {
				return false;
			}

			auto it = partyChannels.find(party);
			if (it == partyChannels.end()) {
				return false;
			}

			partyChannels.erase(it);
			break;
		}

		default: {
			auto it = privateChannels.find(channelId);
			if (it == privateChannels.end()) {
				return false;
			}

			it->second.closeChannel();

			privateChannels.erase(it);
			break;
		}
	}
	return true;
}

ChatChannel* Chat::addUserToChannel(Player& player, uint16_t channelId)
{
	ChatChannel* channel = getChannel(player, channelId);
	if (channel && channel->addUser(g_game.getCreatureSharedRef<Player>(&player))) {
		return channel;
	}
	return nullptr;
}

bool Chat::removeUserFromChannel(const Player& player, uint16_t channelId)
{
	ChatChannel* channel = getChannel(player, channelId);
	if (!channel || !channel->removeUser(player)) {
		return false;
	}

	const bool ownerLeft = channel->getOwner() == player.getGUID();

	if (channelId == CHANNEL_PARTY && channel->users.empty()) {
		std::erase_if(partyChannels, [channel](const auto& entry) { return &entry.second == channel; });
		return true;
	}

	if (ownerLeft) {
		deleteChannel(player, channelId);
	}
	return true;
}

void Chat::removeUserFromAllChannels(const Player& player)
{
	for (auto& it : normalChannels) {
		it.second.removeUser(player);
	}

	for (auto& it : partyChannels) {
		it.second.removeUser(player);
	}

	for (auto& it : guildChannels) {
		it.second.removeUser(player);
	}

	std::erase_if(privateChannels, [&player](auto& entry) {
		PrivateChatChannel* channel = &entry.second;
		channel->removeInvite(player.getGUID());
		channel->removeUser(player);
		if (channel->getOwner() == player.getGUID()) {
			channel->closeChannel();
			return true;
		}
		return false;
	});
}

bool Chat::talkToChannel(const Player& player, SpeakClasses type, std::string_view text, uint16_t channelId)
{
	ChatChannel* channel = getChannel(player, channelId);
	if (!channel) {
		return false;
	}

	if (channelId == CHANNEL_GUILD) {
		const auto& rank = player.getGuildRank();
		if (rank && rank->level > 1) {
			type = TALKTYPE_CHANNEL_O;
		} else if (type != TALKTYPE_CHANNEL_Y) {
			type = TALKTYPE_CHANNEL_Y;
		}
	} else if (channelId == CHANNEL_PRIVATE || channelId == CHANNEL_PARTY) {
		type = TALKTYPE_CHANNEL_Y;
	}

	if (!channel->executeOnSpeakEvent(player, type, text)) {
		return false;
	}

	return channel->talk(player, type, text);
}

ChannelList Chat::getChannelList(const Player& player)
{
	ChannelList list;
	if (player.getGuild()) {
		ChatChannel* channel = getChannel(player, CHANNEL_GUILD);
		if (channel) {
			list.push_back(channel);
		} else {
			channel = createChannel(player, CHANNEL_GUILD);
			if (channel) {
				list.push_back(channel);
			}
		}
	}

	if (player.getParty()) {
		ChatChannel* channel = getChannel(player, CHANNEL_PARTY);
		if (channel) {
			list.push_back(channel);
		} else {
			channel = createChannel(player, CHANNEL_PARTY);
			if (channel) {
				list.push_back(channel);
			}
		}
	}

	for (const auto& it : normalChannels) {
		ChatChannel* channel = getChannel(player, it.first);
		if (channel) {
			list.push_back(channel);
		}
	}

	bool hasPrivate = false;
	for (auto& it : privateChannels) {
		if (PrivateChatChannel* channel = &it.second) {
			uint32_t guid = player.getGUID();
			if (channel->isInvited(guid)) {
				list.push_back(channel);
			}

			if (channel->getOwner() == guid) {
				hasPrivate = true;
			}
		}
	}

	if (!hasPrivate && player.isPremium()) {
		list.push_front(&dummyPrivate);
	}
	return list;
}

ChatChannel* Chat::getChannel(const Player& player, uint16_t channelId)
{
	switch (channelId) {
		case CHANNEL_GUILD: {
			if (const auto& guild = player.getGuild()) {
				auto it = guildChannels.find(guild->getId());
				if (it != guildChannels.end()) {
					return &it->second;
				}
			}
			break;
		}

		case CHANNEL_PARTY: {
			Party* party = player.getParty();
			if (party) {
				auto it = partyChannels.find(party);
				if (it != partyChannels.end()) {
					return &it->second;
				}
			}
			break;
		}

		default: {
			auto it = normalChannels.find(channelId);
			if (it != normalChannels.end()) {
				ChatChannel& channel = it->second;
				if (!channel.executeCanJoinEvent(player)) {
					return nullptr;
				}
				return &channel;
			} else {
				auto it2 = privateChannels.find(channelId);
				if (it2 != privateChannels.end() && it2->second.isInvited(player.getGUID())) {
					return &it2->second;
				}
			}
			break;
		}
	}
	return nullptr;
}

ChatChannel* Chat::getGuildChannelById(uint32_t guildId)
{
	auto it = guildChannels.find(guildId);
	if (it == guildChannels.end()) {
		return nullptr;
	}
	return &it->second;
}

ChatChannel* Chat::getChannelById(uint16_t channelId)
{
	auto it = normalChannels.find(channelId);
	if (it == normalChannels.end()) {
		return nullptr;
	}
	return &it->second;
}

PrivateChatChannel* Chat::getPrivateChannel(const Player& player)
{
	for (auto& it : privateChannels) {
		if (it.second.getOwner() == player.getGUID()) {
			return &it.second;
		}
	}
	return nullptr;
}
