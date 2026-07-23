// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "../otpch.h"

#include "../creature.h"
#include "../events.h"
#include "../luascript.h"
#include "../player.h"
#include "../scriptmanager.h"

#include "test_support.h"

extern LuaEnvironment g_luaEnvironment;

namespace {

class EventsGuard
{
public:
	EventsGuard() : previous(g_events) { g_events = &events; }
	~EventsGuard() { g_events = previous; }

private:
	Events* previous;
	Events events;
};

class TestCreature final : public Creature
{
public:
	const std::string& getName() const override { return name; }
	const std::string& getNameDescription() const override { return name; }
	std::string getDescription(int32_t) const override { return name; }
	CreatureType_t getType() const override { return CREATURETYPE_MONSTER; }
	void setID() override { id = nextId.fetch_add(1, std::memory_order_relaxed); }
	void removeList() override {}
	void addList() override {}
	double snapshotDamageRatioAfterClear(const std::shared_ptr<Creature>& attacker)
	{
		const auto snapshot = getDamageMapSnapshot();
		damageMap.clear();
		return getDamageRatio(attacker, snapshot);
	}

private:
	inline static std::atomic<uint32_t> nextId{1};
	std::string name = "regression test creature";
};

std::shared_ptr<TestCreature> makeTestCreature()
{
	auto creature = std::make_shared<TestCreature>();
	creature->setID();
	return creature;
}

} // namespace

TEST_CASE(live_creatures_thread_safe_basic)
{
	auto creature = makeTestCreature();
	const Creature* rawCreature = creature.get();
	CHECK(Creature::isAlive(rawCreature));

	std::mutex phaseMutex;
	std::condition_variable phaseChanged;
	bool workerReady = false;
	bool startWorker = false;
	bool workerStartedReads = false;
	bool firstObservationAlive = false;
	bool aliveAfterDestruction = false;
	std::atomic<bool> destructionStarted{false};
	std::atomic<bool> destructionComplete{false};

	std::thread worker([&]() {
		{
			std::unique_lock lock(phaseMutex);
			workerReady = true;
			phaseChanged.notify_one();
			phaseChanged.wait(lock, [&]() { return startWorker; });
		}

		firstObservationAlive = Creature::isAlive(rawCreature);
		{
			std::lock_guard lock(phaseMutex);
			workerStartedReads = true;
		}
		phaseChanged.notify_one();

		while (!destructionStarted.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}
		for (uint32_t i = 0; i < 4096; ++i) {
			(void)Creature::isAlive(rawCreature);
			std::this_thread::yield();
		}
		while (!destructionComplete.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}

		for (uint32_t i = 0; i < 1024; ++i) {
			if (Creature::isAlive(rawCreature)) {
				aliveAfterDestruction = true;
			}
		}
	});

	{
		std::unique_lock lock(phaseMutex);
		phaseChanged.wait(lock, [&]() { return workerReady; });
		startWorker = true;
		phaseChanged.notify_one();
		phaseChanged.wait(lock, [&]() { return workerStartedReads; });
	}

	destructionStarted.store(true, std::memory_order_release);
	creature.reset();
	destructionComplete.store(true, std::memory_order_release);
	worker.join();

	CHECK(firstObservationAlive);
	CHECK(!aliveAfterDestruction);
	CHECK(!Creature::isAlive(rawCreature));
}

TEST_CASE(damage_map_snapshot_keeps_experience_ratio_after_reentrant_clear)
{
	auto creature = makeTestCreature();
	auto primaryAttacker = makeTestCreature();
	creature->addDamagePoints(primaryAttacker, 100);
	creature->addDamagePoints(makeTestCreature(), 50);

	const double ratio = creature->snapshotDamageRatioAfterClear(primaryAttacker);
	CHECK(std::abs(ratio - (2.0 / 3.0)) < 0.000001);
	CHECK(creature->getDamageMap().empty());
}

TEST_CASE(storage_database_load_sets_and_erases_value)
{
	auto creature = makeTestCreature();
	creature->loadStorageValue(2000, 84);
	CHECK(creature->getStorageValue(2000).value_or(-2) == 84);

	creature->loadStorageValue(2000, -1);
	CHECK(!creature->getStorageValue(2000).has_value());
}

TEST_CASE(storage_live_update_normalizes_minus_one)
{
	EventsGuard eventsGuard;
	auto creature = makeTestCreature();

	creature->setStorageValue(3000, 42);
	CHECK(creature->getStorageValue(3000).value_or(-2) == 42);

	creature->setStorageValue(3000, -1);
	CHECK(!creature->getStorageValue(3000).has_value());
}

TEST_CASE(network_message_constructor_associates_player_userdata)
{
	CHECK(g_luaEnvironment.initState());
	lua_State* L = g_luaEnvironment.getLuaState();
	CHECK(L != nullptr);

	auto player = std::make_shared<Player>(nullptr);
	player->setID();

	Lua::pushUserdata<Player>(L, player.get());
	Lua::setMetatable(L, -1, "Player");
	lua_setglobal(L, "regressionTestPlayer");

	CHECK(luaL_loadstring(L, "return NetworkMessage(regressionTestPlayer)") == LUA_OK);
	CHECK(lua_pcall(L, 0, 1, 0) == LUA_OK);
	CHECK(luaL_testudata(L, -1, "NetworkMessage") != nullptr);
	CHECK(lua_getiuservalue(L, -1, 1) == LUA_TNUMBER);
	CHECK(Lua::getInteger<uint32_t>(L, -1) == player->getID());

	lua_settop(L, 0);
	CHECK(g_luaEnvironment.closeState());
}

TFS_TEST_MAIN()
