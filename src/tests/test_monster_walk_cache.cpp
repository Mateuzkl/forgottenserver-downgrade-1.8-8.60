#include "../otpch.h"

#include "../combat.h"
#include "../game.h"
#include "../item.h"
#include "../monster.h"
#include "../performance_metrics.h"
#include "../tile.h"
#include "test_support.h"

struct MonsterWalkTestAccess
{
	static bool walk(const Monster& monster, Direction direction)
	{
		return monster.canWalkTo(monster.getPosition(), direction);
	}
	static Tile* tile(const Monster& monster, const Position& pos) { return monster.getWalkTile(pos); }
	static void ignoreFieldDamage(Monster& monster, bool value) { monster.ignoreFieldDamage = value; }
};

namespace {
class WalkTile final : public DynamicTile
{
public:
	using DynamicTile::DynamicTile;
	ReturnValue queryAdd(int32_t index, const Thing& thing, uint32_t count, uint32_t flags,
	                     Creature* actor = nullptr) const override
	{
		++queries;
		return Tile::queryAdd(index, thing, count, flags, actor);
	}
	mutable uint64_t queries = 0;
};

class Fixture
{
public:
	Fixture()
	{
		type = std::make_shared<MonsterType>();
		type->name = "Walk cache test";
		type->nameDescription = "a walk cache test monster";
		monster = std::make_shared<Monster>(type);
		place(monster, Position{55000, 55000, 7});
	}
	~Fixture()
	{
		for (const auto& creature : creatures) {
			creature->getTile()->removeThing(creature.get(), 0);
			creature->setParent(nullptr);
		}
	}
	WalkTile* ensure(const Position& pos)
	{
		if (!g_game.map.getTile(pos)) {
			g_game.map.setTile(pos, std::make_unique<WalkTile>(pos.x, pos.y, pos.z));
		}
		auto tile = static_cast<WalkTile*>(g_game.map.getTile(pos));
		tile->resetFlag(UINT32_MAX);
		tile->setGround(Item::make<Item>(0));
		tile->queries = 0;
		return tile;
	}
	void place(const std::shared_ptr<Monster>& creature, const Position& pos)
	{
		ensure(pos)->internalAddThing(creature.get());
		creatures.push_back(creature);
	}
	void move(const Position& pos)
	{
		monster->getTile()->removeThing(monster.get(), 0);
		ensure(pos)->internalAddThing(monster.get());
	}
	std::shared_ptr<MonsterType> type;
	std::shared_ptr<Monster> monster;
	std::vector<std::shared_ptr<Monster>> creatures;
};
} // namespace

TEST_CASE(walk_cache_hits_still_validate_each_potential_destination)
{
	Fixture fixture;
	WalkTile* tile = fixture.ensure(Position{55001, 55000, 7});
	CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	CHECK(tile->queries == 2);
	tile->setFlag(TILESTATE_BLOCKSOLID);
	CHECK(!MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	fixture.type->info.canPushItems = true;
	CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	tile->resetFlag(TILESTATE_BLOCKSOLID);
}

TEST_CASE(walk_cache_reads_current_ground_and_unconditional_blockers)
{
	Fixture fixture;
	WalkTile* tile = fixture.ensure(Position{55001, 55000, 7});
	for (const uint32_t flag : {TILESTATE_TELEPORT, TILESTATE_FLOORCHANGE, TILESTATE_IMMOVABLEBLOCKSOLID,
	                            TILESTATE_IMMOVABLENOFIELDBLOCKPATH}) {
		tile->setFlag(flag);
		CHECK(!MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
		const uint64_t before = tile->queries;
		tile->resetFlag(flag);
		CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
		CHECK(tile->queries == before + 1);
	}
	tile->setGround(nullptr);
	CHECK(!MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	tile->setGround(Item::make<Item>(0));
	CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	tile->setFlag(TILESTATE_PROTECTIONZONE);
	CHECK(!MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	tile->resetFlag(TILESTATE_PROTECTIONZONE);
	CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
}

TEST_CASE(walk_cache_does_not_cache_creature_or_instance_permissions)
{
	Fixture fixture;
	fixture.monster->setInstanceID(41);
	WalkTile* tile = fixture.ensure(Position{55001, 55000, 7});
	CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	auto blocker = std::make_shared<Monster>(fixture.type);
	blocker->setInstanceID(41);
	fixture.place(blocker, tile->getPosition());
	CHECK(!MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	blocker->setInstanceID(42);
	CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	fixture.monster->setInstanceID(42);
	CHECK(!MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
}

TEST_CASE(walk_cache_invalidates_missing_removed_and_recreated_map_slots)
{
	Fixture fixture;
	const Position pos{55002, 55000, 7};
	fixture.move(Position{55001, 55000, 7});
	if (Tile* existing = g_game.map.getTile(pos)) {
		existing->setGround(nullptr);
		CHECK(g_game.map.removeTile(pos));
	}
	CHECK(!MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	WalkTile* tile = fixture.ensure(pos);
	CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	tile->setGround(nullptr);
	const uint64_t revision = g_game.map.getTileLayoutRevision();
	CHECK(g_game.map.removeTile(pos));
	CHECK(g_game.map.getTileLayoutRevision() >= revision + 2);
	CHECK(!MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	fixture.ensure(pos);
	CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
}

TEST_CASE(walk_cache_retains_step_overlap_but_resets_for_teleports_and_floors)
{
	Fixture fixture;
	const Position shared{55001, 55001, 7};
	fixture.ensure(Position{55001, 55000, 7});
	fixture.ensure(shared);
	g_performanceMetrics.setEnabled(true);
	CHECK(MonsterWalkTestAccess::tile(*fixture.monster, shared));
	const uint64_t hits = g_performanceMetrics.getMovementWork(MovementWork::WalkTileCacheHits);
	fixture.move(Position{55001, 55000, 7});
	CHECK(MonsterWalkTestAccess::tile(*fixture.monster, shared));
	CHECK(g_performanceMetrics.getMovementWork(MovementWork::WalkTileCacheHits) == hits + 1);
	fixture.move(Position{55010, 55010, 7});
	fixture.ensure(Position{55011, 55010, 7});
	CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	fixture.move(Position{55010, 55010, 8});
	CHECK(!MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	fixture.ensure(Position{55011, 55010, 8});
	CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	g_performanceMetrics.setEnabled(false);
}

TEST_CASE(walk_cache_observes_live_fields_and_monster_field_permissions)
{
	const auto items =
	    std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "data/items/items.otb";
	CHECK(Item::items.loadFromOtb(items.string()));
	pugi::xml_document document;
	CHECK(document.load_string(R"xml(<item name="walk cache test fire">
		<attribute key="type" value="magicfield"/>
		<attribute key="field" value="fire"><attribute key="ticks" value="5000"/>
		<attribute key="count" value="3"/><attribute key="damage" value="100"/></attribute>
	</item>)xml"));
	Item::items.parseItemNode(document.child("item"), 2118);
	Fixture fixture;
	fixture.type->info.canWalkOnFire = false;
	WalkTile* tile = fixture.ensure(Position{55001, 55000, 7});
	CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	auto field = Item::make<MagicField>(2118);
	CHECK(field->getDamage() > 0);
	tile->internalAddThing(field.get());
	CHECK(!MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	fixture.type->info.canWalkOnFire = true;
	CHECK(!MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	MonsterWalkTestAccess::ignoreFieldDamage(*fixture.monster, true);
	CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	fixture.type->info.canWalkOnFire = false;
	CHECK(!MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
	tile->removeThing(field.get(), 1);
	field->setParent(nullptr);
	CHECK(MonsterWalkTestAccess::walk(*fixture.monster, DIRECTION_EAST));
}

TFS_TEST_MAIN()
