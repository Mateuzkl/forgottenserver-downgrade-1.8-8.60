// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "house.h"

#include "bed.h"
#include "configmanager.h"
#include "game.h"
#include "iologindata.h"
#include "iomapserialize.h"
#include "save_manager.h"
#include "tasks.h"
#include "pugicast.h"
#include "logger.h"
#include <fmt/format.h>

extern Game g_game;

House::House(uint32_t houseId) : id(houseId) {}

House::~House()
{
}

void House::addTile(HouseTile* tile)
{
	if (!tile) {
		return;
	}

	tile->setFlag(TILESTATE_PROTECTIONZONE);
	if (auto tileRef = tile->weak_from_this().lock()) {
		addTile(std::static_pointer_cast<HouseTile>(tileRef));
	}
}

void House::addTile(const std::shared_ptr<HouseTile>& tile)
{
	if (!tile) {
		return;
	}

	tile->setFlag(TILESTATE_PROTECTIONZONE);
	for (auto it = houseTiles.begin(); it != houseTiles.end();) {
		auto locked = it->lock();
		if (!locked) {
			it = houseTiles.erase(it);
		} else if (locked == tile) {
			return;
		} else {
			++it;
		}
	}
	houseTiles.push_back(tile);
}

size_t House::getTileCount() const
{
	size_t count = 0;
	for (const auto& weakTile : houseTiles) {
		if (!weakTile.expired()) {
			++count;
		}
	}
	return count;
}

std::vector<std::shared_ptr<HouseTile>> House::getTilesSnapshot() const
{
	std::vector<std::shared_ptr<HouseTile>> tiles;
	tiles.reserve(houseTiles.size());
	for (const auto& weakTile : houseTiles) {
		if (auto tile = weakTile.lock()) {
			tiles.push_back(std::move(tile));
		}
	}
	return tiles;
}

std::tuple<uint32_t, uint32_t, std::string, uint32_t, std::string> House::initializeOwnerDataFromDatabase(uint32_t guid_guild, HouseType_t type)
{
	if (guid_guild == 0) {
		return std::make_tuple(0, 0, std::string{}, 0, std::string{});
	}

	Database& db = Database::getInstance();

	if (type == HOUSE_TYPE_NORMAL) {
		std::ostringstream query;
		query << "SELECT `id`, `name`, `account_id` FROM `players` WHERE `id`=" << guid_guild;
		if (DBResult_ptr result = db.storeQuery(query.str())) {
			return std::make_tuple(
			    result->getNumber<uint32_t>("id"),
			    result->getNumber<uint32_t>("account_id"),
			    std::string(result->getString("name")),
			    uint32_t{0},
			    std::string{}
			);
		}
		throw std::runtime_error("Error in House::setOwner - Failed to find player GUID");
	}

	// HOUSE_TYPE_GUILDHALL
	std::ostringstream query;
	query << "SELECT `g`.`id`, `g`.`name` as `guild_name`, `g`.`ownerid`, `p`.`name`, `p`.`account_id` ";
	query << "FROM `guilds` as `g` INNER JOIN `players` AS `p` ON `g`.`ownerid` = `p`.`id` ";
	query << "WHERE `g`.`id`=" << guid_guild;
	if (DBResult_ptr result = db.storeQuery(query.str())) {
		return std::make_tuple(
		    result->getNumber<uint32_t>("ownerid"),
		    result->getNumber<uint32_t>("account_id"),
		    std::string(result->getString("name")),
		    result->getNumber<uint32_t>("id"),
		    std::string(result->getString("guild_name"))
		);
	}
	throw std::runtime_error("Error in House::setOwner - Failed to find guild ID");
}

bool House::updateOwnerInDatabase(uint32_t guid_guild, bool resetProtection)
{
	Database& db = Database::getInstance();
	if (!db.isInTransaction()) {
		// Also persist empty houses: a previous tile checkpoint must not survive
		// an ownership change just because there are no current depot moves.
		const auto image = IOMapSerialize::buildHouseSave(this, {});
		if (!image) return false;
		// Releasing a house whose owner row was deleted: the optional
		// `ondelete_players` trigger (schema.sql; migration 13 asks admins to
		// recreate it) has already set houses.owner to 0 in the database while
		// the loaded House still holds the old GUID. Comparing the receipt to
		// that GUID would reject the release forever, so the house could never
		// be reset or auctioned. When the target is 0, a database owner of 0 is
		// the expected state, not a concurrent change.
		uint64_t expectedOwner = owner;
		if (guid_guild == 0 && owner != 0) {
			const auto row = db.storeQuery(fmt::format("SELECT `owner` FROM `houses` WHERE `id` = {}", id));
			if (!row) return false;
			if (row->getNumber<uint32_t>("owner") == 0) expectedOwner = 0;
		}
		return g_saveManager.commitTransfer(
		    fmt::format("SELECT `owner` AS `receipt` FROM `houses` WHERE `id` = {} FOR UPDATE", id), expectedOwner,
		    guid_guild, [&] {
			    for (const auto& query : *image)
				    if (!db.executeQuery(query)) return false;
			    return updateOwnerInDatabase(guid_guild, resetProtection) &&
			           db.executeQuery(fmt::format("DELETE FROM `house_lists` WHERE `house_id` = {}", id)) &&
			           db.executeQuery(fmt::format("DELETE FROM `house_guests` WHERE `house_id` = {}", id));
		    });
	}
	if (resetProtection) {
		return db.executeQuery(fmt::format(
		    "UPDATE `houses` SET `owner` = {:d}, `bid` = 0, `bid_end` = 0, `last_bid` = 0, `highest_bidder` = 0, `is_protected` = 0 WHERE `id` = {:d}",
		    guid_guild, id));
	}
	return db.executeQuery(fmt::format(
	    "UPDATE `houses` SET `owner` = {:d}, `bid` = 0, `bid_end` = 0, `last_bid` = 0, `highest_bidder` = 0, `is_protected` = 0 WHERE `id` = {:d}",
	    guid_guild, id));
}

bool House::setOwner(uint32_t guid_guild, bool updateDatabase /* = true*/, Player* previousPlayer /* = nullptr*/,
                     const std::vector<Player*>& participants)
{
	if (ownerTransitionInProgress || g_saveManager.isPersistenceBlocked()) {
		return false;
	}
	if (isLoaded && owner == guid_guild) {
		return true;
	}
	const auto houseRef = weak_from_this().lock();
	const bool replacingOwner = owner != 0 && updateDatabase;
	ownerTransitionInProgress = true;
	struct TransitionGuard
	{
		bool& inProgress;
		~TransitionGuard() { inProgress = false; }
	} transitionGuard{ownerTransitionInProgress};

	OwnerData newOwnerData;
	if (guid_guild != 0) {
		try {
			newOwnerData = initializeOwnerDataFromDatabase(guid_guild, type);
		} catch (const std::runtime_error& err) {
			LOG_ERROR("{}", err.what());
			return false;
		}
	}

	// Do this before owner SQL, depot moves, kicks or access-list changes.
	// A failed offline load/save must preserve every occupied sleep session.
	if (owner != 0 && updateDatabase && !BedItem::wakeUpAll(getBeds())) {
		return false;
	}
	std::function<void()> notifyTransfer;
	if (updateDatabase && owner != guid_guild) {
		const bool resetProtection = (guid_guild == 0 || owner == 0);
		if (!(owner ? transferToDepot(guid_guild, resetProtection, previousPlayer, notifyTransfer, participants)
		            : updateOwnerInDatabase(guid_guild, resetProtection))) {
			return false;
		}
		if (resetProtection) {
			setProtected(false);
		}
		protectionGuests.clear(); // SQL removal committed with the ownership change.
	}

	isLoaded = true;

	if (owner != 0 && updateDatabase) {
		// Publish the new owner before kicks/access-list callbacks can save the world.
		owner = guid_guild;
		ownerAccountId = std::get<1>(newOwnerData);
		ownerName = type == HOUSE_TYPE_GUILDHALL ? "The " + std::get<4>(newOwnerData) : std::get<2>(newOwnerData);
		// clean access lists
		if (updateDatabase) {
			isProtected = false;
		}
		subOwnerList.parseList("");
		guestList.parseList("");

		{
			auto doorsSnapshot = getDoors();
			for (const auto& door : doorsSnapshot) {
				door->setAccessList("");
			}
		}
	} else {
		auto strRentPeriod = asLowerCaseString(std::string{getString(ConfigManager::HOUSE_RENT_PERIOD)});
		time_t currentTime = time(nullptr);
		if (strRentPeriod == "yearly" || strRentPeriod == "annual") {
			currentTime += 24 * 60 * 60 * 365;
		} else if (strRentPeriod == "monthly") {
			currentTime += 24 * 60 * 60 * 30;
		} else if (strRentPeriod == "weekly") {
			currentTime += 24 * 60 * 60 * 7;
		} else if (strRentPeriod == "daily") {
			currentTime += 24 * 60 * 60;
		} else if (strRentPeriod == "dev") {
			currentTime += 5 * 60;
		} else {
			currentTime = 0;
		}

		paidUntil = currentTime;
	}

	rentWarnings = 0;

	if (guid_guild != 0) {
		const auto& [sqlPlayerGuid, sqlAccountId, sqlPlayerName, sqlGuildId, sqlGuildName] = newOwnerData;

		owner = guid_guild;
		ownerAccountId = sqlAccountId;
		if (type == HOUSE_TYPE_GUILDHALL) {
			std::ostringstream ss;
			ss << "The " << sqlGuildName;
			ownerName = ss.str();
		} else {
			ownerName = sqlPlayerName;
		}
		updateDoorDescription();
	} else {
		owner = 0;
		ownerAccountId = 0;
		ownerName.clear();
		updateDoorDescription();
	}
	if (replacingOwner) {
		// Kicking a player runs movement callbacks, which may remove other
		// creatures or unregister this tile from the house.
		for (const auto& tile : getTilesSnapshot()) {
			if (const CreatureVector* creatures = tile->getCreatures()) {
				const auto snapshot = *creatures;
				for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it) {
					const auto& creature = *it;
					if (creature && !creature->isRemoved() && creature->getTile() == tile.get()) {
						kickPlayer(nullptr, creature->getPlayer());
					}
				}
			}
		}
	}
	if (notifyTransfer) notifyTransfer();
	return true;
}

AccessHouseLevel_t House::getHouseAccessLevel(const Player* player) const
{
    if (!player) {
        return HOUSE_OWNER;
    }

	if (player->hasFlag(PlayerFlag_CanEditHouses)) {
		return HOUSE_OWNER;
	}

	if (type == HOUSE_TYPE_NORMAL) {
		if (getBoolean(ConfigManager::HOUSE_OWNED_BY_ACCOUNT)) {
			if (ownerAccountId == player->getAccount()) {
				return HOUSE_OWNER;
			}
		}

		uint32_t guid = player->getGUID();
		if (guid == owner) {
			return HOUSE_OWNER;
		}
    } else { // HOUSE_TYPE_GUILDHALL
        const auto& guild = player->getGuild();
        uint32_t guid = player->getGUID();
        if (guild && guild->getId() == owner) {
            if (guild->getOwnerGUID() == guid) {
                return HOUSE_OWNER;
            }
            if (player->getGuildRank() == guild->getRankByLevel(2)) {
                return HOUSE_SUBOWNER;
            }
            return HOUSE_GUEST;
        }
    }

	if (subOwnerList.isInList(player)) {
		return HOUSE_SUBOWNER;
	}

	if (guestList.isInList(player)) {
		return HOUSE_GUEST;
	}

	return HOUSE_NOT_INVITED;
}

bool House::kickPlayer(Player* player, Player* target) const
{
	if (!target) {
		return false;
	}

	const auto tile = target->getTile();
	if (!tile) {
		return false;
	}

	const auto houseTile = tile->getHouseTile();
	if (!houseTile || houseTile->getHouse().get() != this) {
		return false;
	}

	if (getHouseAccessLevel(player) < getHouseAccessLevel(target) || target->hasFlag(PlayerFlag_CanEditHouses)) {
		return false;
	}

	Position oldPosition = target->getPosition();
	if (g_game.internalTeleport(target, getEntryPosition(), true, 0, CONST_ME_NONE) == RETURNVALUE_NOERROR) {
		g_game.addMagicEffect(oldPosition, CONST_ME_POFF, target->getInstanceID());
		g_game.addMagicEffect(getEntryPosition(), CONST_ME_TELEPORT, target->getInstanceID());
	}
	return true;
}

void House::setAccessList(uint32_t listId, std::string_view textlist)
{
	if (listId == GUEST_LIST) {
		guestList.parseList(textlist);
	} else if (listId == SUBOWNER_LIST) {
		subOwnerList.parseList(textlist);
	} else {
		auto door = getDoorByNumber(listId);
		if (door) {
			door->setAccessList(textlist);
		}

		// We do not have to kick anyone
		return;
	}

	// kick uninvited players
	for (const auto& tile : getTilesSnapshot()) {
		if (const CreatureVector* creatures = tile->getCreatures()) {
			const auto snapshot = *creatures;
			for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it) {
				const auto& creature = *it;
				if (!creature || creature->isRemoved() || creature->getTile() != tile.get()) {
					continue;
				}
				Player* player = creature->getPlayer();
				if (player && !isInvited(player)) {
					kickPlayer(nullptr, player);
				}
			}
		}
	}
}

bool House::transferToDepot(uint32_t newOwner, bool resetProtection, Player* previousPlayer,
                            std::function<void()>& notify, const std::vector<Player*>& participants)
{
	struct Move
	{
		std::shared_ptr<Item> source;
		std::shared_ptr<Item> destination;
		std::shared_ptr<Item> image;
		std::shared_ptr<HouseTile> tile;
		std::shared_ptr<Container> container;
		std::vector<std::shared_ptr<Container>> browseFields;
		int32_t index;
	};
	std::vector<Move> moves;
	std::unordered_set<const Item*> excluded;
	const auto tiles = getTilesSnapshot();
	const auto addMove = [&](const std::shared_ptr<Item>& item, const std::shared_ptr<HouseTile>& tile) {
		if (!excluded.insert(item.get()).second) return;
		auto* container = dynamic_cast<Container*>(item->getParent());
		// Browse Field temporarily owns the parent pointer of a tile item, but
		// the actual house tile still contains it. Remove both references before
		// publishing the inbox item, otherwise the next map save can duplicate it.
		const bool onTile = tile->getThingIndex(item.get()) >= 0;
		auto parent = g_game.getContainerSharedRef(container);
		std::vector<ContainerPtr> fields;
		if (onTile) {
			if (parent) fields.push_back(parent);
			for (const auto& field : g_game.getBrowseFieldContainers(tile.get())) {
				if (field != parent && field->getThingIndex(item.get()) >= 0) fields.push_back(field);
			}
		}
		moves.push_back({item,
		                 item,
		                 {},
		                 tile,
		                 onTile ? nullptr : parent,
		                 std::move(fields),
		                 onTile ? tile->getThingIndex(item.get()) : item->getParent()->getThingIndex(item.get())});
	};
	// Preserve the existing transfer layout: pickupable leaves first, then their
	// (now emptied) bags; static furniture remains and releases its contents.
	for (const auto& tile : tiles) {
		if (const auto* items = tile->getItemList()) {
			for (const auto& item : *items) {
				if (auto* root = item->getContainer()) {
					std::vector<Container*> queue{root};
					for (size_t i = 0; i < queue.size(); ++i) {
						for (const auto& child : queue[i]->getItemList()) {
							if (auto* nested = child->getContainer())
								queue.push_back(nested);
							else if (child->isPickupable() || child->getIntAttr(ITEM_ATTRIBUTE_WRAPID) != 0)
								addMove(child, tile);
						}
					}
				}
				if (item->isPickupable() || item->getIntAttr(ITEM_ATTRIBUTE_WRAPID) != 0) {
					addMove(item, tile);
				} else if (const auto* container = item->getContainer()) {
					for (const auto& child : container->getItemList()) {
						addMove(child, tile);
					}
				}
			}
		}
	}
	// No recipient is needed for an empty house (including houses without a town).
	if (moves.empty() && participants.empty()) return updateOwnerInDatabase(newOwner, resetProtection);
	if (!g_dispatcher.isDispatcherThread()) return false;
	const auto keepItems = [&] {
		if (participants.empty()) return updateOwnerInDatabase(newOwner, resetProtection);
		const auto image = IOMapSerialize::buildHouseSave(this, {});
		if (!image) return false;
		return g_saveManager.savePlayerTransfer(
		    participants.front(), {},
		    [&] {
			    Database& db = Database::getInstance();
			    const auto row =
			        db.storeQuery(fmt::format("SELECT `owner` FROM `houses` WHERE `id` = {} FOR UPDATE", id));
			    if (!row || row->getNumber<uint32_t>("owner") != owner) return false;
			    for (const auto& query : *image)
				    if (!db.executeQuery(query)) return false;
			    return updateOwnerInDatabase(newOwner, resetProtection) &&
			           db.executeQuery(fmt::format("DELETE FROM `house_lists` WHERE `house_id` = {}", id)) &&
			           db.executeQuery(fmt::format("DELETE FROM `house_guests` WHERE `house_id` = {}", id));
		    },
		    participants);
	};

	uint32_t recipientGuid = owner;
	if (type == HOUSE_TYPE_GUILDHALL) {
		auto guild = g_game.getGuild(owner);
		if (!guild) guild = IOGuild::loadGuild(owner);
		if (!guild) {
			// COUNT distinguishes a deleted guild from a failed load/query.
			const auto row = Database::getInstance().storeQuery(
			    fmt::format("SELECT COUNT(*) AS `count` FROM `guilds` WHERE `id` = {}", owner));
			return row && row->getNumber<uint32_t>("count") == 0 && keepItems();
		}
		recipientGuid = guild->getOwnerGUID();
	}
	// Never credit an arbitrary Player supplied by a caller.
	if (previousPlayer && previousPlayer->getGUID() != recipientGuid) return false;
	const auto online = g_game.getPlayerByGUID(recipientGuid);
	Player offline(nullptr);
	Player* player = online ? online.get() : previousPlayer;
	if (!player) {
		if (!IOLoginData::loadPlayerById(&offline, recipientGuid)) {
			// Retain orphaned items on their tiles; never treat a broken existing
			// player load (or a database error) as proof that the owner was deleted.
			const auto row = Database::getInstance().storeQuery(
			    fmt::format("SELECT COUNT(*) AS `count` FROM `players` WHERE `id` = {}", recipientGuid));
			return row && row->getNumber<uint32_t>("count") == 0 && keepItems();
		}
		player = &offline;
	}
	if (g_saveManager.hasPendingPlayerSave(recipientGuid) || g_saveManager.hasFailedRecovery(recipientGuid))
		return false;
	if (townId == 0 && !moves.empty()) return false;
	Inbox* inbox = player->getInbox(townId);
	if ((!inbox && !moves.empty()) || !player->getSaveFlag()) return false;

	// Detached projections preserve item identity/attributes; clone() intentionally
	// allocates new UIDs and therefore is not appropriate for a transfer snapshot.
	const auto project = [&](auto&& self, const std::shared_ptr<Item>& item) -> std::shared_ptr<Item> {
		const auto wrap = static_cast<uint16_t>(item->getIntAttr(ITEM_ATTRIBUTE_WRAPID));
		const uint16_t id = wrap ? wrap : item->getID();
		if (Item::items[id].id == 0) return {};
		auto copy = Item::CreateItem(id, item->getSubType());
		if (!copy) return {};
		PropWriteStream attributes;
		item->serializeAttr(attributes);
		attributes.write<uint8_t>(0);
		PropStream input;
		input.init(attributes.getStream().data(), attributes.getStream().size());
		if (!copy->unserializeAttr(input)) return {};
		if (const auto* contents = item->getContainer()) {
			Container* target = copy->getContainer();
			// Fail closed instead of destroying contents when incompatible furniture
			// is configured to wrap into a non-container.
			for (auto it = contents->getReversedItems(); it != contents->getReversedEnd(); ++it) {
				if (excluded.contains(it->get())) continue;
				if (!target) return {};
				auto child = self(self, *it);
				if (!child) return {};
				target->internalAddThing(child.get());
			}
		}
		return copy;
	};
	ItemBlockList credit;
	const auto needsProjection = [&](auto&& self, const Item* item) -> bool {
		const uint16_t wrap = static_cast<uint16_t>(item->getIntAttr(ITEM_ATTRIBUTE_WRAPID));
		if (wrap && wrap != item->getID()) return true;
		if (const auto* container = item->getContainer()) {
			for (const auto& child : container->getItemList()) {
				if (!excluded.contains(child.get()) && self(self, child.get())) return true;
			}
		}
		return false;
	};
	for (auto& move : moves) {
		move.image = project(project, move.source);
		if (!move.image) return false;
		if (needsProjection(needsProjection, move.source.get())) move.destination = move.image;
		credit.emplace_back(static_cast<int32_t>(townId), move.image.get());
	}
	const auto houseSave = IOMapSerialize::buildHouseSave(this, excluded);
	if (!houseSave) return false;
	Database& db = Database::getInstance();
	const auto sideChanges = [&] {
		const auto row = db.storeQuery(fmt::format("SELECT `owner` FROM `houses` WHERE `id` = {} FOR UPDATE", id));
		if (!row || row->getNumber<uint32_t>("owner") != owner) return false;
		for (const auto& query : *houseSave)
			if (!db.executeQuery(query)) return false;
		if (!updateOwnerInDatabase(newOwner, resetProtection)) return false;
		return db.executeQuery(fmt::format("DELETE FROM `house_lists` WHERE `house_id` = {}", id)) &&
		       db.executeQuery(fmt::format("DELETE FROM `house_guests` WHERE `house_id` = {}", id));
	};
	if (!g_saveManager.savePlayerTransfer(player, credit, sideChanges, participants)) return false;

	// Publish all items before invoking Lua/trade notifications. No move can be
	// declined after the DB credit: these operations bypass capacity/stack merging.
	// Every source parent is kept alive and the dispatcher has not yielded.
	for (auto& move : moves) {
		const bool removed = move.container ? move.container->removeItemForHouseTransfer(move.source.get())
		                                    : move.tile->removeItemForHouseTransfer(move.source.get());
		if (!removed) std::terminate(); // Invariant failure: never save a half-published transfer.
		for (const auto& field : move.browseFields) {
			if (!field->removeItemForHouseTransfer(move.source.get())) std::terminate();
		}
		// Originals are now detached; stop their decay, including nested contents,
		// before publishing the identity-preserving replacement tree.
		if (move.destination != move.source) {
			move.source->stopDecaying();
			if (auto* container = move.source->getContainer()) {
				for (const auto& child : container->getItems(true)) child->stopDecaying();
			}
		}
		inbox->internalAddThing(move.destination.get());
		if (move.destination->getParent() != inbox) std::terminate();
		move.destination->startDecaying();
	}
	// Owner/access-list publication must also finish before a callback can save
	// the world. Capture shared references, never a temporary offline Player.
	notify = [moves = std::move(moves), tiles, online, town = townId] {
		if (online) {
			online->onReceiveMail();
			online->onSendContainer(online->getInbox(town));
		}
		for (const auto& move : moves) {
			// Container parents may now be in the inbox, or already removed by an
			// earlier callback. Notify the captured original tile, not its new parent.
			move.tile->postRemoveNotification(move.destination.get(), nullptr, move.index);
		}
		for (const auto& tile : tiles) {
			SpectatorVec spectators;
			g_game.map.getSpectators(spectators, tile->getPosition(), true, true);
			for (const auto& spectator : spectators.players()) {
				auto* viewer = static_cast<Player*>(spectator.get());
				viewer->sendUpdateTile(tile.get(), tile->getPosition());
				for (const auto& move : moves) {
					if (move.tile != tile) continue;
					if (move.container) viewer->onSendContainer(move.container.get());
					for (const auto& field : move.browseFields) viewer->onSendContainer(field.get());
					viewer->onRemoveTileItem(tile.get(), tile->getPosition(), Item::items[move.source->getID()],
					                         move.source.get());
				}
			}
		}
	};
	return true;
}
std::optional<std::string_view> House::getAccessList(uint32_t listId) const
{
	if (listId == GUEST_LIST) {
		return std::make_optional(guestList.getList());
	} else if (listId == SUBOWNER_LIST) {
		return std::make_optional(subOwnerList.getList());
	}

	auto door = getDoorByNumber(listId);
	if (!door) {
		return std::nullopt;
	}

	return door->getAccessList();
}

bool House::isInvited(const Player* player) const { return getHouseAccessLevel(player) != HOUSE_NOT_INVITED; }

void House::addDoor(Door* door)
{
	if (!door) {
		return;
	}

	auto doorRef = door->weak_from_this().lock();
	if (!doorRef) {
		return;
	}
	auto doorShared = std::static_pointer_cast<Door>(doorRef);

	// Check for duplicates and prune expired entries
	for (auto it = doorList.begin(); it != doorList.end();) {
		auto locked = it->lock();
		if (!locked) {
			it = doorList.erase(it);
		} else if (locked == doorShared) {
			door->setHouse(this);
			return;
		} else {
			++it;
		}
	}

	door->setHouse(this);
	doorList.emplace_back(doorShared);
}

void House::removeDoor(Door* door)
{
	if (!door) {
		return;
	}

	for (auto it = doorList.begin(); it != doorList.end();) {
		auto locked = it->lock();
		if (!locked || locked.get() == door) {
			const bool found = (locked && locked.get() == door);
			it = doorList.erase(it);
			if (found) {
				return;
			}
		} else {
			++it;
		}
	}
}

void House::addBed(BedItem* bed)
{
	if (!bed) {
		return;
	}

	auto bedRef = bed->weak_from_this().lock();
	if (!bedRef) {
		return;
	}
	auto bedShared = std::static_pointer_cast<BedItem>(bedRef);

	// Check for duplicates and prune expired entries
	for (auto it = bedsList.begin(); it != bedsList.end();) {
		auto locked = it->lock();
		if (!locked) {
			it = bedsList.erase(it);
		} else if (locked == bedShared) {
			bed->setHouse(weak_from_this().lock());
			return;
		} else {
			++it;
		}
	}

	bed->setHouse(weak_from_this().lock());
	bedsList.emplace_back(bedShared);
}

void House::removeBed(BedItem* bed)
{
	if (!bed) {
		return;
	}

	for (auto it = bedsList.begin(); it != bedsList.end();) {
		auto locked = it->lock();
		if (!locked || locked.get() == bed) {
			const bool found = (locked && locked.get() == bed);
			it = bedsList.erase(it);
			if (found) {
				return;
			}
		} else {
			++it;
		}
	}
}

std::shared_ptr<Door> House::getDoorByNumber(uint32_t doorId) const
{
	for (const auto& weakDoor : doorList) {
		auto door = weakDoor.lock();
		if (door && door->getDoorId() == doorId) {
			return door;
		}
	}
	return nullptr;
}

std::shared_ptr<Door> House::getDoorByPosition(const Position& pos) const
{
	for (const auto& weakDoor : doorList) {
		auto door = weakDoor.lock();
		if (door && door->getPosition() == pos) {
			return door;
		}
	}
	return nullptr;
}

std::vector<std::shared_ptr<Door>> House::getDoors() const
{
	std::vector<std::shared_ptr<Door>> result;
	result.reserve(doorList.size());
	for (const auto& weakDoor : doorList) {
		if (auto door = weakDoor.lock()) {
			result.push_back(std::move(door));
		}
	}
	return result;
}

size_t House::getDoorCount() const
{
	size_t count = 0;
	for (const auto& weakDoor : doorList) {
		if (!weakDoor.expired()) {
			++count;
		}
	}
	return count;
}

std::vector<std::shared_ptr<BedItem>> House::getBeds() const
{
	std::vector<std::shared_ptr<BedItem>> result;
	result.reserve(bedsList.size());
	for (const auto& weakBed : bedsList) {
		if (auto bed = weakBed.lock()) {
			result.push_back(std::move(bed));
		}
	}
	return result;
}

uint32_t House::getBedCount() const
{
	size_t liveCount = 0;
	for (const auto& weakBed : bedsList) {
		if (!weakBed.expired()) {
			++liveCount;
		}
	}
	return static_cast<uint32_t>(
	    std::ceil(liveCount / 2.)); // each bed takes 2 sqms of space, ceil is just for bad maps
}

void House::removeTile(const HouseTile* tile)
{
	if (!tile) {
		return;
	}
	for (auto it = houseTiles.begin(); it != houseTiles.end();) {
		auto locked = it->lock();
		if (!locked || locked.get() == tile) {
			it = houseTiles.erase(it);
			if (locked) {
				return;
			}
		} else {
			++it;
		}
	}
}

bool House::canEditAccessList(uint32_t listId, const Player* player) const
{
	switch (getHouseAccessLevel(player)) {
		case HOUSE_OWNER:
			return true;

		case HOUSE_SUBOWNER:
			return listId == GUEST_LIST;

		default:
			return false;
	}
}

std::shared_ptr<HouseTransferItem> House::getTransferItem()
{
	if (auto item = transferItem.lock()) {
		return item;
	}

	auto newTransferItem = HouseTransferItem::createHouseTransferItem(this);
	if (!newTransferItem) {
		return nullptr;
	}

	transferItem = newTransferItem;
	transfer_container.addThing(newTransferItem.get());
	return newTransferItem;
}

void House::resetTransferItem()
{
	if (auto item = transferItem.lock()) {
		Item* rawItem = item.get();
		transferItem.reset();
		transfer_container.removeThing(rawItem, rawItem->getItemCount());
	}
}

HouseTransferItem::HouseTransferItem(std::shared_ptr<House> house) : Item(0), house(std::move(house)) {}

std::shared_ptr<HouseTransferItem> HouseTransferItem::createHouseTransferItem(House* house)
{
	auto houseRef = house ? house->weak_from_this().lock() : nullptr;
	if (!houseRef) {
		return nullptr;
	}

	auto transferItem = std::make_shared<HouseTransferItem>(houseRef);
	transferItem->setID(ITEM_DOCUMENT_RO);
	transferItem->setSubType(1);
	transferItem->setSpecialDescription(fmt::format("It is a house transfer document for '{:s}'.", house->getName()));
	return transferItem;
}

void HouseTransferItem::onTradeEvent(TradeEvents_t event, Player* owner)
{
	if (event == ON_TRADE_TRANSFER) {
		if (auto h = house.lock()) {
			if (!h->executeTransfer(this, owner)) {
				// The trade engine has already moved this virtual document. Do not
				// leave a removed/stale document cached as the house transfer item.
				h->resetTransferItem();
				if (owner) {
					owner->sendTextMessage(MESSAGE_EVENT_ADVANCE,
					                       "The house transfer could not be completed. No ownership was changed.");
				}
				return;
			}
		}

		g_game.internalRemoveItem(this, 1);
	} else if (event == ON_TRADE_CANCEL) {
		if (auto h = house.lock()) {
			h->resetTransferItem();
		}
	}
}

bool HouseTransferItem::executeAtomicTrade(Player* seller, Player* buyer, Item* payment)
{
	const auto h = house.lock();
	const auto item = g_game.getItemSharedRef(payment);
	if (!h || !seller || !buyer || seller == buyer || !item || h->getTransferItem().get() != this ||
	    payment->getTopParent() != buyer || !g_dispatcher.isDispatcherThread())
		return false;
	if (h->getType() == HOUSE_TYPE_NORMAL && (h->getOwner() != seller->getGUID() || h->getOwner() == buyer->getGUID()))
		return false;
	if (h->getType() == HOUSE_TYPE_GUILDHALL && (!seller->getGuild() || seller->getGuild()->getId() != h->getOwner() ||
	                                             !buyer->getGuild() || buyer->getGuild()->getId() == h->getOwner()))
		return false;
	if (g_saveManager.hasPendingPlayerSave(seller->getGUID()) || g_saveManager.hasPendingPlayerSave(buyer->getGUID()) ||
	    g_saveManager.isPersistenceBlocked())
		return false;

	Cylinder* source = payment->getParent();
	const auto sourceContainer = g_game.getContainerSharedRef(dynamic_cast<Container*>(source));
	const int32_t sourceIndex = source->getThingIndex(payment);
	int32_t destinationIndex = INDEX_WHEREEVER;
	uint32_t flags = FLAG_IGNOREAUTOSTACK;
	Item* destinationItem = nullptr;
	Cylinder* destination =
	    seller->queryDestination(destinationIndex, *payment, &destinationItem, flags, seller->getInstanceID());
	const auto destinationContainer = g_game.getContainerSharedRef(dynamic_cast<Container*>(destination));
	if (sourceIndex < 0 || (!sourceContainer && source != buyer) || (!destinationContainer && destination != seller) ||
	    destination->queryAdd(destinationIndex, *payment, payment->getItemCount(), flags, nullptr) !=
	        RETURNVALUE_NOERROR)
		return false;

	// Stage just the payment without notifications, merging or yielding. Both
	// inventories and the house/inbox commit together. On failure the exact
	// original slot/container order is restored before the trade is cancelled.
	if (!(sourceContainer ? sourceContainer->removeItemForHouseTransfer(payment)
	                      : buyer->removeItemForHouseTransfer(payment)))
		return false;
	destination->internalAddThing(static_cast<uint32_t>(destinationIndex), payment);
	if (payment->getParent() != destination) std::terminate();
	const auto restorePayment = [&] {
		if (!(destinationContainer ? destinationContainer->removeItemForHouseTransfer(payment)
		                           : seller->removeItemForHouseTransfer(payment)))
			std::terminate();
		if (sourceContainer) {
			if (!sourceContainer->restoreItemForHouseTransfer(payment, sourceIndex)) std::terminate();
		} else
			source->internalAddThing(static_cast<uint32_t>(sourceIndex), payment);
		if (payment->getParent() != source) std::terminate();
	};
	const uint64_t buyerGeneration = buyer->getSaveGeneration();
	const uint64_t sellerGeneration = seller->getSaveGeneration();
	try {
		if (!h->executeTransfer(this, buyer, {seller, buyer})) {
			restorePayment();
			return false;
		}
	} catch (...) {
		// Never undo a committed payment if a post-commit notification throws.
		// Terminating preserves the coherent DB image for restart recovery.
		if (buyer->getSaveGeneration() != buyerGeneration || seller->getSaveGeneration() != sellerGeneration)
			std::terminate();
		restorePayment();
		throw;
	}
	// The virtual document never enters either persisted inventory.
	g_game.internalRemoveItem(this, 1);
	if (sourceContainer)
		buyer->onSendContainer(sourceContainer.get());
	else {
		buyer->sendInventoryItem(static_cast<slots_t>(sourceIndex),
		                         buyer->getInventoryItem(static_cast<slots_t>(sourceIndex)));
		buyer->onRemoveInventoryItem(payment);
	}
	source->postRemoveNotification(payment, destination, sourceIndex);
	if (payment->getParent() == destination) {
		destination->postAddNotification(payment, source, destination->getThingIndex(payment));
		if (destinationContainer)
			seller->onSendContainer(destinationContainer.get());
		else
			seller->refreshThing(payment);
	}
	buyer->scheduleAstraPlayerInventorySnapshot();
	seller->scheduleAstraPlayerInventorySnapshot();
	return true;
}

bool House::executeTransfer(HouseTransferItem* item, Player* newOwner, const std::vector<Player*>& participants)
{
	if (!newOwner) {
		return false;
	}
	auto currentItem = transferItem.lock();
	if (!currentItem || currentItem.get() != item) {
		return false;
	}

	if (type == HOUSE_TYPE_NORMAL) {
		if (!setOwner(newOwner->getGUID(), true, nullptr, participants)) {
			return false;
		}
	} else {
		const auto& newOwnerGuild = newOwner->getGuild();
		if (!newOwnerGuild || !setOwner(newOwnerGuild->getId(), true, nullptr, participants)) {
			return false;
		}
	}
	transferItem.reset();
	return true;
}

void AccessList::parseList(std::string_view list)
{
	playerList.clear();
	guildRankList.clear();
	allowEveryone = false;
	this->list = list;
	if (list.empty()) {
		return;
	}

	std::istringstream listStream(list.data());
	std::string line;

	uint16_t lineNo = 1;
	while (getline(listStream, line)) {
		if (++lineNo > 100) {
			break;
		}

		trimString(line);

		if (line.empty() || line.front() == '#' || line.length() > 100) {
			continue;
		}

		std::string::size_type at_pos = line.find("@");
		if (at_pos != std::string::npos) {
			if (at_pos == 0) {
				addGuild(line.substr(1));
			} else {
				addGuildRank(line.substr(0, at_pos), line.substr(at_pos + 1));
			}
		} else if (line == "*") {
			allowEveryone = true;
		} else if (line.find("!") != std::string::npos || line.find("*") != std::string::npos ||
		           line.find("?") != std::string::npos) {
			continue; // regexp no longer supported
		} else {
			addPlayer(line);
		}
	}
}

void AccessList::addPlayer(std::string_view name)
{
	auto player = g_game.getPlayerByName(name);
	if (player) {
		playerList.insert(player->getGUID());
	} else {
		uint32_t guid = IOLoginData::getGuidByName(name);
		if (guid != 0) {
			playerList.insert(guid);
		}
	}
}

namespace {

const Guild_ptr getGuildByName(std::string_view name)
{
	uint32_t guildId = IOGuild::getGuildIdByName(name);
	if (guildId == 0) {
		return nullptr;
	}

	if (const auto& guild = g_game.getGuild(guildId)) {
		return guild;
	}

	return IOGuild::loadGuild(guildId);
}

} // namespace

void AccessList::addGuild(std::string_view name)
{
	if (const auto& guild = getGuildByName(name)) {
		for (const auto& rank : guild->getRanks()) {
			guildRankList.insert(rank->id);
		}
	}
}

void AccessList::addGuildRank(std::string_view name, std::string_view rankName)
{
	if (const auto& guild = getGuildByName(name)) {
		if (const auto& rank = guild->getRankByName(rankName)) {
			guildRankList.insert(rank->id);
		}
	}
}

bool AccessList::isInList(const Player* player) const
{
	if (allowEveryone) {
		return true;
	}

	auto playerIt = playerList.find(player->getGUID());
	if (playerIt != playerList.end()) {
		return true;
	}

	const auto& rank = player->getGuildRank();
	return rank && guildRankList.contains(rank->id);
}

Door::Door(uint16_t type) : Item(type) {}

Attr_ReadValue Door::readAttr(AttrTypes_t attr, PropStream& propStream)
{
	if (attr == ATTR_HOUSEDOORID) {
		uint8_t doorId;
		if (!propStream.read<uint8_t>(doorId)) {
			return ATTR_READ_ERROR;
		}

		setDoorId(doorId);
		return ATTR_READ_CONTINUE;
	}
	return Item::readAttr(attr, propStream);
}

void Door::setHouse(House* house)
{
	auto houseRef = house ? house->weak_from_this().lock() : nullptr;
	if (auto currentHouse = this->house.lock()) {
		if (currentHouse != houseRef) {
			currentHouse->removeDoor(this);
		}
	}

	this->house = houseRef;

	if (houseRef && !accessList) {
		accessList = std::make_unique<AccessList>();
	}
}

bool Door::canUse(const Player* player)
{
    auto h = house.lock();
    if (!h) {
        return true;
    }

    if (h->getType() == HOUSE_TYPE_GUILDHALL) {
        if (h->getHouseAccessLevel(player) >= HOUSE_GUEST) {
            return true;
        }
    } else {
        if (h->getHouseAccessLevel(player) >= HOUSE_SUBOWNER) {
            return true;
        }
    }

    return accessList->isInList(player);
}

void Door::setAccessList(std::string_view textlist)
{
	if (!accessList) {
		accessList = std::make_unique<AccessList>();
	}

	accessList->parseList(textlist);
}

std::optional<std::string_view> Door::getAccessList() const
{
	if (house.expired()) {
		return std::nullopt;
	}

	return std::make_optional(accessList->getList());
}

void Door::onRemoved()
{
	Item::onRemoved();

	if (auto h = house.lock()) {
		h->removeDoor(this);
	}
	house.reset();
}

void House::updateDoorDescription() const
{
	const bool isGuildHall = (type == HOUSE_TYPE_GUILDHALL);
	const std::string_view houseType = isGuildHall ? "guildhall" : "house";

	std::ostringstream description;
	description << "It belongs to " << houseType << " '" << houseName << "'. ";

	if (owner != 0) {
		description << ownerName << " owns this " << houseType << ".";
	}
	else {
		description << "Nobody owns this " << houseType << ".";

		const int32_t housePrice = getInteger(ConfigManager::HOUSE_PRICE);
		if (housePrice != -1 && getBoolean(ConfigManager::HOUSE_DOOR_SHOW_PRICE)) {
			description << " It costs " << (getTileCount() * housePrice) << " gold coins.";
		}
	}

	// Reset system - show required resets
	if (requiredReset > 0) {
		description << " It requires " << requiredReset << " resets.";
	}

	{
		auto doorsSnapshot = getDoors();
		for (const auto& door : doorsSnapshot) {
			door->setSpecialDescription(description.str());
		}
	}
}

std::shared_ptr<House> Houses::getHouseByPlayerId(uint32_t playerId) const
{
	for (const auto& it : houseMap) {
		if (it.second && it.second->getOwner() == playerId) {
			return it.second;
		}
	}
	return nullptr;
}

bool Houses::loadHousesXML(const std::string& filename)
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.c_str());
	if (!result) {
		printXMLError("Error - Houses::loadHousesXML", filename, result);
		return false;
	}

	for (auto houseNode : doc.child("houses").children()) {
		pugi::xml_attribute houseIdAttribute = houseNode.attribute("houseid");
		if (!houseIdAttribute) {
			return false;
		}

		int32_t houseId = pugi::cast<int32_t>(houseIdAttribute.value());

		auto house = getHouse(houseId);
		if (!house) {
			LOG_ERROR(fmt::format("Error: [Houses::loadHousesXML] Unknown house, id = {}", houseId));
			return false;
		}

		house->setName(houseNode.attribute("name").as_string());

		Position entryPos(pugi::cast<uint16_t>(houseNode.attribute("entryx").value()),
		                  pugi::cast<uint16_t>(houseNode.attribute("entryy").value()),
		                  pugi::cast<uint16_t>(houseNode.attribute("entryz").value()));
		if (entryPos.x == 0 && entryPos.y == 0 && entryPos.z == 0) {
			LOG_WARN(fmt::format("[Warning - Houses::loadHousesXML] House entry not set - Name: {} - House id: {}", house->getName(), houseId));
		}
		house->setEntryPos(entryPos);

		house->setRent(pugi::cast<uint32_t>(houseNode.attribute("rent").value()));
		house->setRequiredReset(pugi::cast<uint32_t>(houseNode.attribute("reqreset").value()));
		house->setTownId(pugi::cast<uint32_t>(houseNode.attribute("townid").value()));
		if (houseNode.attribute("guildhall").as_bool()) {
			house->setType(HOUSE_TYPE_GUILDHALL);
		}

		house->setOwner(0, false);
	}
	return true;
}

time_t Houses::increasePaidUntil(RentPeriod_t rentPeriod, time_t paidUntil) const
{
	switch (rentPeriod) {
		case RENTPERIOD_DAILY:
			return paidUntil += 24 * 60 * 60;
		case RENTPERIOD_WEEKLY:
			return paidUntil += 7 * 24 * 60 * 60;
		case RENTPERIOD_MONTHLY:
			return paidUntil += 30 * 24 * 60 * 60;
		case RENTPERIOD_YEARLY:
			return paidUntil += 365 * 24 * 60 * 60;
		case RENTPERIOD_DEV:
			return paidUntil += 5 * 60;
		default:
			return paidUntil;
	}
}

std::string Houses::getRentPeriod(RentPeriod_t rentPeriod) const
{
	switch (rentPeriod) {
		case RENTPERIOD_DAILY:
			return "daily";
		case RENTPERIOD_WEEKLY:
			return "weekly";
		case RENTPERIOD_MONTHLY:
			return "monthly";
		case RENTPERIOD_YEARLY:
			return "annual";
		case RENTPERIOD_DEV:
			return "dev";
		default:
			return "never";
	}
}

void Houses::payHouses(RentPeriod_t rentPeriod) const
{
	if (rentPeriod == RENTPERIOD_NEVER) {
		return;
	}

	time_t currentTime = time(nullptr);
	for (const auto& it : houseMap) {
		House* house = it.second.get();
		if (house->getOwner() == 0) {
			continue;
		}

		const uint32_t rent = house->getRent();
		if (rent == 0 || house->getPaidUntil() > currentTime) {
			continue;
		}

		const uint32_t ownerId = house->getOwner();
		Town* town = g_game.map.towns.getTown(house->getTownId());
		if (!town) {
			continue;
		}

		if (house->getType() == HOUSE_TYPE_NORMAL) {
			Player player(nullptr);
			if (!IOLoginData::loadPlayerById(&player, ownerId)) {
				// Player doesn't exist, reset house owner
				house->setOwner(0);
				continue;
			}

			if (player.getBankBalance() >= rent) {
				player.setBankBalance(player.getBankBalance() - rent);

				time_t paidUntil = increasePaidUntil(rentPeriod, currentTime);
				house->setPaidUntil(paidUntil);
				house->setPayRentWarnings(0);
			} else {
				if (house->getPayRentWarnings() < 7) {
					int32_t daysLeft = 7 - house->getPayRentWarnings();

					auto letterPtr = Item::CreateItem(ITEM_LETTER_STAMPED);
				std::string period = getRentPeriod(rentPeriod);

				letterPtr->setText(fmt::format(
				    "Warning! \nThe {:s} rent of {:d} gold for your house \"{:s}\" is payable. Have it within {:d} days or you will lose this house.",
				    period, house->getRent(), house->getName(), daysLeft));
				g_game.internalAddItem(player.getInbox(), letterPtr.get(), INDEX_WHEREEVER, FLAG_NOLIMIT);
				house->setPayRentWarnings(house->getPayRentWarnings() + 1);
				} else {
					house->setOwner(0, true, &player);
				}
			}

			if (g_saveManager.savePlayerSync(&player) != SaveResult::Persisted) {
				LOG_ERROR(fmt::format("[House::payHouses] Failed to save player {} after rent payment.", player.getName()));
				continue;
			}
		} else { // HOUSE_TYPE_GUILDHALL
			auto guild = g_game.getGuild(ownerId);
			if (!guild) {
				guild = IOGuild::loadGuild(ownerId);
				if (!guild) {
					house->setOwner(0);
					continue;
				}
			}

			if (guild->getBankBalance() >= rent) {
				LOG_INFO(fmt::format("[Info - Houses::payHouses] Paying rent info - Name: {} - Guild: {} - Balance {} - Rent {} - New balance {}",
					house->getName(), guild->getName(), guild->getBankBalance(), rent, guild->getBankBalance() - rent));
				guild->setBankBalance(guild->getBankBalance() - rent);

				Database& db = Database::getInstance();
				std::ostringstream query;
				query << "INSERT INTO `guild_transactions` (`guild_id`,`type`,`category`,`balance`,`time`) VALUES (" << ownerId << ",\"WITHDRAW\",\"RENT\"," << rent << "," << currentTime << ");";
				db.executeQuery(query.str());

				time_t paidUntil = increasePaidUntil(rentPeriod, currentTime);
				house->setPaidUntil(paidUntil);
			} else {
				Player player(nullptr);
				if (!IOLoginData::loadPlayerById(&player, guild->getOwnerGUID())) {
					// Player doesn't exist, reset house owner
					house->setOwner(0);
					std::ostringstream ss;
					ss << "Error: Guild " << guild->getName() << " has an owner that does not exist: " << guild->getOwnerGUID();
					LOG_ERROR(ss.str());
					continue;
				}

				if (house->getPayRentWarnings() < 7) {
					int32_t daysLeft = 7 - house->getPayRentWarnings();

					auto letterPtr = Item::CreateItem(ITEM_LETTER_STAMPED);
				std::string period = getRentPeriod(rentPeriod);

				letterPtr->setText(fmt::format(
				    "Warning! \nThe {:s} rent of {:d} gold for your guildhall \"{:s}\" is payable. Have it within {:d} days or you will lose this guildhall.",
				    period, house->getRent(), house->getName(), daysLeft));
				DepotLocker* depot = player.getDepotLocker(town->getID());
				if (depot) {
					g_game.internalAddItem(depot, letterPtr.get(), INDEX_WHEREEVER, FLAG_NOLIMIT);
				}
				house->setPayRentWarnings(house->getPayRentWarnings() + 1);
				} else {
					house->setOwner(0, true, &player);
				}

			if (g_saveManager.savePlayerSync(&player) != SaveResult::Persisted) {
				LOG_ERROR(fmt::format("[House::payHouses] Failed to save player {} after rent payment.", player.getName()));
				continue;
			}
			}
		}
	}
}

bool House::addProtectionGuest(uint32_t playerId)
{
	if (playerId == 0 || playerId == owner) {
		return false;
	}

	auto result = protectionGuests.insert(playerId);
	if (result.second) {
		Database& db = Database::getInstance();
		std::ostringstream query;
		query << "INSERT INTO `house_guests` (`house_id`, `player_id`) VALUES (" << id << ", " << playerId << ")";
		return db.executeQuery(query.str());
	}
	return false;
}

bool House::removeProtectionGuest(uint32_t playerId)
{
	auto it = protectionGuests.find(playerId);
	if (it != protectionGuests.end()) {
		protectionGuests.erase(it);

		Database& db = Database::getInstance();
		std::ostringstream query;
		query << "DELETE FROM `house_guests` WHERE `house_id` = " << id << " AND `player_id` = " << playerId;
		return db.executeQuery(query.str());
	}
	return false;
}

bool House::isProtectionGuest(uint32_t playerId) const
{
	return protectionGuests.contains(playerId);
}

void House::clearProtectionGuests()
{
	if (!protectionGuests.empty()) {
		protectionGuests.clear();

		Database& db = Database::getInstance();
		std::ostringstream query;
		query << "DELETE FROM `house_guests` WHERE `house_id` = " << id;
		db.executeQuery(query.str());
	}
}

bool House::canModifyItems(const Player* player) const
{
	if (!player) {
		return false;
	}

	if (!isProtected) {
		return true;
	}

	if (player->hasFlag(PlayerFlag_CanEditHouses)) {
		return true;
	}

	if (player->getGUID() == owner) {
		return true;
	}

	if (isProtectionGuest(player->getGUID())) {
		return true;
	}

	return false;
}
