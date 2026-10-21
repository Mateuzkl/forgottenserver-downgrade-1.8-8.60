#include "../otpch.h"

#include "../condition.h"
#include "../creatureevent.h"
#include "../game.h"
#include "../monster.h"
#include "../player.h"
#include "../scriptmanager.h"
#include "test_support.h"

extern LuaEnvironment g_luaEnvironment;

namespace {

struct Fixture
{
	bool previousReset = ConfigManager::getBoolean(ConfigManager::RESET_SYSTEM_ENABLED);
	bool previousLevel = ConfigManager::getBoolean(ConfigManager::MONSTER_LEVEL_ENABLED);
	float previousBonus = monster_level::getBonusDamage();
	CreatureEvents* previousEvents = g_creatureEvents;
	std::unique_ptr<LuaScriptInterface> script;
	std::unique_ptr<CreatureEvents> events;

	Fixture()
	{
		CHECK(g_luaEnvironment.initState());
		script = std::make_unique<LuaScriptInterface>("Combat reentry regression");
		CHECK(script->initState());
		events = std::make_unique<CreatureEvents>();
		g_creatureEvents = events.get();
		ConfigManager::setBoolean(ConfigManager::RESET_SYSTEM_ENABLED, true);
		ConfigManager::setBoolean(ConfigManager::MONSTER_LEVEL_ENABLED, true);
		CHECK(monster_level::setBonus("damage", 0.1f));
	}
	~Fixture()
	{
		g_creatureEvents = previousEvents;
		events.reset();
		script.reset();
		g_luaEnvironment.closeState();
		ConfigManager::setBoolean(ConfigManager::RESET_SYSTEM_ENABLED, previousReset);
		ConfigManager::setBoolean(ConfigManager::MONSTER_LEVEL_ENABLED, previousLevel);
		monster_level::setBonus("damage", previousBonus);
	}
	void hook(Creature& target, bool mana = false, bool changeDamage = false)
	{
		lua_State* L = script->getLuaState();
		const std::string code = std::string{
		    "auditCalls = 0; return function(target, attacker, primary, primaryType, secondary, secondaryType, origin) "
		    "auditCalls = auditCalls + 1; auditPrimary = primary; auditSecondary = secondary; "} +
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

TFS_TEST_MAIN()
