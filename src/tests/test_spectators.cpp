#include "../otpch.h"

#include "../spectators.h"

#include "../creature.h"
#include "test_support.h"

namespace {

class TestCreatureBase : public Creature
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
	std::string name = "test creature";
};

class TestCreature final : public TestCreatureBase
{
};

// partitionByType and filterPlayers only null-check or identity-compare the
// getPlayer()/getMonster() results, never dereference them, so a reinterpreted
// self-pointer is enough to land a stub in the player/monster partition.
class TestPlayerLike final : public TestCreatureBase
{
public:
	Player* getPlayer() override { return reinterpret_cast<Player*>(this); }
	const Player* getPlayer() const override { return reinterpret_cast<const Player*>(this); }
};

class TestMonsterLike final : public TestCreatureBase
{
public:
	Monster* getMonster() override { return reinterpret_cast<Monster*>(this); }
	const Monster* getMonster() const override { return reinterpret_cast<const Monster*>(this); }
};

std::shared_ptr<TestCreature> makeTestCreature()
{
	return std::make_shared<TestCreature>();
}

size_t countOf(const SpectatorVec& spectators, const Creature* creature)
{
	size_t count = 0;
	for (const auto& spectator : spectators) {
		if (spectator.get() == creature) {
			++count;
		}
	}
	return count;
}

bool hasNull(const SpectatorVec& spectators)
{
	for (const auto& spectator : spectators) {
		if (!spectator) {
			return true;
		}
	}
	return false;
}

bool spanContains(std::span<const std::shared_ptr<Creature>> span, const Creature* creature)
{
	for (const auto& spectator : span) {
		if (spectator.get() == creature) {
			return true;
		}
	}
	return false;
}

} // namespace

TEST_CASE(addSpectators_deduplicates_across_vectors)
{
	auto c1 = makeTestCreature(), c2 = makeTestCreature(), c3 = makeTestCreature();
	auto c4 = makeTestCreature(), c5 = makeTestCreature();

	SpectatorVec a;
	a.emplace_back(c1);
	a.emplace_back(c2);
	a.emplace_back(c3);

	SpectatorVec b;
	b.emplace_back(c2);
	b.emplace_back(c3);
	b.emplace_back(c4);
	b.emplace_back(c5);

	a.addSpectators(b);

	CHECK(a.size() == 5);
	CHECK(countOf(a, c1.get()) == 1);
	CHECK(countOf(a, c2.get()) == 1);
	CHECK(countOf(a, c3.get()) == 1);
	CHECK(countOf(a, c4.get()) == 1);
	CHECK(countOf(a, c5.get()) == 1);
}

TEST_CASE(addSpectators_removes_preexisting_destination_duplicates)
{
	auto c1 = makeTestCreature(), c2 = makeTestCreature(), c3 = makeTestCreature();

	SpectatorVec a;
	a.emplace_back(c1);
	a.emplace_back(c1);
	a.emplace_back(c2);

	SpectatorVec b;
	b.emplace_back(c3);

	a.addSpectators(b);

	CHECK(a.size() == 3);
	CHECK(countOf(a, c1.get()) == 1);
	CHECK(countOf(a, c2.get()) == 1);
	CHECK(countOf(a, c3.get()) == 1);
}

TEST_CASE(addSpectators_removes_source_internal_duplicates)
{
	auto c1 = makeTestCreature(), c2 = makeTestCreature();

	SpectatorVec a;
	a.emplace_back(c1);

	SpectatorVec b;
	b.emplace_back(c2);
	b.emplace_back(c2);

	a.addSpectators(b);

	CHECK(a.size() == 2);
	CHECK(countOf(a, c1.get()) == 1);
	CHECK(countOf(a, c2.get()) == 1);
}

TEST_CASE(addSpectators_self_merge_is_noop)
{
	auto c1 = makeTestCreature(), c2 = makeTestCreature();

	SpectatorVec a;
	a.emplace_back(c1);
	a.emplace_back(c1);
	a.emplace_back(c2);

	a.addSpectators(a);

	// guarded early return: not even the pre-existing duplicate is touched
	CHECK(a.size() == 3);
	CHECK(countOf(a, c1.get()) == 2);
	CHECK(countOf(a, c2.get()) == 1);
}

TEST_CASE(addSpectators_empty_source_is_noop)
{
	auto c1 = makeTestCreature();

	SpectatorVec a;
	a.emplace_back(c1);
	a.emplace_back(c1);

	SpectatorVec b;
	a.addSpectators(b);

	CHECK(a.size() == 2);
	CHECK(countOf(a, c1.get()) == 2);
}

TEST_CASE(addSpectators_removes_nulls_from_both_sides)
{
	auto c1 = makeTestCreature(), c2 = makeTestCreature();

	SpectatorVec a;
	a.emplaceNull();
	a.emplace_back(c1);

	SpectatorVec b;
	b.emplace_back(c2);
	b.emplaceNull();

	a.addSpectators(b);

	CHECK(a.size() == 2);
	CHECK(!hasNull(a));
	CHECK(countOf(a, c1.get()) == 1);
	CHECK(countOf(a, c2.get()) == 1);
}

TEST_CASE(emplace_back_ignores_null)
{
	SpectatorVec a;
	a.emplace_back(nullptr);
	CHECK(a.empty());
}

TEST_CASE(partitionByType_removes_nulls)
{
	auto c1 = makeTestCreature(), c2 = makeTestCreature();

	SpectatorVec a;
	a.emplace_back(c1);
	a.emplaceNull();
	a.emplace_back(c2);

	a.partitionByType();

	CHECK(a.size() == 2);
	CHECK(!hasNull(a));
	// TestCreature is neither Player nor Monster, so everything lands in npcs()
	CHECK(a.players().size() == 0);
	CHECK(a.monsters().size() == 0);
	CHECK(a.npcs().size() == 2);
}

TEST_CASE(partitionByType_groups_players_monsters_npcs)
{
	auto p1 = std::make_shared<TestPlayerLike>(), p2 = std::make_shared<TestPlayerLike>();
	auto m1 = std::make_shared<TestMonsterLike>(), m2 = std::make_shared<TestMonsterLike>(),
	     m3 = std::make_shared<TestMonsterLike>();
	auto n1 = makeTestCreature(), n2 = makeTestCreature(), n3 = makeTestCreature(), n4 = makeTestCreature();

	// interleave the types so the partition has real work to do
	SpectatorVec a;
	a.emplace_back(n1);
	a.emplace_back(p1);
	a.emplace_back(m1);
	a.emplace_back(n2);
	a.emplace_back(m2);
	a.emplace_back(p2);
	a.emplace_back(n3);
	a.emplace_back(m3);
	a.emplace_back(n4);

	a.partitionByType();

	CHECK(a.size() == 9);
	CHECK(a.players().size() == 2);
	CHECK(a.monsters().size() == 3);
	CHECK(a.npcs().size() == 4);

	CHECK(spanContains(a.players(), p1.get()));
	CHECK(spanContains(a.players(), p2.get()));
	CHECK(spanContains(a.monsters(), m1.get()));
	CHECK(spanContains(a.monsters(), m2.get()));
	CHECK(spanContains(a.monsters(), m3.get()));
	CHECK(spanContains(a.npcs(), n1.get()));
	CHECK(spanContains(a.npcs(), n2.get()));
	CHECK(spanContains(a.npcs(), n3.get()));
	CHECK(spanContains(a.npcs(), n4.get()));
}

TEST_CASE(erase_removes_member_and_ignores_nonmember)
{
	auto c1 = makeTestCreature(), c2 = makeTestCreature(), c3 = makeTestCreature();
	auto outsider = makeTestCreature();

	SpectatorVec a;
	a.emplace_back(c1);
	a.emplace_back(c2);
	a.emplace_back(c3);

	a.erase(c2.get());
	CHECK(a.size() == 2);
	CHECK(countOf(a, c2.get()) == 0);
	CHECK(countOf(a, c1.get()) == 1);
	CHECK(countOf(a, c3.get()) == 1);

	a.erase(outsider.get());
	CHECK(a.size() == 2);
}

TEST_CASE(filterPlayers_auto_partitions_and_skips_nonplayers)
{
	auto c1 = makeTestCreature(), c2 = makeTestCreature();

	SpectatorVec a;
	a.emplace_back(c1);
	a.emplaceNull();
	a.emplace_back(c2);

	size_t invocations = 0;
	a.filterPlayers([&invocations](const Player*) {
		++invocations;
		return true;
	});

	// no players in the vec: predicate never runs, nothing removed by the
	// filter itself — but the implicit partitionByType must purge the null,
	// which is the observable proof that auto-partitioning happened
	CHECK(invocations == 0);
	CHECK(a.size() == 2);
	CHECK(!hasNull(a));
	CHECK(a.players().size() == 0);
	CHECK(a.npcs().size() == 2);
}

TEST_CASE(filterPlayers_removes_rejected_players_and_keeps_the_rest)
{
	auto p1 = std::make_shared<TestPlayerLike>(), p2 = std::make_shared<TestPlayerLike>(),
	     p3 = std::make_shared<TestPlayerLike>();
	auto m1 = std::make_shared<TestMonsterLike>();
	auto n1 = makeTestCreature();

	// left unpartitioned on purpose: filterPlayers must auto-partition first
	SpectatorVec a;
	a.emplace_back(p1);
	a.emplace_back(m1);
	a.emplace_back(p2);
	a.emplace_back(n1);
	a.emplace_back(p3);

	const Player* keep = static_cast<const Creature*>(p2.get())->getPlayer();
	a.filterPlayers([keep](const Player* player) { return player == keep; });

	CHECK(a.size() == 3);
	CHECK(a.players().size() == 1);
	CHECK(spanContains(a.players(), p2.get()));
	CHECK(!spanContains(a.players(), p1.get()));
	CHECK(!spanContains(a.players(), p3.get()));
	// monster/npc ranges must survive the removal and stay consistent
	CHECK(a.monsters().size() == 1);
	CHECK(spanContains(a.monsters(), m1.get()));
	CHECK(a.npcs().size() == 1);
	CHECK(spanContains(a.npcs(), n1.get()));
}

TFS_TEST_MAIN()
