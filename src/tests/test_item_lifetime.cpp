#include "../otpch.h"

#include "../bed.h"
#include "../chat.h"
#include "../configmanager.h"
#include "../events.h"
#include "../game.h"
#include "../globalevent.h"
#include "../house.h"
#include "../item.h"
#include "../luascript.h"
#include "../player.h"
#include "../scriptmanager.h"
#include "../vocation.h"

#include "test_support.h"
#include <memory>

extern bool isValidItemPointer(Item* item);
extern LuaEnvironment g_luaEnvironment;
extern Vocations g_vocations;

namespace {

class LuaFixture
{
public:
	LuaFixture()
	{
		prevWarnUnsafe = ConfigManager::getBoolean(ConfigManager::WARN_UNSAFE_SCRIPTS);
		prevConvertUnsafe = ConfigManager::getBoolean(ConfigManager::CONVERT_UNSAFE_SCRIPTS);
		ConfigManager::setBoolean(ConfigManager::WARN_UNSAFE_SCRIPTS, false);
		ConfigManager::setBoolean(ConfigManager::CONVERT_UNSAFE_SCRIPTS, false);
		CHECK(g_luaEnvironment.initState());
		L = g_luaEnvironment.getLuaState();
		CHECK(L != nullptr);
	}

	~LuaFixture()
	{
		g_luaEnvironment.closeState();
		ConfigManager::setBoolean(ConfigManager::WARN_UNSAFE_SCRIPTS, prevWarnUnsafe);
		ConfigManager::setBoolean(ConfigManager::CONVERT_UNSAFE_SCRIPTS, prevConvertUnsafe);
	}

	lua_State* L = nullptr;
	bool prevWarnUnsafe{false};
	bool prevConvertUnsafe{false};
};

struct ItemTypePropertyGuard
{
	uint16_t itemId;
	bool origMoveable;
	bool origStackable;
	bool origPickupable;
	Direction origBedPartnerDir;

	explicit ItemTypePropertyGuard(uint16_t id)
	    : itemId(id),
	      origMoveable(Item::items.getItemType(id).moveable),
	      origStackable(Item::items.getItemType(id).stackable),
	      origPickupable(Item::items.getItemType(id).pickupable),
	      origBedPartnerDir(Item::items.getItemType(id).bedPartnerDir)
	{
	}

	~ItemTypePropertyGuard()
	{
		Item::items.getItemType(itemId).moveable = origMoveable;
		Item::items.getItemType(itemId).stackable = origStackable;
		Item::items.getItemType(itemId).pickupable = origPickupable;
		Item::items.getItemType(itemId).bedPartnerDir = origBedPartnerDir;
	}
};

struct MapTileGuard
{
	std::vector<Position> positions;

	void track(uint16_t x, uint16_t y, uint8_t z) { positions.emplace_back(x, y, z); }

	~MapTileGuard()
	{
		for (const auto& pos : positions) {
			g_game.map.removeTile(pos);
		}
	}
};

} // namespace

TEST_CASE(item_lifetime_registry_tracks_destroyed_item)
{
	Item* rawItem = nullptr;
	{
		auto item = std::make_shared<Item>(0);
		rawItem = item.get();
		CHECK(isValidItemPointer(rawItem));
	}

	CHECK(!isValidItemPointer(rawItem));
}

TEST_CASE(house_transfer_item_keeps_identity_until_reset)
{
	auto house = std::make_shared<House>(1);
	house->setName("Lifetime Test House");

	auto first = house->getTransferItem();
	auto second = house->getTransferItem();

	CHECK(first != nullptr);
	CHECK(second == first);

	house->resetTransferItem();
	auto replacement = house->getTransferItem();

	CHECK(replacement != nullptr);
	CHECK(replacement != first);
}

TEST_CASE(house_transfer_item_trade_cancel_resets_document)
{
	auto house = std::make_shared<House>(2);
	house->setName("Cancel Test House");

	auto transferItem = house->getTransferItem();
	CHECK(transferItem != nullptr);

	transferItem->onTradeEvent(ON_TRADE_CANCEL, nullptr);
	auto replacement = house->getTransferItem();

	CHECK(replacement != nullptr);
	CHECK(replacement != transferItem);
}

TEST_CASE(houses_get_house_preserves_house_lifetime)
{
	std::shared_ptr<House> house;
	{
		Houses houses;
		auto addedHouse = houses.addHouse(42);
		house = houses.getHouse(42);

		CHECK(house != nullptr);
		CHECK(house == addedHouse);
		CHECK(houses.getHouse(42) == house);
		CHECK(houses.getHouse(43) == nullptr);
	}

	CHECK(house != nullptr);
	CHECK(house->getId() == 42);
}

TEST_CASE(houses_add_house_creates_and_returns_shared_ptr)
{
	Houses houses;
	auto house100 = houses.addHouse(100);
	CHECK(house100 != nullptr);
	CHECK(house100->getId() == 100);

	auto house100_again = houses.addHouse(100);
	CHECK(house100_again != nullptr);
	CHECK(house100 == house100_again);
	CHECK(house100.get() == house100_again.get());

	auto house200 = houses.addHouse(200);
	CHECK(house200 != nullptr);
	CHECK(house200->getId() == 200);
	CHECK(house100 != house200);

	CHECK(houses.getHouse(100) == house100);
	CHECK(houses.getHouse(200) == house200);
	CHECK(houses.getHouse(300) == nullptr);
}

TEST_CASE(houses_add_house_preserves_lifetime_beyond_houses_scope)
{
	std::shared_ptr<House> house;
	{
		Houses houses;
		house = houses.addHouse(100);
		CHECK(house != nullptr);
		CHECK(house->getId() == 100);
	}

	CHECK(house != nullptr);
	CHECK(house->getId() == 100);
}

TEST_CASE(houses_get_house_by_player_id_preserves_lifetime_and_semantics)
{
	Houses houses;
	auto house1 = houses.addHouse(101);
	auto house2 = houses.addHouse(102);

	CHECK(house1 != nullptr);
	CHECK(house2 != nullptr);
	CHECK(house1->getOwner() == 0);
	CHECK(house2->getOwner() == 0);

	// Searching for non-existent owner returns nullptr
	CHECK(houses.getHouseByPlayerId(999) == nullptr);
	CHECK(houses.getHouseByPlayerId(1) == nullptr);

	// Const access test
	const Houses& constHouses = houses;
	CHECK(constHouses.getHouseByPlayerId(999) == nullptr);

	// Searching for owner 0 finds the first house with owner 0
	auto unowned = constHouses.getHouseByPlayerId(0);
	CHECK(unowned != nullptr);
	CHECK(unowned->getId() == 101);
	CHECK(unowned == house1);
	CHECK(unowned.get() == house1.get());
}

TEST_CASE(door_get_house_returns_valid_shared_ptr_and_preserves_identity)
{
	auto house = std::make_shared<House>(100);
	auto door = std::make_shared<Door>(0);
	door->setDoorId(1);

	CHECK(door->getHouse() == nullptr);

	house->addDoor(door.get());

	auto retrievedHouse = door->getHouse();
	CHECK(retrievedHouse != nullptr);
	CHECK(retrievedHouse == house);
	CHECK(retrievedHouse->getId() == 100);
	CHECK(house->getDoors().size() == 1);
	CHECK(house->getDoorByNumber(1) == door);
	CHECK(house->getDoorByNumber(1).get() == door.get());
}

TEST_CASE(door_get_house_returns_nullptr_when_house_is_destroyed)
{
	auto door = std::make_shared<Door>(0);
	door->setDoorId(2);

	{
		auto house = std::make_shared<House>(200);
		house->addDoor(door.get());
		CHECK(door->getHouse() != nullptr);
		CHECK(door->getHouse()->getId() == 200);
	}

	CHECK(door->getHouse() == nullptr);
	// onRemoved when house is destroyed should be safe and not throw/crash
	door->onRemoved();
	CHECK(door->getHouse() == nullptr);
}

void ensureItemTypes()
{
	static const bool loaded = [] {
		const auto itemFile = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
		                      "data/items/items.otb";
		return Item::items.loadFromOtb(itemFile.string());
	}();
	CHECK(loaded);
}

void ensureVocations()
{
	static const bool loaded = [] {
		const auto originalPath = std::filesystem::current_path();
		const auto projectPath = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
		std::filesystem::current_path(projectPath);
		const bool result = g_vocations.loadFromXml();
		std::filesystem::current_path(originalPath);
		return result;
	}();
	CHECK(loaded);
}

std::shared_ptr<Player> makeTestPlayer(uint32_t guid, std::string_view name)
{
	ensureItemTypes();
	auto player = std::make_shared<Player>(nullptr);
	player->setGUID(guid);
	player->setName(name);
	player->setGroup(std::make_shared<Group>());
	return player;
}

TEST_CASE(door_can_use_and_access_list_behavior)
{
	auto house = std::make_shared<House>(300);
	auto door = std::make_shared<Door>(0);
	door->setDoorId(3);
	house->addDoor(door.get());

	auto player = makeTestPlayer(1, "TestPlayer");

	// Uninvited player on restricted door cannot use
	CHECK(!door->canUse(player.get()));

	// Door with wildcard access list allows player
	door->setAccessList("*");
	CHECK(door->canUse(player.get()));

	// When house is reset/destroyed, door allows usage freely
	house.reset();
	CHECK(door->getHouse() == nullptr);
	CHECK(door->canUse(player.get()));
}

TEST_CASE(housetile_get_house_returns_valid_shared_ptr_and_preserves_identity)
{
	auto house = std::make_shared<House>(500);
	auto houseTile = std::make_shared<HouseTile>(100, 100, 7, house);
	house->addTile(houseTile);

	auto retrievedHouse = houseTile->getHouse();
	CHECK(retrievedHouse != nullptr);
	CHECK(retrievedHouse == house);
	CHECK(retrievedHouse->getId() == 500);
	CHECK(retrievedHouse.get() == house.get());
}

TEST_CASE(housetile_get_house_returns_nullptr_when_house_is_null_or_destroyed)
{
	// Null/empty shared_ptr
	auto nullHouseTile = std::make_shared<HouseTile>(102, 102, 7, nullptr);
	CHECK(nullHouseTile->getHouse() == nullptr);

	// Expiration after House dies
	std::shared_ptr<HouseTile> houseTile;
	{
		auto house = std::make_shared<House>(600);
		houseTile = std::make_shared<HouseTile>(101, 101, 7, house);
		house->addTile(houseTile);
		CHECK(houseTile->getHouse() != nullptr);
		CHECK(houseTile->getHouse()->getId() == 600);
	}

	CHECK(houseTile->getHouse() == nullptr);
}

TEST_CASE(item_get_door_returns_nullptr_for_regular_item_and_valid_shared_ptr_for_door)
{
	// Regular Item returns nullptr
	auto regularItem = std::make_shared<Item>(100);
	CHECK(regularItem->getDoor() == nullptr);
	const auto& constRegularItem = regularItem;
	CHECK(constRegularItem->getDoor() == nullptr);

	// Door returns valid shared_ptr and preserves identity
	auto door = std::make_shared<Door>(0);
	door->setDoorId(45);
	Door* rawDoor = door.get();

	std::shared_ptr<Door> nonConstDoor = door->getDoor();
	CHECK(nonConstDoor != nullptr);
	CHECK(nonConstDoor == door);
	CHECK(nonConstDoor.get() == rawDoor);
	CHECK(nonConstDoor->getDoorId() == 45);

	const auto& constDoor = door;
	std::shared_ptr<const Door> constDoorRef = constDoor->getDoor();
	CHECK(constDoorRef != nullptr);
	CHECK(constDoorRef == door);
	CHECK(constDoorRef.get() == rawDoor);
	CHECK(constDoorRef->getDoorId() == 45);

	// Lifetime extension
	door.reset();
	CHECK(nonConstDoor != nullptr);
	CHECK(nonConstDoor.get() == rawDoor);
	CHECK(constDoorRef != nullptr);
	CHECK(constDoorRef.get() == rawDoor);
}

TEST_CASE(housetile_update_house_registers_door_using_get_door_and_handles_destruction)
{
	auto house = std::make_shared<House>(560);
	auto houseTile = std::make_shared<HouseTile>(112, 112, 7, house);
	house->addTile(houseTile);

	auto door = std::make_shared<Door>(0);
	door->setDoorId(77);
	CHECK(door->getHouse() == nullptr);

	// Adding door with valid doorId to houseTile updates house registration
	houseTile->internalAddThing(0, door.get());

	CHECK(door->getHouse() != nullptr);
	CHECK(door->getHouse() == house);
	CHECK(house->getDoors().size() == 1);
	CHECK(house->getDoorByNumber(77) == door);

	// Safe removal / destruction
	door->onRemoved();
	CHECK(house->getDoorByNumber(77) == nullptr);
	CHECK(house->getDoors().empty());
}

TEST_CASE(item_get_bed_returns_nullptr_for_regular_item_and_valid_shared_ptr_for_bed_item)
{
	// Regular Item returns nullptr
	auto regularItem = std::make_shared<Item>(100);
	CHECK(regularItem->getBed() == nullptr);
	const auto& constRegularItem = regularItem;
	CHECK(constRegularItem->getBed() == nullptr);

	// BedItem returns valid shared_ptr and preserves identity
	auto bed = std::make_shared<BedItem>(694);
	BedItem* rawBed = bed.get();

	std::shared_ptr<BedItem> nonConstBed = bed->getBed();
	CHECK(nonConstBed != nullptr);
	CHECK(nonConstBed == bed);
	CHECK(nonConstBed.get() == rawBed);

	const auto& constBed = bed;
	std::shared_ptr<const BedItem> constBedRef = constBed->getBed();
	CHECK(constBedRef != nullptr);
	CHECK(constBedRef == bed);
	CHECK(constBedRef.get() == rawBed);

	// Lifetime extension
	bed.reset();
	CHECK(nonConstBed != nullptr);
	CHECK(nonConstBed.get() == rawBed);
	CHECK(constBedRef != nullptr);
	CHECK(constBedRef.get() == rawBed);
}

TEST_CASE(housetile_update_house_registers_bed_item_using_get_bed)
{
	auto house = std::make_shared<House>(550);
	auto houseTile = std::make_shared<HouseTile>(110, 110, 7, house);
	house->addTile(houseTile);

	auto bed = std::make_shared<BedItem>(694);
	CHECK(bed->getHouse() == nullptr);

	// Adding bed to houseTile updates house registration
	houseTile->internalAddThing(0, bed.get());

	CHECK(bed->getHouse() != nullptr);
	CHECK(bed->getHouse() == house);
	CHECK(!bed->canRemove());
}

TEST_CASE(bed_get_house_returns_valid_shared_ptr_and_preserves_identity)
{
	auto bed = std::make_shared<BedItem>(0);
	CHECK(bed->getHouse() == nullptr);

	auto house = std::make_shared<House>(700);
	house->addBed(bed.get());

	auto retrievedHouse = bed->getHouse();
	CHECK(retrievedHouse != nullptr);
	CHECK(retrievedHouse == house);
	CHECK(retrievedHouse->getId() == 700);
	CHECK(retrievedHouse.get() == house.get());
}

TEST_CASE(bed_get_house_returns_nullptr_when_house_is_destroyed_and_can_remove)
{
	auto bed = std::make_shared<BedItem>(0);
	CHECK(bed->canRemove());

	{
		auto house = std::make_shared<House>(701);
		house->addBed(bed.get());
		CHECK(bed->getHouse() != nullptr);
		CHECK(!bed->canRemove());
	}

	CHECK(bed->getHouse() == nullptr);
	CHECK(bed->canRemove());
}

TEST_CASE(house_add_bed_ignores_nullptr_and_set_owner_succeeds)
{
	auto house = std::make_shared<House>(702);
	house->addBed(nullptr);
	CHECK(house->getBeds().empty());

	// setOwner should not crash or dereference nullptr in bedsList
	house->setOwner(0, false);
	CHECK(house->getOwner() == 0);
}

TEST_CASE(bed_get_house_preserves_lifetime_for_caller)
{
	auto bed = std::make_shared<BedItem>(0);
	std::shared_ptr<House> callerHouseRef;

	{
		auto house = std::make_shared<House>(800);
		house->addBed(bed.get());
		callerHouseRef = bed->getHouse();
		CHECK(callerHouseRef != nullptr);
	}

	// Original house out of scope, but callerHouseRef still keeps house alive
	CHECK(callerHouseRef != nullptr);
	CHECK(callerHouseRef->getId() == 800);
	CHECK(bed->getHouse() != nullptr);

	// When caller releases reference, house is destroyed
	callerHouseRef.reset();
	CHECK(bed->getHouse() == nullptr);
	CHECK(bed->canRemove());
}

TEST_CASE(bed_set_house_preserves_identity_and_handles_nullptr_and_expiration)
{
	auto bed = std::make_shared<BedItem>(0);
	CHECK(bed->getHouse() == nullptr);

	auto house = std::make_shared<House>(900);
	House* rawHouse = house.get();

	// Set house via shared_ptr
	bed->setHouse(house);
	CHECK(!bed->canRemove());
	std::shared_ptr<House> retrievedHouse = bed->getHouse();
	CHECK(retrievedHouse != nullptr);
	CHECK(retrievedHouse == house);
	CHECK(retrievedHouse.get() == rawHouse);
	CHECK(retrievedHouse->getId() == 900);

	// Set nullptr explicitly
	bed->setHouse(nullptr);
	CHECK(bed->getHouse() == nullptr);
	CHECK(bed->canRemove());

	// Test expiration
	{
		auto tempHouse = std::make_shared<House>(901);
		bed->setHouse(tempHouse);
		CHECK(!bed->canRemove());
		CHECK(bed->getHouse() != nullptr);
		CHECK(bed->getHouse()->getId() == 901);
	}
	CHECK(bed->getHouse() == nullptr);
	CHECK(bed->canRemove());
}

TEST_CASE(bed_get_next_bed_item_finds_partner_and_preserves_identity_and_lifetime)
{
	ensureItemTypes();

	ItemTypePropertyGuard guard694(694);
	ItemTypePropertyGuard guard695(695);
	Item::items.getItemType(694).bedPartnerDir = DIRECTION_SOUTH;
	Item::items.getItemType(695).bedPartnerDir = DIRECTION_NORTH;

	MapTileGuard tileGuard;
	tileGuard.track(100, 100, 7);
	tileGuard.track(100, 101, 7);

	auto bed1 = std::make_shared<BedItem>(694);
	auto bed2 = std::make_shared<BedItem>(695);
	std::weak_ptr<BedItem> weakBed2 = bed2;

	auto tile1 = std::make_unique<DynamicTile>(100, 100, 7);
	auto tile2 = std::make_unique<DynamicTile>(100, 101, 7);

	Tile* rawTile1 = tile1.get();
	Tile* rawTile2 = tile2.get();

	g_game.map.setTile(100, 100, 7, std::move(tile1));
	g_game.map.setTile(100, 101, 7, std::move(tile2));

	rawTile1->internalAddThing(bed1.get());
	rawTile2->internalAddThing(bed2.get());

	// Test partner detection and identity
	auto partner = bed1->getNextBedItem();
	CHECK(partner != nullptr);
	CHECK(partner == bed2);
	CHECK(partner.get() == bed2.get());
	CHECK(partner->getID() == 695);

	// Test reverse partner
	auto partnerReverse = bed2->getNextBedItem();
	CHECK(partnerReverse != nullptr);
	CHECK(partnerReverse == bed1);
	CHECK(partnerReverse.get() == bed1.get());
	CHECK(partnerReverse->getID() == 694);
	partnerReverse.reset();

	// Reset bed2 ownership; partner and tile2 are now the holders
	bed2.reset();
	CHECK(!weakBed2.expired());

	// Clear partner from tile
	rawTile2->getItemList()->clear();
	CHECK(rawTile2->getBedItem() == nullptr);
	CHECK(bed1->getNextBedItem() == nullptr);

	// partner returned earlier keeps the object alive
	CHECK(!weakBed2.expired());
	CHECK(partner != nullptr);
	CHECK(partner->getID() == 695);

	// Releasing partner must expire the weak pointer
	partner.reset();
	CHECK(weakBed2.expired());
}

TEST_CASE(bed_get_next_bed_item_returns_nullptr_when_partner_absent)
{
	ensureItemTypes();

	ItemTypePropertyGuard guard694(694);
	Item::items.getItemType(694).bedPartnerDir = DIRECTION_SOUTH;

	MapTileGuard tileGuard;
	tileGuard.track(200, 200, 7);
	tileGuard.track(200, 201, 7);

	auto bed = std::make_shared<BedItem>(694);
	auto tile = std::make_unique<DynamicTile>(200, 200, 7);
	Tile* rawTile = tile.get();

	g_game.map.setTile(200, 200, 7, std::move(tile));
	rawTile->internalAddThing(bed.get());

	// Target tile (200, 201, 7) does not exist -> returns nullptr
	CHECK(bed->getNextBedItem() == nullptr);

	// Create target tile without a bed item
	auto emptyTile = std::make_unique<DynamicTile>(200, 201, 7);
	g_game.map.setTile(200, 201, 7, std::move(emptyTile));

	// Target tile exists but has no bed -> returns nullptr
	CHECK(bed->getNextBedItem() == nullptr);
}

TEST_CASE(tile_get_bed_item_returns_nullptr_when_no_bed)
{
	ensureItemTypes();

	auto tile = std::make_unique<DynamicTile>(50, 50, 7);
	CHECK(tile->getBedItem() == nullptr);

	auto regularItem = std::make_shared<Item>(0);
	tile->internalAddThing(regularItem.get());
	CHECK(tile->getBedItem() == nullptr);
}

TEST_CASE(tile_get_bed_item_from_ground_preserves_identity_and_lifetime)
{
	ensureItemTypes();

	auto tile = std::make_unique<DynamicTile>(51, 51, 7);
	auto bedGround = std::make_shared<BedItem>(694);
	BedItem* rawBed = bedGround.get();

	tile->setGround(bedGround);
	tile->setFlag(TILESTATE_BED);

	// Assert bed is stored as ground and not in item list
	CHECK(tile->getGround() == rawBed);
	CHECK(tile->getItemList() == nullptr || tile->getItemList()->empty());

	std::shared_ptr<BedItem> retrievedBed = tile->getBedItem();
	CHECK(retrievedBed != nullptr);
	CHECK(retrievedBed.get() == rawBed);
	CHECK(retrievedBed == bedGround);

	// Release test-created shared_ptr owner before removing from tile
	bedGround.reset();

	// Remove from actual storage location (ground)
	tile->setGround(nullptr);
	tile->resetFlag(TILESTATE_BED);
	CHECK(tile->getBedItem() == nullptr);

	// Validate retrievedBed remains alive and preserves identity
	CHECK(retrievedBed != nullptr);
	CHECK(retrievedBed.get() == rawBed);
	CHECK(retrievedBed->getID() == 694);
}

TEST_CASE(tile_get_bed_item_from_item_list_preserves_identity_and_lifetime)
{
	ensureItemTypes();

	auto tile = std::make_unique<DynamicTile>(52, 52, 7);
	auto bed = std::make_shared<BedItem>(694);
	BedItem* rawBed = bed.get();

	tile->internalAddThing(bed.get());

	// Assert bed is stored in item list and not as ground
	CHECK(tile->getGround() == nullptr);
	CHECK(tile->getItemList() != nullptr);
	CHECK(!tile->getItemList()->empty());

	std::shared_ptr<BedItem> retrievedBed = tile->getBedItem();
	CHECK(retrievedBed != nullptr);
	CHECK(retrievedBed == bed);
	CHECK(retrievedBed.get() == rawBed);
	CHECK(retrievedBed->getID() == 694);

	// Release test-created shared_ptr owner before removing from tile
	bed.reset();

	// Remove from actual storage location (item list)
	tile->getItemList()->clear();
	tile->resetFlag(TILESTATE_BED);
	CHECK(tile->getBedItem() == nullptr);

	// Validate retrievedBed remains alive and preserves identity
	CHECK(retrievedBed != nullptr);
	CHECK(retrievedBed.get() == rawBed);
	CHECK(retrievedBed->getID() == 694);
}

TEST_CASE(house_get_door_by_number_finds_door_and_preserves_identity_and_lifetime)
{
	auto house = std::make_shared<House>(500);
	auto door = std::make_shared<Door>(0);
	door->setDoorId(10);
	Door* rawDoor = door.get();

	house->addDoor(door.get());

	// Door found & identity
	std::shared_ptr<Door> retrievedDoor = house->getDoorByNumber(10);
	CHECK(retrievedDoor != nullptr);
	CHECK(retrievedDoor.get() == rawDoor);
	CHECK(retrievedDoor == door);
	CHECK(retrievedDoor->getDoorId() == 10);

	// Door non-existent
	CHECK(house->getDoorByNumber(999) == nullptr);

	// Release original owner
	door.reset();
	CHECK(retrievedDoor != nullptr);
	CHECK(retrievedDoor.get() == rawDoor);

	// Remove door from house
	house->removeDoor(rawDoor);
	CHECK(house->getDoorByNumber(10) == nullptr);

	// retrievedDoor remains alive
	CHECK(retrievedDoor != nullptr);
	CHECK(retrievedDoor.get() == rawDoor);
	CHECK(retrievedDoor->getDoorId() == 10);
}

TEST_CASE(house_get_door_by_number_returns_nullptr_when_door_is_destroyed)
{
	auto house = std::make_shared<House>(501);

	{
		auto door = std::make_shared<Door>(0);
		door->setDoorId(20);
		house->addDoor(door.get());
		CHECK(house->getDoorByNumber(20) != nullptr);
		door->onRemoved();
		// door goes out of scope and is destroyed
	}

	CHECK(house->getDoorByNumber(20) == nullptr);
}

TEST_CASE(house_get_door_by_position_finds_door_and_preserves_identity_and_lifetime)
{
	ensureItemTypes();

	const Position doorPos{105, 105, 7};
	auto house = std::make_shared<House>(502);
	auto door = std::make_shared<Door>(0);
	door->setDoorId(30);
	Door* rawDoor = door.get();

	CHECK(g_game.map.getTile(doorPos) == nullptr);
	MapTileGuard tileGuard;
	tileGuard.track(105, 105, 7);

	auto tile = std::make_unique<DynamicTile>(doorPos.x, doorPos.y, doorPos.z);
	g_game.map.setTile(doorPos.x, doorPos.y, doorPos.z, std::move(tile));
	Tile* rawTile = g_game.map.getTile(doorPos);
	rawTile->internalAddThing(door.get());

	house->addDoor(door.get());

	// Found by position & identity
	std::shared_ptr<Door> retrievedDoor = house->getDoorByPosition(doorPos);
	CHECK(retrievedDoor != nullptr);
	CHECK(retrievedDoor.get() == rawDoor);
	CHECK(retrievedDoor == door);
	CHECK(retrievedDoor->getDoorId() == 30);

	// Non-existent position
	CHECK(house->getDoorByPosition(Position{999, 999, 7}) == nullptr);

	// Release original owner
	door.reset();
	CHECK(retrievedDoor != nullptr);
	CHECK(retrievedDoor.get() == rawDoor);

	// Remove door from house
	house->removeDoor(rawDoor);
	CHECK(house->getDoorByPosition(doorPos) == nullptr);

	// retrievedDoor remains alive
	CHECK(retrievedDoor != nullptr);
	CHECK(retrievedDoor.get() == rawDoor);
	CHECK(retrievedDoor->getDoorId() == 30);
	retrievedDoor.reset();
}

TEST_CASE(house_get_door_by_position_returns_nullptr_when_door_is_destroyed)
{
	const Position doorPos{106, 106, 7};
	auto house = std::make_shared<House>(503);

	{
		auto door = std::make_shared<Door>(0);
		auto tile = std::make_unique<DynamicTile>(doorPos.x, doorPos.y, doorPos.z);
		Tile* rawTile = tile.get();
		rawTile->internalAddThing(door.get());

		house->addDoor(door.get());
		CHECK(house->getDoorByPosition(doorPos) != nullptr);

		door->onRemoved();
		// door goes out of scope and is destroyed
	}

	CHECK(house->getDoorByPosition(doorPos) == nullptr);
}

TEST_CASE(container_get_item_by_index_preserves_item_lifetime)
{
	auto container = std::make_shared<Container>(0, 2);
	auto insertedItem = std::make_shared<Item>(0);
	Item* rawItem = insertedItem.get();
	CHECK(container->addItem(insertedItem));
	insertedItem.reset();

	auto item = container->getItemByIndex(0);
	CHECK(item != nullptr);
	CHECK(item.get() == rawItem);
	CHECK(container->getItemByIndex(container->size()) == nullptr);

	container->removeThing(rawItem, rawItem->getItemCount());
	CHECK(container->empty());
	CHECK(isValidItemPointer(item.get()));
	CHECK(item->getID() == 0);
}

TEST_CASE(container_iterator_preserves_item_lifetime)
{
	auto container = std::make_shared<Container>(0, 1);
	auto insertedItem = std::make_shared<Item>(0);
	Item* rawItem = insertedItem.get();
	CHECK(container->addItem(insertedItem));
	insertedItem.reset();

	std::shared_ptr<Item> item;
	{
		auto iterator = container->iterator();
		CHECK(iterator.hasNext());
		item = *iterator;
		CHECK(item.get() == rawItem);
		iterator.advance();
		CHECK(!iterator.hasNext());
		CHECK(*iterator == nullptr);
	}

	container->removeThing(rawItem, rawItem->getItemCount());
	CHECK(container->empty());
	CHECK(isValidItemPointer(item.get()));
	CHECK(item->getID() == 0);
}

TEST_CASE(lua_container_item_userdata_preserves_item_lifetime)
{
	LuaFixture fixture;
	auto container = std::make_shared<Container>(0, 1);
	auto insertedItem = std::make_shared<Item>(0);
	Item* rawItem = insertedItem.get();
	std::weak_ptr<Item> weakItem = insertedItem;
	CHECK(container->addItem(insertedItem));
	insertedItem.reset();

	Lua::pushSharedPtr(fixture.L, std::static_pointer_cast<Item>(container));
	Lua::setItemMetatable(fixture.L, -1, container.get());
	lua_setglobal(fixture.L, "lifetimeTestContainer");

	CHECK(luaL_dostring(fixture.L,
	                    "heldItem = lifetimeTestContainer:getItem(0)\n"
	                    "return heldItem:getId()") == LUA_OK);
	CHECK(lua_tointeger(fixture.L, -1) == 0);
	lua_pop(fixture.L, 1);

	container->removeThing(rawItem, rawItem->getItemCount());
	CHECK(container->empty());
	CHECK(!weakItem.expired());
	CHECK(luaL_dostring(fixture.L, "return heldItem:getId()") == LUA_OK);
	CHECK(lua_tointeger(fixture.L, -1) == 0);
	lua_pop(fixture.L, 1);

	CHECK(luaL_dostring(fixture.L,
	                    "heldItem = nil\n"
	                    "lifetimeTestContainer = nil\n"
	                    "collectgarbage('collect')\n"
	                    "collectgarbage('collect')") == LUA_OK);
	CHECK(weakItem.expired());
}

TEST_CASE(player_remove_item_of_type_inventory_and_container_lifetime)
{
	ensureItemTypes();
	ItemTypePropertyGuard guard100(100);
	Item::items.getItemType(100).moveable = true;

	auto player = makeTestPlayer(100, "RemoveItemPlayer");

	// Non-stackable items spread across inventory and backpack container
	auto backpack = std::make_shared<Container>(ITEM_BAG, 10);
	static_cast<Cylinder*>(player.get())->internalAddThing(CONST_SLOT_BACKPACK, backpack.get());

	auto invItem = std::make_shared<Item>(100);
	std::weak_ptr<Item> weakInvItem = invItem;
	static_cast<Cylinder*>(player.get())->internalAddThing(CONST_SLOT_RIGHT, invItem.get());
	invItem.reset();

	auto contItem1 = std::make_shared<Item>(100);
	std::weak_ptr<Item> weakContItem1 = contItem1;
	backpack->addItem(contItem1);
	contItem1.reset();

	auto contItem2 = std::make_shared<Item>(100);
	std::weak_ptr<Item> weakContItem2 = contItem2;
	backpack->addItem(contItem2);
	contItem2.reset();

	CHECK(player->getItemTypeCount(100) == 3);
	CHECK(!weakInvItem.expired());
	CHECK(!weakContItem1.expired());
	CHECK(!weakContItem2.expired());

	// Remove 2 non-stackable items of type 100 (backpack in slot 3 is scanned before right-hand in slot 6)
	CHECK(player->removeItemOfType(100, 2, -1));
	CHECK(player->getItemTypeCount(100) == 1);
	g_game.cleanup();
	CHECK(weakContItem1.expired());
	CHECK(weakContItem2.expired());
	CHECK(!weakInvItem.expired());

	// Remove remaining 1 (removes from right-hand inventory slot)
	CHECK(player->removeItemOfType(100, 1, -1));
	CHECK(player->getItemTypeCount(100) == 0);
	g_game.cleanup();
	CHECK(weakInvItem.expired());
}

TEST_CASE(player_remove_item_of_type_stackable_partial_and_multi_container)
{
	ensureItemTypes();
	ItemTypePropertyGuard guard2160(2160);
	Item::items.getItemType(2160).stackable = true;
	Item::items.getItemType(2160).moveable = true;

	auto player = makeTestPlayer(101, "StackablePlayer");

	auto backpack = std::make_shared<Container>(ITEM_BAG, 10);
	static_cast<Cylinder*>(player.get())->internalAddThing(CONST_SLOT_BACKPACK, backpack.get());

	auto stack1 = std::make_shared<Item>(2160, 50);
	std::weak_ptr<Item> weakStack1 = stack1;
	backpack->addItem(stack1);
	stack1.reset();

	auto stack2 = std::make_shared<Item>(2160, 100);
	std::weak_ptr<Item> weakStack2 = stack2;
	backpack->addItem(stack2);
	stack2.reset();

	CHECK(player->getItemTypeCount(2160) == 150);

	// Remove 75 items: stack1 (50) is fully removed, stack2 (100) is reduced to 75
	CHECK(player->removeItemOfType(2160, 75, -1));
	CHECK(player->getItemTypeCount(2160) == 75);
	g_game.cleanup();
	CHECK(weakStack1.expired());
	CHECK(!weakStack2.expired());

	// Remove remaining 75
	CHECK(player->removeItemOfType(2160, 75, -1));
	CHECK(player->getItemTypeCount(2160) == 0);
	g_game.cleanup();
	CHECK(weakStack2.expired());
}

TEST_CASE(game_internal_remove_items_robust_against_concurrent_pre_removal)
{
	ensureItemTypes();
	ItemTypePropertyGuard guard100(100);
	Item::items.getItemType(100).moveable = true;

	auto player = makeTestPlayer(102, "PreRemovePlayer");
	auto container = std::make_shared<Container>(ITEM_BAG, 5);
	static_cast<Cylinder*>(player.get())->internalAddThing(CONST_SLOT_BACKPACK, container.get());

	auto item1 = std::make_shared<Item>(100);
	auto item2 = std::make_shared<Item>(100);
	auto item3 = std::make_shared<Item>(100);

	std::weak_ptr<Item> weak1 = item1;
	std::weak_ptr<Item> weak2 = item2;
	std::weak_ptr<Item> weak3 = item3;

	container->addItem(item1);
	container->addItem(item2);
	container->addItem(item3);

	// Pre-remove item2 before calling internalRemoveItems on the batch
	container->removeThing(item2.get(), 1);
	CHECK(item2->isRemoved());

	// Collect batch and execute removal
	{
		std::vector<std::shared_ptr<Item>> batch = {std::move(item1), std::move(item2), std::move(item3)};
		g_game.internalRemoveItems(std::move(batch), 3, false);
	}

	g_game.cleanup();
	CHECK(weak1.expired());
	CHECK(weak2.expired());
	CHECK(weak3.expired());
	CHECK(container->empty());
}

TEST_CASE(house_bed_list_drops_destroyed_bed)
{
	auto house = std::make_shared<House>(705);
	auto bed = std::make_shared<BedItem>(694);
	BedItem* rawBed = bed.get();

	house->addBed(rawBed);
	CHECK(house->getBeds().size() == 1);
	CHECK(bed->getHouse() == house);

	// Removing bed triggers onRemoved, which removes bed from house bedsList
	bed->onRemoved();
	CHECK(house->getBeds().empty());
	CHECK(bed->getHouse() == nullptr);

	// Destroying bed leaves no dangling pointers; setOwner must be safe
	bed.reset();
	house->setOwner(0, false);
	CHECK(house->getBeds().empty());
}

TEST_CASE(house_tile_list_survives_map_tile_removal)
{
	ensureItemTypes();
	const Position tilePos{120, 120, 7};
	MapTileGuard tileGuard;
	tileGuard.track(120, 120, 7);

	auto house = std::make_shared<House>(706);
	auto houseTile = std::make_unique<HouseTile>(tilePos.x, tilePos.y, tilePos.z, house);

	g_game.map.setTile(tilePos.x, tilePos.y, tilePos.z, std::move(houseTile));
	CHECK(house->getTileCount() == 1);

	// Remove tile via Map::removeTile()
	g_game.map.removeTile(tilePos);
	CHECK(g_game.map.getTile(tilePos) == nullptr);

	// houseTiles weak_ptr is expired; getTileCount and setOwner must safely ignore / clean it
	CHECK(house->getTileCount() == 0);
	house->setOwner(0, false);
	CHECK(house->getTileCount() == 0);
}

TEST_CASE(get_bed_by_sleeper_keeps_bed_alive_for_caller)
{
	auto bed = std::make_shared<BedItem>(694);
	BedItem* rawBed = bed.get();
	std::weak_ptr<BedItem> weakBed = bed;

	g_game.setBedSleeper(rawBed, 12345);

	// Caller retrieves strong shared_ptr
	std::shared_ptr<BedItem> callerBed = g_game.getBedBySleeper(12345);
	CHECK(callerBed != nullptr);
	CHECK(callerBed.get() == rawBed);
	CHECK(callerBed == bed);

	// Original bed owner releases ownership; caller keeps it alive
	bed.reset();
	CHECK(!weakBed.expired());
	CHECK(callerBed != nullptr);
	CHECK(callerBed.get() == rawBed);

	// Caller releases reference; bed is now destroyed and getter returns nullptr
	callerBed.reset();
	CHECK(weakBed.expired());
	CHECK(g_game.getBedBySleeper(12345) == nullptr);

	g_game.removeBedSleeper(12345);
}

TEST_CASE(house_door_set_drops_tile_destroyed_door)
{
	ensureItemTypes();
	const Position doorPos{121, 121, 7};
	MapTileGuard tileGuard;
	tileGuard.track(121, 121, 7);

	auto house = std::make_shared<House>(707);
	auto door = std::make_shared<Door>(0);
	door->setDoorId(88);

	auto tile = std::make_unique<DynamicTile>(doorPos.x, doorPos.y, doorPos.z);
	g_game.map.setTile(doorPos.x, doorPos.y, doorPos.z, std::move(tile));
	Tile* rawTile = g_game.map.getTile(doorPos);
	rawTile->internalAddThing(door.get());

	house->addDoor(door.get());
	CHECK(house->getDoors().size() == 1);
	CHECK(house->getDoorByNumber(88) == door);

	// Map::removeTile removes and destroys tile contents, triggering Door::onRemoved()
	door.reset();
	g_game.map.removeTile(doorPos);

	CHECK(house->getDoors().empty());
	CHECK(house->getDoorByNumber(88) == nullptr);
}

TEST_CASE(map_remove_tile_safely_removes_all_items_without_iterator_invalidation)
{
	ensureItemTypes();
	const Position tilePos{122, 122, 7};
	MapTileGuard tileGuard;
	tileGuard.track(122, 122, 7);

	auto house = std::make_shared<House>(708);
	auto houseTile = std::make_unique<HouseTile>(tilePos.x, tilePos.y, tilePos.z, house);
	g_game.map.setTile(tilePos.x, tilePos.y, tilePos.z, std::move(houseTile));

	Tile* rawTile = g_game.map.getTile(tilePos);
	CHECK(rawTile != nullptr);

	auto ground = std::make_shared<Item>(100);
	auto item1 = std::make_shared<Item>(2160);
	auto item2 = std::make_shared<Item>(2160);
	auto door = std::make_shared<Door>(0);
	door->setDoorId(99);
	auto bed = std::make_shared<BedItem>(694);

	std::weak_ptr<Item> weakGround = ground;
	std::weak_ptr<Item> weak1 = item1;
	std::weak_ptr<Item> weak2 = item2;
	std::weak_ptr<Door> weakDoor = door;
	std::weak_ptr<BedItem> weakBed = bed;

	rawTile->setGround(ground);
	rawTile->internalAddThing(0, item1.get());
	rawTile->internalAddThing(0, item2.get());
	rawTile->internalAddThing(0, door.get());
	rawTile->internalAddThing(0, bed.get());

	house->addDoor(door.get());
	house->addBed(bed.get());

	CHECK(house->getDoors().size() == 1);
	CHECK(house->getBeds().size() == 1);

	// Release local strong pointers
	ground.reset();
	item1.reset();
	item2.reset();
	door.reset();
	bed.reset();

	CHECK(!weakGround.expired());
	CHECK(!weak1.expired());
	CHECK(!weak2.expired());
	CHECK(!weakDoor.expired());
	CHECK(!weakBed.expired());

	// Remove tile from map: must remove all items safely via snapshot
	g_game.map.removeTile(tilePos);
	CHECK(g_game.map.getTile(tilePos) == nullptr);

	// Flush deferred releases
	g_game.cleanup();

	// All items must be removed and destroyed (none skipped due to iterator invalidation)
	CHECK(weakGround.expired());
	CHECK(weak1.expired());
	CHECK(weak2.expired());
	CHECK(weakDoor.expired());
	CHECK(weakBed.expired());

	// House registries must be clean
	CHECK(house->getDoors().empty());
	CHECK(house->getBeds().empty());
	CHECK(house->getTileCount() == 0);
}

TEST_CASE(door_reparenting_between_houses_and_destruction_lifetime)
{
	auto houseA = std::make_shared<House>(801);
	auto houseB = std::make_shared<House>(802);
	auto houseTileA = std::make_shared<HouseTile>(140, 140, 7, houseA);
	auto houseTileB = std::make_shared<HouseTile>(141, 141, 7, houseB);
	houseA->addTile(houseTileA);
	houseB->addTile(houseTileB);

	auto door = std::make_shared<Door>(0);
	door->setDoorId(101);
	std::weak_ptr<Door> weakDoor = door;

	// Add door to houseTileA
	houseTileA->internalAddThing(0, door.get());
	CHECK(door->getHouse() == houseA);
	CHECK(houseA->getDoors().size() == 1);
	CHECK(houseA->getDoorByNumber(101) == door);
	CHECK(houseB->getDoors().empty());
	CHECK(houseB->getDoorByNumber(101) == nullptr);

	// Move door to houseTileB: simulate move from tileA to tileB
	houseTileA->removeThing(door.get(), 1);
	houseTileB->internalAddThing(0, door.get());
	CHECK(door->getHouse() == houseB);
	CHECK(houseA->getDoors().empty());
	CHECK(houseA->getDoorByNumber(101) == nullptr);
	CHECK(houseB->getDoors().size() == 1);
	CHECK(houseB->getDoorByNumber(101) == door);

	// Resetting houseA owner must succeed without touching the door
	houseA->setOwner(0);
	CHECK(houseA->getDoors().empty());

	// Destroy door: remove from tile and call onRemoved()
	houseTileB->removeThing(door.get(), 1);
	door->onRemoved();
	door.reset();
	g_game.cleanup();

	CHECK(weakDoor.expired());
	CHECK(houseB->getDoors().empty());
	CHECK(houseB->getDoorByNumber(101) == nullptr);

	// Resetting houseB owner must succeed without any UAF
	houseB->setOwner(0);
}

TEST_CASE(bed_reparenting_between_houses_and_destruction_lifetime)
{
	auto houseA = std::make_shared<House>(803);
	auto houseB = std::make_shared<House>(804);
	auto houseTileA = std::make_shared<HouseTile>(142, 142, 7, houseA);
	auto houseTileB = std::make_shared<HouseTile>(143, 143, 7, houseB);
	houseA->addTile(houseTileA);
	houseB->addTile(houseTileB);

	auto bed = std::make_shared<BedItem>(694);
	std::weak_ptr<BedItem> weakBed = bed;

	// Add bed to houseTileA
	houseTileA->internalAddThing(0, bed.get());
	CHECK(bed->getHouse() == houseA);
	CHECK(!bed->canRemove());
	CHECK(houseA->getBeds().size() == 1);
	CHECK(houseB->getBeds().empty());

	// Move bed to houseTileB: simulate move from tileA to tileB
	houseTileA->removeThing(bed.get(), 1);
	houseTileB->internalAddThing(0, bed.get());
	CHECK(bed->getHouse() == houseB);
	CHECK(!bed->canRemove());
	CHECK(houseA->getBeds().empty());
	CHECK(houseB->getBeds().size() == 1);

	// Resetting houseA owner must succeed without touching the bed
	houseA->setOwner(0);
	CHECK(houseA->getBeds().empty());

	// Destroy bed: remove from tile and call onRemoved()
	houseTileB->removeThing(bed.get(), 1);
	bed->onRemoved();
	bed.reset();
	g_game.cleanup();

	CHECK(weakBed.expired());
	CHECK(houseB->getBeds().empty());

	// Resetting houseB owner must succeed without any UAF
	houseB->setOwner(0);
}

TEST_CASE(flag_nolimit_vs_can_remove_semantics)
{
	ensureItemTypes();
	const Position pos{144, 144, 7};
	MapTileGuard tileGuard;
	tileGuard.track(144, 144, 7);

	auto house = std::make_shared<House>(805);
	auto houseTile = std::make_unique<HouseTile>(pos.x, pos.y, pos.z, house);
	g_game.map.setTile(pos.x, pos.y, pos.z, std::move(houseTile));

	Tile* rawTile = g_game.map.getTile(pos);
	CHECK(rawTile != nullptr);

	auto bed = std::make_shared<BedItem>(694);
	std::weak_ptr<BedItem> weakBed = bed;
	rawTile->internalAddThing(0, bed.get());
	house->addBed(bed.get());

	CHECK(!bed->canRemove());
	CHECK(house->getBeds().size() == 1);

	// Normal removal without FLAG_NOLIMIT fails because canRemove() is false
	ReturnValue normalRet = g_game.internalRemoveItem(bed.get(), 1, false, 0);
	CHECK(normalRet == RETURNVALUE_NOTPOSSIBLE);
	CHECK(!bed->isRemoved());
	CHECK(house->getBeds().size() == 1);

	// Forced removal with FLAG_NOLIMIT bypasses canRemove() constraint
	ReturnValue forcedRet = g_game.internalRemoveItem(bed.get(), 1, false, FLAG_NOLIMIT);
	CHECK(forcedRet == RETURNVALUE_NOERROR);
	CHECK(bed->isRemoved());
	CHECK(house->getBeds().empty());

	bed.reset();
	g_game.cleanup();
	CHECK(weakBed.expired());
}

TEST_CASE(house_reassigns_door_and_bed_without_stale_back_references)
{
	auto houseA = std::make_shared<House>(901);
	auto houseB = std::make_shared<House>(902);

	auto door = std::make_shared<Door>(0);
	door->setDoorId(1);
	auto bed = std::make_shared<BedItem>(694);

	// Register in House A
	houseA->addDoor(door.get());
	houseA->addBed(bed.get());

	CHECK(houseA->getDoors().size() == 1);
	CHECK(houseA->getBeds().size() == 1);
	CHECK(door->getHouse() == houseA);
	CHECK(bed->getHouse() == houseA);

	// Reassign to House B
	houseB->addDoor(door.get());
	houseB->addBed(bed.get());

	CHECK(houseA->getDoors().empty());
	CHECK(houseA->getBeds().empty());
	CHECK(houseB->getDoors().size() == 1);
	CHECK(houseB->getBeds().size() == 1);
	CHECK(door->getHouse() == houseB);
	CHECK(bed->getHouse() == houseB);

	// Remove from House B and destroy
	door->onRemoved();
	bed->onRemoved();
	door.reset();
	bed.reset();
	g_game.cleanup();

	CHECK(houseA->getDoors().empty());
	CHECK(houseA->getBeds().empty());
	CHECK(houseB->getDoors().empty());
	CHECK(houseB->getBeds().empty());

	// Call methods on House A and House B under ASan
	houseA->setOwner(0, false);
	houseB->setOwner(0, false);
	CHECK(houseA->getDoorByNumber(1) == nullptr);
	CHECK(houseB->getDoorByNumber(1) == nullptr);
	CHECK(houseA->getBedCount() == 0);
	CHECK(houseB->getBedCount() == 0);
}

TEST_CASE(container_on_orphan_house_tile_does_not_dereference_expired_house)
{
	ensureItemTypes();
	const Position pos{150, 150, 7};
	MapTileGuard tileGuard;
	tileGuard.track(150, 150, 7);

	std::shared_ptr<HouseTile> orphanTile;
	{
		auto house = std::make_shared<House>(903);
		auto houseTile = std::make_unique<HouseTile>(pos.x, pos.y, pos.z, house);
		g_game.map.setTile(pos.x, pos.y, pos.z, std::move(houseTile));
		orphanTile = std::static_pointer_cast<HouseTile>(g_game.map.getTile(pos)->weak_from_this().lock());
	}
	// House is now destroyed; orphanTile->getHouse() is nullptr
	CHECK(orphanTile != nullptr);
	CHECK(orphanTile->getHouse() == nullptr);

	ItemTypePropertyGuard guard100(100);
	Item::items.getItemType(100).moveable = true;
	Item::items.getItemType(100).pickupable = true;

	auto ground = std::make_shared<Item>(100);
	orphanTile->setGround(ground);

	auto container = std::make_shared<Container>(ITEM_BAG, 10);
	orphanTile->internalAddThing(container.get());

	auto item = std::make_shared<Item>(100);
	container->addItem(item);

	auto player = makeTestPlayer(10, "OrphanHousePlayer");

	// Test queryAdd and queryRemove with ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS enabled
	bool prevOnlyInvited = ConfigManager::getBoolean(ConfigManager::ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS);
	ConfigManager::setBoolean(ConfigManager::ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS, true);

	auto addRet = container->queryAdd(0, *item, 1, 0, player.get());
	CHECK(addRet == RETURNVALUE_NOERROR);

	auto removeRet = container->queryRemove(*item, 1, 0, player.get());
	CHECK(removeRet == RETURNVALUE_NOERROR);

	ConfigManager::setBoolean(ConfigManager::ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS, prevOnlyInvited);
}

TEST_CASE(house_tile_registry_is_unregistered_when_map_tile_is_removed)
{
	ensureItemTypes();
	const Position pos{151, 151, 7};
	MapTileGuard tileGuard;
	tileGuard.track(151, 151, 7);

	auto house = std::make_shared<House>(904);

	for (int cycle = 0; cycle < 3; ++cycle) {
		auto houseTile = std::make_unique<HouseTile>(pos.x, pos.y, pos.z, house);
		g_game.map.setTile(pos.x, pos.y, pos.z, std::move(houseTile));

		CHECK(house->getTileCount() == 1);
		CHECK(house->getTiles().size() == 1);

		g_game.map.removeTile(pos);

		CHECK(house->getTileCount() == 0);
		CHECK(house->getTiles().empty());
	}
}

TEST_CASE(door_and_bed_remove_with_expired_first_element_regression)
{
	auto house = std::make_shared<House>(910);

	// Create two doors
	auto door1 = std::make_shared<Door>(0);
	door1->setDoorId(1);
	auto door2 = std::make_shared<Door>(0);
	door2->setDoorId(2);

	// Add both doors
	house->addDoor(door1.get());
	house->addDoor(door2.get());
	CHECK(house->getDoors().size() == 2);

	// Expire door1 (first element in doorList)
	door1.reset();
	g_game.cleanup();

	// Now remove door2 (valid door, while first element is expired)
	// This must not trigger iterator decrement UB (--it on begin()) and must successfully remove door2
	house->removeDoor(door2.get());
	CHECK(house->getDoors().empty());

	// Create two beds
	auto bed1 = std::make_shared<BedItem>(694);
	auto bed2 = std::make_shared<BedItem>(694);

	// Add both beds
	house->addBed(bed1.get());
	house->addBed(bed2.get());
	CHECK(house->getBeds().size() == 2);

	// Expire bed1 (first element in bedsList)
	bed1.reset();
	g_game.cleanup();

	// Now remove bed2 (valid bed, while first element is expired)
	// Must not trigger iterator decrement UB and must successfully remove bed2
	house->removeBed(bed2.get());
	CHECK(house->getBeds().empty());
}

TEST_CASE(house_door_bed_tile_pruning_prevents_unbounded_registry_growth)
{
	auto house = std::make_shared<House>(911);

	// Repeat add -> expire -> add cycle multiple times
	for (int i = 0; i < 5; ++i) {
		auto door = std::make_shared<Door>(0);
		door->setDoorId(100 + i);
		house->addDoor(door.get());

		auto bed = std::make_shared<BedItem>(694);
		house->addBed(bed.get());

		auto tile = std::make_shared<HouseTile>(10 + i, 10 + i, 7, house);
		house->addTile(tile);

		// Expire previous items
		door.reset();
		bed.reset();
		tile.reset();
		g_game.cleanup();
	}

	// Now add one new live door, bed, and tile
	auto liveDoor = std::make_shared<Door>(0);
	liveDoor->setDoorId(999);
	house->addDoor(liveDoor.get());

	auto liveBed = std::make_shared<BedItem>(694);
	house->addBed(liveBed.get());

	auto liveTile = std::make_shared<HouseTile>(50, 50, 7, house);
	house->addTile(liveTile);

	// addDoor, addBed, addTile pruned the dead entries, so the live counts are exactly 1
	CHECK(house->getDoors().size() == 1);
	CHECK(house->getDoorCount() == 1);
	CHECK(house->getBeds().size() == 1);
	CHECK(house->getBedCount() == 1);
	CHECK(house->getTiles().size() == 1);
	CHECK(house->getTileCount() == 1);
}

TEST_CASE(occupied_house_bed_removed_by_map_preserves_sleeper_regeneration)
{
	ensureItemTypes();
	ensureVocations();

	const Position startPos{169, 170, 7};
	const Position bedPos{170, 170, 7};
	MapTileGuard tileGuard;
	tileGuard.track(startPos.x, startPos.y, startPos.z);
	tileGuard.track(bedPos.x, bedPos.y, bedPos.z);

	auto startTile = std::make_unique<DynamicTile>(startPos.x, startPos.y, startPos.z);
	startTile->setGround(std::make_shared<Item>(100));
	g_game.map.setTile(startPos.x, startPos.y, startPos.z, std::move(startTile));

	auto house = std::make_shared<House>(912);
	auto houseTile = std::make_unique<HouseTile>(bedPos.x, bedPos.y, bedPos.z, house);
	houseTile->setGround(std::make_shared<Item>(100));
	g_game.map.setTile(bedPos.x, bedPos.y, bedPos.z, std::move(houseTile));

	ItemTypePropertyGuard bedTypeGuard(694);
	Item::items.getItemType(694).bedPartnerDir = DIRECTION_SOUTH;
	auto bed = std::make_shared<BedItem>(694);
	std::weak_ptr<BedItem> weakBed = bed;
	g_game.map.getTile(bedPos)->internalAddThing(bed.get());

	auto player = makeTestPlayer(912, "SleepingPlayer");
	CHECK(player->setVocation(0));
	player->setMaxHealth(100);
	player->setHealth(10);
	player->setMaxMana(100);
	player->setMana(10);
	CHECK(player->addCondition(
	    Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_REGENERATION, -1, 0)));

	GlobalEvents globalEvents;
	GlobalEvents* previousGlobalEvents = g_globalEvents;
	g_globalEvents = &globalEvents;
	struct GlobalEventsGuard {
		GlobalEvents* previous;
		~GlobalEventsGuard() { g_globalEvents = previous; }
	} globalEventsGuard{previousGlobalEvents};
	Events events;
	Events* previousEvents = g_events;
	g_events = &events;
	struct EventsGuard {
		Events* previous;
		~EventsGuard() { g_events = previous; }
	} eventsGuard{previousEvents};
	Chat chat;
	Chat* previousChat = g_chat;
	g_chat = &chat;
	struct ChatGuard {
		Chat* previous;
		~ChatGuard() { g_chat = previous; }
	} chatGuard{previousChat};

	CHECK(g_game.internalPlaceCreature(player.get(), startPos, false, true));

	struct PlayerRemovalGuard {
		Player* player;
		~PlayerRemovalGuard()
		{
			if (player && !player->isRemoved()) {
				g_game.removeCreature(player);
			}
		}
	} playerRemovalGuard{player.get()};

	CHECK(bed->sleep(player.get()));
	CHECK(bed->getSleeper() == player->getGUID());
	CHECK(g_game.getBedBySleeper(player->getGUID()) == bed);

	PropWriteStream writeStream;
	const auto sleepStart = static_cast<uint32_t>(std::time(nullptr) - 1800);
	writeStream.write<uint32_t>(sleepStart);
	const std::string_view serialized = writeStream.getStream();
	PropStream readStream;
	readStream.init(serialized.data(), serialized.size());
	CHECK(bed->readAttr(ATTR_SLEEPSTART, readStream) == ATTR_READ_CONTINUE);

	// Keep the test player online, but away from the tile being removed. This
	// exercises the same wake-up path used when an online sleeper is found.
	g_game.map.moveCreature(*player, *g_game.map.getTile(startPos), true);
	CHECK(player->getPosition() == startPos);

	bed.reset();
	g_game.map.removeTile(bedPos);
	g_game.cleanup();

	CHECK(weakBed.expired());
	CHECK(player->getHealth() == 70);
	CHECK(player->getMana() == 70);
	CHECK(player->getSoul() == 2);
	CHECK(g_game.getBedBySleeper(player->getGUID()) == nullptr);

	// A later login lookup must not regenerate a second time.
	CHECK(player->getHealth() == 70);
	CHECK(player->getMana() == 70);
	CHECK(player->getSoul() == 2);
}

TEST_CASE(lua_tile_userdata_is_invalidated_after_map_removal)
{
	ensureItemTypes();
	LuaFixture fixture;
	lua_State* L = fixture.L;

	const Position pos{160, 160, 7};
	MapTileGuard tileGuard;
	tileGuard.track(160, 160, 7);

	// 1. Create Tile A on map
	auto tileA = std::make_unique<DynamicTile>(pos.x, pos.y, pos.z);
	g_game.map.setTile(pos.x, pos.y, pos.z, std::move(tileA));
	Tile* rawTileA = g_game.map.getTile(pos);
	CHECK(rawTileA != nullptr);

	// Push Tile A to Lua
	Lua::pushUserdata<Tile>(L, rawTileA);
	Lua::setMetatable(L, -1, "Tile");
	lua_setglobal(L, "tileA");

	// Verify Lua can read Tile A
	lua_getglobal(L, "tileA");
	Tile* luaTileA = Lua::getUserdata<Tile>(L, -1);
	CHECK(luaTileA == rawTileA);
	lua_pop(L, 1);

	// 2. Remove Tile A from map
	g_game.map.removeTile(pos);

	// Verify old userdata for Tile A is now invalidated
	lua_getglobal(L, "tileA");
	Tile* invalidatedTileA = Lua::getUserdata<Tile>(L, -1);
	CHECK(invalidatedTileA == nullptr);
	lua_pop(L, 1);

	// 3. Create new Tile B at the same position
	auto tileB = std::make_unique<DynamicTile>(pos.x, pos.y, pos.z);
	g_game.map.setTile(pos.x, pos.y, pos.z, std::move(tileB));
	Tile* rawTileB = g_game.map.getTile(pos);
	CHECK(rawTileB != nullptr);
	// The allocator may immediately reuse Tile A's address for Tile B. Object
	// identity is verified by the weak Lua userdata checks below, not by comparing
	// raw addresses from non-overlapping lifetimes.

	// Push Tile B to Lua
	Lua::pushUserdata<Tile>(L, rawTileB);
	Lua::setMetatable(L, -1, "Tile");
	lua_setglobal(L, "tileB");

	// Verify old userdata tileA remains invalidated (no ABA)
	lua_getglobal(L, "tileA");
	Tile* staleTileA = Lua::getUserdata<Tile>(L, -1);
	CHECK(staleTileA == nullptr);
	lua_pop(L, 1);

	// Verify new userdata tileB is valid
	lua_getglobal(L, "tileB");
	Tile* validTileB = Lua::getUserdata<Tile>(L, -1);
	CHECK(validTileB == rawTileB);
	lua_pop(L, 1);

	// Run Lua GC to verify proper destruction of weak userdata
	lua_gc(L, LUA_GCCOLLECT, 0);
}

TFS_TEST_MAIN()
