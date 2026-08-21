#include "../otpch.h"

#include "../bed.h"
#include "../configmanager.h"
#include "../game.h"
#include "../house.h"
#include "../item.h"
#include "../luascript.h"
#include "../player.h"

#include "test_support.h"
#include <memory>

extern bool isValidItemPointer(Item* item);
extern LuaEnvironment g_luaEnvironment;

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

	explicit ItemTypePropertyGuard(uint16_t id)
	    : itemId(id),
	      origMoveable(Item::items.getItemType(id).moveable),
	      origStackable(Item::items.getItemType(id).stackable)
	{
	}

	~ItemTypePropertyGuard()
	{
		Item::items.getItemType(itemId).moveable = origMoveable;
		Item::items.getItemType(itemId).stackable = origStackable;
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
	auto houseTile = std::make_unique<HouseTile>(100, 100, 7, house);
	house->addTile(houseTile.get());

	auto retrievedHouse = houseTile->getHouse();
	CHECK(retrievedHouse != nullptr);
	CHECK(retrievedHouse == house);
	CHECK(retrievedHouse->getId() == 500);
	CHECK(retrievedHouse.get() == house.get());
}

TEST_CASE(housetile_get_house_returns_nullptr_when_house_is_null_or_destroyed)
{
	// Null/empty shared_ptr
	auto nullHouseTile = std::make_unique<HouseTile>(102, 102, 7, nullptr);
	CHECK(nullHouseTile->getHouse() == nullptr);

	// Expiration after House dies
	std::unique_ptr<HouseTile> houseTile;
	{
		auto house = std::make_shared<House>(600);
		houseTile = std::make_unique<HouseTile>(101, 101, 7, house);
		house->addTile(houseTile.get());
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
	auto houseTile = std::make_unique<HouseTile>(112, 112, 7, house);
	house->addTile(houseTile.get());

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
	auto houseTile = std::make_unique<HouseTile>(110, 110, 7, house);
	house->addTile(houseTile.get());

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

	MapTileGuard tileGuard;
	tileGuard.track(100, 100, 7);
	tileGuard.track(100, 101, 7);

	const auto origDir694 = Item::items.getItemType(694).bedPartnerDir;
	const auto origDir695 = Item::items.getItemType(695).bedPartnerDir;
	Item::items.getItemType(694).bedPartnerDir = DIRECTION_SOUTH;
	Item::items.getItemType(695).bedPartnerDir = DIRECTION_NORTH;

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

	Item::items.getItemType(694).bedPartnerDir = origDir694;
	Item::items.getItemType(695).bedPartnerDir = origDir695;
}

TEST_CASE(bed_get_next_bed_item_returns_nullptr_when_partner_absent)
{
	ensureItemTypes();

	MapTileGuard tileGuard;
	tileGuard.track(200, 200, 7);
	tileGuard.track(200, 201, 7);

	const auto origDir694 = Item::items.getItemType(694).bedPartnerDir;
	Item::items.getItemType(694).bedPartnerDir = DIRECTION_SOUTH;

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

	Item::items.getItemType(694).bedPartnerDir = origDir694;
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
	MapTileGuard tileGuard;
	tileGuard.track(105, 105, 7);

	auto house = std::make_shared<House>(502);
	auto door = std::make_shared<Door>(0);
	door->setDoorId(30);
	Door* rawDoor = door.get();

	CHECK(g_game.map.getTile(doorPos) == nullptr);
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

TFS_TEST_MAIN()
