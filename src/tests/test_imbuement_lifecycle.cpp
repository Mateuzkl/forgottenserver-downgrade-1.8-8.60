#include "../otpch.h"

#include "../events.h"
#include "../imbuement.h"
#include "../item.h"
#include "../player.h"
#include "../scriptmanager.h"

#include "test_support.h"

#include <memory>

namespace {

// removeImbuement() fires itemOnRemoveImbue, so g_events has to point somewhere.
class EventsGuard
{
public:
	EventsGuard() : previous(g_events) { g_events = &events; }
	~EventsGuard() { g_events = previous; }

private:
	Events* previous;
	Events events;
};

std::shared_ptr<Imbuement> makeSwordImbuement(uint32_t value)
{
	return std::make_shared<Imbuement>(ImbuementType::IMBUEMENT_TYPE_SWORD_SKILL, value, 3600);
}

} // namespace

// The bug this guards against: addImbuement() applied the effect once per call,
// but removeImbuement() subtracts it once while erasing every matching entry.
// Adding the same imbuement twice and removing it once therefore emptied the
// vector while leaving one application of the bonus behind, permanently.
TEST_CASE(imbuement_add_twice_remove_once_leaves_nothing_behind)
{
	EventsGuard eventsGuard;

	auto item = std::make_shared<Item>(0);
	CHECK(item->addImbuementSlots(2));

	auto imbuement = makeSwordImbuement(5);

	CHECK(item->addImbuement(imbuement, false));
	CHECK(item->getImbuements().size() == 1);

	// The same shared_ptr must be refused rather than stored a second time.
	CHECK(!item->addImbuement(imbuement, false));
	CHECK(item->getImbuements().size() == 1);

	// One removal is now enough to leave nothing behind.
	CHECK(item->removeImbuement(imbuement, false));
	CHECK(item->getImbuements().empty());

	// And removing again reports failure rather than succeeding a second time.
	CHECK(!item->removeImbuement(imbuement, false));
}

// A distinct imbuement object is still allowed, even with equal contents:
// Imbuement::operator== compares by value, but the vector holds shared_ptrs and
// uniqueness is by identity.
TEST_CASE(imbuement_distinct_objects_are_still_accepted)
{
	EventsGuard eventsGuard;

	auto item = std::make_shared<Item>(0);
	CHECK(item->addImbuementSlots(2));

	auto first = makeSwordImbuement(5);
	auto second = makeSwordImbuement(5);

	CHECK(item->addImbuement(first, false));
	CHECK(item->addImbuement(second, false));
	CHECK(item->getImbuements().size() == 2);

	CHECK(item->removeImbuement(first, false));
	CHECK(item->getImbuements().size() == 1);

	CHECK(item->removeImbuement(second, false));
	CHECK(item->getImbuements().empty());
}

// Slots still cap how many imbuements an item can hold.
TEST_CASE(imbuement_respects_slot_limit)
{
	EventsGuard eventsGuard;

	auto item = std::make_shared<Item>(0);
	CHECK(item->addImbuementSlots(1));

	CHECK(item->addImbuement(makeSwordImbuement(5), false));
	CHECK(!item->addImbuement(makeSwordImbuement(7), false));
	CHECK(item->getImbuements().size() == 1);
}

// The effect accounting itself: applying and then removing one imbuement has to
// return the player to exactly the value they started with. This is what makes a
// single removal sufficient once duplicates are rejected.
TEST_CASE(imbuement_effect_application_is_symmetric)
{
	auto player = std::make_shared<Player>(nullptr);
	const uint16_t baseline = player->getSkillLevel(SKILL_SWORD);

	auto imbuement = makeSwordImbuement(5);

	player->addImbuementEffect(imbuement);
	CHECK(player->getSkillLevel(SKILL_SWORD) == baseline + 5);

	player->removeImbuementEffect(imbuement);
	CHECK(player->getSkillLevel(SKILL_SWORD) == baseline);
}

TFS_TEST_MAIN()
