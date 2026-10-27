#include "../otpch.h"

#include "../condition.h"
#include "../game.h"
#include "../monster.h"
#include "../player.h"
#include "../tile.h"
#include "../vocation.h"
#include "protocolgame_audit_support.h"
#include "test_support.h"

extern Vocations g_vocations;

namespace {

void ensureVocations()
{
	static const bool loaded = [] {
		const auto previous = std::filesystem::current_path();
		std::filesystem::current_path(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path());
		const bool result = g_vocations.loadFromXml();
		std::filesystem::current_path(previous);
		return result;
	}();
	CHECK(loaded);
}

struct Fixture
{
	std::array<std::shared_ptr<ProtocolGame>, 6> protocols;
	std::array<std::shared_ptr<Player>, 6> players;

	explicit Fixture(bool reverse)
	{
		ensureVocations();
		const std::array<std::string, 6> names{"Caster", "Patient", "ObserverOne", "ObserverTwo", "OtherInstance", "OtherFloor"};
		for (size_t i = 0; i < players.size(); ++i) {
			protocols[i] = std::make_shared<ProtocolGame>(nullptr);
			players[i] = std::make_shared<Player>(protocols[i]);
			players[i]->setGroup(std::make_shared<Group>());
			players[i]->setName(names[i]);
			players[i]->setSex(PLAYERSEX_MALE);
			players[i]->setMaxHealth(1000);
			players[i]->setHealth(500);
			players[i]->setMaxMana(1000);
			players[i]->setMana(500);
			ProtocolGameAuditTestAccess::bind(*protocols[i], players[i]);
			CHECK(players[i]->setVocation(0));
		}
		players[4]->setInstanceID(10);
		for (size_t index = 0; index < players.size(); ++index) {
			const size_t i = reverse ? players.size() - index - 1 : index;
			const Position pos{700, 700, static_cast<uint8_t>(i == 5 ? 6 : 7)};
			if (!g_game.map.getTile(pos)) {
				g_game.map.setTile(pos.x, pos.y, pos.z, std::make_unique<StaticTile>(pos.x, pos.y, pos.z));
			}
			g_game.map.getTile(pos)->internalAddThing(players[i].get());
			g_game.map.getQTNode(pos.x, pos.y)->addCreature(players[i].get());
		}
	}
	~Fixture()
	{
		for (size_t i = 0; i < players.size(); ++i) {
			const Position pos = players[i]->getPosition();
			g_game.map.getQTNode(pos.x, pos.y)->removeCreature(players[i].get());
			players[i]->getTile()->removeThing(players[i].get(), 0);
			players[i]->setParent(nullptr);
			ProtocolGameAuditTestAccess::bind(*protocols[i], nullptr);
		}
	}
	bool hasText(size_t i, const std::string& text) const
	{
		NetworkMessage expected;
		expected.addByte(0xB4);
		expected.addByte(MESSAGE_STATUS_DEFAULT);
		expected.addString(text);
		const auto& output = protocols[i]->getCurrentBuffer();
		const auto* begin = output->getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION;
		const auto* end = begin + output->getLength();
		return std::search(begin, end, expected.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION,
		                   expected.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION + expected.getLength()) != end;
	}
};

void checkObservers(int kind)
{
	for (bool reverse : {false, true}) {
		for (int source = 0; source < 4; ++source) {
			Fixture f(reverse);
			auto type = std::make_shared<MonsterType>();
			type->name = "Demon";
			type->nameDescription = "a demon";
			auto monster = std::make_shared<Monster>(type);
			Creature* attacker = source == 0 ? nullptr : source == 1 ? f.players[1].get() : source == 2 ? f.players[0].get() : static_cast<Creature*>(monster.get());
			CombatDamage damage;
			damage.primary.type = kind == 0 ? COMBAT_HEALING : kind == 2 ? COMBAT_MANADRAIN : COMBAT_PHYSICALDAMAGE;
			damage.primary.value = kind == 0 ? 25 : -25;
			if (kind == 1) {
				CHECK(f.players[1]->addCondition(Condition::createCondition(CONDITIONID_COMBAT, CONDITION_MANASHIELD, -1, 0)));
			}
			if (kind == 2) {
				CHECK(g_game.combatChangeMana(attacker, f.players[1].get(), damage));
			} else {
				CHECK(g_game.combatChangeHealth(attacker, f.players[1].get(), damage));
			}
			std::string observer;
			if (kind == 0) {
				observer = !attacker ? "Patient was healed for 25 hitpoints." : source == 1 ? "Patient healed himself for 25 hitpoints." :
				           fmt::format("{} healed Patient for 25 hitpoints.", attacker->getNameDescription());
			} else {
				observer = !attacker ? "Patient loses 25 mana." : source == 1 ? "Patient loses 25 mana due to his own attack." :
				           fmt::format("Patient loses 25 mana due to an attack by {}.", attacker->getNameDescription());
			}
			observer[0] = static_cast<char>(std::toupper(observer[0]));
			CHECK(f.hasText(2, observer));
			CHECK(f.hasText(3, observer));
			CHECK(!f.hasText(4, observer));
			CHECK(!f.hasText(5, observer));
			CHECK(!f.hasText(1, observer));
			if (source == 2) {
				CHECK(f.hasText(0, kind == 0 ? "You heal Patient for 25 hitpoints." : "Patient loses 25 mana due to your attack."));
			}
		}
	}
}

void checkObserversOnDispatcher(int kind)
{
	struct DispatcherScope
	{
		DispatcherScope() { g_dispatcher.start(); }
		~DispatcherScope() { g_dispatcher.shutdown(); }
	} dispatcherScope;
	bool ran = false;
	std::exception_ptr failure;
	CHECK(g_dispatcher.addTask([&] {
		try {
			checkObservers(kind);
		} catch (...) {
			failure = std::current_exception();
		}
		ran = true;
		g_dispatcher.shutdown();
	}));
	// Drive the real reactor loop synchronously: it establishes dispatcher
	// affinity, without disabling the production getOutputBuffer assertion.
	g_reactor.runLoop();
	CHECK(ran);
	if (failure) std::rethrow_exception(failure);
}

} // namespace

TEST_CASE(healing_observers_do_not_receive_another_players_message) { checkObserversOnDispatcher(0); }
TEST_CASE(mana_shield_observers_do_not_receive_another_players_message) { checkObserversOnDispatcher(1); }
TEST_CASE(mana_drain_observers_do_not_receive_another_players_message) { checkObserversOnDispatcher(2); }

TFS_TEST_MAIN()
