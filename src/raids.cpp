// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "raids.h"

#include "configmanager.h"
#include "game.h"
#include "monster.h"
#include "pugicast.h"
#include "scheduler.h"
#include "logger.h"
#include <fmt/format.h>

extern Game g_game;

namespace {

bool isValidRaidSpawnFile(std::string_view file)
{
	if (file.empty()) {
		return true;
	}

	std::filesystem::path path(file);
	return !path.has_root_path() && !path.has_parent_path() && path.filename() == path;
}

std::filesystem::path getRaidSpawnFileDirectory()
{
	std::string directory{ConfigManager::getString(ConfigManager::RAID_SPAWNFILE_DIRECTORY)};
	if (directory.empty()) {
		directory = "data/raids";
	}
	return directory;
}

struct RaidSpawnFileGroup
{
	Position center;
	std::vector<const RaidSpawnRecord*> records;
};

bool isInRaidSpawnFileGroup(const RaidSpawnFileGroup& group, const Position& position, int32_t radius)
{
	return group.center.z == position.z && std::abs(position.getX() - group.center.getX()) <= radius &&
	       std::abs(position.getY() - group.center.getY()) <= radius;
}

std::vector<RaidSpawnFileGroup> groupRaidSpawnRecords(const std::vector<RaidSpawnRecord>& records, int32_t radius)
{
	std::vector<RaidSpawnFileGroup> groups;

	for (const RaidSpawnRecord& record : records) {
		auto it = std::ranges::find_if(groups, [&record, radius](const RaidSpawnFileGroup& group) {
			return isInRaidSpawnFileGroup(group, record.position, radius);
		});

		if (it != groups.end()) {
			it->records.push_back(&record);
			continue;
		}

		RaidSpawnFileGroup& group = groups.emplace_back();
		group.center = record.position;
		group.records.push_back(&record);
	}

	return groups;
}

} // namespace

Raids::Raids() { }

Raids::~Raids() = default;

bool Raids::loadFromXml()
{
	if (isLoaded()) {
		return true;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file("data/raids/raids.xml");
	if (!result) {
		printXMLError("Error - Raids::loadFromXml", "data/raids/raids.xml", result);
		return false;
	}

	for (auto raidNode : doc.child("raids").children()) {
		std::string name, file, spawnFile;
		uint32_t interval, margin;

		pugi::xml_attribute attr;
		if ((attr = raidNode.attribute("name"))) {
			name = attr.as_string();
		} else {
			LOG_ERROR("[Error - Raids::loadFromXml] Name tag missing for raid");
			continue;
		}

		if ((attr = raidNode.attribute("file"))) {
			file = attr.as_string();
		} else {
			file = fmt::format("raids/{:s}.xml", name);
			LOG_WARN(fmt::format("[Warning - Raids::loadFromXml] File tag missing for raid {}. Using default: {}", name, file));
		}

		interval = pugi::cast<uint32_t>(raidNode.attribute("interval2").value()) * 60;
		if (interval == 0) {
			LOG_ERROR(fmt::format("[Error - Raids::loadFromXml] interval2 tag missing or zero (would divide by 0) for raid: {}", name));
			continue;
		}

		if ((attr = raidNode.attribute("margin"))) {
			margin = pugi::cast<uint32_t>(attr.value()) * 60 * 1000;
		} else {
			LOG_WARN(fmt::format("[Warning - Raids::loadFromXml] margin tag missing for raid: {}", name));
			margin = 0;
		}

		bool repeat;
		if ((attr = raidNode.attribute("repeat"))) {
			repeat = booleanString(attr.as_string());
		} else {
			repeat = false;
		}

		if ((attr = raidNode.attribute("spawnFile"))) {
			spawnFile = attr.as_string();
			if (!std::filesystem::path(spawnFile).has_extension()) {
				spawnFile += ".xml";
			}

			if (!isValidRaidSpawnFile(spawnFile)) {
				LOG_ERROR(fmt::format("[Error - Raids::loadFromXml] Invalid spawnFile tag for raid {}: {}", name, spawnFile));
				continue;
			}
		}

		auto newRaid = std::make_shared<Raid>(name, interval, margin, repeat, spawnFile);
		if (newRaid->loadFromXml("data/raids/" + file)) {
			raidList.push_back(std::move(newRaid));
		} else {
			LOG_ERROR(fmt::format("[Error - Raids::loadFromXml] Failed to load raid: {}", name));
		}
	}

	loaded = true;
	return true;
}

inline constexpr int32_t MAX_RAND_RANGE = 10000000;

bool Raids::startup()
{
	if (!isLoaded() || isStarted()) {
		return false;
	}

	setLastRaidEnd(OTSYS_TIME());

	// Make sure not to duplicate the event.
    if (checkRaidsEvent != 0) {
        g_scheduler.stopEvent(checkRaidsEvent);
        checkRaidsEvent = 0;
    }

	checkRaidsEvent =
	    g_scheduler.addEvent(createSchedulerTask(CHECK_RAIDS_INTERVAL * 1000, [this]() { checkRaids(); }));

	started = true;
	return started;
}

void Raids::shutdown()
{
    // Cancel the recursive verification event.
    if (checkRaidsEvent != 0) {
        g_scheduler.stopEvent(checkRaidsEvent);
        checkRaidsEvent = 0;
    }
    
    // For any running RAID
    if (running) {
        running->stopEvents();
        running = nullptr;
    }
    
    // Clear the raid list.
    raidList.clear();
    
    LOG_INFO("[Raids] Shutdown completed");
}

void Raids::checkRaids()
{
	if (!getRunning()) {
		uint64_t now = OTSYS_TIME();

		auto it = std::find_if(raidList.begin(), raidList.end(), [this, now](const auto& raidPtr) {
			Raid* raid = raidPtr.get();
			if (!raid->canBeRepeated() && raid->hasExecuted()) {
				return false;
			}

			if (now >= (getLastRaidEnd() + raid->getMargin())) {
				if (((MAX_RAND_RANGE * CHECK_RAIDS_INTERVAL) / raid->getInterval()) >=
				    static_cast<uint32_t>(uniform_random(0, MAX_RAND_RANGE))) {
					return true;
				}
			}
			return false;
		});

		if (it != raidList.end()) {
			Raid* raid = it->get();
			setRunning(raid);
			raid->startRaid(!raid->canBeRepeated());
		}
	}

	// FIX: Cancel previous event before scheduling a new one
    if (checkRaidsEvent != 0) {
        g_scheduler.stopEvent(checkRaidsEvent);
        checkRaidsEvent = 0;
    }

	checkRaidsEvent =
	    g_scheduler.addEvent(createSchedulerTask(CHECK_RAIDS_INTERVAL * 1000, [this]() { checkRaids(); }));
}

void Raids::clear()
{
	g_scheduler.stopEvent(checkRaidsEvent);
	checkRaidsEvent = 0;

	for (const auto& raid : raidList) {
		raid->stopEvents();
	}
	raidList.clear();

	loaded = false;
	started = false;
	running = nullptr;
	lastRaidEnd = 0;

	scriptInterface.reInitState();
}

bool Raids::reload()
{
	clear();
	return loadFromXml();
}

Raid* Raids::getRaidByName(std::string_view name)
{
	for (const auto& raid : raidList) {
		if (caseInsensitiveEqual(raid->getName(), name)) {
			return raid.get();
		}
	}
	return nullptr;
}

Raid::~Raid() = default;

bool Raid::loadFromXml(const std::string& filename)
{
	if (isLoaded()) {
		return true;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.c_str());
	if (!result) {
		printXMLError("Error - Raid::loadFromXml", filename, result);
		return false;
	}

	for (const auto& eventNode : doc.child("raid").children()) {
		std::unique_ptr<RaidEvent> event;
		if (caseInsensitiveEqual(eventNode.name(), "announce")) {
			event = std::make_unique<AnnounceEvent>();
		} else if (caseInsensitiveEqual(eventNode.name(), "singlespawn")) {
			event = std::make_unique<SingleSpawnEvent>();
		} else if (caseInsensitiveEqual(eventNode.name(), "areaspawn")) {
			event = std::make_unique<AreaSpawnEvent>();
		} else if (caseInsensitiveEqual(eventNode.name(), "script")) {
			event = std::make_unique<ScriptEvent>(&g_game.raids.getScriptInterface());
		} else {
			continue;
		}

		if (event->configureRaidEvent(eventNode)) {
			event->setRaid(this);
			raidEvents.push_back(std::move(event));
		} else {
			LOG_ERROR(fmt::format("[Error - Raid::loadFromXml] In file ({}), eventNode: {}", filename, eventNode.name()));
		}
	}

	// sort by delay time
	std::ranges::sort(raidEvents,
	          [](const auto& lhs, const auto& rhs) { return lhs->getDelay() < rhs->getDelay(); });

	loaded = true;
	return true;
}

void Raid::startRaid(bool markExecutedAfterExecution)
{
	this->markExecutedAfterExecution = markExecutedAfterExecution;
	spawnRecords.clear();

	RaidEvent* raidEvent = getNextRaidEvent();
	if (raidEvent) {
		state = RAIDSTATE_EXECUTING;
		std::weak_ptr<Raid> weakRaid = weak_from_this();
		nextEventEvent = g_scheduler.addEvent(createSchedulerTask(raidEvent->getDelay(), ([weakRaid, raidEvent]() {
			if (std::shared_ptr<Raid> raid = weakRaid.lock()) {
				raid->executeRaidEvent(raidEvent);
			}
		})));
	} else {
		resetRaid();
	}
}

void Raid::executeRaidEvent(RaidEvent* raidEvent)
{
	if (raidEvent->executeEvent()) {
		nextEvent++;
		RaidEvent* newRaidEvent = getNextRaidEvent();

		if (newRaidEvent) {
			uint32_t ticks = static_cast<uint32_t>(
			    std::max<int32_t>(RAID_MINTICKS, newRaidEvent->getDelay() - raidEvent->getDelay()));
			std::weak_ptr<Raid> weakRaid = weak_from_this();
			nextEventEvent = g_scheduler.addEvent(createSchedulerTask(ticks, ([weakRaid, newRaidEvent]() {
				if (std::shared_ptr<Raid> raid = weakRaid.lock()) {
					raid->executeRaidEvent(newRaidEvent);
				}
			})));
		} else {
			resetRaid();
		}
	} else {
		resetRaid();
	}
}

void Raid::resetRaid()
{
	saveSpawnFile();
	spawnRecords.clear();

	const bool markExecutedAfterReset = markExecutedAfterExecution;
	nextEvent = 0;
	nextEventEvent = 0;
	markExecutedAfterExecution = false;
	state = RAIDSTATE_IDLE;
	g_game.raids.setRunning(nullptr);
	g_game.raids.setLastRaidEnd(OTSYS_TIME());

	if (markExecutedAfterReset) {
		executed = true;
	}
}

void Raid::stopEvents()
{
	if (nextEventEvent != 0) {
		g_scheduler.stopEvent(nextEventEvent);
		nextEventEvent = 0;
	}
}

RaidEvent* Raid::getNextRaidEvent()
{
	if (nextEvent < raidEvents.size()) {
		return raidEvents[nextEvent].get();
	}
	return nullptr;
}

void Raid::recordSpawn(std::string_view monsterName, const Position& position)
{
	if (spawnFile.empty() || !ConfigManager::getBoolean(ConfigManager::RAID_SPAWN_FILE_ENABLED)) {
		return;
	}

	spawnRecords.emplace_back(monsterName, position);
}

bool Raid::saveSpawnFile() const
{
	if (spawnFile.empty() || !ConfigManager::getBoolean(ConfigManager::RAID_SPAWN_FILE_ENABLED)) {
		return true;
	}

	pugi::xml_document doc;
	pugi::xml_node declaration = doc.append_child(pugi::node_declaration);
	declaration.append_attribute("version") = "1.0";

	const int32_t spawnRadius = static_cast<int32_t>(std::max<int64_t>(0, ConfigManager::getInteger(ConfigManager::RAID_SPAWN_FILE_RADIUS)));
	const int64_t spawnTime = std::max<int64_t>(0, ConfigManager::getInteger(ConfigManager::RAID_SPAWN_FILE_SPAWNTIME));
	const int64_t direction = std::clamp<int64_t>(ConfigManager::getInteger(ConfigManager::RAID_SPAWN_FILE_DIRECTION), 0, 3);
	const std::vector<RaidSpawnFileGroup> groups = groupRaidSpawnRecords(spawnRecords, spawnRadius);

	pugi::xml_node spawnsNode = doc.append_child("spawns");
	for (const RaidSpawnFileGroup& group : groups) {
		pugi::xml_node spawnNode = spawnsNode.append_child("spawn");
		spawnNode.append_attribute("centerx") = group.center.x;
		spawnNode.append_attribute("centery") = group.center.y;
		spawnNode.append_attribute("centerz") = static_cast<unsigned int>(group.center.z);
		spawnNode.append_attribute("radius") = spawnRadius;

		for (const RaidSpawnRecord* record : group.records) {
			pugi::xml_node monsterNode = spawnNode.append_child("monster");
			monsterNode.append_attribute("name") = record->monsterName.c_str();
			monsterNode.append_attribute("x") = record->position.getX() - group.center.getX();
			monsterNode.append_attribute("y") = record->position.getY() - group.center.getY();
			monsterNode.append_attribute("z") = record->position.getZ() - group.center.getZ();
			monsterNode.append_attribute("spawntime") = spawnTime;
			monsterNode.append_attribute("direction") = direction;
		}
	}

	const std::filesystem::path directory = getRaidSpawnFileDirectory();
	std::error_code errorCode;
	std::filesystem::create_directories(directory, errorCode);
	if (errorCode) {
		LOG_ERROR(fmt::format("[Error - Raid::saveSpawnFile] Failed to create raid spawn directory: {} ({})", directory.string(), errorCode.message()));
		return false;
	}

	const std::filesystem::path filePath = directory / spawnFile;
	if (!doc.save_file(filePath.string().c_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
		LOG_ERROR(fmt::format("[Error - Raid::saveSpawnFile] Failed to save raid spawn file: {}", filePath.string()));
		return false;
	}

	LOG_INFO(fmt::format("[Raid] Saved {} generated monsters in {} spawn blocks to {}", spawnRecords.size(), groups.size(), filePath.string()));
	return true;
}

bool RaidEvent::configureRaidEvent(const pugi::xml_node& eventNode)
{
	pugi::xml_attribute delayAttribute = eventNode.attribute("delay");
	if (!delayAttribute) {
		LOG_ERROR("[Error] Raid: delay tag missing.");
		return false;
	}

	delay = std::max<uint32_t>(RAID_MINTICKS, pugi::cast<uint32_t>(delayAttribute.value()));
	return true;
}

bool AnnounceEvent::configureRaidEvent(const pugi::xml_node& eventNode)
{
	if (!RaidEvent::configureRaidEvent(eventNode)) {
		return false;
	}

	pugi::xml_attribute messageAttribute = eventNode.attribute("message");
	if (!messageAttribute) {
		LOG_ERROR("[Error] Raid: message tag missing for announce event.");
		return false;
	}
	message = messageAttribute.as_string();

	pugi::xml_attribute typeAttribute = eventNode.attribute("type");
	if (typeAttribute) {
		std::string tmpStrValue = asLowerCaseString(typeAttribute.as_string());
		if (tmpStrValue == "warning") {
			messageType = MESSAGE_STATUS_WARNING;
		} else if (tmpStrValue == "event") {
			messageType = MESSAGE_EVENT_ADVANCE;
		} else if (tmpStrValue == "default") {
			messageType = MESSAGE_EVENT_DEFAULT;
		} else if (tmpStrValue == "description") {
			messageType = MESSAGE_INFO_DESCR;
		} else if (tmpStrValue == "smallstatus") {
			messageType = MESSAGE_STATUS_SMALL;
		} else if (tmpStrValue == "blueconsole") {
			messageType = MESSAGE_STATUS_CONSOLE_BLUE;
		} else if (tmpStrValue == "redconsole") {
			messageType = MESSAGE_STATUS_CONSOLE_RED;
		} else {
			LOG_WARN(fmt::format("[Notice] Raid: Unknown type tag missing for announce event. Using default: {}", static_cast<uint32_t>(messageType)));
		}
	} else {
		messageType = MESSAGE_EVENT_ADVANCE;
		LOG_WARN(fmt::format("[Notice] Raid: type tag missing for announce event. Using default: {}", static_cast<uint32_t>(messageType)));
	}
	return true;
}

bool AnnounceEvent::executeEvent()
{
	g_game.broadcastMessage(message, messageType);
	return true;
}

bool SingleSpawnEvent::configureRaidEvent(const pugi::xml_node& eventNode)
{
	if (!RaidEvent::configureRaidEvent(eventNode)) {
		return false;
	}

	pugi::xml_attribute attr;
	if ((attr = eventNode.attribute("name"))) {
		monsterName = attr.as_string();
	} else {
		LOG_ERROR("[Error] Raid: name tag missing for singlespawn event.");
		return false;
	}

	if ((attr = eventNode.attribute("x"))) {
		position.x = pugi::cast<uint16_t>(attr.value());
	} else {
		LOG_ERROR("[Error] Raid: x tag missing for singlespawn event.");
		return false;
	}

	if ((attr = eventNode.attribute("y"))) {
		position.y = pugi::cast<uint16_t>(attr.value());
	} else {
		LOG_ERROR("[Error] Raid: y tag missing for singlespawn event.");
		return false;
	}

	if ((attr = eventNode.attribute("z"))) {
		position.z = pugi::cast<uint16_t>(attr.value());
	} else {
		LOG_ERROR("[Error] Raid: z tag missing for singlespawn event.");
		return false;
	}
	return true;
}

bool SingleSpawnEvent::executeEvent()
{
	auto monsterUnique = Monster::createMonster(monsterName);
	if (!monsterUnique) {
		LOG_ERROR(fmt::format("[Error] Raids: Cant create monster {}", monsterName));
		return false;
	}

	std::shared_ptr<Monster> monster(std::move(monsterUnique));
	if (!g_game.placeCreature(monster.get(), position, false, true)) {
		LOG_ERROR(fmt::format("[Error] Raids: Cant place monster {}", monsterName));
		return false;
	}

	if (Raid* raid = getRaid()) {
		raid->recordSpawn(monsterName, position);
	}
	return true;
}

bool AreaSpawnEvent::configureRaidEvent(const pugi::xml_node& eventNode)
{
	if (!RaidEvent::configureRaidEvent(eventNode)) {
		return false;
	}

	pugi::xml_attribute attr;
	if ((attr = eventNode.attribute("radius"))) {
		int32_t radius = pugi::cast<int32_t>(attr.value());
		Position centerPos;

		if ((attr = eventNode.attribute("centerx"))) {
			centerPos.x = pugi::cast<uint16_t>(attr.value());
		} else {
			LOG_ERROR("[Error] Raid: centerx tag missing for areaspawn event.");
			return false;
		}

		if ((attr = eventNode.attribute("centery"))) {
			centerPos.y = pugi::cast<uint16_t>(attr.value());
		} else {
			LOG_ERROR("[Error] Raid: centery tag missing for areaspawn event.");
			return false;
		}

		if ((attr = eventNode.attribute("centerz"))) {
			centerPos.z = pugi::cast<uint16_t>(attr.value());
		} else {
			LOG_ERROR("[Error] Raid: centerz tag missing for areaspawn event.");
			return false;
		}

		fromPos.x = static_cast<uint16_t>(std::max<int32_t>(0, centerPos.getX() - radius));
		fromPos.y = static_cast<uint16_t>(std::max<int32_t>(0, centerPos.getY() - radius));
		fromPos.z = centerPos.z;

		toPos.x = static_cast<uint16_t>(std::min<int32_t>(0xFFFF, centerPos.getX() + radius));
		toPos.y = static_cast<uint16_t>(std::min<int32_t>(0xFFFF, centerPos.getY() + radius));
		toPos.z = centerPos.z;
	} else {
		if ((attr = eventNode.attribute("fromx"))) {
			fromPos.x = pugi::cast<uint16_t>(attr.value());
		} else {
			LOG_ERROR("[Error] Raid: fromx tag missing for areaspawn event.");
			return false;
		}

		if ((attr = eventNode.attribute("fromy"))) {
			fromPos.y = pugi::cast<uint16_t>(attr.value());
		} else {
			LOG_ERROR("[Error] Raid: fromy tag missing for areaspawn event.");
			return false;
		}

		if ((attr = eventNode.attribute("fromz"))) {
			fromPos.z = static_cast<uint8_t>(pugi::cast<int32_t>(attr.value()));
		} else {
			LOG_ERROR("[Error] Raid: fromz tag missing for areaspawn event.");
			return false;
		}

		if ((attr = eventNode.attribute("tox"))) {
			toPos.x = pugi::cast<uint16_t>(attr.value());
		} else {
			LOG_ERROR("[Error] Raid: tox tag missing for areaspawn event.");
			return false;
		}

		if ((attr = eventNode.attribute("toy"))) {
			toPos.y = pugi::cast<uint16_t>(attr.value());
		} else {
			LOG_ERROR("[Error] Raid: toy tag missing for areaspawn event.");
			return false;
		}

		if ((attr = eventNode.attribute("toz"))) {
			toPos.z = static_cast<uint8_t>(pugi::cast<int32_t>(attr.value()));
		} else {
			LOG_ERROR("[Error] Raid: toz tag missing for areaspawn event.");
			return false;
		}
	}

	for (auto& monsterNode : eventNode.children()) {
		const char* name;

		if ((attr = monsterNode.attribute("name"))) {
			name = attr.value();
		} else {
			LOG_ERROR("[Error] Raid: name tag missing for monster node.");
			return false;
		}

		uint32_t minAmount;
		if ((attr = monsterNode.attribute("minamount"))) {
			minAmount = pugi::cast<uint32_t>(attr.value());
		} else {
			minAmount = 0;
		}

		uint32_t maxAmount;
		if ((attr = monsterNode.attribute("maxamount"))) {
			maxAmount = pugi::cast<uint32_t>(attr.value());
		} else {
			maxAmount = 0;
		}

		if (maxAmount == 0 && minAmount == 0) {
			if ((attr = monsterNode.attribute("amount"))) {
				minAmount = pugi::cast<uint32_t>(attr.value());
				maxAmount = minAmount;
			} else {
				LOG_ERROR("[Error] Raid: amount tag missing for monster node.");
				return false;
			}
		}

		spawnList.emplace_back(name, minAmount, maxAmount);
	}
	return true;
}

bool AreaSpawnEvent::executeEvent()
{
	for (const MonsterSpawn& spawn : spawnList) {
		uint32_t amount = uniform_random(spawn.minAmount, spawn.maxAmount);
		for (uint32_t i = 0; i < amount; ++i) {
			auto monsterUnique = Monster::createMonster(spawn.name);
			if (!monsterUnique) {
				LOG_ERROR(fmt::format("[Error - AreaSpawnEvent::executeEvent] Can't create monster {}", spawn.name));
				return false;
			}

			std::shared_ptr<Monster> monster(std::move(monsterUnique));
			bool placed = false;
			for (int32_t tries = 0; tries < MAXIMUM_TRIES_PER_MONSTER; tries++) {
				Tile* tile = g_game.map.getTile(static_cast<uint16_t>(uniform_random(fromPos.x, toPos.x)),
				                                static_cast<uint16_t>(uniform_random(fromPos.y, toPos.y)),
				                                static_cast<uint16_t>(uniform_random(fromPos.z, toPos.z)));
				if (tile && !tile->isMoveableBlocking() && !tile->hasFlag(TILESTATE_PROTECTIONZONE) &&
				    tile->getTopCreature() == nullptr &&
				    g_game.placeCreature(monster.get(), tile->getPosition(), false, true)) {
					if (Raid* raid = getRaid()) {
						raid->recordSpawn(spawn.name, tile->getPosition());
					}
					placed = true;
					break;
				}
			}

			if (!placed) {
				LOG_WARN(fmt::format("[Warning - AreaSpawnEvent::executeEvent] Could not place monster {} after {} tries", spawn.name, MAXIMUM_TRIES_PER_MONSTER));
			}
		}
	}
	return true;
}

bool ScriptEvent::configureRaidEvent(const pugi::xml_node& eventNode)
{
	if (!RaidEvent::configureRaidEvent(eventNode)) {
		return false;
	}

	pugi::xml_attribute scriptAttribute = eventNode.attribute("script");
	if (!scriptAttribute) {
		LOG_ERROR("Error: [ScriptEvent::configureRaidEvent] No script file found for raid");
		return false;
	}

	if (!loadScript("data/raids/scripts/" + std::string(scriptAttribute.as_string()))) {
		LOG_ERROR("Error: [ScriptEvent::configureRaidEvent] Can not load raid script.");
		return false;
	}
	return true;
}

bool ScriptEvent::executeEvent()
{
	// onRaid()
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - ScriptEvent::onRaid] Call stack overflow");
		return false;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(scriptId, scriptInterface);

	scriptInterface->pushFunction(scriptId);

	return scriptInterface->callFunction(0);
}
