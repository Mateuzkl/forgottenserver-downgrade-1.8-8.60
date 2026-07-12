#include "../otpch.h"

#include "../container.h"
#include "../item.h"
#include "../networkmessage.h"

#include "test_support.h"
#include <memory>

extern bool isValidItemPointer(Item* item);

namespace {

class ItemTypeGuard
{
public:
	ItemTypeGuard(ItemType& type) : type(type), id(type.id), group(type.group) {}
	~ItemTypeGuard()
	{
		type.id = id;
		type.group = group;
	}

private:
	ItemType& type;
	uint16_t id;
	itemgroup_t group;
};

void configureContainerType(ItemType& type)
{
	type.id = 0x1234;
	type.group = ITEM_GROUP_CONTAINER;
}

void checkSerializedQuickLootMarker(const NetworkMessage& message)
{
	CHECK(message.getLength() == sizeof(uint16_t) + sizeof(uint8_t));
	CHECK(message.getBuffer()[NetworkMessage::INITIAL_BUFFER_POSITION + sizeof(uint16_t)] == 0);
}

} // namespace

TEST_CASE(item_lifetime_registry_tracks_destroyed_item)
{
	Item* rawItem = nullptr;
	{
		auto item = std::make_shared<Item>(0);
		rawItem = item.get();
		CHECK(isValidItemPointer(rawItem));
	}

	CHECK(!isValidItemPointer(rawItem));
}

TEST_CASE(network_message_add_item_id_writes_quickloot_marker)
{
	ItemType& type = Item::items.getItemType(0);
	ItemTypeGuard guard(type);
	configureContainerType(type);

	NetworkMessage message;
	message.addItem(static_cast<uint16_t>(0), static_cast<uint8_t>(1), false, false, true);
	checkSerializedQuickLootMarker(message);
}

TEST_CASE(network_message_add_item_pointer_writes_quickloot_marker)
{
	ItemType& type = Item::items.getItemType(0);
	ItemTypeGuard guard(type);
	configureContainerType(type);

	Container container(0, 8);
	NetworkMessage message;
	message.addItem(&container, false, false, false, true);
	checkSerializedQuickLootMarker(message);
}

TEST_CASE(network_message_omits_quickloot_marker_when_feature_disabled)
{
	ItemType& type = Item::items.getItemType(0);
	ItemTypeGuard guard(type);
	configureContainerType(type);

	NetworkMessage message;
	message.addItem(static_cast<uint16_t>(0), static_cast<uint8_t>(1), false, false, false);
	CHECK(message.getLength() == sizeof(uint16_t));
}

TFS_TEST_MAIN()
