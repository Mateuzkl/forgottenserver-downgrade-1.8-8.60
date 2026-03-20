// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "bed.h"
#include "database.h"
#include "game.h"
#include "house.h"
#include "iologindata.h"
#include "iomapserialize.h"
#include "luascript.h"

extern Game g_game;

namespace {
using namespace Lua;

// House
int luaHouseCreate(lua_State* L)
{
	// House(id)
	House* house = g_game.map.houses.getHouse(getInteger<uint32_t>(L, 2));
	if (house) {
		pushUserdata<House>(L, house);
		setMetatable(L, -1, "House");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetId(lua_State* L)
{
	// house:getId()
	House* house = getUserdata<House>(L, 1);
	if (house) {
		lua_pushinteger(L, house->getId());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetName(lua_State* L)
{
	// house:getName()
	House* house = getUserdata<House>(L, 1);
	if (house) {
		pushString(L, house->getName());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetTown(lua_State* L)
{
	// house:getTown()
	House* house = getUserdata<House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	Town* town = g_game.map.towns.getTown(house->getTownId());
	if (town) {
		pushUserdata<Town>(L, town);
		setMetatable(L, -1, "Town");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetExitPosition(lua_State* L)
{
	// house:getExitPosition()
	const House* house = getUserdata<const House>(L, 1);
	if (house) {
		pushPosition(L, house->getEntryPosition());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetRent(lua_State* L)
{
	// house:getRent()
	const House* house = getUserdata<const House>(L, 1);
	if (house) {
		lua_pushinteger(L, house->getRent());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseSetRent(lua_State* L)
{
	// house:setRent(rent)
	House* house = getUserdata<House>(L, 1);
	if (house) {
		uint32_t rent = getInteger<uint32_t>(L, 2);
		house->setRent(rent);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetPaidUntil(lua_State* L)
{
	// house:getPaidUntil()
	const House* house = getUserdata<const House>(L, 1);
	if (house) {
		lua_pushinteger(L, house->getPaidUntil());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseSetPaidUntil(lua_State* L)
{
	// house:setPaidUntil(timestamp)
	House* house = getUserdata<House>(L, 1);
	if (house) {
		time_t timestamp = getInteger<time_t>(L, 2);
		house->setPaidUntil(timestamp);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetPayRentWarnings(lua_State* L)
{
	// house:getPayRentWarnings()
	const House* house = getUserdata<const House>(L, 1);
	if (house) {
		lua_pushinteger(L, house->getPayRentWarnings());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseSetPayRentWarnings(lua_State* L)
{
	// house:setPayRentWarnings(warnings)
	House* house = getUserdata<House>(L, 1);
	if (house) {
		uint32_t warnings = getInteger<uint32_t>(L, 2);
		house->setPayRentWarnings(warnings);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetOwnerName(lua_State* L)
{
	// house:getOwnerName()
	const House* house = getUserdata<const House>(L, 1);
	if (house) {
		pushString(L, house->getOwnerName());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetOwnerGuid(lua_State* L)
{
	// house:getOwnerGuid()
	const House* house = getUserdata<const House>(L, 1);
	if (house) {
		if (house->getType() == HOUSE_TYPE_NORMAL) {
			lua_pushinteger(L, house->getOwner());
		} else {
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetOwnerAccountId(lua_State* L)
{
	// house:getOwnerAccountId()
	const House* house = getUserdata<const House>(L, 1);
	if (house) {
		lua_pushinteger(L, house->getOwnerAccountId());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetType(lua_State* L)
{
	// house:getType()
	House* house = getUserdata<House>(L, 1);
	if (house) {
		lua_pushinteger(L, house->getType());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetOwnerGuild(lua_State* L)
{
	// house:getOwnerGuild()
	House* house = getUserdata<House>(L, 1);
	if (house) {
		if (house->getType() == HOUSE_TYPE_GUILDHALL) {
			lua_pushinteger(L, house->getOwner());
		} else {
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseSetOwnerGuid(lua_State* L)
{
	// house:setOwnerGuid(guid[, updateDatabase = true])
	House* house = getUserdata<House>(L, 1);
	if (house) {
		uint32_t guid_guild = getInteger<uint32_t>(L, 2);
		bool updateDatabase = getBoolean(L, 3, true);

		if (house->getType() == HOUSE_TYPE_GUILDHALL && guid_guild == 0) {
			house->setOwner(0, updateDatabase);
			pushBoolean(L, true);
			return 1;
		}

		if (house->getType() == HOUSE_TYPE_GUILDHALL) {
			Database& db = Database::getInstance();
			std::ostringstream query;
			query << "SELECT `guild_id` FROM `guild_membership` WHERE `player_id`=" << guid_guild;
			if (DBResult_ptr result = db.storeQuery(query.str())) {
				guid_guild = result->getNumber<uint32_t>("guild_id");
			} else {
				pushBoolean(L, false);
				return 1;
			}
		}
		house->setOwner(guid_guild, updateDatabase);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseStartTrade(lua_State* L)
{
	// house:startTrade(player, tradePartner)
	House* house = getUserdata<House>(L, 1);
	Player* player = getUserdata<Player>(L, 2);
	Player* tradePartner = getUserdata<Player>(L, 3);

	if (!player || !tradePartner || !house) {
		lua_pushnil(L);
		return 1;
	}

	if (!tradePartner->getPosition().isInRange(player->getPosition(), 2, 2, 0)) {
		lua_pushinteger(L, RETURNVALUE_TRADEPLAYERFARAWAY);
		return 1;
	}

	if (house->getType() == HOUSE_TYPE_NORMAL) {
		if (house->getOwner() != player->getGUID()) {
			lua_pushinteger(L, RETURNVALUE_YOUDONTOWNTHISHOUSE);
			return 1;
		}

		if (g_game.map.houses.getHouseByPlayerId(tradePartner->getGUID())) {
			lua_pushinteger(L, RETURNVALUE_TRADEPLAYERALREADYOWNSAHOUSE);
			return 1;
		}

		if (IOLoginData::hasBiddedOnHouse(tradePartner->getGUID())) {
			lua_pushinteger(L, RETURNVALUE_TRADEPLAYERHIGHESTBIDDER);
			return 1;
		}

		Item* transferItem = house->getTransferItem();
		if (!transferItem) {
			lua_pushinteger(L, RETURNVALUE_YOUCANNOTTRADETHISHOUSE);
			return 1;
		}

		transferItem->getParent()->setParent(player);
		if (!g_game.internalStartTrade(player, tradePartner, transferItem)) {
			house->resetTransferItem();
		}

		lua_pushinteger(L, RETURNVALUE_NOERROR);
		return 1;
	}

	// HOUSE_TYPE_GUILDHALL
	const auto& guild = player->getGuild();
	if (!guild || house->getOwner() != guild->getId()) {
		lua_pushinteger(L, RETURNVALUE_YOUDONTOWNTHISHOUSE);
		return 1;
	}

	const auto& tradeGuild = tradePartner->getGuild();
	if (!tradeGuild) {
		lua_pushinteger(L, RETURNVALUE_TRADEPLAYERNOTINAGUILD);
		return 1;
	}
	if (tradeGuild->getHouseId() > 0) {
		lua_pushinteger(L, RETURNVALUE_TRADEGUILDALREADYOWNSAHOUSE);
		return 1;
	}
	if (player->getGUID() != guild->getOwnerGUID()) {
		lua_pushinteger(L, RETURNVALUE_YOUARENOTGUILDLEADER);
		return 1;
	}
	if (tradePartner->getGUID() != tradeGuild->getOwnerGUID()) {
		lua_pushinteger(L, RETURNVALUE_TRADEPLAYERNOTGUILDLEADER);
		return 1;
	}
	if (IOLoginData::hasBiddedOnHouse(tradeGuild->getId())) {
		lua_pushinteger(L, RETURNVALUE_TRADEPLAYERHIGHESTBIDDER);
		return 1;
	}

	Item* transferItem = house->getTransferItem();
	if (!transferItem) {
		lua_pushinteger(L, RETURNVALUE_YOUCANNOTTRADETHISHOUSE);
		return 1;
	}

	transferItem->getParent()->setParent(player);
	if (!g_game.internalStartTrade(player, tradePartner, transferItem)) {
		house->resetTransferItem();
	}

	lua_pushinteger(L, RETURNVALUE_NOERROR);
	return 1;
}

int luaHouseGetBeds(lua_State* L)
{
	// house:getBeds()
	const House* house = getUserdata<const House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	const auto& beds = house->getBeds();
	lua_createtable(L, beds.size(), 0);

	int index = 0;
	for (BedItem* bedItem : beds) {
		pushUserdata<Item>(L, bedItem);
		setItemMetatable(L, -1, bedItem);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaHouseGetBedCount(lua_State* L)
{
	// house:getBedCount()
	const House* house = getUserdata<const House>(L, 1);
	if (house) {
		lua_pushinteger(L, house->getBedCount());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetDoors(lua_State* L)
{
	// house:getDoors()
	const House* house = getUserdata<const House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	const auto& doors = house->getDoors();
	lua_createtable(L, doors.size(), 0);

	int index = 0;
	for (Door* door : doors) {
		pushUserdata<Item>(L, door);
		setItemMetatable(L, -1, door);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaHouseGetDoorCount(lua_State* L)
{
	// house:getDoorCount()
	const House* house = getUserdata<const House>(L, 1);
	if (house) {
		lua_pushinteger(L, house->getDoors().size());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetDoorIdByPosition(lua_State* L)
{
	// house:getDoorIdByPosition(position)
	const House* house = getUserdata<const House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	const Door* door = house->getDoorByPosition(getPosition(L, 2));
	if (door) {
		lua_pushinteger(L, door->getDoorId());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseGetTiles(lua_State* L)
{
	// house:getTiles()
	const House* house = getUserdata<const House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	const auto& tiles = house->getTiles();
	lua_createtable(L, tiles.size(), 0);

	int index = 0;
	for (Tile* tile : tiles) {
		pushUserdata<Tile>(L, tile);
		setMetatable(L, -1, "Tile");
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaHouseGetItems(lua_State* L)
{
	// house:getItems()
	const House* house = getUserdata<const House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	const auto& tiles = house->getTiles();
	lua_newtable(L);

	int index = 0;
	for (Tile* tile : tiles) {
		TileItemVector* itemVector = tile->getItemList();
		if (itemVector) {
			for (Item* item : *itemVector) {
				pushUserdata<Item>(L, item);
				setItemMetatable(L, -1, item);
				lua_rawseti(L, -2, ++index);
			}
		}
	}
	return 1;
}

int luaHouseGetTileCount(lua_State* L)
{
	// house:getTileCount()
	const House* house = getUserdata<const House>(L, 1);
	if (house) {
		lua_pushinteger(L, house->getTiles().size());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseCanEditAccessList(lua_State* L)
{
	// house:canEditAccessList(listId, player)
	const House* house = getUserdata<const House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	uint32_t listId = getInteger<uint32_t>(L, 2);
	Player* player = getPlayer(L, 3);

	pushBoolean(L, house->canEditAccessList(listId, player));
	return 1;
}

int luaHouseGetAccessList(lua_State* L)
{
	// house:getAccessList(listId)
	const House* house = getUserdata<const House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	uint32_t listId = getInteger<uint32_t>(L, 2);
	if (auto list = house->getAccessList(listId)) {
		pushString(L, list.value());
	} else {
		pushBoolean(L, false);
	}
	return 1;
}

int luaHouseSetAccessList(lua_State* L)
{
	// house:setAccessList(listId, list)
	House* house = getUserdata<House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	uint32_t listId = getInteger<uint32_t>(L, 2);
	const std::string& list = getString(L, 3);
	house->setAccessList(listId, list);
	pushBoolean(L, true);
	return 1;
}

int luaHouseKickPlayer(lua_State* L)
{
	// house:kickPlayer(player, targetPlayer)
	const House* house = getUserdata<const House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	pushBoolean(L, house->kickPlayer(getPlayer(L, 2), getPlayer(L, 3)));
	return 1;
}

int luaHouseSave(lua_State* L)
{
	// house:save()
	const House* house = getUserdata<const House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	pushBoolean(L, IOMapSerialize::saveHouse(house));
	return 1;
}

// House protection system
int luaHouseGetProtected(lua_State* L)
{
	// house:getProtected()
	House* house = getUserdata<House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}
	pushBoolean(L, house->getProtected());
	return 1;
}

int luaHouseSetProtected(lua_State* L)
{
	// house:setProtected(protected)
	House* house = getUserdata<House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	bool protect = getBoolean(L, 2);
	house->setProtected(protect);
	pushBoolean(L, true);
	return 1;
}

int luaHouseAddProtectionGuest(lua_State* L)
{
	// house:addProtectionGuest(playerId)
	House* house = getUserdata<House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	uint32_t playerId = getInteger<uint32_t>(L, 2);
	pushBoolean(L, house->addProtectionGuest(playerId));
	return 1;
}

int luaHouseRemoveProtectionGuest(lua_State* L)
{
	// house:removeProtectionGuest(playerId)
	House* house = getUserdata<House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	uint32_t playerId = getInteger<uint32_t>(L, 2);
	pushBoolean(L, house->removeProtectionGuest(playerId));
	return 1;
}

int luaHouseIsProtectionGuest(lua_State* L)
{
	// house:isProtectionGuest(playerId)
	House* house = getUserdata<House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	uint32_t playerId = getInteger<uint32_t>(L, 2);
	pushBoolean(L, house->isProtectionGuest(playerId));
	return 1;
}

int luaHouseGetProtectionGuests(lua_State* L)
{
	// house:getProtectionGuests()
	House* house = getUserdata<House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	const auto& guests = house->getProtectionGuests();
	lua_createtable(L, guests.size(), 0);

	int index = 0;
	for (uint32_t guestId : guests) {
		lua_pushnumber(L, guestId);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

int luaHouseGetRequiredReset(lua_State* L)
{
	// house:getRequiredReset()
	House* house = getUserdata<House>(L, 1);
	if (house) {
		lua_pushinteger(L, house->getRequiredReset());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaHouseClearProtectionGuests(lua_State* L)
{
	// house:clearProtectionGuests()
	House* house = getUserdata<House>(L, 1);
	if (!house) {
		lua_pushnil(L);
		return 1;
	}

	house->clearProtectionGuests();
	pushBoolean(L, true);
	return 1;
}

} // namespace

void LuaScriptInterface::registerHouse()
{
	// House
	registerClass("House", "", luaHouseCreate);
	registerMetaMethod("House", "__eq", LuaScriptInterface::luaUserdataCompare);

	registerMethod("House", "getId", luaHouseGetId);
	registerMethod("House", "getName", luaHouseGetName);
	registerMethod("House", "getTown", luaHouseGetTown);
	registerMethod("House", "getExitPosition", luaHouseGetExitPosition);

	registerMethod("House", "getRent", luaHouseGetRent);
	registerMethod("House", "setRent", luaHouseSetRent);

	registerMethod("House", "getPaidUntil", luaHouseGetPaidUntil);
	registerMethod("House", "setPaidUntil", luaHouseSetPaidUntil);

	registerMethod("House", "getPayRentWarnings", luaHouseGetPayRentWarnings);
	registerMethod("House", "setPayRentWarnings", luaHouseSetPayRentWarnings);

	registerMethod("House", "getOwnerName", luaHouseGetOwnerName);
	registerMethod("House", "getOwnerGuid", luaHouseGetOwnerGuid);
	registerMethod("House", "setOwnerGuid", luaHouseSetOwnerGuid);
	registerMethod("House", "getOwnerAccountId", luaHouseGetOwnerAccountId);
	registerMethod("House", "getType", luaHouseGetType);
	registerMethod("House", "getOwnerGuild", luaHouseGetOwnerGuild);
	registerMethod("House", "startTrade", luaHouseStartTrade);

	registerMethod("House", "getBeds", luaHouseGetBeds);
	registerMethod("House", "getBedCount", luaHouseGetBedCount);

	registerMethod("House", "getDoors", luaHouseGetDoors);
	registerMethod("House", "getDoorCount", luaHouseGetDoorCount);
	registerMethod("House", "getDoorIdByPosition", luaHouseGetDoorIdByPosition);

	registerMethod("House", "getTiles", luaHouseGetTiles);
	registerMethod("House", "getItems", luaHouseGetItems);
	registerMethod("House", "getTileCount", luaHouseGetTileCount);

	registerMethod("House", "canEditAccessList", luaHouseCanEditAccessList);
	registerMethod("House", "getAccessList", luaHouseGetAccessList);
	registerMethod("House", "setAccessList", luaHouseSetAccessList);

	registerMethod("House", "kickPlayer", luaHouseKickPlayer);

	registerMethod("House", "save", luaHouseSave);

	// House protection system
	registerMethod("House", "getProtected", luaHouseGetProtected);
	registerMethod("House", "setProtected", luaHouseSetProtected);
	registerMethod("House", "addProtectionGuest", luaHouseAddProtectionGuest);
	registerMethod("House", "removeProtectionGuest", luaHouseRemoveProtectionGuest);
	registerMethod("House", "isProtectionGuest", luaHouseIsProtectionGuest);
	registerMethod("House", "getProtectionGuests", luaHouseGetProtectionGuests);
	registerMethod("House", "clearProtectionGuests", luaHouseClearProtectionGuests);

	registerMethod("House", "getRequiredReset", luaHouseGetRequiredReset);
}
