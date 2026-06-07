// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "networkmessage.h"

#include "configmanager.h"
#include "container.h"
#include "creature.h"
#include "lockfree.h"

#include <simdutf.h>

std::string NetworkMessage::getString(uint16_t stringLen /* = 0*/)
{
	if (stringLen == 0) {
		stringLen = get<uint16_t>();
	}

	if (!canRead(stringLen)) {
		return {};
	}

	auto it = reinterpret_cast<char*>(buffer.data() + info.position);
	info.position += stringLen;

	const auto outLen = simdutf::utf8_length_from_latin1(it, stringLen);
	std::string out(outLen, '\0');
	const auto writtenLen = simdutf::convert_latin1_to_utf8(it, stringLen, out.data());
	out.resize(writtenLen);
	return out;
}

Position NetworkMessage::getPosition()
{
	Position pos;
	pos.x = get<uint16_t>();
	pos.y = get<uint16_t>();
	pos.z = getByte();
	return pos;
}

void NetworkMessage::addString(std::string_view value)
{
	const auto stringLen = simdutf::latin1_length_from_utf8(value.data(), value.size());
	if (!canAdd(stringLen + 2) || stringLen > 8192) {
		return;
	}

	const auto startPosition = info.position;
	const auto startLength = info.length;
	add<uint16_t>(static_cast<uint16_t>(stringLen));
	auto it = reinterpret_cast<char*>(buffer.data() + info.position);
	const auto writtenLen = simdutf::convert_utf8_to_latin1(value.data(), value.size(), it);
	if (writtenLen != stringLen) {
		info.position = startPosition;
		info.length = startLength;
		return;
	}

	info.position += writtenLen;
	info.length += writtenLen;
}

void NetworkMessage::addDouble(double value, uint8_t precision /* = 2*/)
{
	addByte(precision);
	add<uint32_t>(static_cast<uint32_t>((value * std::pow(static_cast<float>(10), precision)) +
	                                    std::numeric_limits<int32_t>::max()));
}

void NetworkMessage::addBytes(const char* bytes, size_t size)
{
	if (!canAdd(size) || size > 8192) {
		return;
	}

	std::memcpy(buffer.data() + info.position, bytes, size);
	info.position += size;
	info.length += size;
}

void NetworkMessage::addPaddingBytes(size_t n)
{
	if (!canAdd(n)) {
		return;
	}

	std::fill_n(buffer.data() + info.position, n, 0x33);
	info.length += n;
}

void NetworkMessage::addPosition(const Position& pos)
{
	add<uint16_t>(pos.x);
	add<uint16_t>(pos.y);
	addByte(pos.z);
}

void NetworkMessage::addItemId(uint16_t itemId)
{
	const ItemType& it = Item::items[itemId];
	uint16_t clientId = it.id;

	add<uint16_t>(clientId);
}

void NetworkMessage::addItem(uint16_t id, uint8_t count, bool sendTier, bool alwaysSendTier, bool sendQuickLootFlags)
{
	addItemId(id);

	const ItemType& it = Item::items[id];
	if (it.stackable) {
		addByte(count);
	} else if (it.isSplash() || it.isFluidContainer()) {
		addByte(fluidMap[count & 7]);
	}

	if (sendQuickLootFlags && it.isContainer()) {
		addByte(0);
	}

	if (sendTier && ConfigManager::getBoolean(ConfigManager::ITEM_TIER_DISPLAY) &&
	    (alwaysSendTier || (ConfigManager::getBoolean(ConfigManager::ITEM_UPGRADE_CLASSIFICATION) && it.classification > 0))) {
		addByte(static_cast<uint8_t>(it.tier));
	}
}

void NetworkMessage::addItem(const Item* item, bool sendTier, bool alwaysSendTier, bool sendQuiverCount,
                             bool sendQuickLootFlags)
{
	addItemId(item->getID());

	const ItemType& it = Item::items[item->getID()];
	if (sendQuiverCount && item->getWeaponType() == WEAPON_QUIVER) {
		const Container* quiver = item->getContainer();
		addByte(static_cast<uint8_t>(std::min<uint32_t>(0xFF, quiver ? quiver->getAmmoCount() : 0)));
	} else if (it.stackable) {
		addByte(static_cast<uint8_t>(std::min<uint16_t>(0xFF, item->getItemCount())));
	} else if (it.isSplash() || it.isFluidContainer()) {
		addByte(fluidMap[item->getFluidType() & 7]);
	}

	if (sendQuickLootFlags && it.isContainer()) {
		addByte(0);
	}

	if (sendTier && ConfigManager::getBoolean(ConfigManager::ITEM_TIER_DISPLAY) &&
	    (alwaysSendTier || (ConfigManager::getBoolean(ConfigManager::ITEM_UPGRADE_CLASSIFICATION) && it.classification > 0))) {
		addByte(item->getTier());
	}
}

namespace {

const uint16_t NETWORKMESSAGE_FREE_LIST_CAPACITY = 2048;

} // namespace

std::shared_ptr<NetworkMessage> tfs::net::make_network_message()
{
	return std::allocate_shared<NetworkMessage>(LockfreePoolingAllocator<void, NETWORKMESSAGE_FREE_LIST_CAPACITY>());
}

std::shared_ptr<NetworkMessage> tfs::net::make_network_message(const NetworkMessage& other)
{
	return std::allocate_shared<NetworkMessage>(LockfreePoolingAllocator<void, NETWORKMESSAGE_FREE_LIST_CAPACITY>(),
	                                            other);
}
