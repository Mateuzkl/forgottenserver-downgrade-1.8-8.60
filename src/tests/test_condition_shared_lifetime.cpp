// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../condition.h"
#include "../configmanager.h"
#include "../creature.h"
#include "../luascript.h"

#include "test_support.h"

extern LuaEnvironment g_luaEnvironment;

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
	std::string name = "condition lifetime test creature";
};

class CountingCondition final : public Condition
{
public:
	explicit CountingCondition(int32_t ticks) : Condition(CONDITIONID_COMBAT, CONDITION_INFIGHT, ticks) { ++alive; }
	CountingCondition(const CountingCondition& other) : Condition(other) { ++alive; }
	~CountingCondition() override { --alive; }

	void endCondition(Creature*) override {}
	void addCondition(Creature*, const Condition*) override {}
	Condition_ptr clone() const override { return std::make_shared<CountingCondition>(*this); }

	inline static int32_t alive = 0;
};

class LuaFixture
{
public:
	LuaFixture()
	{
		ConfigManager::setBoolean(ConfigManager::WARN_UNSAFE_SCRIPTS, false);
		ConfigManager::setBoolean(ConfigManager::CONVERT_UNSAFE_SCRIPTS, false);
		CHECK(g_luaEnvironment.initState());
		L = g_luaEnvironment.getLuaState();
		CHECK(L != nullptr);
	}

	~LuaFixture() { g_luaEnvironment.closeState(); }

	lua_State* L = nullptr;
};

} // namespace

TEST_CASE(condition_holder_survives_removal_from_creature)
{
	CHECK(CountingCondition::alive == 0);
	auto creature = std::make_shared<TestCreature>();
	auto condition = std::make_shared<CountingCondition>(5'000);
	std::weak_ptr<Condition> weakCondition = condition;

	CHECK(creature->addCondition(condition));
	condition.reset();

	auto holder = creature->getCondition(CONDITION_INFIGHT, CONDITIONID_COMBAT);
	CHECK(holder != nullptr);
	creature->removeCondition(holder);

	CHECK(!weakCondition.expired());
	CHECK(holder->getTicks() == 5'000);
	holder.reset();
	CHECK(weakCondition.expired());
	CHECK(CountingCondition::alive == 0);
}

TEST_CASE(condition_repeated_add_remove_releases_every_owner)
{
	CHECK(CountingCondition::alive == 0);
	auto creature = std::make_shared<TestCreature>();

	for (int32_t index = 0; index < 10'000; ++index) {
		auto condition = std::make_shared<CountingCondition>(5'000);
		std::weak_ptr<Condition> weakCondition = condition;
		CHECK(creature->addCondition(condition));
		condition.reset();

		auto active = creature->getCondition(CONDITION_INFIGHT, CONDITIONID_COMBAT);
		CHECK(active != nullptr);
		creature->removeCondition(active);
		active.reset();
		CHECK(weakCondition.expired());
	}

	CHECK(CountingCondition::alive == 0);
}

TEST_CASE(all_condition_factories_create_independent_clones)
{
	static constexpr std::array conditionTypes{
	    CONDITION_POISON,
	    CONDITION_HASTE,
	    CONDITION_ROOTED,
	    CONDITION_FEARED,
	    CONDITION_INVISIBLE,
	    CONDITION_OUTFIT,
	    CONDITION_LIGHT,
	    CONDITION_REGENERATION,
	    CONDITION_SOUL,
	    CONDITION_ATTRIBUTES,
	    CONDITION_SPELLCOOLDOWN,
	    CONDITION_SPELLGROUPCOOLDOWN,
	    CONDITION_DRUNK,
	    CONDITION_INFIGHT,
	};

	for (const ConditionType_t type : conditionTypes) {
		auto original = Condition::createCondition(CONDITIONID_COMBAT, type, 5'000, 10);
		CHECK(original != nullptr);
		auto copy = original->clone();
		CHECK(copy != nullptr);
		CHECK(copy.get() != original.get());

		const int32_t originalTicks = original->getTicks();
		copy->setTicks(originalTicks + 1'000);
		CHECK(copy->getTicks() != original->getTicks());
	}
}

TEST_CASE(shared_condition_serialization_round_trip_preserves_state)
{
	auto original = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_REGENERATION, 60'000, 0, true, 77);
	CHECK(original != nullptr);
	CHECK(original->setParam(CONDITION_PARAM_HEALTHTICKS, 2'000));
	CHECK(original->setParam(CONDITION_PARAM_HEALTHGAIN, 12));
	CHECK(original->setParam(CONDITION_PARAM_MANATICKS, 3'000));
	CHECK(original->setParam(CONDITION_PARAM_MANAGAIN, 8));

	PropWriteStream writeStream;
	original->serialize(writeStream);
	writeStream.write<uint8_t>(CONDITIONATTR_END);
	const std::string_view serialized = writeStream.getStream();

	PropStream readStream;
	readStream.init(serialized.data(), serialized.size());
	auto restored = Condition::createCondition(readStream);
	CHECK(restored != nullptr);
	CHECK(restored->unserialize(readStream));
	CHECK(restored.get() != original.get());
	CHECK(restored->getType() == original->getType());
	CHECK(restored->getId() == original->getId());
	CHECK(restored->getSubId() == original->getSubId());
	CHECK(restored->getTicks() == original->getTicks());
	CHECK(restored->getParam(CONDITION_PARAM_HEALTHTICKS) == 2'000);
	CHECK(restored->getParam(CONDITION_PARAM_HEALTHGAIN) == 12);
	CHECK(restored->getParam(CONDITION_PARAM_MANATICKS) == 3'000);
	CHECK(restored->getParam(CONDITION_PARAM_MANAGAIN) == 8);
}

TEST_CASE(expired_condition_is_destroyed_after_execution)
{
	CHECK(CountingCondition::alive == 0);
	auto creature = std::make_shared<TestCreature>();
	auto condition = std::make_shared<CountingCondition>(0);
	std::weak_ptr<Condition> weakCondition = condition;

	CHECK(creature->addCondition(condition));
	condition.reset();
	creature->executeConditions(1);

	CHECK(creature->getCondition(CONDITION_INFIGHT, CONDITIONID_COMBAT) == nullptr);
	CHECK(weakCondition.expired());
	CHECK(CountingCondition::alive == 0);
}

TEST_CASE(lua_condition_userdata_owns_condition_until_gc)
{
	CHECK(CountingCondition::alive == 0);
	LuaFixture fixture;
	auto creature = std::make_shared<TestCreature>();
	auto condition = std::make_shared<CountingCondition>(5'000);
	std::weak_ptr<Condition> weakCondition = condition;

	CHECK(creature->addCondition(condition));
	condition.reset();

	Lua::pushUserdata<Creature>(fixture.L, creature.get());
	Lua::setMetatable(fixture.L, -1, "Creature");
	lua_setglobal(fixture.L, "conditionTestCreature");
	lua_pushinteger(fixture.L, CONDITION_INFIGHT);
	lua_setglobal(fixture.L, "conditionTestType");
	lua_pushinteger(fixture.L, CONDITIONID_COMBAT);
	lua_setglobal(fixture.L, "conditionTestId");

	CHECK(luaL_dostring(fixture.L,
	                    "heldCondition = conditionTestCreature:getCondition(conditionTestType, conditionTestId)\n"
	                    "return heldCondition:getTicks()") == LUA_OK);
	CHECK(lua_tointeger(fixture.L, -1) == 5'000);
	lua_pop(fixture.L, 1);

	auto active = creature->getCondition(CONDITION_INFIGHT, CONDITIONID_COMBAT);
	creature->removeCondition(active);
	active.reset();
	CHECK(!weakCondition.expired());

	CHECK(luaL_dostring(fixture.L, "return heldCondition:getTicks()") == LUA_OK);
	CHECK(lua_tointeger(fixture.L, -1) == 5'000);
	lua_pop(fixture.L, 1);

	CHECK(luaL_dostring(fixture.L,
	                    "heldCondition = nil\n"
	                    "collectgarbage('collect')\n"
	                    "collectgarbage('collect')") == LUA_OK);
	CHECK(weakCondition.expired());
	CHECK(CountingCondition::alive == 0);
}

TFS_TEST_MAIN()
