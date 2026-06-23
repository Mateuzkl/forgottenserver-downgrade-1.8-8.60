// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_RAIDS_H
#define FS_RAIDS_H

#include "baseevents.h"
#include "const.h"
#include "observer_ptr.h"
#include "position.h"

enum RaidState_t
{
	RAIDSTATE_IDLE,
	RAIDSTATE_EXECUTING,
};

struct MonsterSpawn
{
	MonsterSpawn(std::string_view name, uint32_t minAmount, uint32_t maxAmount) :
	    name{name}, minAmount{minAmount}, maxAmount{maxAmount}
	{}

	std::string name;
	uint32_t minAmount;
	uint32_t maxAmount;
};

struct RaidSpawnRecord
{
	RaidSpawnRecord(std::string_view monsterName, const Position& position) :
	    monsterName{monsterName}, position{position}
	{}

	std::string monsterName;
	Position position;
};

// How many times it will try to find a tile to add the monster to before giving up
inline constexpr int32_t MAXIMUM_TRIES_PER_MONSTER = 10;
inline constexpr int32_t CHECK_RAIDS_INTERVAL = 60;
inline constexpr int32_t RAID_MINTICKS = 1000;

class Raid;
class RaidEvent;

class Raids
{
public:
	Raids();
	~Raids();

	// non-copyable
	Raids(const Raids&) = delete;
	Raids& operator=(const Raids&) = delete;

	bool loadFromXml();
	bool startup();
	void shutdown();

	void clear();
	bool reload();

	bool isLoaded() const { return loaded; }
	bool isStarted() const { return started; }

	Raid* getRunning() { return running; }
	void setRunning(Raid* newRunning) { running = newRunning; }

	Raid* getRaidByName(std::string_view name);

	uint64_t getLastRaidEnd() const { return lastRaidEnd; }
	void setLastRaidEnd(uint64_t newLastRaidEnd) { lastRaidEnd = newLastRaidEnd; }

	void checkRaids();

	LuaScriptInterface& getScriptInterface() { return scriptInterface; }

private:
	LuaScriptInterface scriptInterface{"Raid Interface"};

	std::list<std::shared_ptr<Raid>> raidList;
	ObserverPtr<Raid> running = nullptr;
	uint64_t lastRaidEnd = 0;
	uint32_t checkRaidsEvent = 0;
	bool loaded = false;
	bool started = false;
};

class Raid : public std::enable_shared_from_this<Raid>
{
public:
	Raid(std::string_view name, uint32_t interval, uint32_t marginTime, bool repeat, std::string_view spawnFile) :
	    name{name}, spawnFile{spawnFile}, interval{interval}, margin{marginTime}, repeat{repeat}
	{}
	~Raid();

	// non-copyable
	Raid(const Raid&) = delete;
	Raid& operator=(const Raid&) = delete;

	bool loadFromXml(const std::string& filename);

	void startRaid(bool markExecutedAfterExecution = false);

	void executeRaidEvent(RaidEvent* raidEvent);
	void resetRaid();

	RaidEvent* getNextRaidEvent();
	void setState(RaidState_t newState) { state = newState; }
	const std::string& getName() const { return name; }

	bool isLoaded() const { return loaded; }
	uint64_t getMargin() const { return margin; }
	uint32_t getInterval() const { return interval; }
	bool canBeRepeated() const { return repeat; }
	bool hasExecuted() const { return executed; }

	void recordSpawn(std::string_view monsterName, const Position& position);

	void stopEvents();

private:
	bool saveSpawnFile() const;

	std::vector<std::unique_ptr<RaidEvent>> raidEvents;
	std::vector<RaidSpawnRecord> spawnRecords;
	std::string name;
	std::string spawnFile;
	uint32_t interval;
	uint32_t nextEvent = 0;
	uint64_t margin;
	RaidState_t state = RAIDSTATE_IDLE;
	uint32_t nextEventEvent = 0;
	bool loaded = false;
	bool repeat;
	bool executed = false;
	bool markExecutedAfterExecution = false;
};

class RaidEvent
{
public:
	virtual ~RaidEvent() = default;

	virtual bool configureRaidEvent(const pugi::xml_node& eventNode);

	virtual bool executeEvent() = 0;
	uint32_t getDelay() const { return delay; }

	void setRaid(Raid* newRaid) { raid = newRaid; }
	Raid* getRaid() const { return raid; }

private:
	ObserverPtr<Raid> raid = nullptr;
	uint32_t delay = 0;
};

class AnnounceEvent final : public RaidEvent
{
public:
	AnnounceEvent() = default;

	bool configureRaidEvent(const pugi::xml_node& eventNode) override;

	bool executeEvent() override;

private:
	std::string message;
	MessageClasses messageType = MESSAGE_EVENT_ADVANCE;
};

class SingleSpawnEvent final : public RaidEvent
{
public:
	bool configureRaidEvent(const pugi::xml_node& eventNode) override;

	bool executeEvent() override;

private:
	std::string monsterName;
	Position position;
};

class AreaSpawnEvent final : public RaidEvent
{
public:
	bool configureRaidEvent(const pugi::xml_node& eventNode) override;

	bool executeEvent() override;

private:
	std::list<MonsterSpawn> spawnList;
	Position fromPos, toPos;
};

class ScriptEvent final : public RaidEvent, public Event
{
public:
	explicit ScriptEvent(LuaScriptInterface* interface) : Event(interface) {}

	bool configureRaidEvent(const pugi::xml_node& eventNode) override;
	bool executeEvent() override;

private:
	std::string_view getScriptEventName() const { return "onRaid"; }
};

#endif
