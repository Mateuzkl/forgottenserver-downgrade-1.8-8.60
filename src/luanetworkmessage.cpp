// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "luascript.h"
#include "networkmessage.h"
#include "player.h"

namespace {
using namespace Lua;

bool isOtcOnlyLuaOpcode(uint8_t opcode)
{
	switch (opcode) {
		case 0x29: // custom supply stash
		case 0x2F: // custom unjustified points
		case 0x32: // extended opcode
		case 0x48: // custom cyclopedia/bestiary
		case 0x5F: // custom wheel of destiny window
		case 0xA7: // custom fight mode sync
		case 0xD1: // custom hunt analyzer
		case 0xDB: // custom market
		case 0xEB: // imbuing window
		case 0xEC: // close imbuing
		case 0xED: // custom prey
		case 0xEE: // resource balance
		case 0xF0: // custom quest log
		case 0xF1: // custom quest line
		case 0xFD: // custom store
			return true;
		default:
			return false;
	}
}

bool canSendLuaNetworkMessageToPlayer(const NetworkMessage& message, const Player& player)
{
	if (message.getLength() == 0) {
		return true;
	}

	const uint8_t opcode = message.getBuffer()[NetworkMessage::INITIAL_BUFFER_POSITION];
	return !isOtcOnlyLuaOpcode(opcode) || player.isOTC();
}

int sendLuaNetworkMessageToPlayer(lua_State* L, NetworkMessage& message, Player& player)
{
	if (!canSendLuaNetworkMessageToPlayer(message, player)) {
		pushBoolean(L, false);
		return 1;
	}

	player.sendNetworkMessage(message);
	pushBoolean(L, true);
	return 1;
}

// NetworkMessage
int luaNetworkMessageCreate(lua_State* L)
{
	// NetworkMessage([player])
	auto message = std::make_unique<NetworkMessage>();
	pushOwnedUserdata<NetworkMessage>(L, std::move(message));
	setMetatable(L, -1, "NetworkMessage");

	if (const auto player = getPlayer(L, 1)) {
		lua_pushinteger(L, player->getID());
		lua_setiuservalue(L, 2, 1);
	}
	return 1;
}

int luaNetworkMessageDelete(lua_State* L)
{
	return deleteOwnedUserdata(L);
}

int luaNetworkMessageGetByte(lua_State* L)
{
	// networkMessage:getByte()
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		lua_pushinteger(L, message->getByte());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageGetU16(lua_State* L)
{
	// networkMessage:getU16()
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		lua_pushinteger(L, message->get<uint16_t>());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageGetU32(lua_State* L)
{
	// networkMessage:getU32()
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		lua_pushinteger(L, message->get<uint32_t>());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageGetU64(lua_State* L)
{
	// networkMessage:getU64()
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		lua_pushinteger(L, message->get<uint64_t>());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageGetString(lua_State* L)
{
	// networkMessage:getString()
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		pushString(L, message->getString());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageGetPosition(lua_State* L)
{
	// networkMessage:getPosition()
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		pushPosition(L, message->getPosition());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageAddByte(lua_State* L)
{
	// networkMessage:addByte(integer)
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		uint8_t integer = getInteger<uint8_t>(L, 2);
		message->addByte(integer);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageAddU16(lua_State* L)
{
	// networkMessage:addU16(integer)
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		uint16_t integer = getInteger<uint16_t>(L, 2);
		message->add<uint16_t>(integer);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageAddU32(lua_State* L)
{
	// networkMessage:addU32(integer)
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		uint32_t integer = getInteger<uint32_t>(L, 2);
		message->add<uint32_t>(integer);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageAddU64(lua_State* L)
{
	// networkMessage:addU64(integer)
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		uint64_t integer = getInteger<uint64_t>(L, 2);
		message->add<uint64_t>(integer);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageAddString(lua_State* L)
{
	// networkMessage:addString(string)
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		const std::string& string = getString(L, 2);
		message->addString(string);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageAddPosition(lua_State* L)
{
	// networkMessage:addPosition(position)
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		const Position& position = getPosition(L, 2);
		message->addPosition(position);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageAddDouble(lua_State* L)
{
	// networkMessage:addDouble(number)
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		double number = getNumber<double>(L, 2);
		message->addDouble(number);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageAddItem(lua_State* L)
{
	// networkMessage:addItem(item)
	Item* item = getItemUserdata<Item>(L, 2);
	if (!item) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::ITEM_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		if (getAssociatedValue(L, 1, 1)) {
			if (const auto player = getPlayer(L, -1)) {
				message->addItem(item);
			} else {
				reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::PLAYER_NOT_FOUND));
				lua_pushnil(L);
				return 1;
			}
		} else {
			message->addItem(item);
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageAddItemId(lua_State* L)
{
	// networkMessage:addItemId(itemId)
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (!message) {
		lua_pushnil(L);
		return 1;
	}

	uint16_t itemId;
	if (isInteger(L, 2)) {
		itemId = getInteger<uint16_t>(L, 2);
	} else {
		itemId = Item::items.getItemIdByName(getString(L, 2));
		if (itemId == 0) {
			lua_pushnil(L);
			return 1;
		}
	}

	if (getAssociatedValue(L, 1, 1)) {
		if (const auto player = getPlayer(L, -1)) {
			message->addItemId(itemId);
		} else {
			reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::PLAYER_NOT_FOUND));
			lua_pushnil(L);
			return 1;
		}
	} else {
		message->addItemId(itemId);
	}
	pushBoolean(L, true);
	return 1;
}

int luaNetworkMessageReset(lua_State* L)
{
	// networkMessage:reset()
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		message->reset();
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageSeek(lua_State* L)
{
	// networkMessage:seek(position)
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message && isInteger(L, 2)) {
		pushBoolean(L, message->setBufferPosition(getInteger<uint16_t>(L, 2)));
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageTell(lua_State* L)
{
	// networkMessage:tell()
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		lua_pushinteger(L, static_cast<int64_t>(message->getBufferPosition()) - message->INITIAL_BUFFER_POSITION);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageLength(lua_State* L)
{
	// networkMessage:len()
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		lua_pushinteger(L, message->getLength());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageSkipBytes(lua_State* L)
{
	// networkMessage:skipBytes(integer)
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (message) {
		int16_t integer = getInteger<int16_t>(L, 2);
		message->skipBytes(integer);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNetworkMessageSendToPlayer(lua_State* L)
{
	// networkMessage:sendToPlayer([player])
	NetworkMessage* message = getUserdata<NetworkMessage>(L, 1);
	if (!message) {
		lua_pushnil(L);
		return 1;
	}

	if (Player* player = getPlayer(L, 2)) {
		return sendLuaNetworkMessageToPlayer(L, *message, *player);
	}

	if (getAssociatedValue(L, 1, 1)) {
		if (const auto p = getPlayer(L, -1)) {
			return sendLuaNetworkMessageToPlayer(L, *message, *p);
		}
	}

	reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::PLAYER_NOT_FOUND));
	lua_pushnil(L);
	return 1;
}
} // namespace

void LuaScriptInterface::registerNetworkMessage()
{
	// NetworkMessage
	registerClass("NetworkMessage", "", luaNetworkMessageCreate);
	registerMetaMethod("NetworkMessage", "__eq", LuaScriptInterface::luaUserdataCompare);
	registerMetaMethod("NetworkMessage", "__gc", luaNetworkMessageDelete);
	registerMetaMethod("NetworkMessage", "__close", luaNetworkMessageDelete);
	registerMethod("NetworkMessage", "delete", luaNetworkMessageDelete);

	registerMethod("NetworkMessage", "getByte", luaNetworkMessageGetByte);
	registerMethod("NetworkMessage", "getU16", luaNetworkMessageGetU16);
	registerMethod("NetworkMessage", "getU32", luaNetworkMessageGetU32);
	registerMethod("NetworkMessage", "getU64", luaNetworkMessageGetU64);
	registerMethod("NetworkMessage", "getString", luaNetworkMessageGetString);
	registerMethod("NetworkMessage", "getPosition", luaNetworkMessageGetPosition);

	registerMethod("NetworkMessage", "addByte", luaNetworkMessageAddByte);
	registerMethod("NetworkMessage", "addU16", luaNetworkMessageAddU16);
	registerMethod("NetworkMessage", "addU32", luaNetworkMessageAddU32);
	registerMethod("NetworkMessage", "addU64", luaNetworkMessageAddU64);
	registerMethod("NetworkMessage", "addString", luaNetworkMessageAddString);
	registerMethod("NetworkMessage", "addPosition", luaNetworkMessageAddPosition);
	registerMethod("NetworkMessage", "addDouble", luaNetworkMessageAddDouble);
	registerMethod("NetworkMessage", "addItem", luaNetworkMessageAddItem);
	registerMethod("NetworkMessage", "addItemId", luaNetworkMessageAddItemId);

	registerMethod("NetworkMessage", "reset", luaNetworkMessageReset);
	registerMethod("NetworkMessage", "seek", luaNetworkMessageSeek);
	registerMethod("NetworkMessage", "tell", luaNetworkMessageTell);
	registerMethod("NetworkMessage", "len", luaNetworkMessageLength);
	registerMethod("NetworkMessage", "skipBytes", luaNetworkMessageSkipBytes);
	registerMethod("NetworkMessage", "sendToPlayer", luaNetworkMessageSendToPlayer);
}
