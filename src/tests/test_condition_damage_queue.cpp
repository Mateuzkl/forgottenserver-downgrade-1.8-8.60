// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../condition.h"
#include "../creature.h"

#include "test_support.h"

#include <list>

namespace {

class SuppressedDamageCreature final : public Creature
{
public:
	const std::string& getName() const override { return name; }
	const std::string& getNameDescription() const override { return name; }
	std::string getDescription(int32_t) const override { return name; }
	CreatureType_t getType() const override { return CREATURETYPE_MONSTER; }
	void setID() override {}
	void removeList() override {}
	void addList() override {}
	bool isSuppress(ConditionType_t) const override { return true; }

private:
	std::string name = "condition damage queue test creature";
};

SuppressedDamageCreature& suppressedCreature()
{
	static SuppressedDamageCreature creature;
	return creature;
}

std::string serializeCondition(Condition& condition)
{
	PropWriteStream stream;
	condition.serialize(stream);
	stream.write<uint8_t>(CONDITIONATTR_END);
	return std::string(stream.getStream());
}

Condition_ptr unserializeCondition(const std::string& serialized)
{
	PropStream stream;
	stream.init(serialized.data(), serialized.size());
	auto condition = Condition::createCondition(stream);
	CHECK(condition != nullptr);
	CHECK(condition->unserialize(stream));
	return condition;
}

std::string expectedSerializedDamage(ConditionType_t type, uint32_t subId, int32_t ticks,
	                                 std::initializer_list<IntervalInfo> intervals)
{
	PropWriteStream stream;
	stream.write<uint8_t>(CONDITIONATTR_TYPE);
	stream.write<uint32_t>(type);
	stream.write<uint8_t>(CONDITIONATTR_ID);
	stream.write<uint32_t>(CONDITIONID_DEFAULT);
	stream.write<uint8_t>(CONDITIONATTR_TICKS);
	stream.write<uint32_t>(ticks);
	stream.write<uint8_t>(CONDITIONATTR_ISBUFF);
	stream.write<uint8_t>(0);
	stream.write<uint8_t>(CONDITIONATTR_SUBID);
	stream.write<uint32_t>(subId);
	stream.write<uint8_t>(CONDITIONATTR_ISAGGRESSIVE);
	stream.write<uint8_t>(1);
	stream.write<uint8_t>(CONDITIONATTR_CONSTANT);
	stream.write<uint8_t>(0);
	stream.write<uint8_t>(CONDITIONATTR_DELAYED);
	stream.write<uint8_t>(1);
	stream.write<uint8_t>(CONDITIONATTR_PERIODDAMAGE);
	stream.write<int32_t>(0);
	for (const IntervalInfo& interval : intervals) {
		stream.write<uint8_t>(CONDITIONATTR_INTERVALDATA);
		stream.write<IntervalInfo>(interval);
	}
	stream.write<uint8_t>(CONDITIONATTR_END);
	return std::string(stream.getStream());
}

struct LegacyDamageQueueModel
{
	void push(int32_t interval, int32_t value)
	{
		intervals.push_back({interval, value, interval});
		ticks += interval;
	}

	void execute(int32_t elapsed)
	{
		if (!intervals.empty()) {
			IntervalInfo& current = intervals.front();
			current.timeLeft -= elapsed;
			if (current.timeLeft <= 0) {
				intervals.pop_front();
			}
		}
		ticks = std::max<int32_t>(0, ticks - elapsed);
	}

	int32_t totalDamage() const
	{
		int32_t total = 0;
		for (const IntervalInfo& interval : intervals) {
			total += interval.value;
		}
		return std::abs(total);
	}

	std::list<IntervalInfo> intervals;
	int32_t ticks = 0;
};

} // namespace

TEST_CASE(condition_damage_poison_fire_and_energy_keep_queue_behavior)
{
	for (const ConditionType_t type : {CONDITION_POISON, CONDITION_FIRE, CONDITION_ENERGY}) {
		ConditionDamage condition(CONDITIONID_DEFAULT, type);
		CHECK(condition.setParam(CONDITION_PARAM_DELAYED, 1));
		CHECK(condition.addDamage(2, 1'000, -7));
		CHECK(condition.getTotalDamage() == 14);
		CHECK(condition.executeCondition(&suppressedCreature(), 1'000));
		CHECK(condition.getTotalDamage() == 7);
		condition.executeCondition(&suppressedCreature(), 1'000);
		CHECK(condition.getTotalDamage() == 0);
	}
}

TEST_CASE(condition_damage_first_intermediate_last_and_empty_ticks_are_stable)
{
	ConditionDamage condition(CONDITIONID_DEFAULT, CONDITION_POISON);
	CHECK(condition.setParam(CONDITION_PARAM_DELAYED, 1));
	CHECK(condition.addDamage(3, 1'000, -5));
	CHECK(condition.getTicks() == 3'000);
	CHECK(condition.getTotalDamage() == 15);

	CHECK(condition.executeCondition(&suppressedCreature(), 500));
	CHECK(condition.getTicks() == 2'500);
	CHECK(condition.getTotalDamage() == 15);

	CHECK(condition.executeCondition(&suppressedCreature(), 500));
	CHECK(condition.getTicks() == 2'000);
	CHECK(condition.getTotalDamage() == 10);

	CHECK(condition.executeCondition(&suppressedCreature(), 1'000));
	CHECK(condition.getTicks() == 1'000);
	CHECK(condition.getTotalDamage() == 5);

	condition.executeCondition(&suppressedCreature(), 1'000);
	CHECK(condition.getTicks() == 0);
	CHECK(condition.getTotalDamage() == 0);
	condition.executeCondition(&suppressedCreature(), 1'000);
	CHECK(condition.getTotalDamage() == 0);
}

TEST_CASE(condition_damage_expiration_and_removal_during_execution_release_condition)
{
	auto creature = std::make_shared<SuppressedDamageCreature>();
	auto condition = std::make_shared<ConditionDamage>(CONDITIONID_DEFAULT, CONDITION_FIRE);
	CHECK(condition->setParam(CONDITION_PARAM_DELAYED, 1));
	CHECK(condition->addDamage(1, 1'000, -9));
	std::weak_ptr<Condition> weakCondition = condition;
	CHECK(creature->addCondition(condition));
	condition.reset();

	auto active = creature->getCondition(CONDITION_FIRE, CONDITIONID_DEFAULT);
	CHECK(active != nullptr);
	active->setEndTime(0);
	active.reset();
	creature->executeConditions(1'000);

	CHECK(creature->getCondition(CONDITION_FIRE, CONDITIONID_DEFAULT) == nullptr);
	CHECK(weakCondition.expired());
}

TEST_CASE(condition_damage_existing_condition_update_preserves_front_progress)
{
	ConditionDamage active(CONDITIONID_DEFAULT, CONDITION_ENERGY);
	CHECK(active.setParam(CONDITION_PARAM_DELAYED, 1));
	CHECK(active.addDamage(2, 1'000, -5));
	CHECK(active.executeCondition(&suppressedCreature(), 500));

	ConditionDamage stronger(CONDITIONID_DEFAULT, CONDITION_ENERGY);
	CHECK(stronger.setParam(CONDITION_PARAM_DELAYED, 1));
	CHECK(stronger.addDamage(2, 1'000, -10));
	active.addCondition(&suppressedCreature(), &stronger);
	CHECK(active.getTotalDamage() == 20);
	CHECK(active.executeCondition(&suppressedCreature(), 500));
	CHECK(active.getTotalDamage() == 10);

	ConditionDamage weaker(CONDITIONID_DEFAULT, CONDITION_ENERGY);
	CHECK(weaker.setParam(CONDITION_PARAM_DELAYED, 1));
	CHECK(weaker.addDamage(1, 1'000, -1));
	active.addCondition(&suppressedCreature(), &weaker);
	CHECK(active.getTotalDamage() == 10);

	CHECK(weaker.setParam(CONDITION_PARAM_FORCEUPDATE, 1));
	active.addCondition(&suppressedCreature(), &weaker);
	CHECK(active.getTotalDamage() == 1);
}

TEST_CASE(condition_damage_copy_has_independent_queue_storage)
{
	ConditionDamage original(CONDITIONID_DEFAULT, CONDITION_FIRE);
	CHECK(original.setParam(CONDITION_PARAM_DELAYED, 1));
	CHECK(original.addDamage(3, 1'000, -4));
	ConditionDamage copy(original);

	CHECK(original.executeCondition(&suppressedCreature(), 1'000));
	CHECK(original.getTotalDamage() == 8);
	CHECK(copy.getTotalDamage() == 12);
	copy.setEndTime(std::numeric_limits<int64_t>::max());
	CHECK(copy.executeCondition(&suppressedCreature(), 500));
	CHECK(copy.getTotalDamage() == 12);
	CHECK(original.getTotalDamage() == 8);
}

TEST_CASE(condition_damage_serialization_keeps_legacy_wire_bytes_and_queue_values)
{
	ConditionDamage original(CONDITIONID_DEFAULT, CONDITION_FIRE, false, 77, true);
	CHECK(original.setParam(CONDITION_PARAM_DELAYED, 1));
	CHECK(original.addDamage(1, 1'000, -7));
	CHECK(original.addDamage(1, 2'000, -11));

	const std::string serialized = serializeCondition(original);
	const std::string expected = expectedSerializedDamage(
	    CONDITION_FIRE, 77, 3'000, {{1'000, -7, 1'000}, {2'000, -11, 2'000}});
	CHECK(serialized == expected);

	auto restoredBase = unserializeCondition(serialized);
	auto restored = std::dynamic_pointer_cast<ConditionDamage>(restoredBase);
	CHECK(restored != nullptr);
	CHECK(restored->getType() == CONDITION_FIRE);
	CHECK(restored->getSubId() == 77);
	CHECK(restored->getTotalDamage() == 18);
	CHECK(restored->getTicks() == 3'000);
	CHECK(restored->executeCondition(&suppressedCreature(), 1'000));
	CHECK(restored->getTotalDamage() == 11);
}

TEST_CASE(condition_damage_small_and_large_values_do_not_change_queue_semantics)
{
	ConditionDamage condition(CONDITIONID_DEFAULT, CONDITION_POISON);
	CHECK(condition.setParam(CONDITION_PARAM_DELAYED, 1));
	CHECK(condition.addDamage(1, 1'000, -1));
	CHECK(condition.addDamage(1, 1'000, -1'000'000'000));
	CHECK(condition.getTotalDamage() == 1'000'000'001);
	CHECK(condition.executeCondition(&suppressedCreature(), 1'000));
	CHECK(condition.getTotalDamage() == 1'000'000'000);
	condition.executeCondition(&suppressedCreature(), 1'000);
	CHECK(condition.getTotalDamage() == 0);
}

TEST_CASE(condition_damage_ten_thousand_simultaneous_queues_are_independent)
{
	std::vector<ConditionDamage> conditions;
	conditions.reserve(10'000);
	for (int32_t index = 0; index < 10'000; ++index) {
		conditions.emplace_back(CONDITIONID_DEFAULT, CONDITION_POISON);
		CHECK(conditions.back().setParam(CONDITION_PARAM_DELAYED, 1));
		CHECK(conditions.back().addDamage(4, 1'000, -(index % 31 + 1)));
	}

	int64_t remaining = 0;
	for (ConditionDamage& condition : conditions) {
		CHECK(condition.executeCondition(&suppressedCreature(), 1'000));
		remaining += condition.getTotalDamage();
	}
	CHECK(remaining == 479'649);
}

TEST_CASE(condition_damage_matches_legacy_list_model_across_mixed_queue_operations)
{
	ConditionDamage condition(CONDITIONID_DEFAULT, CONDITION_ENERGY);
	CHECK(condition.setParam(CONDITION_PARAM_DELAYED, 1));
	LegacyDamageQueueModel legacy;

	for (const IntervalInfo interval : {
	         IntervalInfo{1'000, -3, 1'000},
	         IntervalInfo{2'000, -11, 2'000},
	         IntervalInfo{1'000, -29, 1'000},
	         IntervalInfo{3'000, -101, 3'000},
	     }) {
		CHECK(condition.addDamage(1, interval.interval, interval.value));
		legacy.push(interval.interval, interval.value);
	}

	for (const int32_t elapsed : {250, 750, 1'000, 1'000, 1'000, 1'500, 1'500}) {
		condition.executeCondition(&suppressedCreature(), elapsed);
		legacy.execute(elapsed);
		CHECK(condition.getTicks() == legacy.ticks);
		CHECK(condition.getTotalDamage() == legacy.totalDamage());
	}
}

TFS_TEST_MAIN()
