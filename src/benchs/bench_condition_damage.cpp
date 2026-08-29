// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../condition.h"
#include "../creature.h"

#include <benchmark/benchmark.h>

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
	std::string name = "condition damage benchmark creature";
};

SuppressedDamageCreature& suppressedCreature()
{
	static SuppressedDamageCreature creature;
	return creature;
}

ConditionDamage makeDamageCondition(int32_t rounds, int32_t interval = 1'000, int32_t value = -7)
{
	ConditionDamage condition(CONDITIONID_DEFAULT, CONDITION_POISON);
	condition.setParam(CONDITION_PARAM_DELAYED, 1);
	condition.addDamage(rounds, interval, value);
	return condition;
}

void bench_condition_ticks(benchmark::State& state)
{
	const int32_t rounds = static_cast<int32_t>(state.range(0));
	for ([[maybe_unused]] auto _ : state) {
		ConditionDamage condition = makeDamageCondition(rounds);
		for (int32_t tick = 0; tick < rounds; ++tick) {
			condition.executeCondition(&suppressedCreature(), 1'000);
		}
		benchmark::DoNotOptimize(condition.getTotalDamage());
	}
	state.SetItemsProcessed(state.iterations() * rounds);
}
BENCHMARK(bench_condition_ticks)->ArgName("ticks")->Arg(4)->Arg(1'000);

void bench_simulated_creatures_dot(benchmark::State& state)
{
	const int32_t creatureCount = static_cast<int32_t>(state.range(0));
	for ([[maybe_unused]] auto _ : state) {
		std::vector<ConditionDamage> conditions;
		conditions.reserve(static_cast<size_t>(creatureCount));
		for (int32_t creature = 0; creature < creatureCount; ++creature) {
			conditions.push_back(makeDamageCondition(8, 1'000, -(creature % 17 + 1)));
		}
		for (ConditionDamage& condition : conditions) {
			condition.executeCondition(&suppressedCreature(), 1'000);
		}
		benchmark::DoNotOptimize(conditions.data());
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * creatureCount);
}
BENCHMARK(bench_simulated_creatures_dot)->ArgName("creatures")->Arg(100)->Arg(1'000)->Arg(10'000);

void bench_queue_churn(benchmark::State& state)
{
	const int32_t operations = static_cast<int32_t>(state.range(0));
	for ([[maybe_unused]] auto _ : state) {
		ConditionDamage condition(CONDITIONID_DEFAULT, CONDITION_ENERGY);
		condition.setParam(CONDITION_PARAM_DELAYED, 1);
		for (int32_t operation = 0; operation < operations; ++operation) {
			condition.addDamage(1, 1'000, -1);
		}
		for (int32_t operation = 0; operation < operations; ++operation) {
			condition.executeCondition(&suppressedCreature(), 1'000);
		}
		benchmark::DoNotOptimize(condition.getTotalDamage());
	}
	state.SetItemsProcessed(state.iterations() * operations * 2);
}
BENCHMARK(bench_queue_churn)->ArgName("operations")->Arg(10'000);

void bench_condition_copy(benchmark::State& state)
{
	const int32_t rounds = static_cast<int32_t>(state.range(0));
	const ConditionDamage source = makeDamageCondition(rounds);
	for ([[maybe_unused]] auto _ : state) {
		ConditionDamage copy(source);
		benchmark::DoNotOptimize(copy);
	}
	state.SetItemsProcessed(state.iterations() * rounds);
}
BENCHMARK(bench_condition_copy)->ArgName("ticks")->Arg(8)->Arg(1'000);

void bench_condition_serialize_unserialize(benchmark::State& state)
{
	const int32_t rounds = static_cast<int32_t>(state.range(0));
	ConditionDamage source = makeDamageCondition(rounds);
	for ([[maybe_unused]] auto _ : state) {
		PropWriteStream writeStream;
		source.serialize(writeStream);
		writeStream.write<uint8_t>(CONDITIONATTR_END);
		const std::string_view serialized = writeStream.getStream();
		PropStream readStream;
		readStream.init(serialized.data(), serialized.size());
		auto restored = Condition::createCondition(readStream);
		if (restored) {
			benchmark::DoNotOptimize(restored->unserialize(readStream));
		}
		benchmark::DoNotOptimize(restored.get());
	}
	state.SetItemsProcessed(state.iterations() * rounds);
}
BENCHMARK(bench_condition_serialize_unserialize)->ArgName("ticks")->Arg(8)->Arg(256);

} // namespace

BENCHMARK_MAIN();
