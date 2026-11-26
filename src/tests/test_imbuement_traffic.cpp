#include "../otpch.h"

#include "../configmanager.h"
#include "../events.h"
#include "../game.h"
#include "../imbuement.h"
#include "../player.h"
#include "../performance_metrics.h"
#include "../scriptmanager.h"
#include "../tile.h"
#include "../vocation.h"
#include "protocolgame_audit_support.h"
#include "test_support.h"

extern Vocations g_vocations;

namespace {

struct Fixture
{
	Events* previousEvents = g_events;
	Events events;
	bool previousEnabled = ConfigManager::getBoolean(ConfigManager::IMBUEMENT_SYSTEM_ENABLED);
	std::shared_ptr<ProtocolGame> protocol = std::make_shared<ProtocolGame>(nullptr);
	std::shared_ptr<Player> player = std::make_shared<Player>(protocol);
	std::shared_ptr<StaticTile> tile = std::make_shared<StaticTile>(760, 760, 7);

	Fixture()
	{
		g_events = &events;
		CHECK(g_vocations.loadFromXml());
		ConfigManager::setBoolean(ConfigManager::IMBUEMENT_SYSTEM_ENABLED, true);
		player->setGroup(std::make_shared<Group>());
		CHECK(player->setVocation(0));
		tile->internalAddThing(player.get());
		ProtocolGameAuditTestAccess::bind(*protocol, player);
		ProtocolGameAuditTestAccess::setImbuementTrackerOpen(*protocol, true);
	}
	~Fixture()
	{
		OutputMessagePool::getInstance().removeProtocolFromAutosend(protocol);
		tile->removeThing(player.get(), 0);
		player->setParent(nullptr);
		ProtocolGameAuditTestAccess::bind(*protocol, nullptr);
		ConfigManager::setBoolean(ConfigManager::IMBUEMENT_SYSTEM_ENABLED, previousEnabled);
		g_events = previousEvents;
	}
	std::shared_ptr<Imbuement> equip(slots_t slot, uint32_t duration, ImbuementDecayType decay)
	{
		auto item = std::make_shared<Item>(0);
		CHECK(item->addImbuementSlots(1));
		static_cast<Cylinder&>(*player).internalAddThing(slot, item.get());
		auto imbue = std::make_shared<Imbuement>(IMBUEMENT_TYPE_SWORD_SKILL, 5, duration, decay);
		CHECK(item->addImbuement(imbue, false));
		return imbue;
	}
	void clearOutput() { protocol->getCurrentBuffer() = std::make_shared<OutputMessage>(); }
	std::vector<uint8_t> output() const
	{
		const auto& msg = protocol->getCurrentBuffer();
		CHECK(msg != nullptr);
		const size_t length = msg->getLength();
		CHECK(length <= NetworkMessage::MAX_PROTOCOL_BODY_LENGTH);
		std::vector<uint8_t> result(length);
		if (length != 0) {
			std::memcpy(result.data(), msg->getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION, length);
		}
		return result;
	}
};

template <typename F> void onDispatcher(F&& function)
{
	g_dispatcher.start();
	bool ran = false;
	std::exception_ptr failure;
	g_dispatcher.addTask([&] {
		try { function(); } catch (...) { failure = std::current_exception(); }
		ran = true;
		g_dispatcher.shutdown();
	});
	g_reactor.runLoop();
	CHECK(ran);
	if (failure) std::rethrow_exception(failure);
}

} // namespace

TEST_CASE(unchanged_imbuements_do_not_resend_skills_and_stats_on_think)
{
	onDispatcher([] {
		Fixture f;
		f.equip(CONST_SLOT_HEAD, 3600, IMBUEMENT_DECAY_NONE);
		f.equip(CONST_SLOT_ARMOR, 3600, IMBUEMENT_DECAY_NONE);
		f.clearOutput();
		for (int i = 0; i < 20; ++i) f.player->onThink(1000);
		CHECK(f.output().empty());
	});
}

TEST_CASE(imbuement_countdown_only_updates_tracker_until_bonus_expires)
{
	onDispatcher([] {
		Fixture f;
		const auto baseline = f.player->getSkillLevel(SKILL_SWORD);
		auto imbue = f.equip(CONST_SLOT_HEAD, 2, IMBUEMENT_DECAY_EQUIPPED);
		CHECK(f.player->getSkillLevel(SKILL_SWORD) == baseline + 5);
		f.clearOutput();
		f.player->sendImbuementDurations();
		const auto before = f.output();
		f.clearOutput();
		f.player->onThink(1000);
		CHECK(imbue->duration == 1);
		const auto tick = f.output();
		f.clearOutput();
		f.player->sendImbuementDurations();
		CHECK(tick == f.output());
		CHECK(tick != before);
		f.clearOutput();
		f.player->onThink(1000);
		CHECK(f.player->getSkillLevel(SKILL_SWORD) == baseline);
		const auto expired = f.output();
		f.clearOutput();
		f.player->sendSkills();
		f.player->sendStats();
		f.player->sendImbuementDurations();
		CHECK(expired == f.output());
	});
}

TEST_CASE(disabled_imbuements_do_not_generate_periodic_stat_packets)
{
	onDispatcher([] {
		Fixture f;
		auto imbue = f.equip(CONST_SLOT_HEAD, 2, IMBUEMENT_DECAY_EQUIPPED);
		ConfigManager::setBoolean(ConfigManager::IMBUEMENT_SYSTEM_ENABLED, false);
		f.clearOutput();
		f.player->onThink(1000);
		CHECK(imbue->duration == 2);
		CHECK(f.output().empty());
	});
}

TEST_CASE(real_game_serializers_and_dispatcher_feed_traffic_metrics)
{
	onDispatcher([] {
		Fixture f;
		struct MetricsGuard
		{
			bool previous = g_performanceMetrics.isEnabled();
			MetricsGuard()
			{
				(void)g_performanceMetrics.takeNetworkTrafficSnapshot();
				g_performanceMetrics.setEnabled(true);
			}
			~MetricsGuard() { g_performanceMetrics.setEnabled(previous); }
		} guard;
		f.player->sendStats();
		f.player->sendSkills();
		auto incoming = std::make_shared<NetworkMessage>();
		incoming->addByte(0x1E); // normal client ping
		CHECK(incoming->setBufferPosition(0));
		ProtocolGameAuditTestAccess::parseAcceptedPacket(*f.protocol, incoming);
		const auto traffic = g_performanceMetrics.takeNetworkTrafficSnapshot();
		CHECK(traffic.incoming[0x1E].messages == 1);
		CHECK(traffic.incoming[0x1E].bytes == 1);
		CHECK(traffic.outgoing[0xA0].messages == 1);
		CHECK(traffic.outgoing[0xA1].messages == 1);
		CHECK(traffic.outgoing[0xA0].bytes + traffic.outgoing[0xA1].bytes == f.output().size());
	});
}

TFS_TEST_MAIN()
