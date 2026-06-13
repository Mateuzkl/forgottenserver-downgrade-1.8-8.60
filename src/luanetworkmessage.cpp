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
		case 0x2B: // custom party hunt analyzer
		case 0x2D: // custom charm activated
		case 0x2F: // custom unjustified points
		case 0x30: // custom imbuement activated
		case 0x31: // custom special skill activated
		case 0x32: // extended opcode
		case 0x48: // custom cyclopedia/bestiary
		case 0x5F: // custom wheel of destiny window
		case 0x61: // native bosstiary base data
		case 0x62: // native bosstiary slots
		case 0x73: // native bosstiary window
		case 0xA7: // custom fight mode sync
		case 0xBB: // native hunting task slot data
		case 0xD1: // custom hunt analyzer
		case 0xDB: // custom market
		case 0xEB: // imbuing window
		case 0xEC: // close imbuing
		case 0xED: // custom prey
		case 0xE8: // native prey slot data
		case 0xE9: // native prey prices
		case 0xF0: // custom quest log
		case 0xF1: // custom quest line
		case 0xFD: // custom store
			return true;
		default:
			return false;
	}
}

bool isAstraOnlyLuaOpcode(uint8_t opcode)
{
	switch (opcode) {
		case 0x2C: // custom boss cooldown
		case 0x37: // custom battle pass
		case 0x53: // task board data
		case 0x9B: // blessing window
		case 0x9C: // blessing status
		case 0xBA: // soulseal data
		case 0xC0: // managed quick-loot containers
		case 0xC6: // custom item values
		case 0xC7: // custom item details
		case 0xCF: // quick-loot statistics
		case 0xEE: // resource balance
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
	if (isAstraOnlyLuaOpcode(opcode)) {
		return player.isAstraClient();
	}
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
std::shared_ptr<NetworkMessage>& getNetworkMessage(lua_State* L)
{
	static thread_local std::shared_ptr<NetworkMessage> sentinel;
	auto* ptr = static_cast<std::shared_ptr<NetworkMessage>*>(luaL_testudata(L, 1, "NetworkMessage"));
	if (!ptr || !*ptr) {
		sentinel.reset();
		return sentinel;
	}
	return *ptr;
}

int luaNetworkMessageCreate(lua_State* L)
{
	// NetworkMessage([player])
	pushSharedPtr(L, tfs::net::make_network_message());
	setMetatable(L, -1, "NetworkMessage");

	if (const auto player = getPlayer(L, 1)) {
		lua_pushinteger(L, player->getID());
		lua_setiuservalue(L, 2, 1);
	}
	return 1;
}

int luaNetworkMessageDelete(lua_State* L)
{
	auto ptr = static_cast<std::shared_ptr<NetworkMessage>*>(lua_touserdata(L, 1));
	if (ptr) {
		ptr->reset();
	}
	return 0;
}

int luaNetworkMessageGetByte(lua_State* L)
{
	// networkMessage:getByte()
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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

	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
	const auto& message = getNetworkMessage(L);
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
