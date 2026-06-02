// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "luascript.h"
#include "movement.h"
#include "script.h"
#include "scriptmanager.h"
#include "logger.h"
#include <fmt/format.h>

namespace {
using namespace Lua;

// MoveEvent
int luaCreateMoveEvent(lua_State* L)
{
	// MoveEvent()
	if (LuaScriptInterface::getScriptEnv()->getScriptInterface() != &g_scripts->getScriptInterface()) {
		reportErrorFunc(L, "MoveEvents can only be registered in the Scripts interface.");
		lua_pushnil(L);
		return 1;
	}

	auto moveevent = std::make_unique<MoveEvent>(LuaScriptInterface::getScriptEnv()->getScriptInterface());
	moveevent->fromLua = true;
	pushOwnedUserdata<MoveEvent>(L, std::move(moveevent));
	setMetatable(L, -1, "MoveEvent");
	return 1;
}

int luaMoveEventType(lua_State* L)
{
	// moveevent:type(callback)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		std::string typeName = getString(L, 2);
		std::string tmpStr = asLowerCaseString(typeName);
		if (tmpStr == "stepin") {
			moveevent->setEventType(MOVE_EVENT_STEP_IN);
			moveevent->stepFunction = moveevent->StepInField;
		} else if (tmpStr == "stepout") {
			moveevent->setEventType(MOVE_EVENT_STEP_OUT);
			moveevent->stepFunction = moveevent->StepOutField;
		} else if (tmpStr == "equip") {
			moveevent->setEventType(MOVE_EVENT_EQUIP);
			moveevent->equipFunction = moveevent->EquipItem;
		} else if (tmpStr == "deequip") {
			moveevent->setEventType(MOVE_EVENT_DEEQUIP);
			moveevent->equipFunction = moveevent->DeEquipItem;
		} else if (tmpStr == "additem") {
			moveevent->setEventType(MOVE_EVENT_ADD_ITEM);
			moveevent->moveFunction = moveevent->AddItemField;
		} else if (tmpStr == "removeitem") {
			moveevent->setEventType(MOVE_EVENT_REMOVE_ITEM);
			moveevent->moveFunction = moveevent->RemoveItemField;
		} else {
			LOG_ERROR(fmt::format("[MoveEvent::configureMoveEvent] No valid event name {}", typeName));
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMoveEventRegister(lua_State* L)
{
	// moveevent:register()
	if (auto moveevent = getUserdata<MoveEvent>(L, 1)) {
		if (!moveevent->isScripted()) {
			pushBoolean(L, g_moveEvents->registerLuaFunction(releaseOwnedUserdata<MoveEvent>(L, 1)));
		} else {
			pushBoolean(L, g_moveEvents->registerLuaEvent(releaseOwnedUserdata<MoveEvent>(L, 1)));
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMoveEventOnCallback(lua_State* L)
{
	// moveevent:onEquip / deEquip / etc. (callback)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		if (!moveevent->loadCallback()) {
			pushBoolean(L, false);
			return 1;
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMoveEventSlot(lua_State* L)
{
	// moveevent:slot(slot)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (!moveevent) {
		lua_pushnil(L);
		return 1;
	}

	if (moveevent->getEventType() == MOVE_EVENT_EQUIP || moveevent->getEventType() == MOVE_EVENT_DEEQUIP) {
		std::string slotName = asLowerCaseString(getString(L, 2));
		if (slotName == "head") {
			moveevent->setSlot(SLOTP_HEAD);
		} else if (slotName == "necklace") {
			moveevent->setSlot(SLOTP_NECKLACE);
		} else if (slotName == "backpack") {
			moveevent->setSlot(SLOTP_BACKPACK);
		} else if (slotName == "armor" || slotName == "body") {
			moveevent->setSlot(SLOTP_ARMOR);
		} else if (slotName == "right-hand") {
			moveevent->setSlot(SLOTP_RIGHT);
		} else if (slotName == "left-hand") {
			moveevent->setSlot(SLOTP_LEFT);
		} else if (slotName == "hand" || slotName == "shield") {
			moveevent->setSlot(SLOTP_RIGHT | SLOTP_LEFT);
		} else if (slotName == "legs") {
			moveevent->setSlot(SLOTP_LEGS);
		} else if (slotName == "feet") {
			moveevent->setSlot(SLOTP_FEET);
		} else if (slotName == "ring") {
			moveevent->setSlot(SLOTP_RING);
		} else if (slotName == "ammo") {
			moveevent->setSlot(SLOTP_AMMO);
		} else {
			LOG_WARN(fmt::format("[Warning - MoveEvent::configureMoveEvent] Unknown slot type: {}", slotName));
			pushBoolean(L, false);
			return 1;
		}
	}

	pushBoolean(L, true);
	return 1;
}

int luaMoveEventLevel(lua_State* L)
{
	// moveevent:level(lvl)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->setRequiredLevel(getInteger<uint32_t>(L, 2));
		moveevent->setWieldInfo(WIELDINFO_LEVEL);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMoveEventMagLevel(lua_State* L)
{
	// moveevent:magicLevel(lvl)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->setRequiredMagLevel(getInteger<uint32_t>(L, 2));
		moveevent->setWieldInfo(WIELDINFO_MAGLV);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMoveEventResets(lua_State* L)
{
	// moveevent:resets(count)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->setRequiredResets(getInteger<uint32_t>(L, 2));
		moveevent->setWieldInfo(WIELDINFO_RESETS);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMoveEventPremium(lua_State* L)
{
	// moveevent:premium(bool)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->setNeedPremium(getBoolean(L, 2));
		moveevent->setWieldInfo(WIELDINFO_PREMIUM);
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMoveEventVocation(lua_State* L)
{
	// moveevent:vocation(vocName[, showInDescription = false, lastVoc = false])
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->addVocationEquipSet(getString(L, 2));
		moveevent->setWieldInfo(WIELDINFO_VOCREQ);
		std::string tmp;
		bool showInDescription = false;
		bool lastVoc = false;
		if (getBoolean(L, 3)) {
			showInDescription = getBoolean(L, 3);
		}
		if (getBoolean(L, 4)) {
			lastVoc = getBoolean(L, 4);
		}
		if (showInDescription) {
			if (moveevent->getVocationString().empty()) {
				tmp = asLowerCaseString(getString(L, 2));
				tmp += "s";
				moveevent->setVocationString(tmp);
			} else {
				tmp = moveevent->getVocationString();
				if (lastVoc) {
					tmp += " and ";
				} else {
					tmp += ", ";
				}
				tmp += asLowerCaseString(getString(L, 2));
				tmp += "s";
				moveevent->setVocationString(tmp);
			}
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMoveEventTileItem(lua_State* L)
{
	// moveevent:tileItem(bool)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		moveevent->setTileItem(getBoolean(L, 2));
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMoveEventItemId(lua_State* L)
{
	// moveevent:id(ids)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				moveevent->addItemId(getInteger<uint16_t>(L, 2 + i));
			}
		} else {
			moveevent->addItemId(getInteger<uint16_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMoveEventActionId(lua_State* L)
{
	// moveevent:aid(ids)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				moveevent->addActionId(getInteger<uint16_t>(L, 2 + i));
			}
		} else {
			moveevent->addActionId(getInteger<uint16_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMoveEventUniqueId(lua_State* L)
{
	// moveevent:uid(ids)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				moveevent->addUniqueId(getInteger<uint16_t>(L, 2 + i));
			}
		} else {
			moveevent->addUniqueId(getInteger<uint16_t>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMoveEventZoneId(lua_State* L)
{
	// moveevent:zoneid(ids)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		int parameters = lua_gettop(L) - 1;
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				moveevent->addZoneId(getInteger<ZoneId>(L, 2 + i));
			}
		} else {
			moveevent->addZoneId(getInteger<ZoneId>(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaMoveEventPosition(lua_State* L)
{
	// moveevent:position(positions)
	MoveEvent* moveevent = getUserdata<MoveEvent>(L, 1);
	if (moveevent) {
		int parameters = lua_gettop(L) - 1; // - 1 because self is a parameter aswell, which we want to skip ofc
		if (parameters > 1) {
			for (int i = 0; i < parameters; ++i) {
				moveevent->addPosList(getPosition(L, 2 + i));
			}
		} else {
			moveevent->addPosList(getPosition(L, 2));
		}
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaDeleteMoveEvent(lua_State* L)
{
	return deleteOwnedUserdata(L);
}
} // namespace

void LuaScriptInterface::registerMoveEvents()
{
	// MoveEvent
	registerClass("MoveEvent", "", luaCreateMoveEvent);
	registerMetaMethod("MoveEvent", "__gc", luaDeleteMoveEvent);
	registerMetaMethod("MoveEvent", "__close", luaDeleteMoveEvent);
	registerMethod("MoveEvent", "delete", luaDeleteMoveEvent);

	registerMethod("MoveEvent", "type", luaMoveEventType);
	registerMethod("MoveEvent", "register", luaMoveEventRegister);
	registerMethod("MoveEvent", "level", luaMoveEventLevel);
	registerMethod("MoveEvent", "magicLevel", luaMoveEventMagLevel);
	registerMethod("MoveEvent", "resets", luaMoveEventResets);
	registerMethod("MoveEvent", "slot", luaMoveEventSlot);
	registerMethod("MoveEvent", "id", luaMoveEventItemId);
	registerMethod("MoveEvent", "aid", luaMoveEventActionId);
	registerMethod("MoveEvent", "uid", luaMoveEventUniqueId);
	registerMethod("MoveEvent", "zoneid", luaMoveEventZoneId);
	registerMethod("MoveEvent", "zoneId", luaMoveEventZoneId);
	registerMethod("MoveEvent", "position", luaMoveEventPosition);
	registerMethod("MoveEvent", "premium", luaMoveEventPremium);
	registerMethod("MoveEvent", "vocation", luaMoveEventVocation);
	registerMethod("MoveEvent", "tileItem", luaMoveEventTileItem);
	registerMethod("MoveEvent", "onEquip", luaMoveEventOnCallback);
	registerMethod("MoveEvent", "onDeEquip", luaMoveEventOnCallback);
	registerMethod("MoveEvent", "onStepIn", luaMoveEventOnCallback);
	registerMethod("MoveEvent", "onStepOut", luaMoveEventOnCallback);
	registerMethod("MoveEvent", "onAddItem", luaMoveEventOnCallback);
	registerMethod("MoveEvent", "onRemoveItem", luaMoveEventOnCallback);
}
