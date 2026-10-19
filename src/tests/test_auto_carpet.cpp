#include "../otpch.h"

#include "../bed.h"
#include "../chat.h"
#include "../configmanager.h"
#include "../creatureevent.h"
#include "../events.h"
#include "../game.h"
#include "../globalevent.h"
#include "../housetile.h"
#include "../iomapserialize.h"
#include "../logger.h"
#include "../luascript.h"
#include "../movement.h"
#include "../player.h"
#include "../scriptmanager.h"
#include "../weapons.h"
#include "test_support.h"

extern Game g_game;
extern LuaEnvironment g_luaEnvironment;

struct IOMapSerializeTestAccess
{
	static std::string save(const Tile* tile)
	{
		PropWriteStream stream;
		IOMapSerialize::saveTile(stream, tile);
		return std::string(stream.getStream());
	}
	static bool load(const std::string& bytes, Tile* tile)
	{
		PropStream stream;
		stream.init(bytes.data(), bytes.size());
		uint16_t x, y;
		uint8_t z;
		uint32_t count;
		if (!stream.read(x) || !stream.read(y) || !stream.read(z) || !stream.read(count) ||
		    tile->getPosition() != Position(x, y, z)) {
			return false;
		}
		for (uint32_t i = 0; i < count; ++i) {
			if (!IOMapSerialize::loadItem(stream, tile)) {
				return false;
			}
		}
		return true;
	}
};

namespace {
constexpr auto marker = "__auto_carpet_source";

struct Fixture
{
	static inline uint16_t next = 1000;
	uint16_t x = next += 10;
	uint16_t y = 2000;
	std::shared_ptr<House> house = std::make_shared<House>(x);
	std::shared_ptr<House> otherHouse = std::make_shared<House>(x + 1);
	std::unique_ptr<MoveEvents> previous = std::make_unique<MoveEvents>();
	std::vector<Position> positions;
	Fixture() { g_moveEvents.swap(previous); }
	~Fixture()
	{
		// Test callback captures may already be out of scope during fixture teardown.
		g_moveEvents->clear(false);
		g_moveEvents->clear(true);
		for (const auto& pos : positions) {
			auto* tile = g_game.map.getTile(pos);
			while (tile && tile->getItemList() && !tile->getItemList()->empty()) {
				auto item = *tile->getItemList()->begin();
				tile->removeThing(item.get(), item->getItemCount());
			}
			g_game.map.removeTile(pos);
		}
		g_moveEvents.swap(previous);
	}
	Tile* tile(int dx, int dy, const std::shared_ptr<House>& owner = {}, uint16_t groundId = 100)
	{
		Position pos{uint16_t(x + dx), uint16_t(y + dy), 7};
		auto result = std::make_unique<HouseTile>(pos.x, pos.y, pos.z, owner ? owner : house);
		auto* raw = result.get();
		if (groundId) {
			auto ground = Item::CreateItem(groundId);
			raw->internalAddThing(ground.get());
		}
		g_game.map.setTile(pos, std::move(result));
		positions.push_back(pos);
		return raw;
	}
	std::shared_ptr<Item> add(Tile* tile, uint16_t id, uint32_t instance = 0, bool notify = true)
	{
		auto item = Item::CreateItem(id, 1);
		CHECK(item);
		item->setInstanceID(instance);
		if (notify) {
			tile->addThing(item.get());
		} else {
			tile->internalAddThing(item.get());
		}
		CHECK(item->getParent() == tile);
		return item;
	}
};

ItemVector copies(Tile* tile, uint32_t instance = 0)
{
	ItemVector result;
	if (const auto* items = tile->getItemList()) {
		for (const auto& item : *items) {
			if (item->getInstanceID() == instance && item->getCustomAttribute(marker)) {
				result.push_back(item);
			}
		}
	}
	return result;
}

void remove(const std::shared_ptr<Item>& item) { CHECK(g_game.internalRemoveItem(item.get()) == RETURNVALUE_NOERROR); }
} // namespace

TEST_CASE(real_item_metadata)
{
	for (uint16_t id :
	     {uint16_t(22736), uint16_t(22737), uint16_t(23536), uint16_t(23537), uint16_t(694), uint16_t(695)}) {
		const auto& t = Item::items[id];
		std::cout << "ITEM " << id << " client=" << Item::CreateItem(id)->getClientID() << " type=" << int(t.type)
		          << " move=" << t.moveable << " top=" << t.alwaysOnTop << " order=" << int(t.alwaysOnTopOrder)
		          << " height=" << t.hasHeight << " solid=" << t.blockSolid << " path=" << t.blockPathFind << '\n';
	}
	CHECK(Item::items[22736].isCarpet());
	CHECK(Item::items[694].isBed());
}

TEST_CASE(real_bed_parts_transform_update_and_100_cycles)
{
	Fixture f;
	auto* source = f.tile(0, 0);
	auto* head = f.tile(0, 1);
	auto* foot = f.tile(1, 1);
	auto bedHead = f.add(head, 694);
	auto bedFoot = f.add(foot, 695);
	CHECK(bedHead->getBed());
	const auto sleeper = bedHead->getBed()->getSleeper();
	auto directCarpet = Item::CreateItem(22737);
	const auto directResult = head->queryAdd(0, *directCarpet, 1, 0);
	std::cout << "Direct placement under bed queryAdd=" << int(directResult) << '\n';
	CHECK(directResult == RETURNVALUE_NOTENOUGHROOM);
	auto carpet = f.add(source, 22737);
	CHECK(copies(head).empty());
	for (int i = 0; i < 100; ++i) {
		CHECK(g_game.transformItem(carpet.get(), 22736, 1) == carpet.get());
		CHECK(copies(head).size() == 1);
		CHECK(copies(foot).size() == 1);
		CHECK(copies(head).front()->getID() == 22736);
		CHECK(g_game.transformItem(carpet.get(), 22737, 1) == carpet.get());
		CHECK(copies(head).empty());
		CHECK(copies(foot).empty());
	}
	CHECK(bedHead->getParent() == head && bedHead->getID() == 694);
	CHECK(bedFoot->getParent() == foot && bedFoot->getID() == 695);
	CHECK(bedHead->getBed()->getSleeper() == sleeper);
	CHECK(head->hasFlag(TILESTATE_BED));
}

TEST_CASE(add_replace_type_remove_and_no_attribute_inheritance)
{
	Fixture f;
	auto* source = f.tile(0, 0);
	auto* target = f.tile(1, 0);
	f.add(target, 694);
	auto carpet = Item::CreateItem(22736);
	carpet->setActionId(58001);
	carpet->setText("manual quest attributes");
	source->addThing(carpet.get());
	CHECK(copies(target).size() == 1);
	CHECK(copies(target).front()->getActionId() == 0);
	CHECK(copies(target).front()->getText().empty());
	CHECK(copies(target).front()->getItemUID() != carpet->getItemUID());
	g_game.transformItem(carpet.get(), 23537, 1);
	CHECK(copies(target).size() == 1);
	CHECK(copies(target).front()->getID() == 23537);
	remove(carpet);
	CHECK(copies(target).empty());
}

TEST_CASE(empty_house_and_public_source_do_not_create_underlays)
{
	Fixture f;
	auto* empty = f.tile(0, 0);
	auto carpet = f.add(empty, 22736);
	CHECK(empty->getItemList()->size() == 1);
	g_game.transformItem(carpet.get(), 22737, 1);
	CHECK(empty->getItemList()->size() == 1);
	auto* neighbor = f.tile(1, 0);
	f.add(neighbor, 694);
	Position publicPosition{uint16_t(f.x), f.y, 7};
	// Clear the old house tile before placing a synthetic public tile at that position.
	empty->removeThing(carpet.get(), 1);
	g_game.map.removeTile(publicPosition);
	auto publicTile = std::make_unique<DynamicTile>(publicPosition.x, publicPosition.y, publicPosition.z);
	auto ground = Item::CreateItem(100);
	publicTile->internalAddThing(ground.get());
	g_game.map.setTile(publicPosition, std::move(publicTile));
	f.add(g_game.map.getTile(publicPosition), 22736);
	CHECK(copies(neighbor).empty());
}

TEST_CASE(boundaries_ground_furniture_and_instances)
{
	Fixture f;
	auto* source = f.tile(0, 0);
	auto* other = f.tile(1, 0, f.otherHouse);
	auto* differentGround = f.tile(-1, 0, nullptr, 101);
	auto* noGround = f.tile(0, -1, nullptr, 0);
	auto* unsupported = f.tile(1, 1);
	auto* differentInstance = f.tile(-1, -1);
	auto* supported = f.tile(-1, 1);
	f.add(other, 694);
	f.add(differentGround, 694);
	f.add(noGround, 694);
	f.add(unsupported, 2472);
	f.add(differentInstance, 694, 22);
	f.add(supported, 694, 11);
	auto carpet = f.add(source, 22736, 11);
	for (auto* tile : {other, differentGround, noGround, unsupported, differentInstance}) {
		CHECK(copies(tile, 11).empty());
	}
	CHECK(copies(supported, 11).empty());
	CHECK(copies(supported).empty());
	remove(carpet);
	CHECK(copies(supported, 11).empty());
	auto* worldTarget = f.tile(0, 1);
	f.add(worldTarget, 694);
	auto worldCarpet = f.add(source, 22736);
	CHECK(copies(worldTarget).size() == 1);
	for (auto* tile : {other, differentGround, noGround, unsupported, differentInstance, supported}) {
		CHECK(copies(tile).empty());
		CHECK(copies(tile, 11).empty());
	}
	remove(worldCarpet);
}

TEST_CASE(removal_callbacks_revalidate_competing_sources_and_manual_carpet)
{
	for (int change = 0; change < 3; ++change) {
		Fixture f;
		auto* a = f.tile(-1, 0);
		auto* b = f.tile(1, 0);
		auto* target = f.tile(0, 0);
		f.add(target, 694);
		auto first = f.add(a, 22736);
		auto second = f.add(b, 23537);
		bool ran = false;
		std::shared_ptr<Item> manual;
		auto event = std::make_unique<MoveEvent>(g_moveEvents->getScriptInterfacePtr());
		event->setEventType(MOVE_EVENT_REMOVE_ITEM);
		event->addPosList(target->getPosition());
		event->moveFunction = [&](Item* item, Item*, const Position&) {
			if (!ran && item->getCustomAttribute(marker)) {
				ran = true;
				if (change == 0) {
					manual = f.add(target, 22736);
				} else if (change == 1) {
					remove(second);
				} else {
					g_game.transformItem(second.get(), 23536, 1);
				}
			}
			return 1;
		};
		CHECK(g_moveEvents->registerLuaEvent(event.release()));
		remove(first);
		CHECK(ran);
		CHECK(copies(target).empty());
		if (manual) {
			CHECK(manual->getParent() == target);
		}
	}
}

TEST_CASE(ground_add_update_and_remove_reconcile_existing_underlays)
{
	Fixture f;
	auto* source = f.tile(0, 0);
	auto* target = f.tile(1, 0);
	f.add(target, 694);
	auto carpet = f.add(source, 22736);
	for (auto* tile : {source, target}) {
		CHECK(copies(target).size() == 1);
		f.add(tile, 101);
		CHECK(copies(target).empty());
		f.add(tile, 100);
		CHECK(copies(target).size() == 1);
		auto ground = tile->getGround()->shared_from_this();
		CHECK(g_game.internalRemoveItem(ground.get(), -1, false, FLAG_IGNORENOTMOVEABLE) == RETURNVALUE_NOERROR);
		CHECK(copies(target).empty());
		f.add(tile, 100);
		CHECK(copies(target).size() == 1);
		g_game.transformItem(tile->getGround(), 101, 1);
		CHECK(copies(target).empty());
		g_game.transformItem(tile->getGround(), 100, 1);
		CHECK(copies(target).size() == 1);
	}
	remove(carpet);
	CHECK(copies(target).empty());
}

TEST_CASE(full_destination_retries_only_while_capacity_is_pending)
{
	CHECK(!Item::items[3043].blockSolid && !Item::items[3043].isGroundTile());
	Fixture f;
	auto* source = f.tile(0, 0);
	auto* target = f.tile(1, 0);
	f.add(target, 694);
	ItemVector filling;
	while (target->getThingCount() < MAX_STACKPOS_THINGS) {
		filling.push_back(f.add(target, 3043));
	}
	auto carpet = f.add(source, 22736);
	CHECK(copies(target).empty());
	remove(filling.back());
	filling.pop_back();
	CHECK(copies(target).size() == 1);
	CHECK(target->getThingCount() == MAX_STACKPOS_THINGS);
	remove(carpet);
	remove(filling.back());
	CHECK(copies(target).empty());
}

TEST_CASE(pending_destination_retries_after_creature_removal_and_cancels_without_source)
{
	for (bool cancel : {false, true}) {
		Fixture f;
		auto* source = f.tile(0, 0);
		auto* target = f.tile(1, 0);
		f.add(target, 694);
		auto creature = std::make_shared<Player>(nullptr);
		creature->setGroup(std::make_shared<Group>());
		target->internalAddThing(creature.get());
		while (target->getThingCount() < MAX_STACKPOS_THINGS) {
			f.add(target, 3043);
		}
		auto carpet = f.add(source, 22736);
		CHECK(copies(target).empty());
		if (cancel) {
			remove(carpet);
		}
		target->removeThing(creature.get(), 0);
		creature->setParent(nullptr);
		target->postRemoveNotification(creature.get(), nullptr, 0);
		CHECK(copies(target).size() == (cancel ? 0 : 1));
		CHECK(target->getThingCount() <= MAX_STACKPOS_THINGS);
	}
}

TEST_CASE(insertion_callbacks_can_remove_source_item_or_source_tile_or_replace_destination)
{
	for (int change = 0; change < 3; ++change) {
		Fixture f;
		auto* source = f.tile(0, 0);
		auto* target = f.tile(1, 0);
		f.add(target, 694);
		auto carpet = f.add(source, 22737);
		const Position pos = target->getPosition();
		std::weak_ptr<Tile> oldTile = target->weak_from_this();
		bool ran = false;
		auto event = std::make_unique<MoveEvent>(g_moveEvents->getScriptInterfacePtr());
		event->setEventType(MOVE_EVENT_ADD_ITEM);
		event->addPosList(pos);
		event->moveFunction = [&](Item* item, Item*, const Position&) {
			if (!ran && item->getCustomAttribute(marker)) {
				ran = true;
				if (change == 0) {
					CHECK(g_game.map.removeTile(pos));
					CHECK(!oldTile.expired());
					auto replacement = std::make_unique<HouseTile>(pos.x, pos.y, pos.z, f.house);
					auto ground = Item::CreateItem(100);
					replacement->internalAddThing(ground.get());
					g_game.map.setTile(pos, std::move(replacement));
				} else if (change == 1) {
					remove(carpet);
				} else {
					CHECK(g_game.map.removeTile(source->getPosition()));
				}
			}
			return 1;
		};
		CHECK(g_moveEvents->registerLuaEvent(event.release()));
		g_game.transformItem(carpet.get(), 22736, 1);
		CHECK(ran);
		CHECK(copies(g_game.map.getTile(pos)).empty());
		if (change == 0) {
			CHECK(oldTile.expired());
		}
	}
}

TEST_CASE(continuously_changing_source_is_bounded_and_leaves_no_stale_copy)
{
	Fixture f;
	auto* source = f.tile(0, 0);
	auto* target = f.tile(1, 0);
	f.add(target, 694);
	auto carpet = f.add(source, 22737);
	unsigned calls = 0;
	auto event = std::make_unique<MoveEvent>(g_moveEvents->getScriptInterfacePtr());
	event->setEventType(MOVE_EVENT_ADD_ITEM);
	event->addPosList(target->getPosition());
	event->moveFunction = [&](Item* item, Item*, const Position&) {
		if (item->getCustomAttribute(marker)) {
			++calls;
			g_game.transformItem(carpet.get(), carpet->getID() == 22736 ? 23537 : 22736, 1);
		}
		return 1;
	};
	CHECK(g_moveEvents->registerLuaEvent(event.release()));
	g_game.transformItem(carpet.get(), 22736, 1);
	CHECK(calls > 1 && calls <= 16);
	CHECK(copies(target).empty());
	g_moveEvents->clear(false);
	g_game.transformItem(carpet.get(), 22737, 1);
	g_game.transformItem(carpet.get(), 22736, 1);
	CHECK(copies(target).size() == 1);
}

TEST_CASE(manual_carpet_has_priority_and_survives)
{
	Fixture f;
	auto* source = f.tile(0, 0);
	auto* target = f.tile(1, 0);
	f.add(target, 694);
	auto carpet = f.add(source, 22736);
	CHECK(copies(target).size() == 1);
	auto manual = f.add(target, 23537);
	CHECK(copies(target).empty());
	remove(carpet);
	CHECK(manual->getParent() == target);
	CHECK(manual->getID() == 23537);
}

TEST_CASE(contested_sources_are_order_independent_and_fall_back)
{
	for (bool reverse : {false, true}) {
		Fixture f;
		auto* a = f.tile(-1, 0);
		auto* b = f.tile(1, 0);
		auto* target = f.tile(0, 0);
		f.add(target, 694);
		std::shared_ptr<Item> first, second;
		if (reverse) {
			second = f.add(b, 23537);
			first = f.add(a, 22736);
		} else {
			first = f.add(a, 22736);
			second = f.add(b, 23537);
		}
		CHECK(copies(target).size() == 1);
		CHECK(copies(target).front()->getID() == 22736);
		remove(first);
		CHECK(copies(target).size() == 1);
		CHECK(copies(target).front()->getID() == 23537);
		remove(second);
		CHECK(copies(target).empty());
	}
}

TEST_CASE(house_item_serializer_round_trip_without_duplication)
{
	Fixture f;
	auto* source = f.tile(0, 0);
	auto* target = f.tile(1, 0);
	f.add(target, 694);
	auto carpet = f.add(source, 22736);
	CHECK(copies(target).size() == 1);
	const auto savedSource = IOMapSerializeTestAccess::save(source);
	const auto savedCopy = IOMapSerializeTestAccess::save(target);
	remove(carpet);
	CHECK(copies(target).empty());
	CHECK(IOMapSerializeTestAccess::load(savedCopy, target));
	CHECK(IOMapSerializeTestAccess::load(savedSource, source));
	CHECK(copies(target).size() == 1);
	auto loaded = *source->getItemList()->begin();
	g_game.transformItem(loaded.get(), 23537, 1);
	CHECK(copies(target).size() == 1);
	CHECK(copies(target).front()->getID() == 23537);
	remove(loaded);
	CHECK(copies(target).empty());
}

TEST_CASE(real_lua_action_gates_and_native_transform)
{
	Fixture f;
	auto* source = f.tile(0, 0);
	auto* target = f.tile(1, 0);
	f.add(target, 694);
	auto carpet = f.add(source, 22737);
	CHECK(g_luaEnvironment.getLuaState());
	LuaScriptInterface script{"Auto carpet test"};
	CHECK(script.initState());
	auto* L = script.getLuaState();
	CHECK(LuaScriptInterface::reserveScriptEnv());
	Lua::pushSharedPtr(L, carpet);
	Lua::setItemMetatable(L, -1, carpet.get());
	lua_setglobal(L, "nativeCarpet");
	const auto run = [L](const char* code) {
		if (luaL_dostring(L, code) != LUA_OK) {
			throw std::runtime_error(lua_tostring(L, -1));
		}
	};
	// The actual action runs with controlled permission answers and a native Item transform.
	run(R"lua(
Action = function() captured = {id=function() end, register=function() end}; return captured end
MESSAGE_FAILURE = 1
dofile('data/scripts/actions/objects/carpets.lua')
invited, modifiable, inHouse = true, true, true
house = {isInvited=function() return invited end, canModifyItems=function() return modifiable end}
tile = {getHouse=function() if inHouse then return house end end,
        getItemCountById=function(_, id) return nativeCarpet:getId() == id and 1 or 0 end}
proxy = {getId=function() return nativeCarpet:getId() end,
         getPosition=function() return {getTile=function() return tile end} end,
         getCount=function() return nativeCarpet:getCount() end,
         getCustomAttribute=function(_, k) return nativeCarpet:getCustomAttribute(k) end,
         transform=function(_, ...) return nativeCarpet:transform(...) end}
player = {sendTextMessage=function(_, _, text) lastFailure = text end}
local function use() assert(captured.onUse(player, proxy, {}, nil, {}, false)) end
invited=false; use(); assert(nativeCarpet:getId()==22737)
assert(lastFailure:find("another person's house", 1, true))
invited=true; modifiable=false; use(); assert(nativeCarpet:getId()==22737)
assert(lastFailure:find('protected house', 1, true))
modifiable=true; inHouse=false; use(); assert(nativeCarpet:getId()==22737)
assert(lastFailure:find('only inside a house', 1, true))
inHouse=true; use(); assert(nativeCarpet:getId()==22736)
)lua");
	CHECK(copies(target).size() == 1);
	Lua::pushSharedPtr(L, copies(target).front());
	Lua::setItemMetatable(L, -1, copies(target).front().get());
	lua_setglobal(L, "generated");
	run(R"lua(
local original = nativeCarpet
nativeCarpet = generated
assert(captured.onUse(player, proxy, {}, nil, {}, false))
assert(generated:getId()==22736)
assert(lastFailure:find('original carpet', 1, true))
nativeCarpet = original
for i=1,100 do
 assert(captured.onUse(player, proxy, {}, nil, {}, false)); assert(nativeCarpet:getId()==22737)
 assert(captured.onUse(player, proxy, {}, nil, {}, false)); assert(nativeCarpet:getId()==22736)
end
)lua");
	CHECK(copies(target).size() == 1);
	LuaScriptInterface::resetScriptEnv();
}

TEST_CASE(real_wrap_target_removes_and_restores_furniture_underlay)
{
	uint16_t furnitureId = 0, wrappedId = 0;
	for (size_t id = 1; id < Item::items.size(); ++id) {
		const auto& type = Item::items[id];
		if (!type.wrapableTo || type.moveable ||
		    (!type.isBed() && !(type.blockSolid && type.alwaysOnTop && !type.hasHeight)) ||
		    (type.alwaysOnTop && type.alwaysOnTopOrder <= Item::items[22736].alwaysOnTopOrder)) {
			continue;
		}
		const auto& wrapped = Item::items[type.wrapableTo];
		if (wrapped.id && !wrapped.blockSolid && !wrapped.isBed() && !wrapped.isCarpet() && !wrapped.isGroundTile()) {
			furnitureId = uint16_t(id);
			wrappedId = type.wrapableTo;
			break;
		}
	}
	CHECK(furnitureId != 0);
	std::cout << "Native wrap fixture: " << furnitureId << " -> " << wrappedId << '\n';
	Fixture f;
	auto* source = f.tile(0, 0);
	auto* target = f.tile(1, 0);
	auto furniture = f.add(target, furnitureId);
	auto carpet = f.add(source, 22736);
	CHECK(copies(target).size() == 1);
	// Exercise the same native transform used by playerWrapableItem.
	auto* wrapped = g_game.transformItem(furniture.get(), wrappedId);
	CHECK(wrapped && wrapped->getParent() == target);
	CHECK(copies(target).empty());
	auto* restored = g_game.transformItem(wrapped, furnitureId);
	CHECK(restored && restored->getParent() == target);
	CHECK(copies(target).size() == 1);
	CHECK(copies(target).front()->getID() == carpet->getID());
}

TEST_CASE(furniture_and_blocker_lifecycle_reconciles_only_the_destination)
{
	uint16_t blockerId = 0;
	for (size_t id = 1; id < Item::items.size(); ++id) {
		const auto& type = Item::items[id];
		if (type.type == ITEM_TYPE_NONE && type.blockSolid && type.moveable && !type.isGroundTile()) {
			blockerId = uint16_t(id);
			break;
		}
	}
	CHECK(blockerId != 0);
	Fixture f;
	auto* source = f.tile(0, 0);
	auto* target = f.tile(1, 0);
	auto carpet = f.add(source, 22736);
	auto bed = f.add(target, 694);
	CHECK(copies(target).size() == 1);
	auto blocker = f.add(target, blockerId);
	CHECK(copies(target).empty());
	remove(blocker);
	CHECK(copies(target).size() == 1);
	CHECK(g_game.internalRemoveItem(bed.get()) == RETURNVALUE_NOTPOSSIBLE);
	// Simulate native house cleanup without weakening the stationary bed's guard.
	CHECK(g_game.internalRemoveItem(bed.get(), -1, false, FLAG_IGNORECANREMOVE) == RETURNVALUE_NOERROR);
	CHECK(copies(target).empty());
	f.add(target, 694);
	CHECK(copies(target).size() == 1);
	remove(carpet);
	CHECK(copies(target).empty());
}

TEST_CASE(unshared_tile_hooks_preserve_native_flags)
{
	// Standalone tiles can be populated before Map acquires shared ownership.
	auto tile = std::make_unique<DynamicTile>(30000, 30000, 7);
	CHECK(tile->weak_from_this().expired());
	auto bed = Item::CreateItem(694);
	tile->addThing(bed.get());
	CHECK(tile->hasFlag(TILESTATE_BED));
	tile->updateThing(bed.get(), 695, 1);
	CHECK(tile->hasFlag(TILESTATE_BED));
	tile->removeThing(bed.get(), 1);
	CHECK(!tile->hasFlag(TILESTATE_BED));
	CHECK(bed->isRemoved());
}

TEST_CASE(tile_hooks_survive_source_deletion_without_a_callers_shared_reference)
{
	const bool previous = ConfigManager::getBoolean(ConfigManager::CLEAN_PROTECTION_ZONES);
	struct ConfigGuard
	{
		bool previous;
		~ConfigGuard() { ConfigManager::setBoolean(ConfigManager::CLEAN_PROTECTION_ZONES, previous); }
	} configGuard{previous};
	ConfigManager::setBoolean(ConfigManager::CLEAN_PROTECTION_ZONES, true);
	for (int operation : {0, 1, 2}) {
		Fixture f;
		auto* source = f.tile(0, 0);
		auto* target = f.tile(1, 0);
		f.add(target, 694);
		auto carpet = f.add(source, 22736);
		const Position pos = source->getPosition();
		std::weak_ptr<Tile> sourceLifetime = source->weak_from_this();
		bool ran = false;
		auto event = std::make_unique<MoveEvent>(g_moveEvents->getScriptInterfacePtr());
		event->setEventType(MOVE_EVENT_REMOVE_ITEM);
		event->addPosList(target->getPosition());
		event->moveFunction = [&](Item* item, Item*, const Position&) {
			if (!ran && item->getCustomAttribute(marker)) {
				ran = true;
				CHECK(g_game.map.removeTile(pos));
			}
			return 1;
		};
		CHECK(g_moveEvents->registerLuaEvent(event.release()));
		// Unlike Game::internalRemoveItem/transformItem, these raw Tile callers
		// do not retain a cylinderTile for the duration of the hook.
		if (operation == 1) {
			auto replacement = Item::CreateItem(22736);
			replacement->setInstanceID(11);
			source->replaceThing(source->getThingIndex(carpet.get()), replacement.get());
		} else if (operation == 2) {
			auto ground = Item::CreateItem(101);
			source->addThing(ground.get());
		} else {
			source->removeThing(carpet.get(), 1);
		}
		CHECK(ran);
		CHECK(g_game.map.getTile(pos) == nullptr);
		CHECK(sourceLifetime.expired());
		CHECK(copies(target).empty());
	}
}

int main()
{
	std::cout << std::unitbuf;
	std::cout << "Loading real item metadata\n";
	if (!initLogger(LogLevel::CRITICAL)) {
		return EXIT_FAILURE;
	}
	const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	std::filesystem::current_path(root);
	Events events;
	g_events = &events;
	ScriptingManager bootstrap;
	if (!bootstrap.loadPreItems()) {
		return EXIT_FAILURE;
	}
	std::cout << "Pre-items ready\n";
	if (!Item::items.loadFromOtb("data/items/items.otb")) {
		return EXIT_FAILURE;
	}
	std::cout << "OTB ready\n";
	if (!Item::items.loadFromXml()) {
		return EXIT_FAILURE;
	}
	std::cout << "Running carpet tests\n";
	int result = EXIT_SUCCESS;
	for (const auto& test : tfs::tests::registry()) {
		std::cout << "[RUN] " << test.name << '\n';
		try {
			test.function();
		} catch (const std::exception& error) {
			std::cerr << "[FAIL] " << error.what() << '\n';
			result = EXIT_FAILURE;
		}
	}
	std::cout << tfs::tests::registry().size() << " tests completed, result=" << result << '\n';
	shutdownLogger();
	return result;
}
