#include "../otpch.h"
#include "../game.h"
#include "../player.h"
#include "../tasks.h"
#include "../tile.h"
#include "protocolgame_audit_support.h"
#include "test_support.h"

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
	std::shared_ptr<ProtocolGame> protocol = std::make_shared<ProtocolGame>(nullptr);
	std::vector<std::shared_ptr<Player>> creatures;
	std::shared_ptr<StaticTile> tile = std::make_shared<StaticTile>(700, 700, 7);

	explicit Fixture(size_t count, bool otc = true)
	{
		for (size_t i = 0; i < count; ++i) {
			auto creature = std::make_shared<Player>(nullptr);
			creature->setGroup(std::make_shared<Group>());
			tile->internalAddThing(creature.get());
			CombatReentryTestAccess::add(creature);
			creatures.push_back(std::move(creature));
		}
		ProtocolGameAuditTestAccess::bind(*protocol, creatures.front());
		ProtocolGameAuditTestAccess::otc(*protocol, otc);
	}
	~Fixture()
	{
		OutputMessagePool::getInstance().removeProtocolFromAutosend(protocol);
		ProtocolGameAuditTestAccess::bind(*protocol, nullptr);
		for (const auto& creature : creatures) {
			CombatReentryTestAccess::remove(*creature);
			tile->removeThing(creature.get(),0);
			creature->setParent(nullptr);
		}
	}
	uint32_t id(size_t index) const { return creatures[index]->getID(); }
	std::pair<bool,uint32_t> remember(size_t index) { return ProtocolGameAuditTestAccess::remember(*protocol,id(index)); }
};
}

TEST_CASE(otc_keeps_all_visible_creatures_when_more_than_250_are_on_screen)
{
	Fixture f(1501);
	for (size_t i=0;i<f.creatures.size();++i) {
		const auto [known,removed] = f.remember(i);
		CHECK(!known);
		CHECK(removed==0);
	}
	CHECK(ProtocolGameAuditTestAccess::knownCount(*f.protocol)==1501);
	for (size_t i=0;i<f.creatures.size();++i) CHECK(ProtocolGameAuditTestAccess::knows(*f.protocol,f.id(i)));
}

TEST_CASE(otc_evicts_an_unseen_creature_instead_of_a_visible_one)
{
	Fixture f(1302);
	for (size_t i=0;i<1301;++i) f.remember(i);
	ProtocolGameAuditTestAccess::pendingEmblem(*f.protocol, f.id(17));
	ProtocolGameAuditTestAccess::pendingEmblem(*f.protocol, f.id(0));
	f.creatures[17]->setInstanceID(99);
	const auto [known,removed] = f.remember(1301);
	CHECK(!known);
	CHECK(removed==f.id(17));
	CHECK(!ProtocolGameAuditTestAccess::knows(*f.protocol,f.id(17)));
	CHECK(!ProtocolGameAuditTestAccess::hasPendingEmblem(*f.protocol, f.id(17)));
	CHECK(ProtocolGameAuditTestAccess::hasPendingEmblem(*f.protocol, f.id(0)));
	CHECK(ProtocolGameAuditTestAccess::knows(*f.protocol,f.id(0)));
}

TEST_CASE(legacy_cache_stays_bounded_and_keeps_the_local_player)
{
	Fixture f(400,false);
	for (size_t i=0;i<f.creatures.size();++i) {
		const auto [known, removed] = f.remember(i);
		CHECK(!known);
		if (removed != 0) CHECK(!ProtocolGameAuditTestAccess::hasPendingEmblem(*f.protocol, removed));
		ProtocolGameAuditTestAccess::pendingEmblem(*f.protocol, f.id(i));
	}
	CHECK(ProtocolGameAuditTestAccess::knownCount(*f.protocol)==250);
	CHECK(ProtocolGameAuditTestAccess::knows(*f.protocol,f.id(0)));
}

TEST_CASE(unannounced_creatures_do_not_receive_outfit_or_walkthrough_deltas)
{
	Fixture f(2);
	Outfit_t outfit; outfit.lookType=128;
	ProtocolGameAuditTestAccess::outfit(*f.protocol,f.creatures[1].get(),outfit);
	ProtocolGameAuditTestAccess::walkthrough(*f.protocol,f.creatures[1].get(),true);
	CHECK(f.protocol->getCurrentBuffer()->getLength()==0);
}

TEST_CASE(announced_creatures_keep_the_outfit_and_walkthrough_wire_format)
{
	Fixture f(2);
	f.remember(1);
	Outfit_t outfit; outfit.lookType=128;
	ProtocolGameAuditTestAccess::outfit(*f.protocol,f.creatures[1].get(),outfit);
	ProtocolGameAuditTestAccess::walkthrough(*f.protocol,f.creatures[1].get(),true);
	const auto& output=f.protocol->getCurrentBuffer();
	const auto* bytes=output->getBuffer()+NetworkMessage::INITIAL_BUFFER_POSITION;
	CHECK(output->getLength()==20);
	CHECK(bytes[0]==0x8e);
	CHECK(bytes[14]==0x92);
	CHECK(bytes[19]==0);
}

int main()
{
	g_dispatcher.start();
	int result=EXIT_FAILURE;
	g_dispatcher.addTask([&] { result=tfs::tests::run(); g_dispatcher.shutdown(); });
	g_reactor.runLoop();
	return result;
}
