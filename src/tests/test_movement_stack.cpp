// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in LICENSE.
#include "../otpch.h"
#include "../game.h"
#include "../player.h"
#include "../protocolgame.h"
#include "../outputmessage.h"
#include "../tile.h"
#include "test_support.h"

struct ProtocolGameMovementTestAccess {
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
