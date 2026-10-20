// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in LICENSE.
#include "../otpch.h"
#include "../game.h"
#include "../player.h"
#include "../protocolgame.h"
#include "../outputmessage.h"
#include "../astraclient.h"
#include "../tile.h"
#include "test_support.h"

struct ProtocolGameMovementTestAccess {
	static void setup(ProtocolGame& protocol, const std::shared_ptr<Player>& viewer)
	{
		protocol.player = viewer;
		protocol.isOTC = true;
		protocol.getCurrentBuffer() = std::make_shared<OutputMessage>();
	}
	static void moveActor(ProtocolGame& protocol, const Creature* actor, const Position& oldPos,
	                      int32_t oldStack, const Position& newPos, int32_t newStack)
	{
		protocol.sendMoveCreature(actor, newPos, newStack, oldPos, oldStack, false);
	}
	static void remove(ProtocolGame& protocol, const Position& pos, uint32_t stack)
	{
		protocol.sendRemoveTileThing(pos, stack);
	}
	static void emblem(ProtocolGame& protocol, const Creature* actor) { protocol.sendCreatureEmblem(actor); }
	static void remember(ProtocolGame& protocol, uint32_t id) { protocol.knownCreatureSet.insert(id); }
	static bool knows(ProtocolGame& protocol, uint32_t id) { return protocol.knownCreatureSet.contains(id); }
	static bool pendingEmblem(ProtocolGame& protocol, uint32_t id) { return protocol.pendingCreatureEmblems.contains(id); }
	static void cacheEchoVisual(ProtocolGame& protocol, uint32_t id)
	{
		protocol.supportsAstraEchoRaidVisuals = true;
		protocol.echoRaidVisualCache[id] = EchoRaidVisualState::None;
	}
	static bool hasEchoVisual(ProtocolGame& protocol, uint32_t id) { return protocol.echoRaidVisualCache.contains(id); }
	static std::pair<bool, uint32_t> know(ProtocolGame& protocol, uint32_t id) { return protocol.isKnownCreature(id); }
	static void pendEmblem(ProtocolGame& protocol, uint32_t id) { protocol.pendingCreatureEmblems.insert(id); }
	static void refresh(ProtocolGame& protocol) { protocol.refreshWorldView(); }
	static void move(ProtocolGame& protocol, const std::shared_ptr<Player>& viewer,
	                 const Position& oldPos, int32_t oldStack, const Position& newPos, int32_t newStack)
	{
		protocol.player = viewer;
		protocol.isOTC = true;
		protocol.getCurrentBuffer() = std::make_shared<OutputMessage>();
		protocol.sendMoveCreature(viewer.get(), newPos, newStack, oldPos, oldStack, false);
	}
	static NetworkMessage describe(ProtocolGame& protocol, const std::shared_ptr<Player>& viewer, const Tile* tile)
	{
		protocol.player = viewer;
		protocol.isOTC = viewer->isOTC();
		NetworkMessage msg;
		protocol.GetTileDescription(tile, msg);
		return msg;
	}
	static NetworkMessage expected(ProtocolGame& protocol, const std::shared_ptr<Player>& viewer,
	                               const std::vector<Thing*>& things)
	{
		protocol.player = viewer;
		protocol.isOTC = viewer->isOTC();
		NetworkMessage msg;
		for (auto* thing : things) {
			if (auto* item = thing->getItem()) protocol.appendItem(msg, item);
			else {
				auto* creature = thing->getCreature();
				auto [known, removed] = protocol.isKnownCreature(creature->getID());
				protocol.AddCreature(msg, creature, known, removed);
			}
		}
		return msg;
	}
};

namespace {
class TestNpc final : public Creature {
public:
	TestNpc() { id = 0x80000101; }
	const std::string& getName() const override { return name; }
	const std::string& getNameDescription() const override { return name; }
	std::string getDescription(int32_t) const override { return name; }
	CreatureType_t getType() const override { return CREATURETYPE_NPC; }
	void setID() override {}
	void removeList() override {}
	void addList() override {}
private:
	std::string name = "Movement fixture NPC";
};

struct Fixture {
	std::shared_ptr<StaticTile> tile = std::make_shared<StaticTile>(32341, 31789, 7);
	std::shared_ptr<Player> player = std::make_shared<Player>(nullptr);
	std::shared_ptr<TestNpc> npc = std::make_shared<TestNpc>();
	std::vector<Thing*> top;
	Fixture(size_t topCount, bool otc = true)
	{
		player->setGroup(std::make_shared<Group>());
		player->setID();
		player->setOperatingSystem(otc ? CLIENTOS_OTCLIENT_WINDOWS : CLIENTOS_WINDOWS);
		tile->internalAddThing(std::make_shared<Item>(100).get());
		for (size_t i = 0; i < topCount; ++i) {
			auto item = std::make_shared<Item>(101);
			tile->internalAddThing(item.get());
			top.push_back(item.get());
		}
		tile->internalAddThing(npc.get());
		tile->internalAddThing(player.get());
	}
	~Fixture()
	{
		tile->removeThing(npc.get(), 0);
		if (player->getParent() == tile.get()) tile->removeThing(player.get(), 0);
		npc->setParent(nullptr);
		player->setParent(nullptr);
	}
};
}

TEST_CASE(full_own_tile_reserves_viewer_and_matches_wire_and_stack_lookups)
{
	for (bool otc : {false, true}) {
		Fixture f(12, otc);
		const auto stack = f.tile->getClientStack(f.player.get());
		CHECK(stack.size == MAX_STACKPOS_THINGS);
		CHECK(stack.things[9] == f.player.get());
		CHECK(f.tile->getClientIndexOfCreature(f.player.get(), f.player.get()) == 9);
		CHECK(f.tile->getClientIndexOfCreature(f.player.get(), f.npc.get()) == MAX_STACKPOS_THINGS);
		std::vector<Thing*> expected{f.tile->getGround()};
		const auto* items = f.tile->getItemList();
		for (auto it = items->getBeginTopItem(); expected.size() < 9; ++it) expected.push_back(it->get());
		expected.push_back(f.player.get());
		for (size_t i = 0; i < expected.size(); ++i) {
			CHECK(f.tile->getThing(f.player.get(), i) == expected[i]);
			if (auto* item = expected[i]->getItem()) CHECK(f.tile->getStackposOfItem(f.player.get(), item) == int32_t(i));
		}
		CHECK(f.tile->getThing(f.player.get(), 10) == nullptr);
		CHECK(f.tile->getStackposOfItem(f.player.get(), (items->getEndTopItem() - 1)->get()) == -1);
		ProtocolGame actualProtocol(nullptr), expectedProtocol(nullptr);
		const auto actual = ProtocolGameMovementTestAccess::describe(actualProtocol, f.player, f.tile.get());
		const auto wanted = ProtocolGameMovementTestAccess::expected(expectedProtocol, f.player, expected);
		CHECK(actual.getLength() == wanted.getLength());
		CHECK(std::memcmp(actual.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION,
		                  wanted.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION, actual.getLength()) == 0);
	}
}

TEST_CASE(normal_npc_order_and_instance_filtering_match_client_indices)
{
	Fixture f(2);
	const auto stack = f.tile->getClientStack(f.player.get());
	CHECK(stack.size == 5);
	CHECK(stack.things[3] == f.npc.get());
	CHECK(stack.things[4] == f.player.get());
	CHECK(f.tile->getClientIndexOfCreature(f.player.get(), f.npc.get()) == 3);
	f.npc->setInstanceID(99);
	CHECK(f.tile->getClientStack(f.player.get()).size == 4);
	CHECK(f.tile->getClientIndexOfCreature(f.player.get(), f.npc.get()) == -1);
	CHECK(f.tile->getClientIndexOfCreature(f.player.get(), f.player.get()) == 3);
}

TEST_CASE(movement_from_dense_own_tile_uses_the_same_stack_as_map_description)
{
	Fixture f(12);
	const auto oldPos = f.tile->getPosition();
	const auto oldStack = f.tile->getClientIndexOfCreature(f.player.get(), f.player.get());
	auto destination = std::make_shared<StaticTile>(oldPos.x + 1, oldPos.y, oldPos.z);
	destination->internalAddThing(std::make_shared<Item>(100).get());
	f.tile->removeThing(f.player.get(), 0);
	destination->internalAddThing(f.player.get());
	ProtocolGame protocol(nullptr);
	ProtocolGameMovementTestAccess::move(protocol, f.player, oldPos, oldStack, destination->getPosition(),
	                                   destination->getClientIndexOfCreature(f.player.get(), f.player.get()));
	auto& msg = *protocol.getCurrentBuffer();
	CHECK(msg.setBufferPosition(0));
	CHECK(msg.getByte() == 0x6D);
	CHECK(msg.getPosition() == oldPos);
	CHECK(msg.getByte() == 9);
	CHECK(msg.getPosition() == destination->getPosition());
	destination->removeThing(f.player.get(), 0);
	f.player->setParent(nullptr);
}

TEST_CASE(detached_viewer_is_not_injected_from_a_stale_parent_pointer)
{
	Fixture f(12);
	f.tile->removeThing(f.player.get(), 0);
	f.player->setParent(f.tile.get());
	const auto stack = f.tile->getClientStack(f.player.get());
	CHECK(stack.size == MAX_STACKPOS_THINGS);
	for (size_t i = 0; i < stack.size; ++i) CHECK(stack.things[i] != f.player.get());
	CHECK(f.tile->getClientIndexOfCreature(f.player.get(), f.player.get()) == -1);
	f.player->setParent(nullptr);
}

TEST_CASE(moving_into_an_omitted_destination_refreshes_both_tiles_instead_of_0x6d)
{
	Fixture f(12);
	ProtocolGame protocol(nullptr);
	ProtocolGameMovementTestAccess::setup(protocol, f.player);
	ProtocolGameMovementTestAccess::cacheEchoVisual(protocol, f.npc->getID());
	const auto pos = f.tile->getPosition();
	const Position oldPos{uint16_t(pos.x - 1), pos.y, pos.z};
	ProtocolGameMovementTestAccess::moveActor(protocol, f.npc.get(), oldPos, 1, pos, MAX_STACKPOS_THINGS);
	auto& msg = *protocol.getCurrentBuffer();
	CHECK(msg.setBufferPosition(0));
	CHECK(msg.getByte() == 0x69);
	CHECK(msg.getPosition() == oldPos);
	CHECK(msg.get<uint16_t>() == 0xFF01); // No source tile in this asset-free fixture.
	CHECK(msg.getByte() == 0x69);
	CHECK(msg.getPosition() == pos);
	ProtocolGame expectedProtocol(nullptr);
	const auto expected = ProtocolGameMovementTestAccess::describe(expectedProtocol, f.player, f.tile.get());
	CHECK(std::memcmp(msg.getBuffer() + msg.getBufferPosition(),
	                  expected.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION, expected.getLength()) == 0);
	CHECK(msg.getLength() == 8 + 6 + expected.getLength() + 2);
	CHECK(ProtocolGameMovementTestAccess::hasEchoVisual(protocol, f.npc->getID()));
}

TEST_CASE(moving_from_an_omitted_stack_sends_followups_even_at_the_same_position)
{
	for (bool samePosition : {false, true}) {
		Fixture f(2);
		ProtocolGame protocol(nullptr);
		ProtocolGameMovementTestAccess::setup(protocol, f.player);
		ProtocolGameMovementTestAccess::cacheEchoVisual(protocol, f.npc->getID());
		const auto pos = f.tile->getPosition();
		const Position oldPos{uint16_t(pos.x - (samePosition ? 0 : 1)), pos.y, pos.z};
		ProtocolGameMovementTestAccess::moveActor(protocol, f.npc.get(), oldPos, MAX_STACKPOS_THINGS, pos, 3);
		auto& msg = *protocol.getCurrentBuffer();
		CHECK(msg.setBufferPosition(0));
		CHECK(msg.getByte() == 0x69); // Tile updates must precede the follow-ups.
		CHECK(msg.getPosition() == oldPos);
		CHECK(msg.setBufferPosition(msg.getLength() - 7));
		CHECK(msg.getByte() == AstraClient::SINGLE_CREATURE_MARK_OPCODE);
		CHECK(msg.get<uint32_t>() == f.npc->getID());
		CHECK(msg.getByte() == AstraClient::ECHO_RAID_VISUAL_MARK_TYPE);
		CHECK(msg.getByte() == 0xFF);
		CHECK(!ProtocolGameMovementTestAccess::hasEchoVisual(protocol, f.npc->getID()));
	}
}

TEST_CASE(overflow_removal_sends_post_removal_tile_contents)
{
	Fixture f(2);
	const auto pos = f.tile->getPosition();
	auto tile = std::make_unique<StaticTile>(pos.x, pos.y, pos.z);
	tile->internalAddThing(std::make_shared<Item>(100).get());
	tile->internalAddThing(std::make_shared<Item>(101).get());
	g_game.map.setTile(pos, std::move(tile));
	struct Cleanup { Position pos; ~Cleanup() { g_game.map.removeTile(pos); } } cleanup{pos};
	ProtocolGame protocol(nullptr), expectedProtocol(nullptr);
	ProtocolGameMovementTestAccess::setup(protocol, f.player);
	ProtocolGameMovementTestAccess::remove(protocol, pos, MAX_STACKPOS_THINGS);
	const auto expected = ProtocolGameMovementTestAccess::describe(expectedProtocol, f.player, g_game.map.getTile(pos));
	auto& msg = *protocol.getCurrentBuffer();
	CHECK(msg.setBufferPosition(0));
	CHECK(msg.getByte() == 0x69);
	CHECK(msg.getPosition() == pos);
	CHECK(msg.getLength() == 6 + expected.getLength() + 2);
	CHECK(std::memcmp(msg.getBuffer() + msg.getBufferPosition(),
	                  expected.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION, expected.getLength()) == 0);
}

TEST_CASE(overflow_emblem_preserves_known_cache_and_replaces_description_once)
{
	Fixture f(12);
	ProtocolGame protocol(nullptr);
	ProtocolGameMovementTestAccess::setup(protocol, f.player);
	ProtocolGameMovementTestAccess::remember(protocol, f.npc->getID());
	ProtocolGameMovementTestAccess::emblem(protocol, f.npc.get());
	CHECK(ProtocolGameMovementTestAccess::knows(protocol, f.npc->getID()));
	CHECK(ProtocolGameMovementTestAccess::pendingEmblem(protocol, f.npc->getID()));
	auto& msg = *protocol.getCurrentBuffer();
	CHECK(msg.setBufferPosition(0));
	CHECK(msg.getByte() == 0x69);
	CHECK(msg.getPosition() == f.tile->getPosition());
	auto refreshed = ProtocolGameMovementTestAccess::expected(protocol, f.player, {f.npc.get()});
	CHECK(refreshed.setBufferPosition(0));
	CHECK(refreshed.get<uint16_t>() == 0x61);
	CHECK(refreshed.get<uint32_t>() == f.npc->getID()); // Replace this cached ID, not a different creature.
	CHECK(refreshed.get<uint32_t>() == f.npc->getID());
	CHECK(ProtocolGameMovementTestAccess::knows(protocol, f.npc->getID()));
	CHECK(!ProtocolGameMovementTestAccess::pendingEmblem(protocol, f.npc->getID()));
	auto unchanged = ProtocolGameMovementTestAccess::expected(protocol, f.player, {f.npc.get()});
	CHECK(unchanged.setBufferPosition(0));
	CHECK(unchanged.get<uint16_t>() == 0x62);
	CHECK(unchanged.get<uint32_t>() == f.npc->getID());
}

TEST_CASE(overflow_emblem_for_unknown_creatures_keeps_normal_cache_insertion)
{
	Fixture f(12);
	ProtocolGame protocol(nullptr);
	ProtocolGameMovementTestAccess::setup(protocol, f.player);
	ProtocolGameMovementTestAccess::emblem(protocol, f.npc.get());
	CHECK(!ProtocolGameMovementTestAccess::pendingEmblem(protocol, f.npc->getID()));
	auto description = ProtocolGameMovementTestAccess::expected(protocol, f.player, {f.npc.get()});
	CHECK(description.setBufferPosition(0));
	CHECK(description.get<uint16_t>() == 0x61);
	CHECK(description.get<uint32_t>() == 0);
	CHECK(description.get<uint32_t>() == f.npc->getID());
}

TEST_CASE(pending_emblems_are_pruned_on_cache_eviction_and_world_refresh)
{
	Fixture f(2);
	ProtocolGame protocol(nullptr);
	ProtocolGameMovementTestAccess::setup(protocol, f.player);
	for (uint32_t id = 1; id <= 250; ++id) {
		ProtocolGameMovementTestAccess::remember(protocol, id);
		ProtocolGameMovementTestAccess::pendEmblem(protocol, id);
	}
	const auto [known, removed] = ProtocolGameMovementTestAccess::know(protocol, 251);
	CHECK(!known);
	CHECK(removed != 0);
	CHECK(!ProtocolGameMovementTestAccess::knows(protocol, removed));
	CHECK(!ProtocolGameMovementTestAccess::pendingEmblem(protocol, removed));
	ProtocolGameMovementTestAccess::refresh(protocol);
	for (uint32_t id = 1; id <= 251; ++id) {
		CHECK(!ProtocolGameMovementTestAccess::knows(protocol, id));
		CHECK(!ProtocolGameMovementTestAccess::pendingEmblem(protocol, id));
	}
}

TEST_CASE(ordinary_emblems_keep_legacy_remove_add_stack)
{
	Fixture f(2);
	ProtocolGame protocol(nullptr);
	ProtocolGameMovementTestAccess::setup(protocol, f.player);
	ProtocolGameMovementTestAccess::emblem(protocol, f.npc.get());
	auto& msg = *protocol.getCurrentBuffer();
	CHECK(msg.setBufferPosition(0));
	CHECK(msg.getByte() == 0x6C);
	CHECK(msg.getPosition() == f.tile->getPosition());
	CHECK(msg.getByte() == 3);
	CHECK(msg.getByte() == 0x6A);
	CHECK(msg.getPosition() == f.tile->getPosition());
	CHECK(msg.getByte() == 3);
}

TEST_CASE(local_movement_into_reserved_last_slot_uses_a_full_map_description)
{
	Fixture f(12);
	ProtocolGame protocol(nullptr);
	ProtocolGameMovementTestAccess::setup(protocol, f.player);
	const auto pos = f.tile->getPosition();
	const Position oldPos{uint16_t(pos.x - 1), pos.y, pos.z};
	ProtocolGameMovementTestAccess::moveActor(protocol, f.player.get(), oldPos, 1, pos, 9);
	auto& msg = *protocol.getCurrentBuffer();
	CHECK(msg.setBufferPosition(0));
	CHECK(msg.getByte() == 0x6C);
	CHECK(msg.getPosition() == oldPos);
	CHECK(msg.getByte() == 1);
	CHECK(msg.getByte() == 0x64);
	CHECK(msg.getPosition() == pos);
}

int main()
{
	// Controlled item metadata; no DB, production map, login or network socket.
	const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	if (!Item::items.loadFromOtb((root / "data/items/items.otb").string())) return EXIT_FAILURE;
	auto& ground = Item::items.getItemType(100);
	ground.id = 100;
	ground.group = ITEM_GROUP_GROUND;
	ground.alwaysOnTop = false;
	auto& top = Item::items.getItemType(101);
	top.id = 101;
	top.group = ITEM_GROUP_NONE;
	top.alwaysOnTop = true;
	top.alwaysOnTopOrder = 1;
	return tfs::tests::run();
}
