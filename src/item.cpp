// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "item.h"

#include "actions.h"
#include "bed.h"
#include "scheduler.h"
#include "container.h"
#include "game.h"
#include "house.h"
#include "mailbox.h"
#include "rewardchest.h"
#include "spells.h"
#include "events.h"
#include "teleport.h"
#include "trashholder.h"

extern Game g_game;
extern Spells* g_spells;
extern Vocations g_vocations;
extern Events* g_events;

Items Item::items;

// Global registry to track valid Item pointers
// Fix: plain static avoids the 82KB "still reachable" block reported by Valgrind.
// The destructor runs automatically at program exit, properly releasing all bucket memory.
static std::unordered_set<Item*> g_validItems;

Item* Item::CreateItem(const uint16_t type, uint16_t count /*= 0*/)
{
	Item* newItem = nullptr;

	const ItemType& it = Item::items[type];
	if (it.group == ITEM_GROUP_DEPRECATED) {
		return nullptr;
	}

	if (it.stackable && count == 0) {
		count = 1;
	}

	if (it.id != 0) {
		if (it.isDepot()) {
			newItem = new DepotLocker(type);
		} else if (it.isRewardChest()) {
			newItem = new RewardChest(type);
		} else if (it.isContainer()) {
			newItem = new Container(type);
		} else if (it.isTeleport()) {
			newItem = new Teleport(type);
		} else if (it.isMagicField()) {
			newItem = new MagicField(type);
		} else if (it.isDoor()) {
			newItem = new Door(type);
		} else if (it.isTrashHolder()) {
			newItem = new TrashHolder(type);
		} else if (it.isMailbox()) {
			newItem = new Mailbox(type);
		} else if (it.isBed()) {
			newItem = new BedItem(type);
		} else if (it.id >= 2210 && it.id <= 2212) { // magic rings
			newItem = new Item(type - 3, count);
		} else if (it.id == 2215 || it.id == 2216) { // magic rings
			newItem = new Item(type - 2, count);
		} else if (it.id >= 2202 && it.id <= 2206) { // magic rings
			newItem = new Item(type - 37, count);
		} else if (it.id == 2640) { // soft boots
			newItem = new Item(6132, count);
		} else if (it.id == 6301) { // death ring
			newItem = new Item(6300, count);
		} else {
			newItem = new Item(type, count);
		}

		newItem->incrementReferenceCounter();
	}

	return newItem;
}

Container* Item::CreateItemAsContainer(const uint16_t type, uint16_t size)
{
	const ItemType& it = Item::items[type];
	if (it.id == 0 || it.group == ITEM_GROUP_DEPRECATED || it.stackable || it.useable || it.moveable || it.pickupable ||
	    it.isDepot() || it.isSplash() || it.isDoor()) {
		return nullptr;
	}

	Container* newItem = new Container(type, size);
	newItem->incrementReferenceCounter();
	return newItem;
}

Item* Item::CreateItem(PropStream& propStream)
{
	uint16_t id;
	if (!propStream.read<uint16_t>(id)) {
		return nullptr;
	}

	switch (id) {
		case ITEM_FIREFIELD_PVP_FULL:
			id = ITEM_FIREFIELD_PERSISTENT_FULL;
			break;

		case ITEM_FIREFIELD_PVP_MEDIUM:
			id = ITEM_FIREFIELD_PERSISTENT_MEDIUM;
			break;

		case ITEM_FIREFIELD_PVP_SMALL:
			id = ITEM_FIREFIELD_PERSISTENT_SMALL;
			break;

		case ITEM_ENERGYFIELD_PVP:
			id = ITEM_ENERGYFIELD_PERSISTENT;
			break;

		case ITEM_POISONFIELD_PVP:
			id = ITEM_POISONFIELD_PERSISTENT;
			break;

		case ITEM_MAGICWALL:
			id = ITEM_MAGICWALL_PERSISTENT;
			break;

		case ITEM_WILDGROWTH:
			id = ITEM_WILDGROWTH_PERSISTENT;
			break;

		default:
			break;
	}

	return Item::CreateItem(id, 0);
}

std::unique_ptr<Item> Item::CreateItemSafe(const uint16_t type, uint16_t count)
{
	return std::unique_ptr<Item>(CreateItem(type, count));
}
std::unique_ptr<Item> Item::CreateItemSafe(PropStream& propStream)
{
	return std::unique_ptr<Item>(CreateItem(propStream));
}

Item::Item(const uint16_t type, uint16_t count /*= 0*/) : id(type)
{
	g_validItems.insert(this);
	const ItemType& it = items[id];

	if (it.isFluidContainer() || it.isSplash()) {
		setFluidType(count);
	} else if (it.stackable) {
		if (count != 0) {
			setItemCount(static_cast<uint8_t>(count));
		} else if (it.charges != 0) {
			setItemCount(static_cast<uint8_t>(it.charges));
		}
	} else if (it.charges != 0) {
		if (count != 0) {
			setCharges(count);
		} else {
			setCharges(static_cast<uint16_t>(it.charges));
		}
	}

	if (it.imbuementSlot != 0) {
		addImbuementSlots(it.imbuementSlot);
	}

	setDefaultDuration();
}

Item::Item(const Item& i) : Thing(), id(i.id), count(i.count), loadedFromMap(i.loadedFromMap)
{
	g_validItems.insert(this);
	if (i.attributes) {
		attributes.reset(new ItemAttributes(*i.attributes));
	}
}

Item::~Item() { g_validItems.erase(this); }

bool isValidItemPointer(Item* item) { return item && g_validItems.count(item) > 0; }

Item* Item::clone() const
{
	Item* item = Item::CreateItem(id, count);
	if (attributes) {
		item->attributes.reset(new ItemAttributes(*attributes));
		if (item->getDuration() > 0) {
			item->incrementReferenceCounter();
			item->setDecaying(DECAYING_TRUE);
			g_game.toDecayItems.push_front(item);
		}
	}
	return item;
}

bool Item::equals(const Item* otherItem) const
{
	if (!otherItem || id != otherItem->id) {
		return false;
	}

	const auto& otherAttributes = otherItem->attributes;
	if (!attributes) {
		return !otherAttributes || (otherAttributes->attributeBits == 0);
	} else if (!otherAttributes) {
		return (attributes->attributeBits == 0);
	}

	if (attributes->attributeBits != otherAttributes->attributeBits) {
		return false;
	}

	const auto& attributeList = attributes->attributes;
	const auto& otherAttributeList = otherAttributes->attributes;
	for (const auto& attribute : attributeList) {
		if (ItemAttributes::isStrAttrType(attribute.type)) {
			for (const auto& otherAttribute : otherAttributeList) {
				if (attribute.type == otherAttribute.type && *attribute.value.string != *otherAttribute.value.string) {
					return false;
				}
			}
		} else {
			for (const auto& otherAttribute : otherAttributeList) {
				if (attribute.type == otherAttribute.type && attribute.value.integer != otherAttribute.value.integer) {
					return false;
				}
			}
		}
	}
	return true;
}

void Item::setDefaultSubtype()
{
	const ItemType& it = items[id];

	setItemCount(1);

	if (it.charges != 0) {
		if (it.stackable) {
			setItemCount(static_cast<uint8_t>(it.charges));
		} else {
			setCharges(static_cast<uint16_t>(it.charges));
		}
	}
}

void Item::onRemoved()
{
	ScriptEnvironment::removeTempItem(this);

	if (hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		g_game.removeUniqueItem(getUniqueId());
	}
}

void Item::setID(uint16_t newid)
{
	const ItemType& prevIt = Item::items[id];
	id = newid;

	const ItemType& it = Item::items[newid];
	uint32_t newDuration = normal_random(it.decayTimeMin, it.decayTimeMax) * 1000;

	if (newDuration == 0 && !it.stopTime && it.decayTo < 0) {
		// We'll get called startDecay anyway so let's schedule it - actually not in all casses
		if (hasAttribute(ITEM_ATTRIBUTE_DECAYSTATE)) {
			setDecaying(DECAYING_STOPPING);
		}
		removeAttribute(ITEM_ATTRIBUTE_DURATION);
	}

	removeAttribute(ITEM_ATTRIBUTE_CORPSEOWNER);

	if (newDuration > 0 && (!prevIt.stopTime || !hasAttribute(ITEM_ATTRIBUTE_DURATION))) {
		setDecaying(DECAYING_PENDING);
		setDuration(newDuration);
	}
}

Cylinder* Item::getTopParent()
{
	Cylinder* aux = getParent();
	Cylinder* prevaux = dynamic_cast<Cylinder*>(this);
	if (!aux) {
		return prevaux;
	}

	while (aux->getParent() != nullptr) {
		prevaux = aux;
		aux = aux->getParent();
	}

	if (prevaux) {
		return prevaux;
	}
	return aux;
}

const Cylinder* Item::getTopParent() const
{
	const Cylinder* aux = getParent();
	const Cylinder* prevaux = dynamic_cast<const Cylinder*>(this);
	if (!aux) {
		return prevaux;
	}

	while (aux->getParent() != nullptr) {
		prevaux = aux;
		aux = aux->getParent();
	}

	if (prevaux) {
		return prevaux;
	}
	return aux;
}

Tile* Item::getTile()
{
	Cylinder* cylinder = getTopParent();
	// get root cylinder
	if (cylinder && cylinder->getParent()) {
		cylinder = cylinder->getParent();
	}
	return dynamic_cast<Tile*>(cylinder);
}

const Tile* Item::getTile() const
{
	const Cylinder* cylinder = getTopParent();
	// get root cylinder
	if (cylinder && cylinder->getParent()) {
		cylinder = cylinder->getParent();
	}
	return dynamic_cast<const Tile*>(cylinder);
}

uint16_t Item::getSubType() const
{
	const ItemType& it = items[id];
	if (it.isFluidContainer() || it.isSplash()) {
		return getFluidType();
	} else if (it.stackable) {
		return count;
	} else if (it.charges != 0) {
		return getCharges();
	}
	return count;
}

const Player* Item::getHoldingPlayer() const
{
	const auto topParent = getTopParent();
	if (!topParent) {
		return nullptr;
	}

	if (const auto creature = topParent->getCreature()) {
		return creature->getPlayer();
	}
	return nullptr;
}

void Item::setSubType(uint16_t n)
{
	const ItemType& it = items[id];
	if (it.isFluidContainer() || it.isSplash()) {
		setFluidType(n);
	} else if (it.stackable) {
		setItemCount(n);
	} else if (it.charges != 0) {
		setCharges(n);
	} else {
		setItemCount(n);
	}
}

Attr_ReadValue Item::readAttr(AttrTypes_t attr, PropStream& propStream)
{
	switch (attr) {
		case ATTR_COUNT:
		case ATTR_RUNE_CHARGES: {
			uint8_t count;
			if (!propStream.read<uint8_t>(count)) {
				return ATTR_READ_ERROR;
			}

			setSubType(count);
			break;
		}

		case ATTR_ACTION_ID: {
			uint16_t actionId;
			if (!propStream.read<uint16_t>(actionId)) {
				return ATTR_READ_ERROR;
			}

			setActionId(actionId);
			break;
		}

		case ATTR_UNIQUE_ID: {
			uint16_t uniqueId;
			if (!propStream.read<uint16_t>(uniqueId)) {
				return ATTR_READ_ERROR;
			}

			setUniqueId(uniqueId);
			break;
		}

		case ATTR_TEXT: {
			auto [text, ok] = propStream.readString();
			if (!ok) {
				return ATTR_READ_ERROR;
			}

			setText(text);
			break;
		}

		case ATTR_WRITTENDATE: {
			uint32_t writtenDate;
			if (!propStream.read<uint32_t>(writtenDate)) {
				return ATTR_READ_ERROR;
			}

			setDate(writtenDate);
			break;
		}

		case ATTR_WRITTENBY: {
			auto [writer, ok] = propStream.readString();
			if (!ok) {
				return ATTR_READ_ERROR;
			}

			setWriter(writer);
			break;
		}

		case ATTR_DESC: {
			auto [text, ok] = propStream.readString();
			if (!ok) {
				return ATTR_READ_ERROR;
			}

			setSpecialDescription(text);
			break;
		}

		case ATTR_CHARGES: {
			uint16_t charges;
			if (!propStream.read<uint16_t>(charges)) {
				return ATTR_READ_ERROR;
			}

			setSubType(charges);
			break;
		}

		case ATTR_DURATION: {
			int32_t duration;
			if (!propStream.read<int32_t>(duration)) {
				return ATTR_READ_ERROR;
			}

			setDuration(duration);
			break;
		}

		case ATTR_DECAYING_STATE: {
			uint8_t state;
			if (!propStream.read<uint8_t>(state)) {
				return ATTR_READ_ERROR;
			}

			if (state != DECAYING_FALSE) {
				setDecaying(DECAYING_PENDING);
			}
			break;
		}

		case ATTR_NAME: {
			auto [name, ok] = propStream.readString();
			if (!ok) {
				return ATTR_READ_ERROR;
			}

			setStrAttr(ITEM_ATTRIBUTE_NAME, name);
			break;
		}

		case ATTR_ARTICLE: {
			auto [article, ok] = propStream.readString();
			if (!ok) {
				return ATTR_READ_ERROR;
			}

			setStrAttr(ITEM_ATTRIBUTE_ARTICLE, article);
			break;
		}

		case ATTR_PLURALNAME: {
			auto [pluralName, ok] = propStream.readString();
			if (!ok) {
				return ATTR_READ_ERROR;
			}

			setStrAttr(ITEM_ATTRIBUTE_PLURALNAME, pluralName);
			break;
		}

		case ATTR_WEIGHT: {
			uint32_t weight;
			if (!propStream.read<uint32_t>(weight)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_WEIGHT, weight);
			break;
		}

		case ATTR_ATTACK: {
			int32_t attack;
			if (!propStream.read<int32_t>(attack)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_ATTACK, attack);
			break;
		}

		case ATTR_ATTACK_SPEED: {
			uint32_t attackSpeed;
			if (!propStream.read<uint32_t>(attackSpeed)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_ATTACK_SPEED, attackSpeed);
			break;
		}

		case ATTR_IMBUESLOTS: {
			uint32_t slots;
			if (!propStream.read<uint32_t>(slots)) {
				return ATTR_READ_ERROR;
			}

			imbuementSlots = slots;
			break;
		}

		case ATTR_DEFENSE: {
			int32_t defense;
			if (!propStream.read<int32_t>(defense)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_DEFENSE, defense);
			break;
		}

		case ATTR_EXTRADEFENSE: {
			int32_t extraDefense;
			if (!propStream.read<int32_t>(extraDefense)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_EXTRADEFENSE, extraDefense);
			break;
		}

		case ATTR_ARMOR: {
			int32_t armor;
			if (!propStream.read<int32_t>(armor)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_ARMOR, armor);
			break;
		}

		case ATTR_HITCHANCE: {
			int8_t hitChance;
			if (!propStream.read<int8_t>(hitChance)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_HITCHANCE, hitChance);
			break;
		}

		case ATTR_SHOOTRANGE: {
			uint8_t shootRange;
			if (!propStream.read<uint8_t>(shootRange)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_SHOOTRANGE, shootRange);
			break;
		}

		case ATTR_WRAPID: {
			uint16_t wrapId;
			if (!propStream.read<uint16_t>(wrapId)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_WRAPID, wrapId);
			break;
		}

		case ATTR_STOREITEM: {
			uint8_t storeItem;
			if (!propStream.read<uint8_t>(storeItem)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_STOREITEM, storeItem);
			break;
		}

		case ATTR_REWARDID: {
			uint32_t rewardid;
			if (!propStream.read<uint32_t>(rewardid)) {
				return ATTR_READ_ERROR;
			}
			setIntAttr(ITEM_ATTRIBUTE_REWARDID, rewardid);
			break;
		}

		case ATTR_CLASSIFICATION: {
			uint32_t classification;
			if (!propStream.read<uint32_t>(classification)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_CLASSIFICATION, classification);
			break;
		}

		case ATTR_TIER: {
			uint32_t tier;
			if (!propStream.read<uint32_t>(tier)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_TIER, tier);
			break;
		}

		case ATTR_IMBUEMENTS: {
			uint16_t size;
			if (!propStream.read<uint16_t>(size)) {
				return ATTR_READ_ERROR;
			}

			for (uint16_t i = 0; i < size; ++i) {
				std::shared_ptr<Imbuement> imb = std::make_shared<Imbuement>();
				if (!imb->unserialize(propStream)) {
					return ATTR_READ_ERROR;
				}

				addImbuement(imb, false);
			}
			break;
		}

		case ATTR_REFLECT: {
			uint16_t size;
			if (!propStream.read<uint16_t>(size)) {
				return ATTR_READ_ERROR;
			}

			for (uint16_t i = 0; i < size; ++i) {
				CombatType_t combatType;
				Reflect reflect;

				if (!propStream.read<CombatType_t>(combatType) || !propStream.read<uint16_t>(reflect.percent) ||
				    !propStream.read<uint16_t>(reflect.chance)) {
					return ATTR_READ_ERROR;
				}

				getAttributes()->reflect[combatType] = reflect;
			}
			break;
		}

		case ATTR_BOOST: {
			uint16_t size;
			if (!propStream.read<uint16_t>(size)) {
				return ATTR_READ_ERROR;
			}

			for (uint16_t i = 0; i < size; ++i) {
				CombatType_t combatType;
				uint16_t percent;

				if (!propStream.read<CombatType_t>(combatType) || !propStream.read<uint16_t>(percent)) {
					return ATTR_READ_ERROR;
				}

				getAttributes()->boostPercent[combatType] = percent;
			}
			break;
		}

		// these should be handled through derived classes If these are called then something has changed in the
		// items.xml since the map was saved just read the values

		// Depot class
		case ATTR_DEPOT_ID: {
			if (!propStream.skip(2)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		// Door class
		case ATTR_HOUSEDOORID: {
			if (!propStream.skip(1)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		// Bed class
		case ATTR_SLEEPERGUID: {
			if (!propStream.skip(4)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		case ATTR_SLEEPSTART: {
			if (!propStream.skip(4)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		// Teleport class
		case ATTR_TELE_DEST: {
			if (!propStream.skip(5)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		// Container class
		case ATTR_CONTAINER_ITEMS: {
			return ATTR_READ_ERROR;
		}

		case ATTR_CUSTOM_ATTRIBUTES: {
			uint64_t size;
			if (!propStream.read<uint64_t>(size)) {
				return ATTR_READ_ERROR;
			}

			for (uint64_t i = 0; i < size; i++) {
				// Unserialize key type and value
				auto [key, ok] = propStream.readString();
				if (!ok) {
					return ATTR_READ_ERROR;
				};

				// Unserialize value type and value
				ItemAttributes::CustomAttribute val;
				if (!val.unserialize(propStream)) {
					return ATTR_READ_ERROR;
				}

				setCustomAttribute(key, val);
			}
			break;
		}

		default:
			return ATTR_READ_ERROR;
	}

	return ATTR_READ_CONTINUE;
}

bool Item::unserializeAttr(PropStream& propStream)
{
	uint8_t attr_type;
	while (propStream.read<uint8_t>(attr_type) && attr_type != 0) {
		Attr_ReadValue ret = readAttr(static_cast<AttrTypes_t>(attr_type), propStream);
		if (ret == ATTR_READ_ERROR) {
			return false;
		} else if (ret == ATTR_READ_END) {
			return true;
		}
	}
	return true;
}

bool Item::unserializeItemNode(OTB::Loader&, const OTB::Node&, PropStream& propStream)
{
	return unserializeAttr(propStream);
}

void Item::serializeAttr(PropWriteStream& propWriteStream) const
{
	const ItemType& it = items[id];
	if (it.stackable || it.isFluidContainer() || it.isSplash()) {
		propWriteStream.write<uint8_t>(ATTR_COUNT);
		propWriteStream.write<uint8_t>(getSubType());
	}

	uint16_t charges = getCharges();
	if (charges != 0) {
		propWriteStream.write<uint8_t>(ATTR_CHARGES);
		propWriteStream.write<uint16_t>(charges);
	}

	if (it.moveable) {
		uint16_t actionId = getActionId();
		if (actionId != 0) {
			propWriteStream.write<uint8_t>(ATTR_ACTION_ID);
			propWriteStream.write<uint16_t>(actionId);
		}
	}

	std::string_view text = getText();
	if (!text.empty()) {
		propWriteStream.write<uint8_t>(ATTR_TEXT);
		propWriteStream.writeString(text);
	}

	const time_t writtenDate = getDate();
	if (writtenDate != 0) {
		propWriteStream.write<uint8_t>(ATTR_WRITTENDATE);
		propWriteStream.write<uint32_t>(writtenDate);
	}

	std::string_view writer = getWriter();
	if (!writer.empty()) {
		propWriteStream.write<uint8_t>(ATTR_WRITTENBY);
		propWriteStream.writeString(writer);
	}

	std::string_view specialDesc = getSpecialDescription();
	if (!specialDesc.empty()) {
		propWriteStream.write<uint8_t>(ATTR_DESC);
		propWriteStream.writeString(specialDesc);
	}

	if (hasAttribute(ITEM_ATTRIBUTE_DURATION)) {
		propWriteStream.write<uint8_t>(ATTR_DURATION);
		propWriteStream.write<int32_t>(getDuration());
	}

	ItemDecayState_t decayState = getDecaying();
	if (decayState == DECAYING_TRUE || decayState == DECAYING_PENDING) {
		propWriteStream.write<uint8_t>(ATTR_DECAYING_STATE);
		propWriteStream.write<uint8_t>(decayState);
	}

	if (hasAttribute(ITEM_ATTRIBUTE_NAME)) {
		propWriteStream.write<uint8_t>(ATTR_NAME);
		propWriteStream.writeString(getStrAttr(ITEM_ATTRIBUTE_NAME));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_ARTICLE)) {
		propWriteStream.write<uint8_t>(ATTR_ARTICLE);
		propWriteStream.writeString(getStrAttr(ITEM_ATTRIBUTE_ARTICLE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_PLURALNAME)) {
		propWriteStream.write<uint8_t>(ATTR_PLURALNAME);
		propWriteStream.writeString(getStrAttr(ITEM_ATTRIBUTE_PLURALNAME));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_WEIGHT)) {
		propWriteStream.write<uint8_t>(ATTR_WEIGHT);
		propWriteStream.write<uint32_t>(getIntAttr(ITEM_ATTRIBUTE_WEIGHT));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_ATTACK)) {
		propWriteStream.write<uint8_t>(ATTR_ATTACK);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_ATTACK));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_ATTACK_SPEED)) {
		propWriteStream.write<uint8_t>(ATTR_ATTACK_SPEED);
		propWriteStream.write<uint32_t>(getIntAttr(ITEM_ATTRIBUTE_ATTACK_SPEED));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_CLASSIFICATION)) {
		propWriteStream.write<uint8_t>(ATTR_CLASSIFICATION);
		propWriteStream.write<uint32_t>(getIntAttr(ITEM_ATTRIBUTE_CLASSIFICATION));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_TIER)) {
		propWriteStream.write<uint8_t>(ATTR_TIER);
		propWriteStream.write<uint32_t>(getIntAttr(ITEM_ATTRIBUTE_TIER));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_REWARDID)) {
		propWriteStream.write<uint8_t>(ATTR_REWARDID);
		propWriteStream.write<uint32_t>(getIntAttr(ITEM_ATTRIBUTE_REWARDID));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_DEFENSE)) {
		propWriteStream.write<uint8_t>(ATTR_DEFENSE);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_DEFENSE));
	}

	if (getImbuementSlots() > 0) {
		propWriteStream.write<uint8_t>(ATTR_IMBUESLOTS);
		propWriteStream.write<uint32_t>(imbuementSlots);
	}

	if (!imbuements.empty()) {
		propWriteStream.write<uint8_t>(ATTR_IMBUEMENTS);
		propWriteStream.write<uint16_t>(static_cast<uint16_t>(imbuements.size()));
		for (const auto& imb : imbuements) {
			imb->serialize(propWriteStream);
		}
	}

	if (hasAttribute(ITEM_ATTRIBUTE_EXTRADEFENSE)) {
		propWriteStream.write<uint8_t>(ATTR_EXTRADEFENSE);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_EXTRADEFENSE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_ARMOR)) {
		propWriteStream.write<uint8_t>(ATTR_ARMOR);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_ARMOR));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_HITCHANCE)) {
		propWriteStream.write<uint8_t>(ATTR_HITCHANCE);
		propWriteStream.write<int8_t>(getIntAttr(ITEM_ATTRIBUTE_HITCHANCE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_SHOOTRANGE)) {
		propWriteStream.write<uint8_t>(ATTR_SHOOTRANGE);
		propWriteStream.write<uint8_t>(getIntAttr(ITEM_ATTRIBUTE_SHOOTRANGE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_WRAPID)) {
		propWriteStream.write<uint8_t>(ATTR_WRAPID);
		propWriteStream.write<uint16_t>(static_cast<uint16_t>(getIntAttr(ITEM_ATTRIBUTE_WRAPID)));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_CUSTOM)) {
		const ItemAttributes::CustomAttributeMap* customAttrMap = attributes->getCustomAttributeMap();
		propWriteStream.write<uint8_t>(ATTR_CUSTOM_ATTRIBUTES);
		propWriteStream.write<uint64_t>(static_cast<uint64_t>(customAttrMap->size()));
		for (const auto& entry : *customAttrMap) {
			// Serializing key type and value
			propWriteStream.writeString(entry.first);

			// Serializing value type and value
			entry.second.serialize(propWriteStream);
		}
	}
}

bool Item::hasProperty(ITEMPROPERTY prop) const
{
	const ItemType& it = items[id];
	switch (prop) {
		case CONST_PROP_BLOCKSOLID:
			return it.blockSolid;
		case CONST_PROP_MOVEABLE:
			return it.moveable && !hasAttribute(ITEM_ATTRIBUTE_UNIQUEID);
		case CONST_PROP_HASHEIGHT:
			return it.hasHeight;
		case CONST_PROP_BLOCKPROJECTILE:
			return it.blockProjectile;
		case CONST_PROP_BLOCKPATH:
			return it.blockPathFind;
		case CONST_PROP_ISVERTICAL:
			return it.isVertical;
		case CONST_PROP_ISHORIZONTAL:
			return it.isHorizontal;
		case CONST_PROP_IMMOVABLEBLOCKSOLID:
			return it.blockSolid && (!it.moveable || hasAttribute(ITEM_ATTRIBUTE_UNIQUEID));
		case CONST_PROP_IMMOVABLEBLOCKPATH:
			return it.blockPathFind && (!it.moveable || hasAttribute(ITEM_ATTRIBUTE_UNIQUEID));
		case CONST_PROP_IMMOVABLENOFIELDBLOCKPATH:
			return !it.isMagicField() && it.blockPathFind && (!it.moveable || hasAttribute(ITEM_ATTRIBUTE_UNIQUEID));
		case CONST_PROP_NOFIELDBLOCKPATH:
			return !it.isMagicField() && it.blockPathFind;
		case CONST_PROP_SUPPORTHANGABLE:
			return it.isHorizontal || it.isVertical;
		default:
			return false;
	}
}

uint32_t Item::getWeight() const
{
	uint32_t weight = getBaseWeight();
	if (isStackable()) {
		return weight * std::max<uint32_t>(1, getItemCount());
	}
	return weight;
}

std::string Item::getNameDescription(const ItemType& it, const Item* item /*= nullptr*/, int32_t subType /*= -1*/,
                                     bool addArticle /*= true*/)
{
	if (item) {
		subType = item->getSubType();
	}

	std::ostringstream s;

	auto name = (item ? item->getName() : it.name);
	if (!name.empty()) {
		if (it.stackable && subType > 1) {
			if (it.showCount) {
				s << subType << ' ';
			}

			s << (item ? item->getPluralName() : it.getPluralName());
		} else {
			if (addArticle) {
				auto article = (item ? item->getArticle() : it.article);
				if (!article.empty()) {
					s << article << ' ';
				}
			}

			s << name;
		}
	} else {
		if (addArticle) {
			s << "an ";
		}
		s << "item of type " << it.id;
	}
	return s.str();
}

std::string Item::getNameDescription() const
{
	const ItemType& it = items[id];
	return getNameDescription(it, this);
}

std::string Item::getWeightDescription(const ItemType& it, uint32_t weight, uint32_t count /*= 1*/)
{
	std::ostringstream ss;
	if (it.stackable && count > 1 && it.showCount != 0) {
		ss << "They weigh ";
	} else {
		ss << "It weighs ";
	}

	if (weight < 10) {
		ss << "0.0" << weight;
	} else if (weight < 100) {
		ss << "0." << weight;
	} else {
		std::string weightString = std::to_string(weight);
		weightString.insert(weightString.end() - 2, '.');
		ss << weightString;
	}

	ss << " oz.";
	return ss.str();
}

std::string Item::getWeightDescription(uint32_t weight) const
{
	const ItemType& it = Item::items[id];
	return getWeightDescription(it, weight, getItemCount());
}

std::string Item::getWeightDescription() const
{
	uint32_t weight = getWeight();
	if (weight == 0) {
		return {};
	}
	return getWeightDescription(weight);
}

void Item::setUniqueId(uint16_t n)
{
	if (hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		return;
	}

	if (g_game.addUniqueItem(n, this)) {
		getAttributes()->setUniqueId(n);
	}
}

void Item::setDefaultDuration()
{
	uint32_t duration = getDefaultDurationMin();
	if (uint32_t durationMax = getDefaultDurationMax()) {
		duration = normal_random(duration, durationMax);
	}

	if (duration != 0) {
		setDuration(duration);
	}
}

bool Item::canDecay() const
{
	if (isRemoved()) {
		return false;
	}

	if (items[id].decayTo < 0 || (getDecayTimeMin() == 0 && getDecayTimeMax() == 0)) {
		return false;
	}

	if (hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		return false;
	}

	return true;
}

uint32_t Item::getWorth() const { return items[id].worth * count; }

LightInfo Item::getLightInfo() const
{
	const ItemType& it = items[id];
	return {it.lightLevel, it.lightColor};
}

Reflect Item::getReflect(CombatType_t combatType, bool total /* = true */) const
{
	const ItemType& it = Item::items[id];

	Reflect reflect;
	if (attributes) {
		reflect += attributes->getReflect(combatType);
	}

	if (total && it.abilities) {
		reflect += it.abilities->reflect[combatTypeToIndex(combatType)];
	}

	return reflect;
}

uint16_t Item::getBoostPercent(CombatType_t combatType, bool total /* = true */) const
{
	const ItemType& it = Item::items[id];

	uint16_t boostPercent = 0;
	if (attributes) {
		boostPercent += attributes->getBoostPercent(combatType);
	}

	if (total && it.abilities) {
		boostPercent += it.abilities->boostPercent[combatTypeToIndex(combatType)];
	}

	return boostPercent;
}

std::string ItemAttributes::emptyString;
int64_t ItemAttributes::emptyInt;
double ItemAttributes::emptyDouble;
bool ItemAttributes::emptyBool;
Reflect ItemAttributes::emptyReflect;

std::string_view ItemAttributes::getStrAttr(itemAttrTypes type) const
{
	if (!isStrAttrType(type)) {
		return "";
	}

	const Attribute* attr = getExistingAttr(type);
	if (!attr) {
		return "";
	}
	return {attr->value.string->data(), attr->value.string->size()};
}

void ItemAttributes::setStrAttr(itemAttrTypes type, std::string_view value)
{
	if (!isStrAttrType(type)) {
		return;
	}

	if (value.empty()) {
		return;
	}

	Attribute& attr = getAttr(type);
	delete attr.value.string;
	attr.value.string = new std::string(value);
}

void ItemAttributes::removeAttribute(itemAttrTypes type)
{
	if (!hasAttribute(type)) {
		return;
	}

	auto prev_it = attributes.rbegin();
	if ((*prev_it).type == type) {
		attributes.pop_back();
	} else {
		auto it = prev_it, end = attributes.rend();
		while (++it != end) {
			if ((*it).type == type) {
				(*it) = attributes.back();
				attributes.pop_back();
				break;
			}
		}
	}
	attributeBits &= ~type;
}

int64_t ItemAttributes::getIntAttr(itemAttrTypes type) const
{
	if (!isIntAttrType(type)) {
		return 0;
	}

	const Attribute* attr = getExistingAttr(type);
	if (!attr) {
		return 0;
	}
	return attr->value.integer;
}

void ItemAttributes::setIntAttr(itemAttrTypes type, int64_t value)
{
	if (!isIntAttrType(type)) {
		return;
	}

	if (type == ITEM_ATTRIBUTE_ATTACK_SPEED && value < 100) {
		value = 100;
	}

	getAttr(type).value.integer = value;
}

void ItemAttributes::increaseIntAttr(itemAttrTypes type, int64_t value) { setIntAttr(type, getIntAttr(type) + value); }

const ItemAttributes::Attribute* ItemAttributes::getExistingAttr(itemAttrTypes type) const
{
	if (hasAttribute(type)) {
		for (const Attribute& attribute : attributes) {
			if (attribute.type == type) {
				return &attribute;
			}
		}
	}
	return nullptr;
}

ItemAttributes::Attribute& ItemAttributes::getAttr(itemAttrTypes type)
{
	if (hasAttribute(type)) {
		for (Attribute& attribute : attributes) {
			if (attribute.type == type) {
				return attribute;
			}
		}
	}

	attributeBits |= type;
	attributes.emplace_back(type);
	return attributes.back();
}

void Item::startDecaying() { g_game.startDecay(this); }

template <>
const std::string& ItemAttributes::CustomAttribute::get<std::string>()
{
	if (value.type() == typeid(std::string)) {
		return boost::get<std::string>(value);
	}

	return emptyString;
}

template <>
const int64_t& ItemAttributes::CustomAttribute::get<int64_t>()
{
	if (value.type() == typeid(int64_t)) {
		return boost::get<int64_t>(value);
	}

	return emptyInt;
}

template <>
const double& ItemAttributes::CustomAttribute::get<double>()
{
	if (value.type() == typeid(double)) {
		return boost::get<double>(value);
	}

	return emptyDouble;
}

template <>
const bool& ItemAttributes::CustomAttribute::get<bool>()
{
	if (value.type() == typeid(bool)) {
		return boost::get<bool>(value);
	}

	return emptyBool;
}

bool Item::isEquipped() const {
	const Player* player = getHoldingPlayer();
	if (player) {
		for (uint32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; slot++) {
			if (player->getInventoryItem(slot) == this) {
				return true;
			}
		}
	}
	return false;
}

uint16_t Item::getImbuementSlots() const
{
	// item:getImbuementSlots() -- returns how many total slots
	return imbuementSlots;
}

uint16_t Item::getFreeImbuementSlots() const
{
	const auto used = static_cast<uint16_t>(imbuements.size());
	if (used >= imbuementSlots) {
		return 0;
	}
	return imbuementSlots - used;
}

bool Item::canImbue() const
{
	// item:canImbue() -- returns true if item has slots that are free
	return (imbuementSlots > 0 && imbuementSlots > imbuements.size()) ? true : false;
}

bool Item::addImbuementSlots(const uint16_t amount)
{
	// item:addImbuementSlots(amount) -- tries to add imbuement slot, returns true if successful
	constexpr uint16_t limit = std::numeric_limits<uint16_t>::max(); // uint16_t size limit
	const uint16_t currentSlots = static_cast<uint16_t>(imbuements.size());

	if ((currentSlots + amount) >= limit)
	{
		std::cout << "Warning in call to Item:addImbuementSlots(). Total added would be more than supported memory limit!" << std::endl;
		return false;
	}

	imbuementSlots += amount;
	return true;
}

bool Item::removeImbuementSlots(const uint16_t amount, const bool destroyImbues)
{
	if (amount == 0 || amount > imbuementSlots) {
		return false;
	}

	const uint16_t newSlotCount = imbuementSlots - amount;
	const auto activeCount = static_cast<uint16_t>(imbuements.size());

	if (activeCount > newSlotCount) {
		if (!destroyImbues) {
			// Not enough free slots, and we can't destroy
			return false;
		}
		// Destroy excess imbuements from the end
		const uint16_t excess = activeCount - newSlotCount;
		for (uint16_t i = 0; i < excess; ++i) {
			if (!imbuements.empty()) {
				removeImbuement(imbuements.back(), false);
			}
		}
	}

	imbuementSlots = newSlotCount;
	return true;
}

bool Item::hasImbuementType(const ImbuementType imbuetype) const
{
	// item:hasImbuementType(type)
	return std::any_of(imbuements.begin(), imbuements.end(), [imbuetype](std::shared_ptr<Imbuement> elem) {
		return elem->imbuetype == imbuetype;
		});
}

bool Item::hasImbuement(const std::shared_ptr<Imbuement>& imbuement) const
{
	// item:hasImbuement(imbuement)
	return std::any_of(imbuements.begin(), imbuements.end(), [&imbuement](const std::shared_ptr<Imbuement>& elem) {
		return elem == imbuement;
		});
}


bool Item::hasImbuements() const
{
	// item:hasImbuements() -- returns true if item has any imbuements
	return imbuements.size() > 0;
}

// Maps category ID (from imbuements.xml) to the key string used in items.xml
static const char* getCategoryKey(uint16_t categoryId) {
	switch (categoryId) {
		case 0:  return "elemental damage";
		case 1:  return "life leech";
		case 2:  return "mana leech";
		case 3:  return "critical hit";
		case 4:  return "elemental protection death";
		case 5:  return "elemental protection earth";
		case 6:  return "elemental protection fire";
		case 7:  return "elemental protection ice";
		case 8:  return "elemental protection energy";
		case 9:  return "elemental protection holy";
		case 10: return "increase speed";
		case 11: return "skillboost axe";
		case 12: return "skillboost sword";
		case 13: return "skillboost club";
		case 14: return "skillboost shielding";
		case 15: return "skillboost distance";
		case 16: return "skillboost magic level";
		case 17: return "increase capacity";
		case 18: return "skillboost fist";
		case 19: return "paralysis removal";
		default: return nullptr;
	}
}

bool Item::canApplyImbuement(uint16_t categoryId, uint8_t tier) const
{
	const ItemType& it = items[id];
	if (it.imbuementAllowedTypes.empty()) {
		return false;
	}

	const char* key = getCategoryKey(categoryId);
	if (!key) {
		return false;
	}

	auto found = it.imbuementAllowedTypes.find(key);
	if (found == it.imbuementAllowedTypes.end()) {
		return false;
	}

	return tier <= found->second;
}

bool Item::addImbuement(std::shared_ptr<Imbuement>  imbuement, bool created)
{
	// item:addImbuement(imbuement) -- returns true if it successfully adds the imbuement
	if (canImbue() && getFreeImbuementSlots() > 0 && g_events->eventItemOnImbue(this, imbuement, created))
	{
		imbuements.push_back(imbuement);
		for (auto imbue : imbuements) {
			if (imbue == imbuement) {
				Player* player = const_cast<Player*>(getHoldingPlayer());
				if (player && isEquipped()) {
					player->addImbuementEffect(imbue);
				}
			}
		}
		return true;
	}
	return false;
}

bool Item::removeImbuement(std::shared_ptr<Imbuement> imbuement, bool decayed)
{
	// item:removeImbuement(imbuement) -- returns true if it found and removed the imbuement
	for (auto imbue : imbuements) {
		if (imbue == imbuement) {
			Player* player = const_cast<Player*>(getHoldingPlayer());
			if (player && isEquipped()) {
				player->removeImbuementEffect(imbue);
			}
			g_events->eventItemOnRemoveImbue(this, imbuement->imbuetype, decayed);
			imbuements.erase(std::remove(imbuements.begin(), imbuements.end(), imbue), imbuements.end());
			return true;
		}
	}
	return false;
}

std::vector<std::shared_ptr<Imbuement>>& Item::getImbuements() {
	return imbuements;
}

void Item::decayImbuements(bool infight) {
	std::vector<std::shared_ptr<Imbuement>> expired;
	for (auto& imbue : imbuements) {
		if (imbue->isEquipDecay()) {
			if (imbue->duration > 0) {
				imbue->duration -= 1;
			}
			if (imbue->duration == 0) {
				expired.push_back(imbue);
				continue;
			}
		}
		if (imbue->isInfightDecay() && infight) {
			if (imbue->duration > 0) {
				imbue->duration -= 1;
			}
			if (imbue->duration == 0) {
				expired.push_back(imbue);
			}
		}
	}
	for (auto& imbue : expired) {
		removeImbuement(imbue, true);
	}
}

void Item::stopDecaying() { g_game.stopDecay(this); }