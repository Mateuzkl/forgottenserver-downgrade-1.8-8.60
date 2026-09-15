#include "../otpch.h"

#include "../condition.h"
#include "../item.h"

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

std::shared_ptr<ConditionDamage> parseField(const char* xml, uint16_t itemId)
{
	ensureItemTypesLoaded();

	pugi::xml_document document;
	CHECK(document.load_string(xml));

	Item::items.parseItemNode(document.child("item"), itemId);
	const auto condition = Item::items[itemId].conditionDamage;
	CHECK(condition != nullptr);
	return condition;
}

} // namespace

TEST_CASE(non_agony_field_start_uses_a_damage_budget_in_any_xml_order)
{
	const auto startBeforeDamage =
	    parseField(R"xml(
		<item name="poison field parser test">
			<attribute key="field" value="poison">
				<attribute key="ticks" value="5000" />
				<attribute key="start" value="5" />
				<attribute key="damage" value="100" />
			</attribute>
		</item>
	)xml",
	               2162);
	CHECK(startBeforeDamage->getType() == CONDITION_POISON);
	CHECK(startBeforeDamage->getTotalDamage() == 100);
	CHECK(startBeforeDamage->getTicks() == 230'000);

	const auto damageBeforeStart =
	    parseField(R"xml(
		<item name="poison field parser test">
			<attribute key="field" value="poison">
				<attribute key="ticks" value="5000" />
				<attribute key="damage" value="100" />
				<attribute key="start" value="5" />
			</attribute>
		</item>
	)xml",
	               2163);
	CHECK(damageBeforeStart->getType() == CONDITION_POISON);
	CHECK(damageBeforeStart->getTotalDamage() == 100);
	CHECK(damageBeforeStart->getTicks() == 230'000);
}

TEST_CASE(agony_field_keeps_its_custom_damage_curve)
{
	const auto agony =
	    parseField(R"xml(
		<item name="agony field parser test">
			<attribute key="field" value="agony">
				<attribute key="ticks" value="5000" />
				<attribute key="start" value="260" />
				<attribute key="damage" value="100" />
			</attribute>
		</item>
	)xml",
	               2164);
	CHECK(agony->getType() == CONDITION_AGONY);
	CHECK(agony->getTotalDamage() == 820);
	CHECK(agony->getTicks() == 5'000);
}

TFS_TEST_MAIN()
