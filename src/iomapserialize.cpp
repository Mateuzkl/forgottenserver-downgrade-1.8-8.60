// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "iomapserialize.h"

#include "bed.h"
#include "game.h"
#include "tools.h"
#include "logger.h"
#include <fmt/format.h>
#include "stats.h"

extern Game g_game;

namespace {
bool mapSerializeCylinderOwnsThing(const Cylinder* cylinder, const Thing* thing)
{
	return cylinder && thing && thing->getParent() == cylinder && cylinder->getThingIndex(thing) != -1;
}

std::array<uint16_t, 4> getBedTransformIds(const ItemType& itemType)
{
	if (!itemType.isBed()) {
		return {};
	}

	return {itemType.id, itemType.transformToFree, itemType.transformToOnUse[PLAYERSEX_FEMALE],
	        itemType.transformToOnUse[PLAYERSEX_MALE]};
}

std::string_view persistentFixtureTypeName(const ItemType& itemType)
{
	if (itemType.isBed()) {
		return "bed";
	}
	if (itemType.isDoor()) {
		return "door";
	}
	return "static";
}

void logStaleHouseFixture(const Tile* tile, uint16_t mapId, uint16_t persistedId, std::string_view fixtureType)
{
	if (!tile) {
		return;
	}

	const Position& position = tile->getPosition();
	g_logger().warn("[HousePersistence] stale fixture ignored: position={},{},{} mapId={} persistedId={} type={} "
	                "reason=different-family action=keep-map-fixture",
	                position.x, position.y, position.z, mapId, persistedId, fixtureType);
}

Container* findReplacementStaticContainer(Tile* tile)
{
	Container* replacement = nullptr;
	const TileItemVector* items = tile ? tile->getItemList() : nullptr;
	if (!items) {
		return nullptr;
	}

	for (const auto& item : *items) {
		const ItemType& itemType = Item::items[item->getID()];
		Container* candidate = item->getContainer();
		if (!candidate || itemType.moveable || itemType.forceSerialize) {
			continue;
		}
		if (replacement) {
			return nullptr;
		}
		replacement = candidate;
	}
	return replacement;
}

void transferContainerContents(Container* source, Container* destination)
{
	if (!source || !destination) {
		return;
	}

	const ItemVector contents = source->getItems();
	for (const auto& item : contents) {
		if (destination->size() >= destination->capacity()) {
			break;
		}
		if (!item || source->getThingIndex(item.get()) == -1) {
			continue;
		}

		source->removeThing(item.get(), item->getItemCount());
		destination->internalAddThing(item.get());
		if (!mapSerializeCylinderOwnsThing(destination, item.get())) {
			// Keep ownership in the temporary container so the caller can try a
			// fallback destination without losing the item.
			source->internalAddThing(item.get());
		}
	}
}
} // namespace

bool IOMapSerialize::isSamePersistentFixtureFamily(const ItemType& mapType, const ItemType& persistedType)
{
	if (mapType.id == 0 || persistedType.id == 0) {
		return false;
	}
	if (mapType.id == persistedType.id) {
		return true;
	}

	if (mapType.isBed() && persistedType.isBed()) {
		const auto mapTransforms = getBedTransformIds(mapType);
		const auto persistedTransforms = getBedTransformIds(persistedType);
		return std::ranges::any_of(mapTransforms, [&](uint16_t mapId) {
			return mapId != 0 && std::ranges::find(persistedTransforms, mapId) != persistedTransforms.end();
		});
	}

	if (mapType.isDoor() && persistedType.isDoor()) {
		return mapType.persistentTransformFamily != 0 &&
		       mapType.persistentTransformFamily == persistedType.persistentTransformFamily;
	}

	return false;
}

void IOMapSerialize::loadHouseItems(Map* map)
{
    AutoStat stat("loadHouseItems", "full");
    int64_t start = OTSYS_TIME();
    
    DBResult_ptr result = Database::getInstance().storeQuery(
        "SELECT `house_id`, `data` FROM `tile_store` ORDER BY `house_id`"
    );
    
    if (!result) {
        return;
    }
    
    size_t tileCount = 0;
    size_t itemCount = 0;
    
    {
        AutoStat statParse("loadHouseItems", "parse_tiles");
        
        do {
            auto attr = result->getString("data");
            PropStream propStream;
            propStream.init(attr.data(), attr.size());
            
            uint16_t x, y;
            uint8_t z;
            if (!propStream.read<uint16_t>(x) || !propStream.read<uint16_t>(y) || !propStream.read<uint8_t>(z)) {
                continue;
            }
            
            Tile* tile = map->getTile(x, y, z);
            if (!tile) {
                continue;
            }
            
            uint32_t item_count;
            if (!propStream.read<uint32_t>(item_count)) {
                continue;
            }
            
            tileCount++;
            itemCount += item_count;
            
            while (item_count--) {
                loadItem(propStream, tile);
            }
        } while (result->next());
    }
    
    LOG_INFO(fmt::format(">> Loaded house items in: [\033[1;33m{:.3f}\033[0m] s ([\033[1;33m{}\033[0m] tiles, [\033[1;33m{}\033[0m] items)",
                         (OTSYS_TIME() - start) / 1000., tileCount, itemCount));
}

bool IOMapSerialize::saveHouseItems()
{
	AutoStat stat("saveHouseItems", "full");
	int64_t start = OTSYS_TIME();
	Database& db = Database::getInstance();

	// Start the transaction
	DBTransaction transaction;
	if (!transaction.begin()) {
		return false;
	}

	// clear old tile data
	if (!db.executeQuery("DELETE FROM `tile_store`")) {
		return false;
	}

	std::ostringstream query;
	query << "INSERT INTO `tile_store` (`house_id`, `data`) VALUES ";

	bool notEmpty = false;
	{
		AutoStat statBuild("saveHouseItems", "build_query");
		PropWriteStream stream;
		for (const auto& it : g_game.map.houses.getHouses()) {
			// save house items
			House* house = it.second.get();
			for (const auto& weakTile : house->getTiles()) {
				if (auto tile = weakTile.lock()) {
					saveTile(stream, tile.get());

					if (auto attributes = stream.getStream(); !attributes.empty()) {
						if (notEmpty) {
							query << ",";
						}
						query << "(" << house->getId() << "," << db.escapeString(attributes) << ")";
						notEmpty = true;
						stream.clear();
					}
				}
			}
		}
	}

	if (notEmpty) {
		AutoStat statExec("saveHouseItems", "execute_query");
		if (!db.executeQuery(query.str())) {
			return false;
		}
	}

	// End the transaction
	bool success = transaction.commit();
	LOG_INFO(fmt::format(">> Saved house items in: \033[1;33m{:.3f}\033[0m s", (OTSYS_TIME() - start) / 1000.));
	return success;
}

bool IOMapSerialize::loadContainer(PropStream& propStream, Container* container)
{
	while (container->serializationCount > 0) {
		if (!loadItem(propStream, container)) {
			LOG_WARN(fmt::format("[Warning - IOMapSerialize::loadContainer] Unserialization error for container item: {}", container->getID()));
			return false;
		}
		container->serializationCount--;
	}

	uint8_t endAttr;
	if (!propStream.read<uint8_t>(endAttr) || endAttr != 0) {
		LOG_WARN(fmt::format("[Warning - IOMapSerialize::loadContainer] Unserialization error for container item: {}", container->getID()));
		return false;
	}
	return true;
}

bool IOMapSerialize::loadItem(PropStream& propStream, Cylinder* parent)
{
	uint16_t id;
	if (!propStream.read<uint16_t>(id)) {
		return false;
	}

	Tile* tile = nullptr;
	if (parent->getParent() == nullptr) {
		tile = parent->getTile();
	}

	const ItemType& iType = Item::items[id];
	if (iType.moveable || iType.forceSerialize || !tile) {
		// create a new item
		auto item = Item::CreateItem(id);
		if (item) {
			if (item->unserializeAttr(propStream)) {
				Container* container = item->getContainer();
				if (container && !loadContainer(propStream, container)) {
					return false;
				}

				Item* raw = item.get();
				parent->internalAddThing(raw);
				if (!mapSerializeCylinderOwnsThing(parent, raw)) {
					return false;
				}

				raw->startDecaying();
				item.reset();
			} else {
				LOG_WARN(fmt::format("WARNING: Unserialization error in IOMapSerialize::loadItem() {}", id));
				return false;
			}
		}
	} else {
		// Stationary items like doors/beds/blackboards/bookcases. Exact identity
		// always wins; a transformed state is accepted only with family proof.
		Item* staticItem = nullptr;
		bool exactMatch = false;
		if (const TileItemVector* items = tile->getItemList()) {
			for (const auto& findItem : *items) {
				if (findItem->getID() == id) {
					staticItem = findItem.get();
					exactMatch = true;
					break;
				}
			}

			if (!staticItem && (iType.isDoor() || iType.isBed())) {
				for (const auto& findItem : *items) {
					const ItemType& mapType = Item::items[findItem->getID()];
					if (isSamePersistentFixtureFamily(mapType, iType)) {
						staticItem = findItem.get();
						break;
					}
				}
			}
		}

		if (staticItem) {
			if (staticItem->unserializeAttr(propStream)) {
				Container* container = staticItem->getContainer();
				if (container && !loadContainer(propStream, container)) {
					return false;
				}

				if (!exactMatch) {
					g_game.transformItem(staticItem, id);
				}
			} else {
				LOG_WARN(fmt::format("WARNING: Unserialization error in IOMapSerialize::loadItem() {}", id));
			}
		} else if (iType.isCarpet() || iType.wrapableTo != 0) {
			auto loadedItem = Item::CreateItem(id);
			if (loadedItem) {
				if (loadedItem->unserializeAttr(propStream)) {
					Container* container = loadedItem->getContainer();
					if (container && !loadContainer(propStream, container)) {
						return false;
					}

					Item* raw = loadedItem.get();
					parent->internalAddThing(raw);
					if (!mapSerializeCylinderOwnsThing(parent, raw)) {
						return false;
					}

					raw->startDecaying();
					loadedItem.reset();
				} else {
					LOG_WARN(fmt::format("WARNING: Unserialization error in IOMapSerialize::loadItem() {}", id));
					return false;
				}
			}
		} else {
			// The map changed since the last save, just read the attributes
			Item* changedFixture = nullptr;
			if (const TileItemVector* items = tile->getItemList(); items && (iType.isDoor() || iType.isBed())) {
				for (const auto& findItem : *items) {
					const ItemType& mapType = Item::items[findItem->getID()];
					if ((iType.isDoor() && mapType.isDoor()) || (iType.isBed() && mapType.isBed())) {
						changedFixture = findItem.get();
						break;
					}
				}
			}

			auto dummy = Item::CreateItem(id);
			if (dummy) {
				if (!dummy->unserializeAttr(propStream)) {
					LOG_WARN(fmt::format("WARNING: Unserialization error in IOMapSerialize::loadItem() {}", id));
					return false;
				}
				Container* container = dummy->getContainer();
				if (container) {
					if (!loadContainer(propStream, container)) {
						return false;
					}

					const size_t persistedContentCount = container->size();
					Container* replacementContainer = findReplacementStaticContainer(tile);
					if (replacementContainer) {
						transferContainerContents(container, replacementContainer);
					}

					// If there is no unambiguous replacement container, or a child
					// cannot be transferred, retain the old container itself as a
					// no-loss fallback instead of destroying player-owned data.
					if (!container->empty()) {
						Item* raw = dummy.get();
						tile->internalAddThing(raw);
						if (!mapSerializeCylinderOwnsThing(tile, raw)) {
							const Position& position = tile->getPosition();
							LOG_ERROR(fmt::format("[HousePersistence] could not preserve stale container {} at {},{},{}",
							                      id, position.x, position.y, position.z));
							return false;
						}
						raw->startDecaying();
						dummy.reset();
					}

					if (persistedContentCount != 0) {
						const Position& position = tile->getPosition();
						g_logger().warn("[HousePersistence] preserved {} item(s) from replaced static container {} "
						                "at {},{},{}",
						                persistedContentCount, id, position.x, position.y, position.z);
					}
				} else if (BedItem* bedItem = dynamic_cast<BedItem*>(dummy.get())) {
					uint32_t sleeperGUID = bedItem->getSleeper();
					if (sleeperGUID != 0) {
						g_game.removeBedSleeper(sleeperGUID);
					}
				}
			}

			if (changedFixture) {
				logStaleHouseFixture(tile, changedFixture->getID(), id, persistentFixtureTypeName(iType));
			}
		}
	}
	return true;
}

void IOMapSerialize::saveItem(PropWriteStream& stream, const Item* item)
{
	const Container* container = item->getContainer();

	// Write ID & props
	stream.write<uint16_t>(item->getID());
	item->serializeAttr(stream);

	if (container) {
		// Hack our way into the attributes
		stream.write<uint8_t>(ATTR_CONTAINER_ITEMS);
		stream.write<uint32_t>(container->size());
		for (auto it = container->getReversedItems(), end = container->getReversedEnd(); it != end; ++it) {
			saveItem(stream, it->get());
		}
	}

	stream.write<uint8_t>(0x00); // attr end
}

void IOMapSerialize::saveTile(PropWriteStream& stream, const Tile* tile)
{
	const TileItemVector* tileItems = tile->getItemList();
	if (!tileItems) {
		return;
	}

	std::forward_list<Item*> items;
	uint16_t count = 0;
	for (const auto& item : *tileItems) {
		const ItemType& it = Item::items[item->getID()];

		// Note that these are NEGATED, ie. these are the items that will be saved.
		if (!(it.moveable || it.forceSerialize || it.isCarpet() || it.wrapableTo != 0 || item->getDoor() ||
		      (item->getContainer() && !item->getContainer()->empty()) || it.canWriteText || item->getBed())) {
			continue;
		}

		items.push_front(item.get());
		++count;
	}

	if (!items.empty()) {
		const Position& tilePosition = tile->getPosition();
		stream.write<uint16_t>(tilePosition.x);
		stream.write<uint16_t>(tilePosition.y);
		stream.write<uint8_t>(tilePosition.z);

		stream.write<uint32_t>(count);
		for (const Item* item : items) {
			saveItem(stream, item);
		}
	}
}

bool IOMapSerialize::loadHouseInfo()
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery("SELECT `id`, CAST(`type` as UNSIGNED) AS `type`, `owner`, `paid`, `warnings`, `is_protected` FROM `houses`");
	if (!result) {
		return false;
	}

	do {
			auto house = g_game.map.houses.getHouse(result->getNumber<uint32_t>("id"));
			if (house) {
				std::string_view typeStr = result->getString("type");
				uint32_t typeVal = house->getType();
				if (caseInsensitiveEqual(typeStr, "guildhall") || typeStr == "2") {
					typeVal = HOUSE_TYPE_GUILDHALL;
				} else if (caseInsensitiveEqual(typeStr, "house") || caseInsensitiveEqual(typeStr, "normal") || typeStr == "1") {
					typeVal = (house->getType() == HOUSE_TYPE_GUILDHALL) ? HOUSE_TYPE_GUILDHALL : HOUSE_TYPE_NORMAL;
				}
			house->setType(static_cast<HouseType_t>(typeVal));
        uint32_t ownerValue = result->getNumber<uint32_t>("owner");
        house->setOwner(ownerValue, false);
        std::string_view isProtectedRaw = result->getString("is_protected");
        char isProtectedChar = isProtectedRaw.empty() ? '0' : isProtectedRaw[0];
        bool protect = (isProtectedChar == '1');
        if (ownerValue == 0) {
            protect = false;
        }
        house->setProtected(protect);
			house->setPaidUntil(result->getNumber<time_t>("paid"));
			house->setPayRentWarnings(result->getNumber<uint32_t>("warnings"));
		}
	} while (result->next());

	result = db.storeQuery("SELECT `house_id`, `listid`, `list` FROM `house_lists`");
	if (result) {
		do {
			auto house = g_game.map.houses.getHouse(result->getNumber<uint32_t>("house_id"));
			if (house) {
				house->setAccessList(result->getNumber<uint32_t>("listid"), result->getString("list"));
			}
		} while (result->next());
	}

	// Load house protection guests
	result = db.storeQuery("SELECT `house_id`, `player_id` FROM `house_guests`");
	if (result) {
		do {
			auto house = g_game.map.houses.getHouse(result->getNumber<uint32_t>("house_id"));
			if (house) {
				house->getProtectionGuests().insert(result->getNumber<uint32_t>("player_id"));
			}
		} while (result->next());
	}
	return true;
}

bool IOMapSerialize::saveHouseInfo()
{
	Database& db = Database::getInstance();

	DBTransaction transaction;
	if (!transaction.begin()) {
		return false;
	}

	if (!db.executeQuery("DELETE FROM `house_lists`")) {
		return false;
	}

	std::ostringstream query;
	query << "INSERT INTO `houses` (`id`, `type`, `owner`, `paid`, `warnings`, `is_protected`, `name`, `town_id`, `rent`, `size`, `beds`) VALUES ";

	bool notEmpty = false;
	for (const auto& it : g_game.map.houses.getHouses()) {
		House* house = it.second.get();
		if (notEmpty) {
			query << ",";
		}
		query << fmt::format("({:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:s}, {:d}, {:d}, {:d}, {:d})",
		                     house->getId(), static_cast<uint32_t>(house->getType()), house->getOwner(), house->getPaidUntil(), house->getPayRentWarnings(),
		                     (house->getProtected() ? 1 : 0), db.escapeString(house->getName()), house->getTownId(), house->getRent(), house->getTileCount(),
		                     house->getBedCount());
		notEmpty = true;
	}

	if (notEmpty) {
		query << " ON DUPLICATE KEY UPDATE "
		      << "`owner` = VALUES(`owner`), "
		      << "`type` = VALUES(`type`), "
		      << "`paid` = VALUES(`paid`), "
		      << "`warnings` = VALUES(`warnings`), "
		      << "`is_protected` = VALUES(`is_protected`), "
		      << "`name` = VALUES(`name`), "
		      << "`town_id` = VALUES(`town_id`), "
		      << "`rent` = VALUES(`rent`), "
		      << "`size` = VALUES(`size`), "
		      << "`beds` = VALUES(`beds`)";

		if (!db.executeQuery(query.str())) {
			return false;
		}
	}

	DBInsert stmt("INSERT INTO `house_lists` (`house_id`, `listid` , `list`) VALUES ");

	for (const auto& it : g_game.map.houses.getHouses()) {
		House* house = it.second.get();

		auto listText = house->getAccessList(GUEST_LIST).value_or("");

		if (!listText.empty()) {
			if (!stmt.addRow(fmt::format("{:d}, {}, {:s}", house->getId(), tfs::to_underlying(GUEST_LIST),
			                             db.escapeString(listText)))) {
				return false;
			}
		}

		listText = house->getAccessList(SUBOWNER_LIST).value_or("");
		if (!listText.empty()) {
			if (!stmt.addRow(fmt::format("{:d}, {}, {:s}", house->getId(), tfs::to_underlying(SUBOWNER_LIST),
			                             db.escapeString(listText)))) {
				return false;
			}
		}

		for (const auto& door : house->getDoors()) {
			listText = door->getAccessList().value_or("");
			if (!listText.empty()) {
				if (!stmt.addRow(fmt::format("{:d}, {:d}, {:s}", house->getId(), door->getDoorId(),
				                             db.escapeString(listText)))) {
					return false;
				}
			}
		}
	}

	if (!stmt.execute()) {
		return false;
	}

	return transaction.commit();
}

bool IOMapSerialize::saveHouse(const House* house)
{
	Database& db = Database::getInstance();

	// Start the transaction
	DBTransaction transaction;
	if (!transaction.begin()) {
		return false;
	}

	uint32_t houseId = house->getId();

	// clear old tile data
	if (!db.executeQuery(fmt::format("DELETE FROM `tile_store` WHERE `house_id` = {:d}", houseId))) {
		return false;
	}

	DBInsert stmt("INSERT INTO `tile_store` (`house_id`, `data`) VALUES ");

	PropWriteStream stream;
	for (const auto& weakTile : house->getTiles()) {
		if (auto tile = weakTile.lock()) {
			saveTile(stream, tile.get());

			if (auto attributes = stream.getStream(); attributes.size() > 0) {
				if (!stmt.addRow(fmt::format("{:d}, {:s}", houseId, db.escapeString(attributes)))) {
					return false;
				}
				stream.clear();
			}
		}
	}

	if (!stmt.execute()) {
		return false;
	}

	// End the transaction
	return transaction.commit();
}
