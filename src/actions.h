// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_ACTIONS_H
#define FS_ACTIONS_H

#include "baseevents.h"
#include "enums.h"
#include "luascript.h"

#include <functional>
#include <memory>

class Action;
using Action_ptr = std::unique_ptr<Action>;
using ActionFunction = std::function<bool(Player*, const std::shared_ptr<Item>&, const Position&, Thing*,
                                          const Position&, bool)>;

class Action : public Event
{
public:
	explicit Action(LuaScriptInterface* interface);

	// scripting
	virtual bool executeUse(Player*, const std::shared_ptr<Item>&, const Position&, Thing*, const Position&, bool);

	bool getAllowFarUse() const { return allowFarUse; }
	void setAllowFarUse(bool v) { allowFarUse = v; }

	bool getCheckLineOfSight() const { return checkLineOfSight; }
	void setCheckLineOfSight(bool v) { checkLineOfSight = v; }

	bool getCheckFloor() const { return checkFloor; }
	void setCheckFloor(bool v) { checkFloor = v; }

	auto stealItemIdRange()
	{
		std::vector<uint16_t> ret{};
		std::swap(ids, ret);
		return ret;
	}
	void addItemId(uint16_t id) { ids.emplace_back(id); }

	auto stealUniqueIdRange()
	{
		std::vector<uint16_t> ret{};
		std::swap(uids, ret);
		return ret;
	}
	void addUniqueId(uint16_t id) { uids.emplace_back(id); }

	auto stealActionIdRange()
	{
		std::vector<uint16_t> ret{};
		std::swap(aids, ret);
		return ret;
	}
	void addActionId(uint16_t id) { aids.emplace_back(id); }

	void setPositionList(const std::vector<Position>& posList) { positionList = posList; }
	const std::vector<Position>& getPositionList() const { return positionList; }
	bool hasPosition() const { return !positionList.empty(); }

	virtual ReturnValue canExecuteAction(const Player* player, const Position& toPos);
	virtual bool hasOwnErrorHandler() { return false; }
	virtual Thing* getTarget(Player* player, Creature* targetCreature, const Position& toPosition,
	                         uint8_t toStackPos) const;

	ActionFunction function;

private:
	std::string_view getScriptEventName() const override;

	bool allowFarUse = false;
	bool checkFloor = true;
	bool checkLineOfSight = true;
	std::vector<uint16_t> ids;
	std::vector<uint16_t> uids;
	std::vector<uint16_t> aids;
	std::vector<Position> positionList;
};

class Actions final : public BaseEvents
{
public:
	Actions();
	~Actions();

	// non-copyable
	Actions(const Actions&) = delete;
	Actions& operator=(const Actions&) = delete;

	bool useItem(Player*, const Position&, uint8_t, const std::shared_ptr<Item>&, bool);
	bool useItemEx(Player*, const Position&, const Position&, uint8_t, const std::shared_ptr<Item>&, bool,
	               Creature* = nullptr);

	ReturnValue canUse(const Player* player, const Position& pos);
	ReturnValue canUse(const Player*, const Position&, const Item*); // raw ptr: lifetime guaranteed by caller
	ReturnValue canUseFar(const Creature* creature, const Position& toPos, bool checkLineOfSight, bool checkFloor);

	bool registerLuaEvent(Action* event);
	void clear(bool fromLua) override final;

	[[nodiscard]] bool hasRegisteredUseAction(uint16_t itemId) const
	{
		return useItemMap.find(itemId) != useItemMap.end();
	}

private:
	ReturnValue internalUseItem(Player*, const Position&, uint8_t, const std::shared_ptr<Item>&, bool);

	LuaScriptInterface& getScriptInterface() override;
	std::string_view getScriptBaseName() const override;

	using ActionUseMap = std::map<uint16_t, Action>;
	ActionUseMap useItemMap;
	ActionUseMap uniqueItemMap;
	ActionUseMap actionItemMap;
	std::map<Position, Action> positionMap;

	Action* getAction(const Item*); // raw ptr: lifetime guaranteed by caller
	Action* getAction(const Position& pos);
	void clearMap(ActionUseMap& map, bool fromLua);

	LuaScriptInterface scriptInterface;
};

#endif
