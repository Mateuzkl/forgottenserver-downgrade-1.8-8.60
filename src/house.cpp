// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "house.h"

#include "bed.h"
#include "configmanager.h"
#include "game.h"
#include "iologindata.h"
#include "logger.h"
#include "pugicast.h"

#include <fmt/format.h>

extern Game g_game;

House::House(uint32_t houseId) : id(houseId) {}

void House::addTile(HouseTile* tile)
{
	tile->setFlag(TILESTATE_PROTECTIONZONE);
	houseTiles.push_back(tile);
}

std::tuple<uint32_t, uint32_t, std::string, uint32_t, std::string> House::initializeOwnerDataFromDatabase(
    uint32_t guid_guild, HouseType_t type)
{
	if (guid_guild == 0) {
		return std::make_tuple(0, 0, std::string{}, 0, std::string{});
	}

	Database& db = Database::getInstance();

	if (type == HOUSE_TYPE_NORMAL) {
		std::ostringstream query;
		query << "SELECT `id`, `name`, `account_id` FROM `players` WHERE `id`=" << guid_guild;
		if (DBResult_ptr result = db.storeQuery(query.str())) {
			return std::make_tuple(result->getNumber<uint32_t>("id"), result->getNumber<uint32_t>("account_id"),
			                       std::string(result->getString("name")), uint32_t{0}, std::string{});
		}
		throw std::runtime_error("Error in House::setOwner - Failed to find player GUID");
	}

	// HOUSE_TYPE_GUILDHALL
	std::ostringstream query;
	query << "SELECT `g`.`id`, `g`.`name` as `guild_name`, `g`.`ownerid`, `p`.`name`, `p`.`account_id` ";
	query << "FROM `guilds` as `g` INNER JOIN `players` AS `p` ON `g`.`ownerid` = `p`.`id` ";
	query << "WHERE `g`.`id`=" << guid_guild;
	if (DBResult_ptr result = db.storeQuery(query.str())) {
		return std::make_tuple(result->getNumber<uint32_t>("ownerid"), result->getNumber<uint32_t>("account_id"),
		                       std::string(result->getString("name")), result->getNumber<uint32_t>("id"),
		                       std::string(result->getString("guild_name")));
	}
	throw std::runtime_error("Error in House::setOwner - Failed to find guild ID");
}

void House::setOwner(uint32_t guid_guild, bool updateDatabase /* = true*/, Player* previousPlayer /* = nullptr*/)
{
	if (updateDatabase && owner != guid_guild) {
		Database& db = Database::getInstance();
		bool resetProtection = (guid_guild == 0 || owner == 0);
		if (resetProtection) {
			db.executeQuery(fmt::format(
			    "UPDATE `houses` SET `owner` = {:d}, `bid` = 0, `bid_end` = 0, `last_bid` = 0, `highest_bidder` = 0, `is_protected` = 0 WHERE `id` = {:d}",
			    guid_guild, id));
			setProtected(false);
		} else {
			db.executeQuery(fmt::format(
			    "UPDATE `houses` SET `owner` = {:d}, `bid` = 0, `bid_end` = 0, `last_bid` = 0, `highest_bidder` = 0 WHERE `id` = {:d}",
			    guid_guild, id));
		}
	}

	if (isLoaded && owner == guid_guild) {
		return;
	}

	isLoaded = true;

	if (owner != 0 && updateDatabase) {
		// send items to depot
		if (previousPlayer) {
			transferToDepot(previousPlayer);
		} else {
			transferToDepot();
		}

		for (HouseTile* tile : houseTiles) {
			if (const CreatureVector* creatures = tile->getCreatures()) {
				for (int32_t i = creatures->size(); --i >= 0;) {
					kickPlayer(nullptr, (*creatures)[i]->getPlayer());
				}
			}
		}

		// Remove players from beds
		for (BedItem* bed : bedsList) {
			if (bed->getSleeper() != 0) {
				bed->wakeUp(nullptr);
			}
		}

		// clean access lists
		owner = 0;
		ownerAccountId = 0;
		ownerName.clear();
		if (updateDatabase) {
			isProtected = false;
		}
		setAccessList(SUBOWNER_LIST, "");
		setAccessList(GUEST_LIST, "");

		for (Door* door : doorSet) {
			door->setAccessList("");
		}
	} else {
		auto strRentPeriod =
		    boost::algorithm::to_lower_copy<std::string>(std::string{getString(ConfigManager::HOUSE_RENT_PERIOD)});
		time_t currentTime = time(nullptr);
		if (strRentPeriod == "yearly") {
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
		uint32_t sqlAccountId = 0, sqlPlayerGuid = 0, sqlGuildId = 0;
		std::string sqlPlayerName, sqlGuildName;
		try {
			std::tie(sqlPlayerGuid, sqlAccountId, sqlPlayerName, sqlGuildId, sqlGuildName) =
			    initializeOwnerDataFromDatabase(guid_guild, type);
		} catch (const std::runtime_error& err) {
			LOG_ERROR(fmt::format("{}", err.what()));
			return;
		}

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
		updateDoorDescription();
	}
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
	if (!houseTile || houseTile->getHouse() != this) {
		return false;
	}

	if (getHouseAccessLevel(player) < getHouseAccessLevel(target) || target->hasFlag(PlayerFlag_CanEditHouses)) {
		return false;
	}

	Position oldPosition = target->getPosition();
	if (g_game.internalTeleport(target, getEntryPosition()) == RETURNVALUE_NOERROR) {
		g_game.addMagicEffect(oldPosition, CONST_ME_POFF);
		g_game.addMagicEffect(getEntryPosition(), CONST_ME_TELEPORT);
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
		Door* door = getDoorByNumber(listId);
		if (door) {
			door->setAccessList(textlist);
		}

		// We do not have to kick anyone
		return;
	}

	// kick uninvited players
	for (HouseTile* tile : houseTiles) {
		if (CreatureVector* creatures = tile->getCreatures()) {
			for (int32_t i = creatures->size(); --i >= 0;) {
				Player* player = (*creatures)[i]->getPlayer();
				if (player && !isInvited(player)) {
					kickPlayer(nullptr, player);
				}
			}
		}
	}
}

bool House::transferToDepot() const
{
	if (townId == 0 || owner == 0) {
		return false;
	}

	if (type == HOUSE_TYPE_NORMAL) {
		Player* player = g_game.getPlayerByGUID(owner);
		if (player) {
			transferToDepot(player);
		} else {
			Player tmpPlayer(nullptr);
			if (!IOLoginData::loadPlayerById(&tmpPlayer, owner)) {
				return false;
			}

			transferToDepot(&tmpPlayer);
			IOLoginData::savePlayer(&tmpPlayer);
		}
	} else { // HOUSE_TYPE_GUILDHALL
		auto guild = g_game.getGuild(owner);
		if (!guild) {
			guild = IOGuild::loadGuild(owner);
			if (!guild) {
				LOG_WARN(fmt::format(
				    "Warning: [Houses::transferToDepot] Failed to find guild associated to guildhall = {}. Guild = {}",
				    id, owner));
				return false;
			}
		}
		Player* player = g_game.getPlayerByGUID(guild->getOwnerGUID());
		if (player) {
			transferToDepot(player);
		} else {
			Player tmpPlayer(nullptr);
			if (!IOLoginData::loadPlayerById(&tmpPlayer, guild->getOwnerGUID())) {
				return false;
			}
			transferToDepot(&tmpPlayer);
			IOLoginData::savePlayer(&tmpPlayer);
		}
	}
	return true;
}

bool House::transferToDepot(Player* player) const
{
	if (townId == 0 || owner == 0) {
		return false;
	}

	for (HouseTile* tile : houseTiles) {
		const TileItemVector* items = tile->getItemList();
		if (!items) {
			continue;
		}

		std::vector<Item*> toProcess;
		for (Item* item : *items) {
			toProcess.push_back(item);
		}

		for (Item* item : toProcess) {
			if (Container* container = item->getContainer()) {
				std::vector<Container*> subContainers = {container};
				size_t idx = 0;
				while (idx < subContainers.size()) {
					Container* current = subContainers[idx++];
					std::vector<Item*> children;
					for (Item* child : current->getItemList()) {
						children.push_back(child);
					}

					for (Item* child : children) {
						if (child->hasAttribute(ITEM_ATTRIBUTE_WRAPID)) {
							uint16_t wrapId = static_cast<uint16_t>(child->getIntAttr(ITEM_ATTRIBUTE_WRAPID));
							if (wrapId != 0) {
								g_game.transformItem(child, wrapId);
							}
						}

						if (Container* sub = child->getContainer()) {
							subContainers.push_back(sub);
						} else if (child->isPickupable()) {
							g_game.internalMoveItem(child->getParent(), player->getInbox(), INDEX_WHEREEVER, child,
							                        child->getItemCount(), nullptr, FLAG_NOLIMIT);
						}
					}
				}
			}

			Item* processedItem = item;
			if (processedItem->hasAttribute(ITEM_ATTRIBUTE_WRAPID)) {
				uint16_t wrapId = static_cast<uint16_t>(processedItem->getIntAttr(ITEM_ATTRIBUTE_WRAPID));
				if (wrapId != 0) {
					if (Item* newItem = g_game.transformItem(processedItem, wrapId)) {
						processedItem = newItem;
					}
				}
			}

			if (processedItem->isPickupable()) {
				g_game.internalMoveItem(processedItem->getParent(), player->getInbox(), INDEX_WHEREEVER, processedItem,
				                        processedItem->getItemCount(), nullptr, FLAG_NOLIMIT);
			} else if (Container* container = processedItem->getContainer()) {
				std::vector<Item*> contents;
				for (Item* content : container->getItemList()) {
					contents.push_back(content);
				}
				for (Item* content : contents) {
					g_game.internalMoveItem(content->getParent(), player->getInbox(), INDEX_WHEREEVER, content,
					                        content->getItemCount(), nullptr, FLAG_NOLIMIT);
				}
			}
		}
	}
	return true;
}

std::optional<std::string_view> House::getAccessList(uint32_t listId) const
{
	if (listId == GUEST_LIST) {
		return std::make_optional(guestList.getList());
	} else if (listId == SUBOWNER_LIST) {
		return std::make_optional(subOwnerList.getList());
	}

	Door* door = getDoorByNumber(listId);
	if (!door) {
		return std::nullopt;
	}

	return door->getAccessList();
}

bool House::isInvited(const Player* player) const { return getHouseAccessLevel(player) != HOUSE_NOT_INVITED; }

void House::addDoor(Door* door)
{
	door->incrementReferenceCounter();
	doorSet.insert(door);
	door->setHouse(this);
}

void House::removeDoor(Door* door)
{
	auto it = doorSet.find(door);
	if (it != doorSet.end()) {
		door->decrementReferenceCounter();
		doorSet.erase(it);
	}
}

void House::addBed(BedItem* bed)
{
	bedsList.push_back(bed);
	bed->setHouse(this);
}

Door* House::getDoorByNumber(uint32_t doorId) const
{
	for (Door* door : doorSet) {
		if (door->getDoorId() == doorId) {
			return door;
		}
	}
	return nullptr;
}

Door* House::getDoorByPosition(const Position& pos) const
{
	for (Door* door : doorSet) {
		if (door->getPosition() == pos) {
			return door;
		}
	}
	return nullptr;
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

HouseTransferItem* House::getTransferItem()
{
	if (transferItem != nullptr) {
		return nullptr;
	}

	transfer_container.setParent(nullptr);
	transferItem = HouseTransferItem::createHouseTransferItem(this);
	transfer_container.addThing(transferItem);
	return transferItem;
}

void House::resetTransferItem()
{
	if (transferItem) {
		Item* tmpItem = transferItem;
		transferItem = nullptr;
		transfer_container.setParent(nullptr);

		transfer_container.removeThing(tmpItem, tmpItem->getItemCount());
		g_game.ReleaseItem(tmpItem);
	}
}

HouseTransferItem* HouseTransferItem::createHouseTransferItem(House* house)
{
	HouseTransferItem* transferItem = new HouseTransferItem(house);
	transferItem->incrementReferenceCounter();
	transferItem->setID(ITEM_DOCUMENT_RO);
	transferItem->setSubType(1);
	transferItem->setSpecialDescription(fmt::format("It is a house transfer document for '{:s}'.", house->getName()));
	return transferItem;
}

void HouseTransferItem::onTradeEvent(TradeEvents_t event, Player* owner)
{
	if (event == ON_TRADE_TRANSFER) {
		if (house) {
			house->executeTransfer(this, owner);
		}

		g_game.internalRemoveItem(this, 1);
	} else if (event == ON_TRADE_CANCEL) {
		if (house) {
			house->resetTransferItem();
		}
	}
}

bool House::executeTransfer(HouseTransferItem* item, Player* newOwner)
{
	if (transferItem != item) {
		return false;
	}

	if (type == HOUSE_TYPE_NORMAL) {
		setOwner(newOwner->getGUID());
	} else {
		const auto& newOwnerGuild = newOwner->getGuild();
		if (newOwnerGuild) {
			setOwner(newOwnerGuild->getId());
		}
	}
	transferItem = nullptr;
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

		boost::algorithm::trim(line);

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
	Player* player = g_game.getPlayerByName(name);
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
	return rank && guildRankList.find(rank->id) != guildRankList.end();
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
	if (this->house != nullptr) {
		return;
	}

	this->house = house;

	if (!accessList) {
		accessList.reset(new AccessList());
	}
}

bool Door::canUse(const Player* player)
{
	if (!house) {
		return true;
	}

	if (house->getType() == HOUSE_TYPE_GUILDHALL) {
		if (house->getHouseAccessLevel(player) >= HOUSE_GUEST) {
			return true;
		}
	} else {
		if (house->getHouseAccessLevel(player) >= HOUSE_SUBOWNER) {
			return true;
		}
	}

	return accessList->isInList(player);
}

void Door::setAccessList(std::string_view textlist)
{
	if (!accessList) {
		accessList.reset(new AccessList());
	}

	accessList->parseList(textlist);
}

std::optional<std::string_view> Door::getAccessList() const
{
	if (!house) {
		return std::nullopt;
	}

	return std::make_optional(accessList->getList());
}

void Door::onRemoved()
{
	Item::onRemoved();

	if (house) {
		house->removeDoor(this);
	}
}

void House::updateDoorDescription() const
{
	const bool isGuildHall = (type == HOUSE_TYPE_GUILDHALL);
	const std::string_view houseType = isGuildHall ? "guildhall" : "house";

	std::ostringstream description;
	description << "It belongs to " << houseType << " '" << houseName << "'. ";

	if (owner != 0) {
		description << ownerName << " owns this " << houseType << ".";
	} else {
		description << "Nobody owns this " << houseType << ".";

		const int32_t housePrice = getInteger(ConfigManager::HOUSE_PRICE);
		if (housePrice != -1 && getBoolean(ConfigManager::HOUSE_DOOR_SHOW_PRICE)) {
			description << " It costs " << (houseTiles.size() * housePrice) << " gold coins.";
		}
	}

	// Reset system - show required resets
	if (requiredReset > 0) {
		description << " It requires " << requiredReset << " resets.";
	}

	for (Door* door : doorSet) {
		door->setSpecialDescription(description.str());
	}
}

House* Houses::getHouseByPlayerId(uint32_t playerId)
{
	for (const auto& it : houseMap) {
		if (it.second->getOwner() == playerId) {
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

		House* house = getHouse(houseId);
		if (!house) {
			LOG_ERROR(fmt::format("Error: [Houses::loadHousesXML] Unknown house, id = {}", houseId));
			return false;
		}

		house->setName(houseNode.attribute("name").as_string());

		Position entryPos(pugi::cast<uint16_t>(houseNode.attribute("entryx").value()),
		                  pugi::cast<uint16_t>(houseNode.attribute("entryy").value()),
		                  pugi::cast<uint16_t>(houseNode.attribute("entryz").value()));
		if (entryPos.x == 0 && entryPos.y == 0 && entryPos.z == 0) {
			LOG_WARN(fmt::format("[Warning - Houses::loadHousesXML] House entry not set - Name: {} - House id: {}",
			                     house->getName(), houseId));
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
		House* house = it.second;
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

					Item* letter = Item::CreateItem(ITEM_LETTER_STAMPED);
					std::string period = getRentPeriod(rentPeriod);

					letter->setText(fmt::format(
					    "Warning! \nThe {:s} rent of {:d} gold for your house \"{:s}\" is payable. Have it within {:d} days or you will lose this house.",
					    period, house->getRent(), house->getName(), daysLeft));
					g_game.internalAddItem(player.getInbox(), letter, INDEX_WHEREEVER, FLAG_NOLIMIT);
					house->setPayRentWarnings(house->getPayRentWarnings() + 1);
				} else {
					house->setOwner(0, true, &player);
				}
			}

			IOLoginData::savePlayer(&player);
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
				LOG_INFO(fmt::format(
				    "[Info - Houses::payHouses] Paying rent info - Name: {} - Guild: {} - Balance {} - Rent {} - New balance {}",
				    house->getName(), guild->getName(), guild->getBankBalance(), rent, guild->getBankBalance() - rent));
				guild->setBankBalance(guild->getBankBalance() - rent);

				Database& db = Database::getInstance();
				std::ostringstream query;
				query << "INSERT INTO `guild_transactions` (`guild_id`,`type`,`category`,`balance`,`time`) VALUES ("
				      << ownerId << ",\"WITHDRAW\",\"RENT\"," << rent << "," << currentTime << ");";
				db.executeQuery(query.str());

				time_t paidUntil = increasePaidUntil(rentPeriod, currentTime);
				house->setPaidUntil(paidUntil);
			} else {
				Player player(nullptr);
				if (!IOLoginData::loadPlayerById(&player, guild->getOwnerGUID())) {
					// Player doesn't exist, reset house owner
					house->setOwner(0);
					std::ostringstream ss;
					ss << "Error: Guild " << guild->getName()
					   << " has an owner that does not exist: " << guild->getOwnerGUID();
					LOG_ERROR(ss.str());
					continue;
				}

				if (house->getPayRentWarnings() < 7) {
					int32_t daysLeft = 7 - house->getPayRentWarnings();

					Item* letter = Item::CreateItem(ITEM_LETTER_STAMPED);
					std::string period = getRentPeriod(rentPeriod);

					letter->setText(fmt::format(
					    "Warning! \nThe {:s} rent of {:d} gold for your guildhall \"{:s}\" is payable. Have it within {:d} days or you will lose this guildhall.",
					    period, house->getRent(), house->getName(), daysLeft));
					DepotLocker* depot = player.getDepotLocker(town->getID());
					if (depot) {
						g_game.internalAddItem(depot, letter, INDEX_WHEREEVER, FLAG_NOLIMIT);
					}
					house->setPayRentWarnings(house->getPayRentWarnings() + 1);
				} else {
					house->setOwner(0, true, &player);
				}

				IOLoginData::savePlayer(&player);
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
	return protectionGuests.find(playerId) != protectionGuests.end();
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
