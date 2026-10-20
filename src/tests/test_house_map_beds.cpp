// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../bed.h"
#include "../game.h"
#include "../housetile.h"
#include "../iomap.h"
#include "../iomapserialize.h"
#include "../logger.h"
#include "test_support.h"

struct IOMapSerializeTestAccess
{
	static std::string save(const Item* item)
	{
		PropWriteStream stream;
		IOMapSerialize::saveItem(stream, item);
		return std::string(stream.getStream());
	}
	static bool load(const std::string& bytes, Cylinder* parent)
	{
		PropStream stream;
		stream.init(bytes.data(), bytes.size());
		return IOMapSerialize::loadItem(stream, parent) && stream.size() == 0;
	}
	static bool loadBatch(const std::vector<std::string>& records, Cylinder* parent)
	{
		std::vector<Item*> restored;
		for (const auto& bytes : records) {
			PropStream stream;
			stream.init(bytes.data(), bytes.size());
			if (!IOMapSerialize::loadItem(stream, parent, &restored) || stream.size() != 0) return false;
		}
		return true;
	}
};

namespace {
constexpr std::array<uint16_t, 4> bedIds{39439, 39437, 39440, 39438};
const std::array<Position, 4> positions{{{993, 978, 7}, {994, 977, 7}, {995, 978, 7}, {997, 977, 7}}};

struct TemporaryMapFile
{
	std::filesystem::path directory = std::filesystem::temp_directory_path() /
	                                ("tfs_house_map_beds_" +
	                                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::path path = directory / "fixture.otbm";
	TemporaryMapFile() { CHECK(std::filesystem::create_directory(directory)); }
	~TemporaryMapFile()
	{
		std::error_code ignored;
		std::filesystem::remove(path, ignored);
		std::filesystem::remove(directory, ignored);
	}
};

struct TestHouseMap
{
	TemporaryMapFile file;
	std::vector<uint8_t> bytes{'O', 'T', 'B', 'M'};
	Map map;

	template <typename T>
	void property(T value)
	{
		const auto* data = reinterpret_cast<const uint8_t*>(&value);
		for (size_t i = 0; i < sizeof(T); ++i) {
			if (data[i] >= OTB::Node::ESCAPE) bytes.push_back(OTB::Node::ESCAPE);
			bytes.push_back(data[i]);
		}
	}
	void begin(OTBM_NodeTypes_t type)
	{
		bytes.push_back(OTB::Node::START);
		bytes.push_back(static_cast<uint8_t>(type));
	}
	void end() { bytes.push_back(OTB::Node::END); }
	void item(uint16_t id, bool child)
	{
		if (child) begin(OTBM_NodeTypes_t::ITEM);
		else property(static_cast<uint8_t>(OTBM_AttrTypes_t::ITEM));
		property(id);
		if (child) end();
	}

	explicit TestHouseMap(bool childItems)
	{
		begin(OTBM_NodeTypes_t::ROOTV1);
		property(OTBM_root_header{2, 1024, 1024, Item::items.majorVersion, Item::items.minorVersion});
		begin(OTBM_NodeTypes_t::MAP_DATA);
		property(static_cast<uint8_t>(OTBM_AttrTypes_t::DESCRIPTION));
		property(uint16_t{0});
		begin(OTBM_NodeTypes_t::TILE_AREA);
		property(OTBM_Destination_coords{768, 768, 7});
		for (size_t i = 0; i < bedIds.size(); ++i) {
			begin(OTBM_NodeTypes_t::HOUSETILE);
			property(OTBM_Tile_coords{static_cast<uint8_t>(positions[i].x - 768),
			                         static_cast<uint8_t>(positions[i].y - 768)});
			property(uint32_t{4});
			item(100, false); // Existing stationary ground remains accepted.
			item(bedIds[i], childItems);
			item(ITEM_CRYSTAL_COIN, childItems); // Ordinary moveable items must still be rejected.
			end();
		}
		end();
		end();
		end();
		std::ofstream output(file.path, std::ios::binary);
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		output.close();
		CHECK(output.good());
		IOMap loader;
		if (!loader.loadMap(&map, file.path)) throw std::runtime_error(std::string(loader.getLastErrorString()));
	}
};

std::string sleepingState(uint16_t id)
{
	PropWriteStream stream;
	stream.write(id);
	stream.write<uint8_t>(ATTR_SLEEPSTART);
	stream.write<uint32_t>(123456);
	stream.write<uint8_t>(0);
	return std::string(stream.getStream());
}

void checkMapBeds(bool childItems)
{
	TestHouseMap fixture(childItems);
	const auto house = fixture.map.houses.getHouse(4);
	CHECK(house && house->getOwner() == 0);
	CHECK(house->getBeds().size() == 4);
	CHECK(house->getBedCount() == 2);
	for (size_t i = 0; i < bedIds.size(); ++i) {
		const auto* tile = fixture.map.getTile(positions[i]);
		CHECK(tile && tile->getGround());
		const auto bed = tile->getBedItem();
		CHECK(bed && bed->getID() == bedIds[i]);
		CHECK(bed->getHouse() == house);
		CHECK(bed->isLoadedFromMap());
		CHECK(bed->isMoveable()); // Do not globally change OTB movement flags.
		CHECK(tile->queryRemove(*bed, 1, 0) == RETURNVALUE_NOTMOVEABLE);
		CHECK(tile->queryRemove(*bed, 1, FLAG_IGNORENOTMOVEABLE) == RETURNVALUE_NOTMOVEABLE);
		CHECK(tile->queryRemove(*bed, 1, FLAG_IGNORECANREMOVE) == RETURNVALUE_NOERROR);
		CHECK(tile->hasFlag(TILESTATE_BED));
		CHECK(tile->getItemList()->size() == 1);
	}
}
} // namespace

TEST_CASE(reported_beds_have_real_bed_metadata_and_moveable_flags)
{
	for (auto id : bedIds) {
		CHECK(Item::items[id].isBed());
		CHECK(Item::items[id].moveable);
	}
	CHECK(Item::items[ITEM_CRYSTAL_COIN].moveable);
}

TEST_CASE(inline_house_items_keep_beds_but_reject_other_moveable_items) { checkMapBeds(false); }
TEST_CASE(child_house_items_keep_beds_but_reject_other_moveable_items) { checkMapBeds(true); }

TEST_CASE(map_bed_persistence_reuses_identity_and_does_not_duplicate)
{
	TestHouseMap fixture(true);
	const auto house = fixture.map.houses.getHouse(4);
	auto* tile = fixture.map.getTile(positions[0]);
	const auto bed = tile->getBedItem();
	CHECK(bed);
	const auto state = sleepingState(bed->getID());
	for (int cycle = 0; cycle < 3; ++cycle) {
		CHECK(IOMapSerializeTestAccess::load(state, tile));
		CHECK(tile->getItemList()->size() == 1);
		CHECK(tile->getBedItem() == bed);
		CHECK(IOMapSerializeTestAccess::save(bed.get()).ends_with(state.substr(sizeof(uint16_t))));
		CHECK(house->getBeds().size() == 4);
	}
}

TEST_CASE(map_bed_persistence_accepts_only_related_transform_states)
{
	TestHouseMap fixture(false);
	auto* tile = fixture.map.getTile(positions[1]); // Free knightly bed 39437.
	const auto bed = tile->getBedItem();
	CHECK(bed);
	const auto related = Item::items[bed->getID()].transformToOnUse[PLAYERSEX_MALE];
	CHECK(related != 0 && Item::items[related].isBed());
	CHECK(IOMapSerializeTestAccess::load(sleepingState(related), tile));
	CHECK(tile->getItemList()->size() == 1);
	CHECK(tile->getBedItem() == bed && bed->getID() == related);
	CHECK(bed->isLoadedFromMap());
	CHECK(IOMapSerializeTestAccess::load(sleepingState(39437), tile));
	CHECK(tile->getItemList()->size() == 1);
	CHECK(bed->getID() == 39437);
}

TEST_CASE(player_placed_beds_and_items_keep_normal_restore_semantics)
{
	Map map;
	auto house = map.houses.addHouse(5);
	auto houseTile = std::make_unique<HouseTile>(1000, 1000, 7, house);
	auto ground = Item::CreateItem(100);
	houseTile->internalAddThing(ground.get());
	map.setTile(1000, 1000, 7, std::move(houseTile));
	auto* tile = map.getTile(1000, 1000, 7);
	auto playerBed = Item::CreateItem(39437);
	tile->internalAddThing(playerBed.get());
	CHECK(!playerBed->isLoadedFromMap());
	CHECK(tile->queryRemove(*playerBed, 1, 0) == RETURNVALUE_NOERROR);
	CHECK(IOMapSerializeTestAccess::load(sleepingState(39437), tile));
	CHECK(tile->getItemList()->size() == 2); // Separate player-owned items are not merged.
	auto coin = Item::CreateItem(ITEM_CRYSTAL_COIN, 5);
	CHECK(IOMapSerializeTestAccess::load(IOMapSerializeTestAccess::save(coin.get()), tile));
	CHECK(tile->getItemList()->size() == 3);
}

TEST_CASE(map_bed_persistence_reuses_rotated_orientation)
{
	TestHouseMap fixture(true);
	auto* tile = fixture.map.getTile(positions[1]);
	const auto bed = tile->getBedItem();
	CHECK(bed);
	const auto rotated = Item::items[bed->getID()].rotateTo;
	CHECK(rotated != 0 && Item::items[rotated].isBed());
	CHECK(IOMapSerializeTestAccess::load(sleepingState(rotated), tile));
	CHECK(tile->getItemList()->size() == 1);
	CHECK(tile->getBedItem() == bed && bed->getID() == rotated);
	CHECK(bed->isLoadedFromMap());
}

TEST_CASE(saved_player_beds_do_not_overwrite_map_beds_in_either_record_order)
{
	for (bool playerFirst : {false, true}) {
		TestHouseMap fixture(true);
		auto* tile = fixture.map.getTile(positions[1]);
		const auto mapBed = tile->getBedItem();
		CHECK(mapBed);
		auto playerBed = Item::CreateItem(mapBed->getID());
		const auto mapState = IOMapSerializeTestAccess::save(mapBed.get());
		const auto legacy = sleepingState(playerBed->getID());
		PropStream attributes;
		attributes.init(legacy.data() + sizeof(uint16_t), legacy.size() - sizeof(uint16_t));
		CHECK(playerBed->unserializeAttr(attributes));
		const auto playerState = IOMapSerializeTestAccess::save(playerBed.get());
		CHECK(IOMapSerializeTestAccess::loadBatch(playerFirst ? std::vector{playerState, mapState}
		                                                     : std::vector{mapState, playerState}, tile));
		CHECK(tile->getItemList()->size() == 2);
		CHECK(mapBed->isLoadedFromMap());
		CHECK(IOMapSerializeTestAccess::save(mapBed.get()) == mapState);
		CHECK(fixture.map.houses.getHouse(4)->getBeds().size() == 5);
	}
}

TEST_CASE(legacy_records_consume_each_map_fixture_only_once)
{
	for (uint16_t id : {uint16_t{39437}, uint16_t{694}}) {
		TestHouseMap fixture(false);
		auto* tile = fixture.map.getTile(positions[1]);
		const auto original = tile->getBedItem();
		tile->removeThing(original.get(), 1);
		auto bed = Item::CreateItem(id);
		bed->setLoadedFromMap(true);
		tile->internalAddThing(bed.get());
		CHECK(IOMapSerializeTestAccess::loadBatch({sleepingState(id), sleepingState(id)}, tile));
		CHECK(tile->getItemList()->size() == 2);
	}
}

TEST_CASE(proven_stale_map_beds_are_ignored_but_unrelated_player_beds_survive)
{
	TestHouseMap source(true);
	auto* sourceTile = source.map.getTile(positions[1]);
	const auto oldMapState = IOMapSerializeTestAccess::save(sourceTile->getBedItem().get());
	// Replace the map fixture with another real bed family, without changing global metadata.
	TestHouseMap destination(true);
	auto* tile = destination.map.getTile(positions[1]);
	const auto old = tile->getBedItem();
	tile->removeThing(old.get(), 1);
	auto replacement = Item::CreateItem(694);
	CHECK(replacement && replacement->getBed());
	replacement->setLoadedFromMap(true);
	tile->internalAddThing(replacement.get());
	CHECK(IOMapSerializeTestAccess::load(oldMapState, tile));
	CHECK(tile->getItemList()->size() == 1);
	CHECK(tile->getBedItem() == replacement->getBed());
	auto playerBed = Item::CreateItem(39437);
	CHECK(IOMapSerializeTestAccess::load(IOMapSerializeTestAccess::save(playerBed.get()), tile));
	CHECK(tile->getItemList()->size() == 2);
	CHECK(replacement->getID() == 694);
}

TEST_CASE(default_script_flags_cannot_move_or_remove_map_beds_but_native_cleanup_can)
{
	TestHouseMap fixture(true);
	auto* source = fixture.map.getTile(positions[1]);
	auto* destination = fixture.map.getTile(positions[0]);
	const auto bed = source->getBedItem();
	constexpr uint32_t scriptFlags = FLAG_NOLIMIT | FLAG_IGNOREBLOCKITEM | FLAG_IGNOREBLOCKCREATURE |
	                                 FLAG_IGNORENOTMOVEABLE;
	CHECK(g_game.internalMoveItem(source, destination, INDEX_WHEREEVER, bed.get(), 1, nullptr, scriptFlags) ==
	      RETURNVALUE_NOTMOVEABLE);
	CHECK(g_game.internalRemoveItem(bed.get()) != RETURNVALUE_NOERROR);
	CHECK(bed->getParent() == source);
	CHECK(g_game.internalRemoveItem(bed.get(), -1, false, FLAG_IGNORECANREMOVE) == RETURNVALUE_NOERROR);
	CHECK(bed->isRemoved());
}

int main()
{
	if (!initLogger(LogLevel::CRITICAL)) return EXIT_FAILURE;
	const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	if (!Item::items.loadFromOtb((root / "data/items/items.otb").string())) return EXIT_FAILURE;
	// Parse the real bed families only: no Lua bootstrap, DB or production map.
	pugi::xml_document document;
	if (!document.load_file((root / "data/items/items.xml").string().c_str())) return EXIT_FAILURE;
	for (auto item : document.child("items").children("item")) {
		const auto id = item.attribute("id").as_uint();
		if ((id >= 39429 && id <= 39440) || id == 694 || id == 695)
			Item::items.parseItemNode(item, static_cast<uint16_t>(id));
	}
	const int result = tfs::tests::run();
	shutdownLogger();
	return result;
}
