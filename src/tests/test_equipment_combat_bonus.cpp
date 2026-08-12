#include "../otpch.h"

#include "../condition.h"
#include "../equipment_combat_bonus.h"
#include "../game.h"
#include "../item.h"
#include "../player.h"

#include "test_support.h"

namespace {

void ensureItemTypesLoaded()
{
	if (Item::items.size() != 0) {
		return;
	}

	const auto itemsPath = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
	                       "data/items/items.otb";
	CHECK(Item::items.loadFromOtb(itemsPath.string()));
}

} // namespace

TEST_CASE(equipment_combat_bonus_math_is_additive_and_clamped)
{
	CHECK(EquipmentCombatBonus::applyAttackSpeedPercent(2000, 5) == 1905);
	CHECK(EquipmentCombatBonus::applyAttackSpeedPercent(2000, 15) == 1740);
	CHECK(EquipmentCombatBonus::applyAttackSpeedPercent(100, 500) == 100);

	CHECK(EquipmentCombatBonus::increaseDamageByPercent(100, 10) == 110);
	CHECK(EquipmentCombatBonus::increaseDamageByPercent(100, 25) == 125);
	CHECK(EquipmentCombatBonus::increaseDamageByPercent(-100, 25) == -100);

	CHECK(EquipmentCombatBonus::reduceDamageByPercent(100, 8) == 92);
	CHECK(EquipmentCombatBonus::reduceDamageByPercent(100, 100) == 0);
	CHECK(EquipmentCombatBonus::reduceDamageByPercent(100, 150) == 0);
	CHECK(EquipmentCombatBonus::reduceDamageByPercent(-100, 8) == -100);
}

TEST_CASE(attack_speed_modifiers_update_the_raw_interval_symmetrically)
{
	Player player(nullptr);
	player.setAttackSpeed(2000);
	player.setResetAttackSpeedBonus(200);

	CHECK(player.getRawAttackSpeed() == 2000);
	CHECK(player.getAttackSpeed() == 1800);

	player.setAttackSpeed(player.getRawAttackSpeed() + 100);
	CHECK(player.getRawAttackSpeed() == 2100);
	CHECK(player.getAttackSpeed() == 1900);

	player.setAttackSpeed(player.getRawAttackSpeed() - 100);
	CHECK(player.getRawAttackSpeed() == 2000);
	CHECK(player.getAttackSpeed() == 1800);
}

TEST_CASE(equipment_combat_bonus_xml_attributes_are_parsed)
{
	ensureItemTypesLoaded();
	constexpr uint16_t testItemId = 2160;

	pugi::xml_document document;
	const auto result = document.load_string(R"xml(
		<item name="equipment combat bonus test">
			<attribute key="attackspeedpercent" value="5" />
			<attribute key="damagepercent" value="10" />
			<attribute key="damagereductionpercent" value="108" />
		</item>
	)xml");
	CHECK(result);

	Item::items.parseItemNode(document.child("item"), testItemId);
	const ItemType& itemType = Item::items[testItemId];
	CHECK(itemType.attackSpeedPercent == 5);
	CHECK(itemType.damagePercent == 10);
	CHECK(itemType.damageReductionPercent == 100);
}

TEST_CASE(equipment_damage_reduction_precedes_full_mana_shield_absorption)
{
	ensureItemTypesLoaded();
	constexpr uint16_t testItemId = 2161;
	pugi::xml_document document;
	const auto result = document.load_string(R"xml(
		<item name="mana shield equipment reduction test">
			<attribute key="damagereductionpercent" value="20" />
		</item>
	)xml");
	CHECK(result);
	Item::items.parseItemNode(document.child("item"), testItemId);

	auto equipment = Item::CreateItem(testItemId);
	CHECK(equipment);

	auto attacker = std::make_shared<Player>(nullptr);
	auto target = std::make_shared<Player>(nullptr);
	auto group = std::make_shared<Group>();
	attacker->setGroup(group);
	target->setGroup(group);
	target->setMaxHealth(100);
	target->setHealth(100);
	target->setMaxMana(200);
	target->setMana(200);
	static_cast<Cylinder*>(target.get())->internalAddThing(CONST_SLOT_RING, equipment.get());
	target->setItemAbility(CONST_SLOT_RING, true);
	CHECK(target->getEquipmentDamageReductionPercent() == 20);
	CHECK(target->addCondition(
	    Condition::createCondition(CONDITIONID_COMBAT, CONDITION_MANASHIELD, -1, 0)));

	CombatDamage damage;
	damage.primary.type = COMBAT_PHYSICALDAMAGE;
	damage.primary.value = -100;
	damage.origin = ORIGIN_SPELL;

	CHECK(g_game.combatChangeHealth(attacker, target, damage));
	CHECK(target->getMana() == 120);
	CHECK(target->getHealth() == 100);
	CHECK(damage.primary.value == 0);
	CHECK(damage.secondary.value == 0);
	CHECK(damage.equipmentDamageReductionApplied);
}

TFS_TEST_MAIN()
