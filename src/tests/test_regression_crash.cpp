// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../creature.h"

#include "test_support.h"

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
	std::string name = "regression test creature";
};

std::shared_ptr<TestCreature> makeTestCreature()
{
	return std::make_shared<TestCreature>();
}

} // namespace

TEST_CASE(live_creatures_thread_safe_basic)
{
	const Creature* rawCreature = nullptr;
	{
		auto creature = makeTestCreature();
		rawCreature = creature.get();
		CHECK(Creature::isAlive(rawCreature));
	}

	CHECK(!Creature::isAlive(rawCreature));
}

TEST_CASE(damage_map_snapshot_is_independent)
{
	auto creature = makeTestCreature();
	creature->addDamagePoints(makeTestCreature(), 100);
	creature->addDamagePoints(makeTestCreature(), 50);

	auto snapshot = creature->getDamageMapSnapshot();
	CHECK(snapshot.size() == 2U);

	creature->getDamageMap().clear();
	CHECK(snapshot.size() == 2U);
}

TEST_CASE(storage_set_normalizes_minus_one)
{
	auto creature = makeTestCreature();
	creature->setStorageValue(1000, std::optional<int64_t>{42});
	CHECK(creature->getStorageValue(1000).value_or(-2) == 42);

	creature->setStorageValue(1000, std::optional<int64_t>{-1});
	CHECK(!creature->getStorageValue(1000).has_value());

	creature->setStorageValue(1000, std::optional<int64_t>{-1});
	CHECK(!creature->getStorageValue(1000).has_value());

	creature->setStorageValue(1000, std::optional<int64_t>{42});
	CHECK(creature->getStorageValue(1000).value_or(-2) == 42);
}

TFS_TEST_MAIN()
