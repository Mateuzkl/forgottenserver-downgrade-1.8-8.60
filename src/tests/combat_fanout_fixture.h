// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in the LICENSE file.

#ifndef FS_COMBAT_FANOUT_FIXTURE_H
#define FS_COMBAT_FANOUT_FIXTURE_H

#include "../game.h"
#include "../outputmessage.h"
#include "../player.h"
#include "../protocolgame.h"

// Private access only for the in-memory test/benchmark. The legacy oracle is
// the routing and serializer from 7bc92ba6, not a synthetic append-only loop.
struct ProtocolGameCombatPacketsTestAccess
{
	static void viewer(ProtocolGame& protocol, const std::shared_ptr<Player>& player) { protocol.player = player; }
	static void spy(ProtocolGame& protocol, const Position& pos)
	{
		protocol.spyActive_ = true;
		protocol.spyViewportPos_ = pos;
	}
	static void expireOwner(ProtocolSpectator& proxy) { proxy.owner.reset(); }
	static void clearFollowers(ProtocolSpectator& proxy)
	{
		proxy.spectators.clear();
		proxy.spyClients_.clear();
	}
	static void cachedEffect(ProtocolGame& protocol, const Position& pos, uint16_t type,
	                         MagicEffectPacket& packet)
	{
		protocol.sendMagicEffect(pos, type, packet);
	}
	static void legacyEffect(ProtocolGame& protocol, const Position& pos, uint16_t type)
	{
		if (!protocol.canSee(pos)) {
			return;
		}
		Tile* tile = g_game.map.getTile(pos);
		if (!tile || !tile->getGround()) {
			return;
		}
		NetworkMessage msg;
		msg.addByte(0x83);
		msg.addPosition(pos);
		msg.add<uint16_t>(type);
		protocol.writeToOutputBuffer(msg);
	}
	static void legacyFanout(const SpectatorVec& spectators, const Position& pos, uint16_t type)
	{
		for (const auto& spectator : spectators) {
			Player* player = spectator ? spectator->getPlayer() : nullptr;
			if (!player || !player->client) {
				continue;
			}
			auto& proxy = *player->client;
			if (auto owner = proxy.owner.lock()) {
				legacyEffect(*owner, pos, type);
			}
			for (auto& cast : proxy.spectators) {
				legacyEffect(*cast, pos, type);
			}
			for (auto& spy : proxy.spyClients_) {
				legacyEffect(*spy, pos, type);
			}
		}
	}
};

struct CombatFanoutFixture
{
	const Position position{1030, 1015, 7};
	std::shared_ptr<StaticTile> viewerTile = std::make_shared<StaticTile>(position.x, position.y, position.z);
	std::vector<std::shared_ptr<ProtocolGame>> protocols;
	std::vector<std::shared_ptr<Player>> viewers;
	SpectatorVec spectators;

	explicit CombatFanoutFixture(size_t owners, size_t casts = 0, size_t spies = 0)
	{
		g_game.map.setTile(position.x, position.y, position.z,
		                   std::make_unique<StaticTile>(position.x, position.y, position.z));
		g_game.map.getTile(position)->setGround(std::make_shared<Item>(0));
		protocols.reserve(owners * (1 + casts + spies));
		viewers.reserve(protocols.capacity());
		for (size_t index = 0; index < owners; ++index) {
			auto owner = makeViewer();
			spectators.emplace_back(owner);
			for (size_t cast = 0; cast < casts; ++cast) {
				makeViewer();
				owner->client->addSpectator(protocols.back());
			}
			for (size_t spy = 0; spy < spies; ++spy) {
				makeViewer();
				owner->client->addSpyClient(protocols.back());
			}
		}
	}

	~CombatFanoutFixture()
	{
		for (auto& viewer : viewers) {
			if (viewer->client) {
				ProtocolGameCombatPacketsTestAccess::clearFollowers(*viewer->client);
			}
			viewer->setParent(nullptr);
		}
		for (auto& protocol : protocols) {
			ProtocolGameCombatPacketsTestAccess::viewer(*protocol, nullptr);
			OutputMessagePool::getInstance().removeProtocolFromAutosend(protocol);
		}
	}

	void reset()
	{
		for (auto& protocol : protocols) {
			protocol->getCurrentBuffer()->reset();
		}
	}

	std::vector<std::vector<uint8_t>> payloads()
	{
		std::vector<std::vector<uint8_t>> result;
		for (auto& protocol : protocols) {
			const auto& output = protocol->getCurrentBuffer();
			const auto* begin = output->getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION;
			result.emplace_back(begin, begin + output->getLength());
		}
		return result;
	}

private:
	std::shared_ptr<Player> makeViewer()
	{
		auto protocol = std::make_shared<ProtocolGame>(nullptr);
		auto player = std::make_shared<Player>(protocol);
		player->setParent(viewerTile.get());
		ProtocolGameCombatPacketsTestAccess::viewer(*protocol, player);
		protocol->getCurrentBuffer() = std::make_shared<OutputMessage>();
		protocols.push_back(protocol);
		viewers.push_back(player);
		return player;
	}
};

#endif
