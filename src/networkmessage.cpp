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

namespace {

constexpr uint8_t AstraItemFlagEquipable = 1 << 0;
constexpr uint8_t AstraItemFlagAmmo = 1 << 1;

bool isAstraItemMetadataEquipable(const ItemType& it)
{
	return it.weaponType != WEAPON_NONE || it.ammoType != AMMO_NONE || it.attack != 0 || it.defense != 0 ||
	       it.extraDefense != 0 || it.armor != 0 || (it.slotPosition & SLOTP_NECKLACE) != 0 ||
	       (it.slotPosition & SLOTP_RING) != 0 || (it.slotPosition & SLOTP_AMMO) != 0 ||
	       (it.slotPosition & SLOTP_FEET) != 0 || (it.slotPosition & SLOTP_HEAD) != 0 ||
	       (it.slotPosition & SLOTP_ARMOR) != 0 || (it.slotPosition & SLOTP_LEGS) != 0;
}

uint8_t getAstraItemMetadataFlags(const ItemType& it)
{
	uint8_t flags = 0;
	if (it.weaponType == WEAPON_AMMO) {
		flags |= AstraItemFlagAmmo;
	}
	if (isAstraItemMetadataEquipable(it)) {
		flags |= AstraItemFlagEquipable;
	}
	return flags;
}

void addAstraItemMetadata(NetworkMessage& msg, const ItemType& it)
{
	msg.add<uint16_t>(it.slotPosition);
	msg.addByte(getAstraItemMetadataFlags(it));
}

} // namespace

void NetworkMessage::addItem(uint16_t id, uint8_t count, bool sendTier, bool alwaysSendTier, bool sendQuickLootFlags,
                             bool sendAstraItemState, bool sendAstraQuiverCountU16)
{
	addItemId(id);

	const ItemType& it = Item::items[id];
	if (sendAstraQuiverCountU16 && it.weaponType == WEAPON_QUIVER) {
		// This overload only has the caller-provided subtype/count; real quiver ammo count is sent by the Item* overload.
		add<uint16_t>(count);
	} else if (it.stackable) {
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

	if (sendAstraItemState) {
		addByte(0); // no instance duration is available in the id/count overload
		addByte(0); // no instance charges are available in the id/count overload
		addAstraItemMetadata(*this, it);
	}
}

void NetworkMessage::addItem(const Item* item, bool sendTier, bool alwaysSendTier, bool sendQuiverCount,
                             bool sendQuickLootFlags, bool sendAstraItemState, bool sendAstraQuiverCountU16)
{
	addItemId(item->getID());

	const ItemType& it = Item::items[item->getID()];
	if ((sendQuiverCount || sendAstraQuiverCountU16) && item->getWeaponType() == WEAPON_QUIVER) {
		const Container* quiver = item->getContainer();
		const uint32_t ammoCount = quiver ? quiver->getAmmoCount() : 0;
		if (sendAstraQuiverCountU16) {
			add<uint16_t>(static_cast<uint16_t>(std::min<uint32_t>(0xFFFF, ammoCount)));
		} else {
			addByte(static_cast<uint8_t>(std::min<uint32_t>(0xFF, ammoCount)));
		}
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

	if (sendAstraItemState) {
		const bool hasVisualDuration = it.showDuration || it.wearOut || it.clockExpire || it.expire || it.expireStop;
		const bool hasDuration = hasVisualDuration && item->getDuration() > 0;
		addByte(hasDuration ? 1 : 0);
		if (hasDuration) {
			add<uint32_t>(static_cast<uint32_t>(std::max<int32_t>(0, item->getDuration()) / 1000));
			addByte(it.stopTime ? 1 : 0);
		}

		uint32_t charges = 0;
		if (it.charges != 0) {
			charges = item->getSubType();
		} else if (it.showCharges || item->hasAttribute(ITEM_ATTRIBUTE_CHARGES)) {
			charges = item->getCharges();
		}

		addByte(charges > 0 ? 1 : 0);
		if (charges > 0) {
			add<uint32_t>(charges);
			addByte((it.charges != 0 && charges == it.charges) ? 1 : 0);
		}

		addAstraItemMetadata(*this, it);
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
