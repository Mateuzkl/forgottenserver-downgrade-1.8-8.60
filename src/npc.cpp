// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "npc.h"

#include "configmanager.h"
#include "game.h"
#include "logger.h"
#include "pugicast.h"

#if defined(__GNUC__) && (__GNUC__ < 8)
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

extern Game g_game;
extern LuaEnvironment g_luaEnvironment;

uint32_t Npc::npcAutoID = 0x80000000;

// ─── Npcs namespace ───────────────────────────────────────────────────────────

namespace Npcs {

bool loaded = false;
std::shared_ptr<NpcScriptInterface> scriptInterface;
std::map<std::string, std::shared_ptr<NpcType>> npcTypes;

namespace {
std::string getConfiguredNpcSystem()
{
	std::string npcSystem = std::string(ConfigManager::getString(ConfigManager::NPC_SYSTEM));
	std::transform(npcSystem.begin(), npcSystem.end(), npcSystem.begin(),
	               [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

	if (npcSystem == "crystal") {
		return npcSystem;
	}

	if (npcSystem != "tfs") {
		LOG_WARN(fmt::format("[Warning - Npcs::load] Unknown npcSystem '{}', falling back to 'tfs'.", npcSystem));
	}
	return "tfs";
}

std::string getNpcLibPath(const std::string& npcSystem)
{
	return "data/npc/lib/" + npcSystem + "/init.lua";
}

std::vector<fs::path> getNpcScriptDirectories(const std::string& npcSystem)
{
	std::vector<fs::path> scriptDirs;

	if (npcSystem == "tfs") {
		scriptDirs.emplace_back("data/npc/lua");
		return scriptDirs;
	}

	for (const fs::path& path : {fs::path{"data/npc/crystalserver"}, fs::path{"data/npc/crystal"}, fs::path{"data/npc/npc_Crystal_Server_15x"}}) {
		if (fs::exists(path) && fs::is_directory(path)) {
			scriptDirs.emplace_back(path);
		}
	}

	return scriptDirs;
}
} // namespace

void load(bool reload /*= false*/)
{
	if (!reload && loaded) {
		return;
	}

	if (!scriptInterface || reload) {
		scriptInterface = std::make_shared<NpcScriptInterface>();
	}

	const std::string npcSystem = getConfiguredNpcSystem();
	const std::string npcLibPath = getNpcLibPath(npcSystem);
	if (!scriptInterface->loadNpcLib(npcLibPath)) {
		LOG_WARN(fmt::format("[Warning - Npcs::load] Can not load {} NPC lib: {}", npcSystem, npcLibPath));
		LOG_WARN(scriptInterface->getLastLuaError());
		return;
	}

	loaded = true;

	if (!reload) {
		LOG_INFO(fmt::format(">> NpcLib loaded ({})", npcSystem));
	} else {
		LOG_INFO(fmt::format(">> NpcLib reloaded ({})", npcSystem));
	}
}

bool loadScripts(bool reload /* = false */)
{
	if (!scriptInterface) {
		load(reload);
	}

	const std::string npcSystem = getConfiguredNpcSystem();
	std::vector<fs::path> scriptDirs = getNpcScriptDirectories(npcSystem);

	std::sort(scriptDirs.begin(), scriptDirs.end());

	for (const auto& dirPath : scriptDirs) {
		if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
			LOG_WARN(fmt::format("[Warning - Npcs::loadScripts] NPC script folder does not exist: {}", dirPath.string()));
			continue;
		}

		LOG_INFO(fmt::format(">> Loading NPC scripts from {}", dirPath.string()));

		std::vector<fs::path> files;
		try {
			for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
				const fs::path path = entry.path();
				if (fs::is_regular_file(path) && path.extension() == ".lua") {
					const std::string filename = path.filename().string();
					if (filename.find('#') == std::string::npos) {
						files.emplace_back(path);
					}
				}
			}
		} catch (const std::exception& e) {
			LOG_WARN(fmt::format("[Warning - Npcs::loadScripts] Can not scan {}: {}", dirPath.string(), e.what()));
			continue;
		}

		std::sort(files.begin(), files.end());

		lua_State* L = scriptInterface->getLuaState();
		lua_newtable(L);
		for (const auto& path : files) {
			const std::string filename = path.filename().generic_string();
			const std::string relativeFile = path.lexically_relative(dirPath).generic_string();
			const std::string scriptFile = path.generic_string();
			lua_pushstring(L, scriptFile.c_str());
			lua_setfield(L, -2, filename.c_str());
			lua_pushstring(L, scriptFile.c_str());
			lua_setfield(L, -2, relativeFile.c_str());
		}
		lua_setglobal(L, "__npcScriptFilesByName");

		for (const auto& path : files) {
			const std::string scriptFile = path.generic_string();
			const std::string scriptDir = path.parent_path().generic_string();
			const std::string scriptRoot = dirPath.generic_string();

			lua_pushstring(L, scriptFile.c_str());
			lua_setglobal(L, "__npcCurrentScriptFile");
			lua_pushstring(L, scriptDir.c_str());
			lua_setglobal(L, "__npcCurrentScriptDir");
			lua_pushstring(L, scriptRoot.c_str());
			lua_setglobal(L, "__npcCurrentScriptRoot");

			if (scriptInterface->loadFile(scriptFile) == -1) {
				LOG_ERROR("> " + scriptFile + " [error]");
				LOG_ERROR(fmt::format("^ {}", scriptInterface->getLastLuaError()));
			} else {
				LOG_DEBUG("> " + scriptFile + " [loaded]");
			}

			lua_pushnil(L);
			lua_setglobal(L, "__npcCurrentScriptFile");
			lua_pushnil(L);
			lua_setglobal(L, "__npcCurrentScriptDir");
			lua_pushnil(L);
			lua_setglobal(L, "__npcCurrentScriptRoot");
		}
	}

	// Summary of registered NPC types (hybrid: Lua + XML)
	size_t luaCount = 0;
	for (const auto& it : npcTypes) {
		const std::string& ntName = it.first;
		const auto& ntType = it.second;
		if (ntType->fromLua) {
			++luaCount;
			std::ostringstream ss;
			ss << "   [Lua NPC] '" << ntName << "' lookType=" << ntType->defaultOutfit.lookType;
			if (ntType->npcEventHandler) {
				ss << " events(say=" << ntType->npcEventHandler->creatureSayEvent 
				   << " appear=" << ntType->npcEventHandler->creatureAppearEvent 
				   << " think=" << ntType->npcEventHandler->thinkEvent << ")";
			}
			LOG_DEBUG(ss.str());
			
			if (ntType->defaultOutfit.lookType == 0 && ntType->defaultOutfit.lookTypeEx == 0) {
				LOG_WARN("   [Warning] Lua NPC '" + ntName + "' has NO outfit (lookType=0) - will be INVISIBLE!");
			}
			if (ntType->npcEventHandler && ntType->npcEventHandler->creatureSayEvent == -1) {
				LOG_WARN("   [Warning] Lua NPC '" + ntName + "' has NO onSay event - will NOT respond to players!");
			}
		}
	}
	if (luaCount > 0) {
		std::ostringstream ss;
		ss << ">> " << luaCount << " Lua NPC type(s) registered (" << npcSystem << " backend, hybrid mode active)";
		LOG_INFO(ss.str());
	} else {
		LOG_WARN(fmt::format(">> No Lua NPC types registered for '{}' backend - only XML NPCs will work.", npcSystem));
	}

	return true;
}

void reload()
{
	// 1. Reload the NPC Lua library (creates fresh scriptInterface)
	load(true);

	// 2. Close all active shop windows before modifying types
	const auto& npcsMap = g_game.getNpcs();
	for (const auto& it : npcsMap) {
		if (auto npc = it.second.lock()) {
			npc->closeAllShopWindows();
			npc->setCreatureFocus(nullptr);
		}
	}

	// 3. Clear Lua NPC types from registry (preserve XML types)
	std::erase_if(npcTypes, [](const auto& entry) { return entry.second && entry.second->fromLua; });

	// 4. Reload XML NPC definitions
	for (const auto& it : npcTypes) {
		if (!it.second->fromLua) {
			it.second->loadFromXml();
		}
	}

	// 5. Re-execute Lua NPC scripts (re-registers fromLua types)
	loadScripts(true);

	// 6. Refresh all active NPC instances on the map
	for (const auto& it : npcsMap) {
		auto npcRef = it.second.lock();
		if (!npcRef) {
			continue;
		}

		if (npcRef->npcType && npcRef->npcType->fromLua) {
			// Re-lookup type from refreshed registry
			auto freshType = getNpcType(npcRef->getName());
			if (freshType && freshType->fromLua) {
				npcRef->npcType = freshType;
				npcRef->loadNpcTypeInfo();

				// Create fresh event handler from template (avoids stale callback IDs)
				npcRef->npcEventHandler = std::make_unique<NpcEventsHandler>(*freshType->npcEventHandler);
				npcRef->npcEventHandler->loaded = true;
				npcRef->npcEventHandler->setNpc(npcRef);

				// Re-initialize runtime state: re-populate spectators, set idle,
				// start walk events, and fire onCreatureAppear to bootstrap Lua state.
				// Npc::reload() already skips reset(true) for fromLua NPCs.
				npcRef->reload();
			} else {
				// Lua NPC type was removed — clear handler
				LOG_WARN(fmt::format(">> [Reload] Lua NPC '{}' type no longer registered.", npcRef->getName()));
				npcRef->npcEventHandler.reset();
			}
		} else {
			// XML NPC: standard reload path
			npcRef->reload();
		}
	}
}

void addNpcType(const std::string& name, const std::shared_ptr<NpcType>& npcType)
{
	npcTypes[name] = npcType;
}

void clearNpcTypes()
{
	npcTypes.clear();
}

const std::map<std::string, std::shared_ptr<NpcType>>& getNpcTypes()
{
	return npcTypes;
}

std::shared_ptr<NpcType> getNpcType(const std::string& name)
{
	auto it = npcTypes.find(name);
	if (it != npcTypes.end()) {
		return it->second;
	}

	for (const auto& [npcName, npcType] : npcTypes) {
		if (caseInsensitiveEqual(npcName, name)) {
			return npcType;
		}
	}
	return nullptr;
}

NpcScriptInterface* getScriptInterface()
{
	return scriptInterface.get();
}

} // namespace Npcs

namespace {
std::shared_ptr<Npc> makeNpcScriptHandle(Npc* npc)
{
	if (!npc) {
		return nullptr;
	}

	if (auto creatureRef = g_game.getCreatureSharedRef(npc)) {
		return std::static_pointer_cast<Npc>(creatureRef);
	}

	return std::shared_ptr<Npc>(npc, [](Npc*) {});
}
} // namespace

// ─── Npc ──────────────────────────────────────────────────────────────────────

std::unique_ptr<Npc> Npc::createNpc(const std::string& name)
{
	// Hybrid NPC system: first check if the NPC was registered via Lua (RevScript),
	// if not found, fall back to the traditional XML system.
	auto npcType = Npcs::getNpcType(name);
	if (npcType) {
		// Found in registry (either Lua or previously cached XML)
		LOG_DEBUG(fmt::format(">> [Hybrid] NPC '{}' -> {} (lookType={})",
			name, npcType->fromLua ? "Lua RevScript" : "XML (cached)", npcType->defaultOutfit.lookType));
	} else {
		// Not found in Lua registry, try traditional XML
		npcType = std::make_shared<NpcType>();
		npcType->name = name;
		npcType->filename = "data/npc/" + name + ".xml";
		if (!npcType->loadFromXml()) {
			const bool xmlExists = fs::exists(npcType->filename);
			LOG_ERROR(fmt::format(
			    "[Error - Npc::createNpc] NPC '{}' was not found as a RevScript NPC in any data/npc/ folder "
			    "(except lib/).",
			    name));
			if (!xmlExists) {
				LOG_ERROR(fmt::format("[Error - Npc::createNpc] XML data/npc/{}.xml also does not exist.", name));
			} else {
				LOG_ERROR(fmt::format("[Error - Npc::createNpc] XML {} exists but failed to load. See the XML error above.",
				                      npcType->filename));
			}
			LOG_ERROR(fmt::format(
			    "[Error - Npc::createNpc] Fix: create a .lua file in any data/npc/ subfolder with "
			    "NpcType:new('{}'), or create data/npc/{}.xml. Make sure the name in NpcType:new(...) matches "
			    "the map name exactly (case-sensitive).",
			    name, name));
			return nullptr;
		}
		Npcs::addNpcType(npcType->name, npcType);
		LOG_DEBUG(fmt::format(">> [Hybrid] NPC '{}' -> XML file (lookType={})",
			name, npcType->defaultOutfit.lookType));
	}

	auto npc = std::make_unique<Npc>(name);
	npc->setName(npcType->name);
	npc->loaded = true;
	npc->npcType = npcType;
	npc->loadNpcTypeInfo();

	if (npcType->fromLua) {
		// Lua RevScript NPC: copy the event handler from the NpcType template
		npc->npcEventHandler = std::make_unique<NpcEventsHandler>(*npcType->npcEventHandler);
		npc->npcEventHandler->loaded = true;

		// Validate that the Lua NPC has essential events registered
		if (npc->npcEventHandler->creatureSayEvent == -1) {
			LOG_WARN(fmt::format("[Warning] Lua NPC '{}' has no onSay event! Check if defaultBehavior() was called.", name));
		}
		if (npc->npcEventHandler->thinkEvent == -1) {
			LOG_WARN(fmt::format("[Warning] Lua NPC '{}' has no onThink event! NPC will not process focus/timeout.", name));
		}
		if (npc->npcEventHandler->creatureAppearEvent == -1) {
			LOG_WARN(fmt::format("[Warning] Lua NPC '{}' has no onAppear event!", name));
		}
	} else {
		// Traditional XML NPC: load script file for events
		npc->npcEventHandler = std::make_unique<NpcEventsHandler>(npcType->scriptFilename, npc.get());
	}
	npc->npcEventHandler->setNpc(npc.get());

	// Validate outfit for both Lua and XML NPCs
	if (npc->currentOutfit.lookType == 0 && npc->currentOutfit.lookTypeEx == 0) {
		LOG_WARN(fmt::format("[Warning] NPC '{}' has lookType=0 - NPC will be INVISIBLE on the map!", name));
	}

	return npc;
}

Npc::Npc(const std::string& name) : Creature(), filename("data/npc/" + name + ".xml"), masterRadius(-1), loaded(false)
{
	reset();
}

Npc::~Npc() { reset(); }

void Npc::addList() { g_game.addNpc(this); }

void Npc::removeList() { g_game.removeNpc(this); }

bool Npc::load()
{
	if (loaded) {
		return true;
	}

	loadNpcTypeInfo();
	npcEventHandler = std::make_unique<NpcEventsHandler>(npcType->scriptFilename, this);
	npcEventHandler->setNpc(this);

	loaded = true;
	return loaded;
}

void Npc::reset(bool reload)
{
	loaded = false;
	isIdle = true;
	walkTicks = 1500;
	pushable = true;
	floorChange = false;
	attackable = false;
	ignoreHeight = false;
	m_focusCreature.reset();
	speechBubble = 0;
	moneyType = 0;
	baseSpeed = 100;

	npcEventHandler.reset();

	parameters.clear();
	shopPlayerSet.clear();
	spectators.clear();

	if (reload) {
		load();
	}
}

void Npc::reload()
{
	if (!npcType->fromLua) {
		reset(true);
	}

	SpectatorVec players;
	g_game.map.getSpectators(players, getPosition(), true, true);
	for (const auto& player : players.players()) {
		spectators.insert(std::static_pointer_cast<Player>(player));
	}

	const bool hasSpectators = !spectators.empty();
	setIdle(!hasSpectators);

	if (hasSpectators && walkTicks > 0) {
		addEventWalk();
	}

	// Simulate that the creature is placed on the map again.
	if (npcEventHandler) {
		npcEventHandler->onCreatureAppear(this);
	}
}

void Npc::loadNpcTypeInfo()
{
	speechBubble = npcType->speechBubble;
	walkTicks = npcType->walkTicks;
	baseSpeed = npcType->baseSpeed;
	masterRadius = npcType->masterRadius;
	floorChange = npcType->floorChange;
	attackable = npcType->attackable;
	ignoreHeight = npcType->ignoreHeight;
	isIdle = npcType->isIdle;
	pushable = npcType->pushable;
	defaultOutfit = npcType->defaultOutfit;
	currentOutfit = defaultOutfit;
	parameters = npcType->parameters;
	health = npcType->health;
	healthMax = npcType->healthMax;
	moneyType = npcType->moneyType;
	setSkull(npcType->skull);
	setGuildEmblem(npcType->emblem);
}

// ─── NpcType ──────────────────────────────────────────────────────────────────

bool NpcType::loadFromXml()
{
	if (!fs::exists(filename)) {
		return false;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.c_str());
	if (!result) {
		LOG_ERROR(fmt::format("[Error - NpcType::loadFromXml] Malformed XML in {}: {}",
		                      filename, result.description()));
		LOG_ERROR(fmt::format("[Error - NpcType::loadFromXml] Fix XML syntax in {} and make sure it has a valid "
		                      "<npc ...> root tag.",
		                      filename));
		return false;
	}

	pugi::xml_node npcNode = doc.child("npc");
	if (!npcNode) {
		LOG_ERROR(fmt::format("[Error - NpcType::loadFromXml] Missing npc tag in {}", filename));
		return false;
	}

	name = npcNode.attribute("name").as_string();
	attackable = npcNode.attribute("attackable").as_bool();
	floorChange = npcNode.attribute("floorchange").as_bool();

	pugi::xml_attribute attr;
	if ((attr = npcNode.attribute("speed"))) {
		baseSpeed = pugi::cast<uint32_t>(attr.value());
	} else {
		baseSpeed = 100;
	}

	if ((attr = npcNode.attribute("pushable"))) {
		pushable = attr.as_bool();
	}

	if ((attr = npcNode.attribute("walkinterval"))) {
		walkTicks = pugi::cast<uint32_t>(attr.value());
	}

	if ((attr = npcNode.attribute("walkradius"))) {
		masterRadius = pugi::cast<int32_t>(attr.value());
	}

	if ((attr = npcNode.attribute("ignoreheight"))) {
		ignoreHeight = attr.as_bool();
	}

	if ((attr = npcNode.attribute("speechbubble"))) {
		speechBubble = pugi::cast<uint32_t>(attr.value());
	}

	if ((attr = npcNode.attribute("skull"))) {
		skull = getSkullType(asLowerCaseString(attr.as_string()));
	}

	pugi::xml_node healthNode = npcNode.child("health");
	if (healthNode) {
		if ((attr = healthNode.attribute("now"))) {
			health = pugi::cast<int32_t>(attr.value());
		} else {
			health = 100;
		}

		if ((attr = healthNode.attribute("max"))) {
			healthMax = pugi::cast<int32_t>(attr.value());
		} else {
			healthMax = 100;
		}

		if (health > healthMax) {
			health = healthMax;
			LOG_WARN(fmt::format("[Warning - NpcType::loadFromXml] Health now is greater than health max in {}", filename));
		}
	}

	pugi::xml_node lookNode = npcNode.child("look");
	if (lookNode) {
		pugi::xml_attribute lookTypeAttribute = lookNode.attribute("type");
		if (lookTypeAttribute) {
			defaultOutfit.lookType = pugi::cast<uint16_t>(lookTypeAttribute.value());
			defaultOutfit.lookHead = pugi::cast<uint16_t>(lookNode.attribute("head").value());
			defaultOutfit.lookBody = pugi::cast<uint16_t>(lookNode.attribute("body").value());
			defaultOutfit.lookLegs = pugi::cast<uint16_t>(lookNode.attribute("legs").value());
			defaultOutfit.lookFeet = pugi::cast<uint16_t>(lookNode.attribute("feet").value());
			defaultOutfit.lookAddons = pugi::cast<uint16_t>(lookNode.attribute("addons").value());
		} else if ((attr = lookNode.attribute("typeex"))) {
			defaultOutfit.lookTypeEx = pugi::cast<uint16_t>(attr.value());
		}
	}

	if ((attr = npcNode.attribute("money"))) {
		moneyType = pugi::cast<uint16_t>(attr.value());
	}

	for (auto& parameterNode : npcNode.child("parameters").children()) {
		parameters[parameterNode.attribute("key").as_string()] = parameterNode.attribute("value").as_string();
	}

	pugi::xml_attribute scriptFile = npcNode.attribute("script");
	if (scriptFile) {
		scriptFilename = scriptFile.as_string();
	}

	return true;
}

bool NpcType::loadCallback(NpcScriptInterface* scriptInterface)
{
	int32_t id = scriptInterface->getEvent();
	if (id == -1) {
		LOG_WARN(fmt::format("[Warning - NpcType::loadCallback] Event '{}' not found for NPC '{}'. "
			"Check if the callback function is passed correctly in defaultBehavior().", eventType, name));
		return false;
	}

	if (eventType == "say") {
		npcEventHandler->creatureSayEvent = id;
	} else if (eventType == "disappear") {
		npcEventHandler->creatureDisappearEvent = id;
	} else if (eventType == "appear") {
		npcEventHandler->creatureAppearEvent = id;
	} else if (eventType == "move") {
		npcEventHandler->creatureMoveEvent = id;
	} else if (eventType == "closechannel") {
		npcEventHandler->playerCloseChannelEvent = id;
	} else if (eventType == "endtrade") {
		npcEventHandler->playerEndTradeEvent = id;
	} else if (eventType == "think") {
		npcEventHandler->thinkEvent = id;
	}

	LOG_DEBUG(fmt::format(">> NPC '{}' event '{}' registered (id={})", name, eventType, id));
	return true;
}

bool Npc::canSee(const Position& pos) const
{
	if (pos.z != getPosition().z) {
		return false;
	}
	return Creature::canSee(getPosition(), pos, 3, 3);
}

std::string Npc::getDescription(int32_t) const
{
	std::string descr;
	descr.reserve(name.length() + 1);
	descr.assign(name);
	descr.push_back('.');
	return descr;
}

void Npc::onCreatureAppear(Creature* creature, bool isLogin)
{
	Creature::onCreatureAppear(creature, isLogin);

	if (creature == this) {
		if (walkTicks > 0) {
			addEventWalk();
		}

		if (npcEventHandler) {
			npcEventHandler->onCreatureAppear(creature);
		}
	} else if (creature->getPlayer()) {
		if (npcEventHandler) {
			npcEventHandler->onCreatureAppear(creature);
		}
	}
}

void Npc::onRemoveCreature(Creature* creature, bool isLogout)
{
	Creature::onRemoveCreature(creature, isLogout);

	if (creature == this) {
		auto focused = m_focusCreature.lock();
		if (focused) {
			Player* focusedPlayer = focused->getPlayer();
			if (focusedPlayer) {
				clearFocusIfNeeded(focusedPlayer);
			}
		}
		closeAllShopWindows();
		if (npcEventHandler) {
			npcEventHandler->onCreatureDisappear(creature);
		}
	} else if (Player* player = creature->getPlayer()) {
		clearFocusIfNeeded(player);
		if (npcEventHandler) {
			npcEventHandler->onCreatureDisappear(creature);
		}
	}
}

void Npc::onCreatureMove(Creature* creature, const Tile* newTile, const Position& newPos, const Tile* oldTile,
                         const Position& oldPos, bool teleport)
{
	Creature::onCreatureMove(creature, newTile, newPos, oldTile, oldPos, teleport);

	if (creature == this || creature->getPlayer()) {
		if (npcEventHandler) {
			npcEventHandler->onCreatureMove(creature, oldPos, newPos);
		}
	}

	if (Player* player = creature->getPlayer()) {
		auto focused = m_focusCreature.lock();
		if (focused && focused.get() == player) {
			bool shouldClear = false;

			if (!canSee(newPos)) {
				shouldClear = true;
			}

			if (!shouldClear && player->getInstanceID() != getInstanceID()) {
				shouldClear = true;
			}

			if (shouldClear) {
				clearFocusIfNeeded(player);
			}
		}
	}
}

void Npc::onCreatureSay(Creature* creature, SpeakClasses type, std::string_view text)
{
	if (creature == this) {
		return;
	}

	// only players for script events
	Player* player = creature->getPlayer();
	if (player) {
		if (npcEventHandler) {
			npcEventHandler->onCreatureSay(player, type, text);
		}
	}
}

void Npc::onPlayerCloseChannel(Player* player)
{
	if (npcEventHandler) {
		npcEventHandler->onPlayerCloseChannel(player);
	}
}

void Npc::onThink(uint32_t interval)
{
	SpectatorVec players;
	g_game.map.getSpectators(players, getPosition(), true, true, Npcs::ViewportX, Npcs::ViewportX,
	                         Npcs::ViewportY, Npcs::ViewportY);
	for (const auto& player : players.players()) {
		spectators.insert(std::static_pointer_cast<Player>(player));
	}

	setIdle(spectators.empty());

	if (isIdle) {
		return;
	}

	auto focused = m_focusCreature.lock();
	if (focused) {
		Player* focusedPlayer = focused->getPlayer();
		if (!focusedPlayer || focusedPlayer->isRemoved()
		    || !canSee(focusedPlayer->getPosition())
		    || focusedPlayer->getInstanceID() != getInstanceID()) {
			if (focusedPlayer && !focusedPlayer->isRemoved()) {
				clearFocusIfNeeded(focusedPlayer);
			} else {
				setCreatureFocus(nullptr);
			}
		}
	}

	Creature::onThink(interval);

	if (npcEventHandler) {
		npcEventHandler->onThink();
	}

	auto focusedAfterThink = m_focusCreature.lock();
	if (focusedAfterThink) {
		turnToCreature(focusedAfterThink.get());
		stopEventWalk();
		listWalkDir.clear();
	} else if (walkTicks > 0 && getTimeSinceLastMove() >= walkTicks) {
		addEventWalk();
	}

	spectators.clear();
}

void Npc::doSay(std::string_view text) { g_game.internalCreatureSay(this, TALKTYPE_SAY, text, false); }

void Npc::doSayToPlayer(Player* player, std::string_view text)
{
	if (player) {
		player->sendCreatureSay(this, TALKTYPE_PRIVATE_NP, text);
		player->onCreatureSay(this, TALKTYPE_PRIVATE_NP, text);
	}
}

void Npc::onPlayerTrade(Player* player, int32_t callback, uint16_t itemId, uint8_t count, uint8_t amount,
                        bool ignore /* = false*/, bool inBackpacks /* = false*/)
{
	if (npcEventHandler) {
		npcEventHandler->onPlayerTrade(player, callback, itemId, count, amount, ignore, inBackpacks);
	}
	player->sendSaleItemList();
}

void Npc::onPlayerEndTrade(Player* player, int32_t buyCallback, int32_t sellCallback)
{
	lua_State* L = Npcs::getScriptInterface()->getLuaState();

	if (buyCallback != -1) {
		luaL_unref(L, LUA_REGISTRYINDEX, buyCallback);
	}

	if (sellCallback != -1) {
		luaL_unref(L, LUA_REGISTRYINDEX, sellCallback);
	}

	removeShopPlayer(std::static_pointer_cast<Player>(g_game.getCreatureSharedRef(player)));

	if (npcEventHandler) {
		npcEventHandler->onPlayerEndTrade(player);
	}
}

bool Npc::getNextStep(Direction& dir, uint32_t& flags)
{
	if (!m_focusCreature.expired()) {
		listWalkDir.clear();
		return false;
	}

	if (Creature::getNextStep(dir, flags)) {
		return true;
	}

	if (walkTicks == 0) {
		return false;
	}

	if (getTimeSinceLastMove() < walkTicks) {
		return false;
	}

	return getRandomStep(dir);
}

void Npc::setIdle(const bool idle)
{
	if (idle == isIdle) {
		return;
	}

	if (isRemoved() || isDead()) {
		return;
	}

	isIdle = idle;

	if (isIdle) {
		onIdleStatus();
	}
}

bool Npc::canWalkTo(const Position& fromPos, Direction dir) const
{
	if (masterRadius == 0) {
		return false;
	}

	Position toPos = getNextPosition(dir, fromPos);
	if (!Spawns::isInZone(masterPos, masterRadius, toPos)) {
		return false;
	}

	Tile* tile = g_game.map.getTile(toPos);
	if (!tile || tile->queryAdd(0, *this, 1, 0) != RETURNVALUE_NOERROR) {
		return false;
	}

	if (!floorChange && (tile->hasFlag(TILESTATE_FLOORCHANGE) || tile->getTeleportItem())) {
		return false;
	}

	if (!ignoreHeight && tile->hasHeight(1)) {
		return false;
	}

	return true;
}

bool Npc::getRandomStep(Direction& direction) const
{
	const Position& creaturePos = getPosition();
	for (Direction dir : getShuffleDirections()) {
		if (canWalkTo(creaturePos, dir)) {
			direction = dir;
			return true;
		}
	}
	return false;
}

bool Npc::doMoveTo(const Position& pos, int32_t minTargetDist /* = 1*/, int32_t maxTargetDist /* = 1*/,
                   bool fullPathSearch /* = true*/, bool clearSight /* = true*/, int32_t maxSearchDist /* = 0*/)
{
	if (!m_focusCreature.expired()) {
		stopEventWalk();
		listWalkDir.clear();
		return false;
	}

	listWalkDir.clear();
	if (getPathTo(pos, listWalkDir, minTargetDist, maxTargetDist, fullPathSearch, clearSight, maxSearchDist)) {
		startAutoWalk();
		return true;
	}
	return false;
}

void Npc::turnToCreature(Creature* creature)
{
	const Position& creaturePos = creature->getPosition();
	const Position& myPos = getPosition();
	const auto dx = myPos.getOffsetX(creaturePos);
	const auto dy = myPos.getOffsetY(creaturePos);

	float tan;
	if (dx != 0) {
		tan = static_cast<float>(dy) / dx;
	} else {
		tan = 10;
	}

	Direction dir;
	if (std::abs(tan) < 1) {
		if (dx > 0) {
			dir = DIRECTION_WEST;
		} else {
			dir = DIRECTION_EAST;
		}
	} else {
		if (dy > 0) {
			dir = DIRECTION_NORTH;
		} else {
			dir = DIRECTION_SOUTH;
		}
	}
	g_game.internalCreatureTurn(this, dir);
}

void Npc::setCreatureFocus(Creature* creature)
{
	Player* player = creature ? creature->getPlayer() : nullptr;
	if (player) {
		m_focusCreature = g_game.getPlayerWeakRef(player);
		Creature::setFollowCreature(nullptr);
		stopEventWalk();
		listWalkDir.clear();
		turnToCreature(player);
	} else {
		m_focusCreature.reset();
		Creature::setFollowCreature(nullptr);
		listWalkDir.clear();
	}
}

bool Npc::setFollowCreature(Creature* creature)
{
	if (!m_focusCreature.expired()) {
		Creature::setFollowCreature(nullptr);
		stopEventWalk();
		listWalkDir.clear();
		return false;
	}

	return Creature::setFollowCreature(creature);
}

void Npc::clearFocusIfNeeded(Player* player)
{
	if (!player) {
		return;
	}

	auto focused = m_focusCreature.lock();
	if (!focused || focused.get() != player) {
		return;
	}

	setCreatureFocus(nullptr);

	bool closedShopWindow = false;
	int32_t onBuy;
	int32_t onSell;
	if (player->getShopOwner(onBuy, onSell) == this) {
		closedShopWindow = player->closeShopWindow();
	}

	auto playerRef = std::static_pointer_cast<Player>(g_game.getCreatureSharedRef(player));
	if (playerRef && !closedShopWindow) {
		removeShopPlayer(playerRef);
	}

	if (npcEventHandler) {
		npcEventHandler->onCreatureDisappear(player);
	}
}

void Npc::addShopPlayer(const std::shared_ptr<Player>& player) { shopPlayerSet.insert(player); }

void Npc::removeShopPlayer(const std::shared_ptr<Player>& player) { shopPlayerSet.erase(player); }

void Npc::closeAllShopWindows()
{
	while (!shopPlayerSet.empty()) {
		auto player = *shopPlayerSet.begin();
		if (!player->closeShopWindow()) {
			removeShopPlayer(player);
		}
	}
}

NpcScriptInterface::NpcScriptInterface() : LuaScriptInterface("Npc interface")
{
	libLoaded = false;
	initState();
}

bool NpcScriptInterface::initState()
{
	if (!LuaScriptInterface::initState()) {
		return false;
	}

	// Npc scripts share the global Lua state with the scripts interface.
	// Re-registering classes here recreates metatables, so we must re-apply
	// the Lua-side core/compat extensions (notably NpcType __newindex hooks)
	// before loading lua NPC definitions.
	if (g_luaEnvironment.loadFile("data/lib/lib.lua") == -1) {
		LOG_WARN("[Warning - NpcScriptInterface::initState] Can not load data/lib/lib.lua");
		LOG_WARN(getLastLuaError());
		return false;
	}

	registerFunctions();
	return true;
}

bool NpcScriptInterface::closeState()
{
	libLoaded = false;
	LuaScriptInterface::closeState();
	return true;
}

bool NpcScriptInterface::loadNpcLib(std::string_view file)
{
	if (libLoaded) {
		return true;
	}

	if (loadFile(file) == -1) {
		LOG_WARN(fmt::format("[Warning - NpcScriptInterface::loadNpcLib] Can not load {}", file));
		return false;
	}

	libLoaded = true;
	return true;
}

void NpcScriptInterface::registerFunctions()
{
	// npc exclusive functions
	lua_register(luaState, "selfSay", NpcScriptInterface::luaActionSay);
	lua_register(luaState, "selfMove", NpcScriptInterface::luaActionMove);
	lua_register(luaState, "selfMoveTo", NpcScriptInterface::luaActionMoveTo);
	lua_register(luaState, "selfTurn", NpcScriptInterface::luaActionTurn);
	lua_register(luaState, "selfFollow", NpcScriptInterface::luaActionFollow);
	lua_register(luaState, "getDistanceTo", NpcScriptInterface::luagetDistanceTo);
	lua_register(luaState, "doNpcSetCreatureFocus", NpcScriptInterface::luaSetNpcFocus);
	lua_register(luaState, "getNpcCid", NpcScriptInterface::luaGetNpcCid);
	lua_register(luaState, "getNpcParameter", NpcScriptInterface::luaGetNpcParameter);
	lua_register(luaState, "openShopWindow", NpcScriptInterface::luaOpenShopWindow);
	lua_register(luaState, "closeShopWindow", NpcScriptInterface::luaCloseShopWindow);
	lua_register(luaState, "doSellItem", NpcScriptInterface::luaDoSellItem);

	// metatable
	registerMethod("Npc", "getParameter", NpcScriptInterface::luaNpcGetParameter);
	registerMethod("Npc", "setFocus", NpcScriptInterface::luaNpcSetFocus);

	registerMethod("Npc", "openShopWindow", NpcScriptInterface::luaNpcOpenShopWindow);
	registerMethod("Npc", "closeShopWindow", NpcScriptInterface::luaNpcCloseShopWindow);
}

int NpcScriptInterface::luaActionSay(lua_State* L)
{
	// selfSay(words[, target])
	Npc* npc = LuaScriptInterface::getScriptEnv()->getNpc();
	if (!npc) {
		return 0;
	}

	const std::string& text = Lua::getString(L, 1);
	if (lua_gettop(L) >= 2) {
		Player* target = Lua::getPlayer(L, 2);
		if (target) {
			npc->doSayToPlayer(target, text);
			return 0;
		}
	}

	npc->doSay(text);
	return 0;
}

int NpcScriptInterface::luaActionMove(lua_State* L)
{
	// selfMove(direction)
	Npc* npc = getScriptEnv()->getNpc();
	if (npc && !npc->hasCreatureFocus()) {
		g_game.internalMoveCreature(npc, Lua::getInteger<Direction>(L, 1));
	}
	return 0;
}

int NpcScriptInterface::luaActionMoveTo(lua_State* L)
{
	// selfMoveTo(x, y, z[, minTargetDist = 1[, maxTargetDist = 1[, fullPathSearch = true[, clearSight = true[,
	// maxSearchDist = 0]]]]]) selfMoveTo(position[, minTargetDist = 1[, maxTargetDist = 1[, fullPathSearch = true[,
	// clearSight = true[, maxSearchDist = 0]]]]])
	Npc* npc = getScriptEnv()->getNpc();
	if (!npc) {
		return 0;
	}

	Position position;
	int32_t argsStart = 2;
	if (Lua::isTable(L, 1)) {
		position = Lua::getPosition(L, 1);
	} else {
		position.x = Lua::getInteger<uint16_t>(L, 1);
		position.y = Lua::getInteger<uint16_t>(L, 2);
		position.z = Lua::getInteger<uint8_t>(L, 3);
		argsStart = 4;
	}

	Lua::pushBoolean(
	    L, npc->doMoveTo(position, Lua::getInteger<int32_t>(L, argsStart, 1),
	                     Lua::getInteger<int32_t>(L, argsStart + 1, 1), Lua::getBoolean(L, argsStart + 2, true),
	                     Lua::getBoolean(L, argsStart + 3, true), Lua::getInteger<int32_t>(L, argsStart + 4, 0)));
	return 1;
}

int NpcScriptInterface::luaActionTurn(lua_State* L)
{
	// selfTurn(direction)
	Npc* npc = getScriptEnv()->getNpc();
	if (npc) {
		g_game.internalCreatureTurn(npc, Lua::getInteger<Direction>(L, 1));
	}
	return 0;
}

int NpcScriptInterface::luaActionFollow(lua_State* L)
{
	// selfFollow(player)
	Npc* npc = getScriptEnv()->getNpc();
	if (!npc) {
		Lua::pushBoolean(L, false);
		return 1;
	}

	if (npc->hasCreatureFocus()) {
		Lua::pushBoolean(L, false);
		return 1;
	}

	Lua::pushBoolean(L, npc->setFollowCreature(Lua::getPlayer(L, 1)));
	return 1;
}

int NpcScriptInterface::luagetDistanceTo(lua_State* L)
{
	// getDistanceTo(uid)
	ScriptEnvironment* env = getScriptEnv();

	Npc* npc = env->getNpc();
	if (!npc) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::THING_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	uint32_t uid = Lua::getInteger<uint32_t>(L, -1);

	Thing* thing = env->getThingByUID(uid);
	if (!thing) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::THING_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	const Position& thingPos = thing->getPosition();
	const Position& npcPos = npc->getPosition();
	if (npcPos.z != thingPos.z) {
		lua_pushinteger(L, -1);
	} else {
		lua_pushinteger(L, std::max(npcPos.getDistanceX(thingPos), npcPos.getDistanceY(thingPos)));
	}
	return 1;
}

int NpcScriptInterface::luaSetNpcFocus(lua_State* L)
{
	// doNpcSetCreatureFocus(cid)
	Npc* npc = getScriptEnv()->getNpc();
	if (npc) {
		npc->setCreatureFocus(Lua::getCreature(L, -1));
	}
	return 0;
}

int NpcScriptInterface::luaGetNpcCid(lua_State* L)
{
	// getNpcCid()
	Npc* npc = getScriptEnv()->getNpc();
	if (npc) {
		lua_pushinteger(L, npc->getID());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int NpcScriptInterface::luaGetNpcParameter(lua_State* L)
{
	// getNpcParameter(paramKey)
	Npc* npc = getScriptEnv()->getNpc();
	if (!npc) {
		lua_pushnil(L);
		return 1;
	}

	std::string paramKey = Lua::getString(L, -1);

	auto it = npc->parameters.find(paramKey);
	if (it != npc->parameters.end()) {
		Lua::pushString(L, it->second);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int NpcScriptInterface::luaOpenShopWindow(lua_State* L)
{
	// openShopWindow(cid, items, onBuy callback, onSell callback)
	int32_t sellCallback;
	if (lua_isfunction(L, -1) == 0) {
		sellCallback = -1;
		lua_pop(L, 1); // skip it - use default value
	} else {
		sellCallback = Lua::popCallback(L);
	}

	int32_t buyCallback;
	if (lua_isfunction(L, -1) == 0) {
		buyCallback = -1;
		lua_pop(L, 1); // skip it - use default value
	} else {
		buyCallback = Lua::popCallback(L);
	}

	if (lua_istable(L, -1) == 0) {
		LuaScriptInterface::reportError(__FUNCTION__, "item list is not a table.");
		Lua::pushBoolean(L, false);
		return 1;
	}

	std::list<ShopInfo> items;
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		const auto tableIndex = lua_gettop(L);
		ShopInfo item;

		item.itemId = Lua::getField<uint16_t>(L, tableIndex, "id");
		item.subType = Lua::getField<int32_t>(L, tableIndex, "subType");
		if (item.subType == 0) {
			item.subType = Lua::getField<int32_t>(L, tableIndex, "subtype");
			lua_pop(L, 1);
		}

		item.buyPrice = Lua::getField<int64_t>(L, tableIndex, "buy");
		item.sellPrice = Lua::getField<int64_t>(L, tableIndex, "sell");
		item.realName = Lua::getFieldString(L, tableIndex, "name");

		items.push_back(item);
		lua_pop(L, 6);
	}
	lua_pop(L, 1);

	Player* player = Lua::getPlayer(L, -1);
	if (!player) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::PLAYER_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	// Close any eventual other shop window currently open.
	player->closeShopWindow(false);

	Npc* npc = getScriptEnv()->getNpc();
	if (!npc) {
		reportErrorFunc(L, LuaScriptInterface::getErrorDesc(LuaErrorCode::CREATURE_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	npc->addShopPlayer(std::static_pointer_cast<Player>(g_game.getCreatureSharedRef(player)));
	player->setShopOwner(npc, buyCallback, sellCallback);
	player->openShopWindow(items);

	Lua::pushBoolean(L, true);
	return 1;
}

int NpcScriptInterface::luaCloseShopWindow(lua_State* L)
{
	// closeShopWindow(cid)
	Npc* npc = LuaScriptInterface::getScriptEnv()->getNpc();
	if (!npc) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::CREATURE_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	Player* player = Lua::getPlayer(L, 1);
	if (!player) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::PLAYER_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	int32_t buyCallback;
	int32_t sellCallback;

	Npc* merchant = player->getShopOwner(buyCallback, sellCallback);

	// Check if we actually have a shop window with this player.
	if (merchant == npc) {
		player->sendCloseShop();

		if (buyCallback != -1) {
			luaL_unref(L, LUA_REGISTRYINDEX, buyCallback);
		}

		if (sellCallback != -1) {
			luaL_unref(L, LUA_REGISTRYINDEX, sellCallback);
		}

		player->setShopOwner(nullptr, -1, -1);
		npc->removeShopPlayer(std::static_pointer_cast<Player>(g_game.getCreatureSharedRef(player)));
	}

	Lua::pushBoolean(L, true);
	return 1;
}

int NpcScriptInterface::luaDoSellItem(lua_State* L)
{
	// doSellItem(cid, itemid, amount, <optional> subtype, <optional> actionid, <optional: default: 1> canDropOnMap)
	Player* player = Lua::getPlayer(L, 1);
	if (!player) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::PLAYER_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	uint32_t sellCount = 0;

	uint32_t itemId = Lua::getInteger<uint32_t>(L, 2);
	uint32_t amount = Lua::getInteger<uint32_t>(L, 3);
	uint32_t subType;

	int32_t n = Lua::getInteger<int32_t>(L, 4, -1);
	if (n != -1) {
		subType = n;
	} else {
		subType = 1;
	}

	uint32_t actionId = Lua::getInteger<uint32_t>(L, 5, 0);
	bool canDropOnMap = Lua::getBoolean(L, 6, true);

	const ItemType& it = Item::items[itemId];
	if (it.stackable) {
		while (amount > 0) {
			int32_t stackCount = std::min<int32_t>(amount, it.stackSize);
			auto itemPtr = Item::CreateItem(it.id, static_cast<uint16_t>(stackCount));
			if (itemPtr && actionId != 0) {
				itemPtr->setActionId(static_cast<uint16_t>(actionId));
			}

			if (g_game.internalPlayerAddItem(player, itemPtr.get(), canDropOnMap) != RETURNVALUE_NOERROR) {
				lua_pushinteger(L, sellCount);
				return 1;
			}

			amount -= stackCount;
			sellCount += stackCount;
		}
	} else {
		for (uint32_t i = 0; i < amount; ++i) {
			auto itemPtr = Item::CreateItem(it.id, static_cast<uint16_t>(subType));
			if (itemPtr && actionId != 0) {
				itemPtr->setActionId(static_cast<uint16_t>(actionId));
			}

			if (g_game.internalPlayerAddItem(player, itemPtr.get(), canDropOnMap) != RETURNVALUE_NOERROR) {
				lua_pushinteger(L, sellCount);
				return 1;
			}

			++sellCount;
		}
	}

	lua_pushinteger(L, sellCount);
	return 1;
}

int NpcScriptInterface::luaNpcGetParameter(lua_State* L)
{
	// npc:getParameter(key)
	const std::string& key = Lua::getString(L, 2);
	Npc* npc = Lua::getUserdata<Npc>(L, 1);
	if (npc) {
		auto it = npc->parameters.find(key);
		if (it != npc->parameters.end()) {
			Lua::pushString(L, it->second);
		} else {
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int NpcScriptInterface::luaNpcSetFocus(lua_State* L)
{
	// npc:setFocus(creature)
	Creature* creature = Lua::getCreature(L, 2);
	Npc* npc = Lua::getUserdata<Npc>(L, 1);
	if (npc) {
		npc->setCreatureFocus(creature);
		Lua::pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int NpcScriptInterface::luaNpcOpenShopWindow(lua_State* L)
{
	// npc:openShopWindow(cid, items, buyCallback, sellCallback)
	if (!Lua::isTable(L, 3)) {
		reportErrorFunc(L, "item list is not a table.");
		Lua::pushBoolean(L, false);
		return 1;
	}

	Player* player = Lua::getPlayer(L, 2);
	if (!player) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::PLAYER_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	Npc* npc = Lua::getUserdata<Npc>(L, 1);
	if (!npc) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::CREATURE_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	int32_t sellCallback = -1;
	if (Lua::isFunction(L, 5)) {
		sellCallback = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	int32_t buyCallback = -1;
	if (Lua::isFunction(L, 4)) {
		buyCallback = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	std::list<ShopInfo> items;

	lua_pushnil(L);
	while (lua_next(L, 3) != 0) {
		const auto tableIndex = lua_gettop(L);
		ShopInfo item;

		item.itemId = Lua::getField<uint16_t>(L, tableIndex, "id");
		item.subType = Lua::getField<int32_t>(L, tableIndex, "subType");
		if (item.subType == 0) {
			item.subType = Lua::getField<int32_t>(L, tableIndex, "subtype");
			lua_pop(L, 1);
		}

		item.buyPrice = Lua::getField<int64_t>(L, tableIndex, "buy");
		item.sellPrice = Lua::getField<int64_t>(L, tableIndex, "sell");
		item.realName = Lua::getFieldString(L, tableIndex, "name");

		items.push_back(item);
		lua_pop(L, 6);
	}
	lua_pop(L, 1);

	player->closeShopWindow(false);
	npc->addShopPlayer(std::static_pointer_cast<Player>(g_game.getCreatureSharedRef(player)));

	player->setShopOwner(npc, buyCallback, sellCallback);
	player->openShopWindow(items);

	Lua::pushBoolean(L, true);
	return 1;
}

int NpcScriptInterface::luaNpcCloseShopWindow(lua_State* L)
{
	// npc:closeShopWindow(player)
	Player* player = Lua::getPlayer(L, 2);
	if (!player) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::PLAYER_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	Npc* npc = Lua::getUserdata<Npc>(L, 1);
	if (!npc) {
		reportErrorFunc(L, getErrorDesc(LuaErrorCode::CREATURE_NOT_FOUND));
		Lua::pushBoolean(L, false);
		return 1;
	}

	int32_t buyCallback;
	int32_t sellCallback;

	Npc* merchant = player->getShopOwner(buyCallback, sellCallback);
	if (merchant == npc) {
		player->sendCloseShop();
		if (buyCallback != -1) {
			luaL_unref(L, LUA_REGISTRYINDEX, buyCallback);
		}

		if (sellCallback != -1) {
			luaL_unref(L, LUA_REGISTRYINDEX, sellCallback);
		}

		player->setShopOwner(nullptr, -1, -1);
		npc->removeShopPlayer(std::static_pointer_cast<Player>(g_game.getCreatureSharedRef(player)));
	}

	Lua::pushBoolean(L, true);
	return 1;
}

NpcEventsHandler::NpcEventsHandler(const std::string& file, Npc* npcPtr) :
    scriptInterface(Npcs::scriptInterface), npc(npcPtr)
{
	// Create a temporary shared_ptr for loadFile; it goes out of scope here
	// so enable_shared_from_this can be properly re-initialized when the
	// owning shared_ptr is created later.
	auto npcHandle = makeNpcScriptHandle(npcPtr);
	loaded = scriptInterface->loadFile("data/npc/scripts/" + file, npcHandle) == 0;
	if (!loaded) {
		LOG_WARN(fmt::format("[Warning - NpcScript::NpcScript] Can not load script: {}", file));
		LOG_WARN(scriptInterface->getLastLuaError());
	} else {
		creatureSayEvent = scriptInterface->getEvent("onCreatureSay");
		creatureDisappearEvent = scriptInterface->getEvent("onCreatureDisappear");
		creatureAppearEvent = scriptInterface->getEvent("onCreatureAppear");
		creatureMoveEvent = scriptInterface->getEvent("onCreatureMove");
		playerCloseChannelEvent = scriptInterface->getEvent("onPlayerCloseChannel");
		playerEndTradeEvent = scriptInterface->getEvent("onPlayerEndTrade");
		thinkEvent = scriptInterface->getEvent("onThink");
	}
}

NpcEventsHandler::NpcEventsHandler() : scriptInterface(Npcs::scriptInterface) {}

void NpcEventsHandler::setNpc(Npc* n)
{
	npc = n;
}

NpcEventsHandler::~NpcEventsHandler()
{
	if (npc && npc->npcType && !npc->npcType->fromLua) {
		for (auto eventId : {creatureSayEvent, creatureDisappearEvent, creatureAppearEvent,
		                     creatureMoveEvent, playerCloseChannelEvent, playerEndTradeEvent, thinkEvent}) {
			scriptInterface->removeEvent(eventId);
		}
	}
}

bool NpcEventsHandler::isLoaded() const { return loaded; }

void NpcEventsHandler::onCreatureAppear(Creature* creature) const
{
	if (creatureAppearEvent == -1) {
		return;
	}

	// onCreatureAppear(creature)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - NpcScript::onCreatureAppear] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(creatureAppearEvent, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(creatureAppearEvent);
	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);
	scriptInterface->callVoidFunction(1);
}

void NpcEventsHandler::onCreatureDisappear(Creature* creature) const
{
	if (creatureDisappearEvent == -1) {
		return;
	}

	// onCreatureDisappear(creature)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - NpcScript::onCreatureDisappear] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(creatureDisappearEvent, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(creatureDisappearEvent);
	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);
	scriptInterface->callVoidFunction(1);
}

void NpcEventsHandler::onCreatureMove(Creature* creature, const Position& oldPos, const Position& newPos) const
{
	if (creatureMoveEvent == -1) {
		return;
	}

	// onCreatureMove(creature, oldPos, newPos)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - NpcScript::onCreatureMove] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(creatureMoveEvent, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(creatureMoveEvent);
	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);
	Lua::pushPosition(L, oldPos);
	Lua::pushPosition(L, newPos);
	scriptInterface->callVoidFunction(3);
}

void NpcEventsHandler::onCreatureSay(Creature* creature, SpeakClasses type, std::string_view text) const
{
	if (creatureSayEvent == -1) {
		return;
	}

	// onCreatureSay(creature, type, msg)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - NpcScript::onCreatureSay] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(creatureSayEvent, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(creatureSayEvent);
	Lua::pushUserdata<Creature>(L, creature);
	Lua::setCreatureMetatable(L, -1, creature);
	lua_pushinteger(L, type);
	Lua::pushString(L, text);
	scriptInterface->callVoidFunction(3);
}

void NpcEventsHandler::onPlayerTrade(Player* player, int32_t callback, uint16_t itemId, uint8_t count, uint8_t amount,
                                     bool ignore, bool inBackpacks) const
{
	if (callback == -1) {
		return;
	}

	// onBuy(player, itemid, count, amount, ignore, inbackpacks)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - NpcScript::onPlayerTrade] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(-1, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	Lua::pushCallback(L, callback);
	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");
	lua_pushinteger(L, itemId);
	lua_pushinteger(L, count);
	lua_pushinteger(L, amount);
	Lua::pushBoolean(L, ignore);
	Lua::pushBoolean(L, inBackpacks);
	scriptInterface->callVoidFunction(6);
}

void NpcEventsHandler::onPlayerCloseChannel(Player* player) const
{
	if (playerCloseChannelEvent == -1) {
		return;
	}

	// onPlayerCloseChannel(player)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - NpcScript::onPlayerCloseChannel] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(playerCloseChannelEvent, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(playerCloseChannelEvent);
	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");
	scriptInterface->callVoidFunction(1);
}

void NpcEventsHandler::onPlayerEndTrade(Player* player) const
{
	if (playerEndTradeEvent == -1) {
		return;
	}

	// onPlayerEndTrade(player)
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - NpcScript::onPlayerEndTrade] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(playerEndTradeEvent, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(playerEndTradeEvent);
	Lua::pushUserdata<Player>(L, player);
	Lua::setMetatable(L, -1, "Player");
	scriptInterface->callVoidFunction(1);
}

void NpcEventsHandler::onThink() const
{
	if (thinkEvent == -1) {
		return;
	}

	// onThink()
	if (!scriptInterface->reserveScriptEnv()) {
		LOG_ERROR("[Error - NpcScript::onThink] Call stack overflow");
		return;
	}

	ScriptEnvironment* env = scriptInterface->getScriptEnv();
	env->setScriptId(thinkEvent, scriptInterface.get());
	env->setNpc(npc);

	scriptInterface->pushFunction(thinkEvent);
	scriptInterface->callVoidFunction(0);
}
