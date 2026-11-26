#include "../otpch.h"

#include "../condition.h"
#include "../creatureevent.h"
#include "../events.h"
#include "../game.h"
#include "../monster.h"
#include "../player.h"
#include "../scriptmanager.h"
#include "test_support.h"

extern LuaEnvironment g_luaEnvironment;

struct CombatReentryTestAccess
{
	static void add(const std::shared_ptr<Creature>& creature)
	{
		creature->setID();
		std::unique_lock lock(g_game.creatureRefsMutex);
		g_game.creatureSharedRefs[creature->getID()] = creature;
	}
	static void remove(const Creature& creature)
	{
		std::unique_lock lock(g_game.creatureRefsMutex);
		g_game.creatureSharedRefs.erase(creature.getID());
	}
};

namespace {

struct Fixture
{
	bool previousReset = ConfigManager::getBoolean(ConfigManager::RESET_SYSTEM_ENABLED);
	bool previousLevel = ConfigManager::getBoolean(ConfigManager::MONSTER_LEVEL_ENABLED);
	float previousBonus = monster_level::getBonusDamage();
	CreatureEvents* previousEvents = g_creatureEvents;
	Events* previousCallbacks = g_events;
	std::unique_ptr<LuaScriptInterface> script;
	std::unique_ptr<CreatureEvents> events;
	std::unique_ptr<Events> callbacks;
	std::vector<std::shared_ptr<Creature>> creatures;
	void track(const std::shared_ptr<Creature>& creature)
	{
		CombatReentryTestAccess::add(creature);
		creatures.push_back(creature);
	}

	Fixture()
	{
		CHECK(g_luaEnvironment.initState());
		script = std::make_unique<LuaScriptInterface>("Combat reentry regression");
		CHECK(script->initState());
		callbacks = std::make_unique<Events>();
		g_events = callbacks.get();
		events = std::make_unique<CreatureEvents>();
		g_creatureEvents = events.get();
		ConfigManager::setBoolean(ConfigManager::RESET_SYSTEM_ENABLED, true);
		ConfigManager::setBoolean(ConfigManager::MONSTER_LEVEL_ENABLED, true);
		CHECK(monster_level::setBonus("damage", 0.1f));
	}
	~Fixture()
	{
		for (const auto& creature : creatures) CombatReentryTestAccess::remove(*creature);
		g_creatureEvents = previousEvents;
		g_events = previousCallbacks;
		callbacks.reset();
		events.reset();
		script.reset();
		g_luaEnvironment.closeState();
		ConfigManager::setBoolean(ConfigManager::RESET_SYSTEM_ENABLED, previousReset);
		ConfigManager::setBoolean(ConfigManager::MONSTER_LEVEL_ENABLED, previousLevel);
		monster_level::setBonus("damage", previousBonus);
	}
	void hook(Creature& target, bool mana = false, bool changeDamage = false, std::string mutation = {})
	{
		lua_State* L = script->getLuaState();
		const std::string code = std::string{
		    "auditCalls = 0; return function(target, attacker, primary, primaryType, secondary, secondaryType, origin) "
		    "auditCalls = auditCalls + 1; auditPrimary = primary; auditSecondary = secondary; "} + mutation +
		    (changeDamage ? "return 7, primaryType, 3, secondaryType end" : "return primary, primaryType, secondary, secondaryType end");
		CHECK(luaL_dostring(L, code.c_str()) == LUA_OK);
		auto event = std::make_unique<CreatureEvent>(script.get());
		event->setName("auditReentry");
		event->setEventType(mana ? CREATURE_EVENT_MANACHANGE : CREATURE_EVENT_HEALTHCHANGE);
		event->setLoaded(true);
		CHECK(event->loadCallback());
		CHECK(events->registerLuaEvent(event.release()));
		CHECK(target.registerCreatureEvent("auditReentry"));
	}
	int value(const char* name)
	{
		lua_State* L = script->getLuaState();
		lua_getglobal(L, name);
		const int result = static_cast<int>(lua_tointeger(L, -1));
		lua_pop(L, 1);
		return result;
	}
};

std::shared_ptr<Player> player()
{
	auto result = std::make_shared<Player>(nullptr);
	result->setGroup(std::make_shared<Group>());
	result->setMaxHealth(1000);
	result->setHealth(1000);
	result->setMaxMana(1000);
	result->setMana(1000);
	return result;
}

std::shared_ptr<Monster> monster(int level = 0)
{
	auto type = std::make_shared<MonsterType>();
	type->name = "Reentry Demon";
	type->nameDescription = "a reentry demon";
	type->info.health = type->info.healthMax = 1000;
	type->info.minLevel = type->info.maxLevel = level;
	return std::make_shared<Monster>(type);
}

CombatDamage hit(CombatOrigin origin = ORIGIN_SPELL)
{
	CombatDamage damage;
	damage.primary.type = COMBAT_PHYSICALDAMAGE;
	damage.primary.value = -100;
	damage.secondary.type = COMBAT_FIREDAMAGE;
	damage.secondary.value = -50;
	damage.origin = origin;
	return damage;
}

} // namespace

TEST_CASE(noop_health_callback_does_not_repeat_pve_reset_damage_bonus)
{
	for (const auto origin : {ORIGIN_SPELL, ORIGIN_CONDITION, ORIGIN_REFLECT}) {
		Fixture f;
		auto attacker = player();
		attacker->setResetDamageBonus(20);
		auto plain = monster(), hooked = monster();
		auto before = hit(origin), after = before;
		before.critical = after.critical = true;
		before.fatal = after.fatal = true;
		before.leeched = after.leeched = true;
		CHECK(g_game.combatChangeHealth(attacker, plain, before));
		f.hook(*hooked);
		CHECK(g_game.combatChangeHealth(attacker, hooked, after));
		CHECK(f.value("auditCalls") == 1);
		CHECK(f.value("auditPrimary") == 120 && f.value("auditSecondary") == 60);
		CHECK(before.primary.value == 120 && before.secondary.value == 60);
		CHECK(after.primary.value == before.primary.value && after.secondary.value == before.secondary.value);
		CHECK(hooked->getHealth() == plain->getHealth());
	}
}

TEST_CASE(noop_health_callback_preserves_monster_level_reset_defense_avatar_and_mana_shield)
{
	for (bool shield : {false, true}) {
		Fixture f;
		auto attacker = monster(2);
		auto plain = player(), hooked = player();
		for (const auto& target : {plain, hooked}) {
			target->setResetDefenseBonus(25);
			target->loadStorageValue(AVATAR_TIMER_STORAGE, OTSYS_TIME() + 60000);
			if (shield) {
				target->setMana(50);
				CHECK(target->addCondition(Condition::createCondition(CONDITIONID_COMBAT, CONDITION_MANASHIELD, -1, 0)));
			}
		}
		auto before = hit(), after = before;
		CHECK(g_game.combatChangeHealth(attacker, plain, before));
		f.hook(*hooked);
		CHECK(g_game.combatChangeHealth(attacker, hooked, after));
		CHECK(f.value("auditCalls") == 1);
		CHECK(before.primary.value == (shield ? 31 : 81) && before.secondary.value == 40);
		CHECK(after.primary.value == before.primary.value && after.secondary.value == before.secondary.value);
		CHECK(hooked->getHealth() == plain->getHealth());
		CHECK(hooked->getMana() == plain->getMana());
	}
}

TEST_CASE(lua_damage_override_is_not_boosted_again_by_reset_system)
{
	Fixture f;
	auto attacker = player();
	attacker->setResetDamageBonus(20);
	auto target = monster();
	f.hook(*target, false, true);
	auto damage = hit();
	CHECK(g_game.combatChangeHealth(attacker, target, damage));
	CHECK(f.value("auditCalls") == 1 && f.value("auditPrimary") == 120);
	CHECK(damage.primary.value == 7 && damage.secondary.value == 3);
	CHECK(target->getHealth() == 990);
}

TEST_CASE(reset_bonus_first_enabled_by_health_callback_is_not_skipped)
{
	Fixture f;
	auto attacker = player();
	auto target = monster();
	f.track(attacker);
	f.track(target);
	f.hook(*target, false, false, "attacker:setResetDamageBonus(20); ");
	auto damage = hit();
	CHECK(g_game.combatChangeHealth(attacker, target, damage));
	CHECK(f.value("auditCalls") == 1 && f.value("auditPrimary") == 100);
	CHECK(damage.primary.value == 120 && damage.secondary.value == 60);
}

TEST_CASE(reset_defense_first_enabled_by_health_callback_is_not_skipped)
{
	Fixture f;
	auto attacker = monster();
	auto target = player();
	f.track(target);
	f.hook(*target, false, false, "target:setResetDefenseBonus(25); ");
	auto damage = hit();
	CHECK(g_game.combatChangeHealth(attacker, target, damage));
	CHECK(f.value("auditCalls") == 1 && f.value("auditPrimary") == 100);
	CHECK(damage.primary.value == 75 && damage.secondary.value == 38);
}

TEST_CASE(avatar_first_activated_by_health_callback_is_not_skipped)
{
	Fixture f;
	auto attacker = monster();
	auto target = player();
	f.track(target);
	f.hook(*target, false, false,
	       fmt::format("target:setStorageValue({}, {}); ", AVATAR_TIMER_STORAGE, OTSYS_TIME() + 60000));
	auto damage = hit();
	CHECK(g_game.combatChangeHealth(attacker, target, damage));
	CHECK(f.value("auditCalls") == 1 && f.value("auditPrimary") == 100);
	CHECK(target->isAvatarActive());
	CHECK(damage.primary.value == 90 && damage.secondary.value == 45);
}

TEST_CASE(healing_keeps_post_heal_callback_order_and_single_reset_bonus)
{
	Fixture f;
	auto plain = player(), hooked = player();
	for (const auto& target : {plain, hooked}) {
		target->setHealth(500);
		target->setResetHealingBonus(30);
	}
	auto before = hit();
	before.primary.type = COMBAT_HEALING;
	before.primary.value = 100;
	before.secondary.type = COMBAT_HEALING;
	before.secondary.value = 50;
	auto after = before;
	CHECK(g_game.combatChangeHealth(nullptr, plain.get(), before));
	f.hook(*hooked);
	CHECK(g_game.combatChangeHealth(nullptr, hooked.get(), after));
	CHECK(f.value("auditCalls") == 1 && f.value("auditPrimary") == 130);
	CHECK(plain->getHealth() == 680 && hooked->getHealth() == 680);
	// Area life leech reuses the object, but each newly supplied heal must
	// receive its bonus (this is not recursive onHealthChange reentry).
	after.primary.value = 100;
	after.secondary.value = 50;
	CHECK(g_game.combatChangeHealth(nullptr, hooked.get(), after));
	CHECK(f.value("auditCalls") == 2 && f.value("auditPrimary") == 130);
	CHECK(hooked->getHealth() == 860);
}

TEST_CASE(noop_mana_callback_does_not_repeat_monster_level_multiplier)
{
	Fixture f;
	auto attacker = monster(2);
	auto plain = player(), hooked = player();
	auto before = hit(), after = before;
	before.primary.type = after.primary.type = COMBAT_MANADRAIN;
	CHECK(g_game.combatChangeMana(attacker.get(), plain.get(), before));
	f.hook(*hooked, true);
	CHECK(g_game.combatChangeMana(attacker.get(), hooked.get(), after));
	CHECK(f.value("auditCalls") == 1 && f.value("auditPrimary") == -120);
	CHECK(plain->getMana() == 820 && hooked->getMana() == 820);
}

TEST_CASE(mana_gain_converted_to_drain_by_callback_receives_monster_bonus_once)
{
	Fixture f;
	auto attacker = monster(2);
	auto target = player();
	f.hook(*target, true, false, "primary = -100; secondary = 0; ");
	auto damage = hit();
	damage.primary.type = COMBAT_MANADRAIN;
	damage.primary.value = 100;
	damage.secondary.value = 0;
	CHECK(g_game.combatChangeMana(attacker.get(), target.get(), damage));
	CHECK(f.value("auditCalls") == 1 && f.value("auditPrimary") == 100);
	CHECK(damage.primary.value == -120);
	CHECK(target->getMana() == 880);
}

TFS_TEST_MAIN()
