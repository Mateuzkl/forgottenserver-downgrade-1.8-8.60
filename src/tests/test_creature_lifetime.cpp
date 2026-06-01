#include "../otpch.h"

#include "../creature.h"

#include <absl/container/flat_hash_map.h>
#include "test_support.h"
#include <type_traits>

namespace {

class TestCreature final : public Creature
{
public:
	const std::string& getName() const override { return name; }
	const std::string& getNameDescription() const override { return name; }
	std::string getDescription(int32_t) const override { return name; }
	CreatureType_t getType() const override { return CREATURETYPE_MONSTER; }
	void setID() override {}
	void removeList() override {}
	void addList() override {}

private:
	std::string name = "test creature";
};

std::shared_ptr<TestCreature> makeTestCreature()
{
	return std::make_shared<TestCreature>();
}

} // namespace

TEST_CASE(creature_lifetime_registry_tracks_destroyed_creature)
{
	const Creature* rawCreature = nullptr;
	{
		auto creature = makeTestCreature();
		rawCreature = creature.get();
		CHECK(Creature::isAlive(rawCreature));
	}

	CHECK(!Creature::isAlive(rawCreature));
}

TEST_CASE(summon_lifecycle_uses_weak_owner_links)
{
	auto master = makeTestCreature();
	auto summon = makeTestCreature();

	CHECK(summon->setMaster(master.get()));
	CHECK(summon->getMaster() == master);
	CHECK(master->getSummonCount() == 1U);

	summon.reset();

	CHECK(master->getSummonCount() == 0U);
	CHECK(master->getSummons().empty());
}

TEST_CASE(remove_master_detaches_summon_from_owner_list)
{
	auto master = makeTestCreature();
	auto summon = makeTestCreature();

	CHECK(summon->setMaster(master.get()));
	CHECK(master->getSummonCount() == 1U);

	summon->removeMaster();

	CHECK(summon->getMaster() == nullptr);
	CHECK(master->getSummonCount() == 0U);
	CHECK(master->getSummons().empty());
}

TEST_CASE(changing_master_detaches_from_previous_owner)
{
	auto oldMaster = makeTestCreature();
	auto newMaster = makeTestCreature();
	auto summon = makeTestCreature();

	CHECK(summon->setMaster(oldMaster.get()));
	CHECK(summon->setMaster(newMaster.get()));

	CHECK(summon->getMaster() == newMaster);
	CHECK(oldMaster->getSummonCount() == 0U);
	CHECK(newMaster->getSummonCount() == 1U);
}

TEST_CASE(master_getter_returns_shared_reference)
{
	auto master = makeTestCreature();
	auto summon = makeTestCreature();

	CHECK(summon->setMaster(master.get()));

	auto masterRef = summon->getMaster();
	CHECK(masterRef == master);
	CHECK(masterRef.use_count() >= 2);
}

TEST_CASE(creature_storage_uses_flat_hash_map_and_preserves_values)
{
	static_assert(std::is_same_v<Creature::StorageMap, absl::flat_hash_map<uint32_t, int64_t>>);

	auto creature = makeTestCreature();
	Creature::StorageMap storage;
	storage.insert_or_assign(100, 2500);
	storage.insert_or_assign(200, -7);

	CHECK(storage.at(100) == 2500);
	CHECK(storage.at(200) == -7);
	CHECK(storage.size() == 2U);
	CHECK(!creature->getStorageValue(100).has_value());

	storage.erase(100);

	CHECK(!storage.contains(100));
	CHECK(storage.size() == 1U);
}

TFS_TEST_MAIN()
