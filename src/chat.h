// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_CHAT_H
#define FS_CHAT_H

#include <memory>

#include "const.h"
#include "luascript.h"
#include "observer_ptr.h"

class Party;
class Player;

using UsersMap = std::unordered_map<uint32_t, std::weak_ptr<Player>>;
using InvitedMap = std::unordered_map<uint32_t, uint32_t>;

class ChatChannel
{
public:
	ChatChannel() = default;
	ChatChannel(uint16_t channelId, std::string_view channelName) : id{channelId}, name{channelName} {}

	virtual ~ChatChannel() = default;

	bool addUser(const std::shared_ptr<Player>& player);
	bool removeUser(const Player& player);
	bool hasUser(const Player& player);

	bool talk(const Player& fromPlayer, SpeakClasses type, std::string_view text);
	void sendToAll(std::string_view message, SpeakClasses type);

	const std::string& getName() const { return name; }
	uint16_t getId() const { return id; }
	const UsersMap& getUsers() const { return users; }
	virtual const InvitedMap* getInvitedUsers() const { return nullptr; }

	virtual uint32_t getOwner() const { return 0; }

	bool isPublicChannel() const { return publicChannel; }

	void executeOnJoinEvent(const Player& player);
	bool executeCanJoinEvent(const Player& player);
	bool executeOnLeaveEvent(const Player& player);
	bool executeOnSpeakEvent(const Player& player, SpeakClasses& type, std::string_view message);

protected:
	UsersMap users;

	uint16_t id;

private:
	std::string name;

	int32_t canJoinEvent = -1;
	int32_t onJoinEvent = -1;
	int32_t onLeaveEvent = -1;
	int32_t onSpeakEvent = -1;

	bool publicChannel = false;

	friend class Chat;
};

class PrivateChatChannel final : public ChatChannel
{
public:
	PrivateChatChannel(uint16_t channelId, std::string_view channelName) : ChatChannel(channelId, channelName) {}

	uint32_t getOwner() const override { return owner; }
	void setOwner(uint32_t owner) { this->owner = owner; }

	bool isInvited(uint32_t guid) const;

	void invitePlayer(const Player& player, Player& invitePlayer);
	void excludePlayer(const Player& player, Player& excludePlayer);

	bool removeInvite(uint32_t guid);

	void closeChannel();

	const InvitedMap* getInvitedUsers() const override { return &invites; }

private:
	InvitedMap invites;
	uint32_t owner = 0;
};

using ChannelList = std::list<ObserverPtr<ChatChannel>>;

class Chat
{
public:
	Chat();

	// non-copyable
	Chat(const Chat&) = delete;
	Chat& operator=(const Chat&) = delete;

	bool load();

	[[nodiscard]] ChatChannel* createChannel(const Player& player, uint16_t channelId);
	bool deleteChannel(const Player& player, uint16_t channelId);

	ChatChannel* addUserToChannel(Player& player, uint16_t channelId);
	bool removeUserFromChannel(const Player& player, uint16_t channelId);
	void removeUserFromAllChannels(const Player& player);

	bool talkToChannel(const Player& player, SpeakClasses type, std::string_view text, uint16_t channelId);

	ChannelList getChannelList(const Player& player);

	[[nodiscard]] ChatChannel* getChannel(const Player& player, uint16_t channelId);
	[[nodiscard]] ChatChannel* getChannelById(uint16_t channelId);
	[[nodiscard]] ChatChannel* getGuildChannelById(uint32_t guildId);
	[[nodiscard]] PrivateChatChannel* getPrivateChannel(const Player& player);

	LuaScriptInterface* getScriptInterface() { return &scriptInterface; }

private:
	std::unordered_map<uint16_t, ChatChannel> normalChannels;
	std::unordered_map<uint16_t, PrivateChatChannel> privateChannels;
	std::unordered_map<ObserverPtr<Party>, ChatChannel> partyChannels;
	std::unordered_map<uint32_t, ChatChannel> guildChannels;

	LuaScriptInterface scriptInterface;

	PrivateChatChannel dummyPrivate;
};

#endif
