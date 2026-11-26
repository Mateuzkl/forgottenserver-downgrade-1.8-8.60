#pragma once

#include "../outputmessage.h"
#include "../protocolgame.h"

struct ProtocolGameAuditTestAccess
{
	static void bind(ProtocolGame& protocol, const std::shared_ptr<Player>& player)
	{
		protocol.player = player;
		protocol.getCurrentBuffer() = std::make_shared<OutputMessage>();
	}
	static bool canSee(const ProtocolGame& protocol, const Position& position)
	{
		return protocol.canSee(position);
	}
	static void otc(ProtocolGame& protocol, bool enabled) { protocol.isOTC = enabled; }
	static std::pair<bool, uint32_t> remember(ProtocolGame& protocol, uint32_t id) { return protocol.isKnownCreature(id); }
	static bool knows(const ProtocolGame& protocol, uint32_t id) { return protocol.knownCreatureSet.contains(id); }
	static size_t knownCount(const ProtocolGame& protocol) { return protocol.knownCreatureSet.size(); }
	static void pendingEmblem(ProtocolGame& protocol, uint32_t id) { protocol.pendingCreatureEmblems.insert(id); }
	static bool hasPendingEmblem(const ProtocolGame& protocol, uint32_t id)
	{
		return protocol.pendingCreatureEmblems.contains(id);
	}
	static void effectSource(ProtocolGame& protocol, bool fonticak, bool astra, bool supported)
	{
		protocol.isFonticakClient = fonticak;
		protocol.isAstraClient = astra;
		protocol.supportsEffectSource = supported;
	}
	static void outfit(ProtocolGame& protocol, const Creature* creature, const Outfit_t& outfit) { protocol.sendCreatureOutfit(creature,outfit); }
	static void walkthrough(ProtocolGame& protocol, const Creature* creature, bool value) { protocol.sendCreatureWalkthrough(creature,value); }
	static void effect(ProtocolGame& protocol, const Position& pos, uint16_t value, Creature* actor = nullptr) { protocol.sendMagicEffect(pos,value,actor); }
	static void health(ProtocolGame& protocol, const Creature* creature) { protocol.sendCreatureHealth(creature); }
	static void distance(ProtocolGame& protocol, const Position& from, const Position& to, uint16_t value, Creature* actor = nullptr) { protocol.sendDistanceShoot(from,to,value,actor); }
	static void animated(ProtocolGame& protocol, std::string_view text, const Position& pos, TextColor_t color) { protocol.sendAnimatedText(text,pos,color); }
	static void text(ProtocolGame& protocol, MessageClasses type, const std::string& text) { protocol.sendTextMessage(type,text); }
	static void text(ProtocolGame& protocol, const TextMessage& text) { protocol.sendTextMessage(text); }
	static void setImbuementTrackerOpen(ProtocolGame& protocol, bool value)
	{
		protocol.imbuementTrackerOpen = value;
	}
	static void parseAcceptedPacket(ProtocolGame& protocol, NetworkMessage_ptr& packet)
	{
		protocol.acceptPackets = true;
		protocol.parsePacketOnDispatcher(packet);
	}
	static void parsePacket(ProtocolGame& protocol, NetworkMessage& msg) { protocol.parsePacket(msg); }
	static void setAcceptPackets(ProtocolGame& protocol, bool value) { protocol.acceptPackets = value; }
	static void writeChunk(ProtocolGame& protocol, std::span<const uint8_t> bytes)
	{
		protocol.writeToOutputBuffer(bytes);
	}
	static void release(ProtocolGame& protocol) { protocol.release(); }
	static size_t incomingQueueSize(ProtocolGame& protocol)
	{
		std::scoped_lock lock(protocol.incomingPacketsMutex);
		return protocol.incomingPackets.size();
	}
	static size_t backlogCount(const ProtocolGame& protocol) { return protocol.packetBacklog.current(); }
	static size_t backlogLimit(const ProtocolGame& protocol) { return protocol.packetBacklog.limit(); }
	static void backdateExpiringDeadlines(ProtocolGame& protocol, std::chrono::steady_clock::time_point deadline)
	{
		std::scoped_lock lock(protocol.incomingPacketsMutex);
		for (auto& queued : protocol.incomingPackets) {
			if (queued.deadline != std::chrono::steady_clock::time_point::max()) {
				queued.deadline = deadline;
			}
		}
	}
};
