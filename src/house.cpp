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
		return false;
	}
	if (resetProtection) {
		return db.executeQuery(fmt::format(
		    "UPDATE `houses` SET `owner` = {:d}, `bid` = 0, `bid_end` = 0, `last_bid` = 0, `highest_bidder` = 0, `is_protected` = 0 WHERE `id` = {:d}",
		    guid_guild, id));
	}
	return db.executeQuery(fmt::format(
	    "UPDATE `houses` SET `owner` = {:d}, `bid` = 0, `bid_end` = 0, `last_bid` = 0, `highest_bidder` = 0 WHERE `id` = {:d}",
	    guid_guild, id));
}

namespace {
enum class OwnerLookup { Found, Missing, Failed };

OwnerLookup findOwnerPlayer(uint32_t guid, std::shared_ptr<Player>& player)
{
	if (g_saveManager.hasFailedRecovery(guid) || g_saveManager.hasPendingPlayerSave(guid)) {
		return OwnerLookup::Failed;
	}
	player = g_game.getPlayerByGUID(guid);
	if (player) {
		return OwnerLookup::Found;
	}
	player = std::make_shared<Player>(nullptr);
	if (IOLoginData::loadPlayerById(player.get(), guid)) {
		return OwnerLookup::Found;
	}

	// An existing player can fail to load if the group or vocation is invalid.
	Database& db = Database::getInstance();
	auto row = db.storeQuery(fmt::format("SELECT 1 FROM `players` WHERE `id` = {}", guid));
	const bool missing = !row && db.getLastErrno() == 0;
	player.reset();
	return missing ? OwnerLookup::Missing : OwnerLookup::Failed;
}

bool lockHouseOwner(Database& db, uint32_t houseId, uint32_t owner)
{
	auto row = db.storeQuery(fmt::format("SELECT `owner` FROM `houses` WHERE `id` = {} FOR UPDATE", houseId));
	return row && row->getNumber<uint32_t>("owner") == owner;
}

time_t initialPaidUntil()
{
	const auto period = asLowerCaseString(std::string{getString(ConfigManager::HOUSE_RENT_PERIOD)});
	const auto now = time(nullptr);
	if (period == "annual" || period == "yearly") {
		return now + 365 * 24 * 60 * 60;
	}
	if (period == "monthly") {
		return now + 30 * 24 * 60 * 60;
	}
	if (period == "weekly") {
		return now + 7 * 24 * 60 * 60;
	}
	if (period == "daily") {
		return now + 24 * 60 * 60;
	}
	return period == "dev" ? now + 5 * 60 : 0;
}
}

bool House::setOwner(uint32_t ownerId, bool updateDatabase, Player* previousPlayer)
{
	if (ownerTransitionInProgress || persistenceBlocked) {
		return false;
	}
	if (isLoaded && owner == ownerId) {
		return true;
	}

	if (updateDatabase && (g_dispatcher.isDispatcherThread() || g_game.map.houses.getHouse(id).get() == this)) {
		return changeOwners({{this, ownerId, previousPlayer}}, {});
	}

	const auto houseRef = weak_from_this().lock();
	ownerTransitionInProgress = true;
	struct TransitionGuard {
		bool& active;
		~TransitionGuard() { active = false; }
	} guard{ownerTransitionInProgress};
	try {
		const auto details = ownerId ? initializeOwnerDataFromDatabase(ownerId, type) : OwnerData{};
		// Detached, synchronous houses can supply their own persistence implementation.
		if (updateDatabase) {
			if ((owner && !BedItem::wakeUpAll(getBeds())) ||
			    (owner && townId && previousPlayer && !transferToDepot(previousPlayer, nullptr)) ||
			    !updateOwnerInDatabase(ownerId, ownerId == 0 || owner == 0)) {
				return false;
			}
		}
		if (!owner || !updateDatabase) {
			paidUntil = initialPaidUntil();
		}
		applyOwner(ownerId, details, updateDatabase);
		return true;
	} catch (const std::runtime_error& err) {
		LOG_ERROR("{}", err.what());
		return false;
	}
}

void House::blockPersistence()
{
	persistenceBlocked = true;
	LOG_CRITICAL("[House] Commit outcome unknown for house {}. House persistence is blocked until restart.", id);
}

bool House::changeOwners(const std::vector<OwnerChange>& changes, std::vector<Player*> players,
                         HouseItemMoves* tradeMoves, const std::function<bool()>& extraQueries)
{
	struct Transition {
		const OwnerChange& change;
		std::shared_ptr<House> houseRef;
		OwnerData details = {};
		Player* recipient = nullptr;
		time_t paidUntil = 0;
		~Transition() { change.house->ownerTransitionInProgress = false; }
	};
	struct Sleeper {
		Player* player;
		uint64_t sleepStart;
	};
	std::vector<std::unique_ptr<Transition>> transitions;
	std::unordered_map<uint32_t, std::shared_ptr<Player>> loadedPlayers;
	std::unordered_map<uint32_t, Player*> activePlayers;
	std::unordered_map<uint32_t, Sleeper> sleepers;
	std::vector<std::shared_ptr<BedItem>> beds;
	std::unordered_set<const BedItem*> releasedBeds;
	HouseItemMoves depotMoves;
	Database& db = Database::getInstance();

	for (auto* player : players) {
		activePlayers.emplace(player->getGUID(), player);
	}
	const auto includePlayer = [&](Player* player) {
		if (activePlayers.emplace(player->getGUID(), player).second) {
			players.push_back(player);
		}
	};
	const auto retainPlayer = [&](std::shared_ptr<Player> player) {
		const auto guid = player->getGUID();
		includePlayer(player.get());
		loadedPlayers.emplace(guid, std::move(player));
	};

	for (const auto& change : changes) {
		House* house = change.house;
		if (house->ownerTransitionInProgress || house->persistenceBlocked) {
			return false;
		}
		if (house->owner == change.ownerId) {
			continue;
		}
		auto transition = std::make_unique<Transition>(change, house->weak_from_this().lock());
		house->ownerTransitionInProgress = true;
		transition->paidUntil = house->owner == 0 ? initialPaidUntil() : house->paidUntil;

		if (change.ownerId != 0) {
			try {
				transition->details = house->initializeOwnerDataFromDatabase(change.ownerId, house->type);
			} catch (const std::runtime_error& err) {
				LOG_ERROR("{}", err.what());
				return false;
			}
		}

		if (house->owner != 0 && house->townId != 0) {
			uint32_t recipientId = house->owner;
			if (house->type == HOUSE_TYPE_GUILDHALL) {
				auto guild = db.storeQuery(fmt::format("SELECT `ownerid` FROM `guilds` WHERE `id` = {}", house->owner));
				if (!guild && db.getLastErrno() != 0) {
					return false;
				}
				recipientId = guild ? guild->getNumber<uint32_t>("ownerid") : 0;
				if (guild && change.previousPlayer && change.previousPlayer->getGuild() &&
				    change.previousPlayer->getGuild()->getId() == house->owner) {
					recipientId = change.previousPlayer->getGUID();
				}
			}

			if (recipientId != 0) {
				if (const auto it = activePlayers.find(recipientId); it != activePlayers.end()) {
					transition->recipient = it->second;
				} else if (change.previousPlayer && change.previousPlayer->getGUID() == recipientId) {
					transition->recipient = change.previousPlayer;
					includePlayer(transition->recipient);
				} else {
					std::shared_ptr<Player> recipient;
					if (findOwnerPlayer(recipientId, recipient) == OwnerLookup::Failed) {
						return false;
					}
					if (recipient) {
						transition->recipient = recipient.get();
						retainPlayer(std::move(recipient));
					}
				}
			}
		}
		transitions.push_back(std::move(transition));
	}

	for (const auto& transition : transitions) {
		House* house = transition->change.house;
		if (house->owner == 0) {
			continue;
		}
		for (const auto& bed : house->getBeds()) {
			if (!bed || bed->isRemoved() || bed->getSleeper() == 0) {
				continue;
			}
			const uint32_t guid = bed->getSleeper();
			const auto includeBed = [&](const std::shared_ptr<BedItem>& candidate) {
				if (candidate && candidate->getHouse().get() == house && candidate->getSleeper() == guid &&
				    releasedBeds.insert(candidate.get()).second) {
					beds.push_back(candidate);
				}
			};
			includeBed(bed);
			includeBed(bed->getNextBedItem());
			if (sleepers.contains(guid)) {
				continue;
			}
			if (!activePlayers.contains(guid)) {
				auto player = g_game.getPlayerByGUID(guid);
				if (!player) {
					player = std::make_shared<Player>(nullptr);
					if (!bed->loadOfflineSleeper(player.get(), guid)) {
						return false;
					}
				}
				retainPlayer(std::move(player));
			}
			sleepers.emplace(guid, Sleeper{activePlayers.at(guid), bed->sleepStart});
		}
	}

	if (!g_saveManager.canSavePlayersSync(players)) {
		return false;
	}
	for (const auto& transition : transitions) {
		if (transition->recipient && !transition->change.house->transferToDepot(transition->recipient, &depotMoves)) {
			return false;
		}
	}
	// Offline objects are disposable on rollback. Defer live regeneration until commit.
	for (const auto& entry : sleepers) {
		const auto& sleeper = entry.second;
		if (sleeper.player->isOffline()) {
			BedItem::regeneratePlayer(sleeper.player, sleeper.sleepStart);
		}
	}

	const auto result = g_saveManager.savePlayersAtomic(players, [&] {
		if (extraQueries && !extraQueries()) {
			return false;
		}
		for (const auto& transition : transitions) {
			House* house = transition->change.house;
			if (!lockHouseOwner(db, house->id, house->owner) ||
			    !house->updateOwnerInDatabase(transition->change.ownerId, true) ||
			    !db.executeQuery(fmt::format("UPDATE `houses` SET `paid` = {}, `warnings` = 0 WHERE `id` = {}",
			                                 transition->paidUntil, house->id)) ||
			    !db.executeQuery(fmt::format("DELETE FROM `house_lists` WHERE `house_id` = {}", house->id)) ||
			    !IOMapSerialize::saveHouseInTransaction(house, releasedBeds)) {
				return false;
			}
		}
		return true;
	});

	if (result == SaveManager::AtomicSaveResult::Failed) {
		return false;
	}
	if (result == SaveManager::AtomicSaveResult::Indeterminate) {
		depotMoves.discard();
		if (tradeMoves) {
			tradeMoves->discard();
		}
		for (const auto& transition : transitions) {
			transition->change.house->blockPersistence();
		}
		return false;
	}

	for (const auto& entry : sleepers) {
		g_game.removeBedSleeper(entry.first);
	}
	for (const auto& bed : beds) {
		bed->internalRemoveSleeper();
	}
	for (const auto& entry : sleepers) {
		const auto& sleeper = entry.second;
		if (!sleeper.player->isOffline()) {
			BedItem::regeneratePlayer(sleeper.player, sleeper.sleepStart);
			g_game.addCreatureHealth(sleeper.player);
		}
	}
	for (const auto& bed : beds) {
		if (!bed->isRemoved() && bed->getSleeper() == 0) {
			bed->updateAppearance(nullptr);
		}
	}
	for (const auto& transition : transitions) {
		House& house = *transition->change.house;
		house.paidUntil = transition->paidUntil;
		house.setProtected(false);
		house.applyOwner(transition->change.ownerId, transition->details, true);
	}
	depotMoves.commit();
	if (tradeMoves) {
		tradeMoves->commit();
	}
	return true;
}

House::GuildhallPurchase House::purchaseGuildhall(Player* buyer, uint64_t price)
{
	using Purchase = GuildhallPurchase;
	if (!buyer || type != HOUSE_TYPE_GUILDHALL || owner || persistenceBlocked || ownerTransitionInProgress) {
		return Purchase::Failed;
	}
	const auto guild = buyer->getGuild();
	const auto rank = buyer->getGuildRank();
	if (!guild || !rank || (rank->level != GUILDLEVEL_VICE && rank->level != GUILDLEVEL_LEADER)) {
		return Purchase::Failed;
	}

	const auto guildId = guild->getId();
	if (IOGuild::isBankBalanceBlocked(guildId)) {
		return Purchase::Indeterminate;
	}
	Database& db = Database::getInstance();
	auto row = db.storeQuery(fmt::format("SELECT `balance` FROM `guilds` WHERE `id` = {}", guildId));
	if (!row) {
		return Purchase::Failed;
	}
	const auto guildBalance = row->getNumber<uint64_t>("balance");
	const bool fromGuild = guildBalance >= price;
	const auto verifyGuild = [&] {
		auto current = db.storeQuery(fmt::format("SELECT `balance` FROM `guilds` WHERE `id` = {} FOR UPDATE", guildId));
		if (!current || current->getNumber<uint64_t>("balance") != guildBalance) {
			return false;
		}
		auto other = db.storeQuery(fmt::format(
		    "SELECT `id` FROM `houses` WHERE `owner` = {} AND `type` IN ('2', 'Guildhall') AND `id` <> {} LIMIT 1",
		    guildId, id));
		return !other && db.getLastErrno() == 0;
	};

	if (fromGuild) {
		const bool purchased = changeOwners({{this, guildId, nullptr}}, {}, nullptr, [&] {
			return verifyGuild() &&
			       (!price || (db.executeQuery(fmt::format("UPDATE `guilds` SET `balance` = `balance` - {} WHERE `id` = {}",
			                                              price, guildId)) && db.getAffectedRows() == 1)) &&
			       db.executeQuery(fmt::format(
			           "INSERT INTO `guild_transactions` (`guild_id`, `player_associated`, `type`, `category`, `balance`, `time`) "
			           "VALUES ({}, {}, 'WITHDRAW', 'RENT', {}, {})", guildId, buyer->getGUID(), price, time(nullptr)));
		});
		if (purchased) {
			guild->loadBankBalance(guildBalance - price);
			return Purchase::GuildBank;
		}
		if (persistenceBlocked) {
			IOGuild::blockBankBalance(guildId);
		}
	} else {
		const auto cash = std::min(price, buyer->getMoney());
		const auto balance = buyer->getBankBalance();
		const auto debit = price - cash;
		if (balance < debit) {
			return Purchase::InsufficientFunds;
		}
		if (!g_saveManager.canSavePlayersSync({buyer})) {
			return Purchase::Failed;
		}
		HouseItemMoves cashMoves;
		uint64_t change = 0;
		if (!g_game.removeMoney(buyer, cash, 0, &cashMoves, &change)) {
			return Purchase::Failed;
		}
		buyer->setBankBalance(balance - debit + change);
		if (changeOwners({{this, guildId, nullptr}}, {buyer}, &cashMoves, verifyGuild)) {
			return Purchase::PersonalFunds;
		}
		if (!persistenceBlocked) {
			buyer->setBankBalance(balance);
		}
	}
	return persistenceBlocked ? Purchase::Indeterminate : Purchase::Failed;
}

void House::applyOwner(uint32_t guid_guild, const OwnerData& newOwnerData, bool updateDatabase)
{
	isLoaded = true;

	if (owner != 0 && updateDatabase) {
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

		// clean access lists
		owner = 0;
		ownerAccountId = 0;
		ownerName.clear();
		if (updateDatabase) {
			isProtected = false;
		}
		setAccessList(SUBOWNER_LIST, "");
		setAccessList(GUEST_LIST, "");

		{
			auto doorsSnapshot = getDoors();
			for (const auto& door : doorsSnapshot) {
				door->setAccessList("");
			}
		}
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
}

AccessHouseLevel_t House::getHouseAccessLevel(const Player* player) const
{
    if (player && persistenceBlocked) {
        return HOUSE_NOT_INVITED;
    }
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

HouseItemMoves::Change HouseItemMoves::record(const std::shared_ptr<Item>& item) const
{
	Cylinder* source = item->getParent();
	std::shared_ptr<Thing> sourceRef;
	if (auto* container = source->getItem()) {
		sourceRef = container->weak_from_this().lock();
	} else if (auto* creature = source->getCreature()) {
		sourceRef = creature->weak_from_this().lock();
	} else if (auto* tile = dynamic_cast<Tile*>(source)) {
		sourceRef = tile->weak_from_this().lock();
	}
	return {item, item, std::move(sourceRef), source, source->getThingIndex(item.get()),
	        item->getInstanceID(), item->getID(), item->getSubType()};
}

ReturnValue HouseItemMoves::move(Item* item, Cylinder* destination, uint32_t flags, Item* tradeItem)
{
	auto ref = g_game.getItemSharedRef(item);
	if (!ref || !item->getParent()) {
		return RETURNVALUE_NOTPOSSIBLE;
	}
	auto change = record(ref);
	const auto result = g_game.internalMoveItem(change.source, destination, INDEX_WHEREEVER, item,
	    item->getItemCount(), nullptr, flags | FLAG_IGNOREAUTOSTACK | FLAG_MOVEWHOLE, nullptr, tradeItem);
	bool moved = false;
	for (Cylinder* parent = item->getParent(); parent; parent = parent->getParent()) {
		if (parent == destination) {
			moved = item->getParent() != change.source;
			break;
		}
	}
	if (item->getParent() != change.source || item->getInstanceID() != change.instanceId) {
		change.notify = moved;
		change.moved = moved;
		changes.push_back(std::move(change));
	}
	if (result != RETURNVALUE_NOERROR) {
		return result;
	}
	return moved ? RETURNVALUE_NOERROR : RETURNVALUE_NOTPOSSIBLE;
}

ReturnValue HouseItemMoves::remove(Item* item, uint32_t count)
{
	auto ref = g_game.getItemSharedRef(item);
	if (!ref || !item->getParent()) {
		return RETURNVALUE_NOTPOSSIBLE;
	}
	auto change = record(ref);
	const auto result = g_game.internalRemoveItem(item, count, false, FLAG_MOVEWHOLE);
	if (result == RETURNVALUE_NOERROR) {
		change.notify = true;
		changes.push_back(std::move(change));
	}
	return result;
}

std::shared_ptr<Item> HouseItemMoves::wrap(const std::shared_ptr<Item>& item, uint16_t wrapId)
{
	auto change = record(item);
	change.replacement = g_game.getItemSharedRef(g_game.transformItem(item.get(), wrapId, -1, false));
	const auto wrapped = change.replacement;
	changes.push_back(std::move(change));
	if (!wrapped || wrapped->isRemoved() || wrapped->getID() != wrapId ||
	    (wrapped != item && item->getContainer() && !item->getContainer()->empty())) {
		return nullptr;
	}
	return wrapped;
}

void HouseItemMoves::commit()
{
	auto committed = std::exchange(changes, {});
	for (const auto& change : committed) {
		if (!change.notify) {
			continue;
		}
		Item* item = change.original.get();
		Cylinder* destination = change.moved ? item->getParent() : nullptr;
		const auto instanceId = item->getInstanceID();
		item->setInstanceID(change.instanceId);
		change.source->postRemoveNotification(item, destination, change.index);
		item->setInstanceID(instanceId);
		if (destination && item->getParent() == destination) {
			const auto index = destination->getThingIndex(item);
			if (index != -1) {
				destination->postAddNotification(item, change.source, index);
			}
			item->startDecaying();
		}
	}
}

HouseItemMoves::~HouseItemMoves()
{
	for (auto it = changes.rbegin(); it != changes.rend(); ++it) {
		auto& change = *it;
		Item* current = change.replacement.get();
		Item* original = change.original.get();
		if ((current == original || !current) && original->getParent() == change.source &&
		    change.source->getThingIndex(original) != -1) {
			if (original->getID() != change.id || original->getSubType() != change.subtype) {
				change.source->updateThing(original, change.id, change.subtype);
			}
			original->setInstanceID(change.instanceId);
			continue;
		}
		Cylinder* parent = current ? current->getParent() : nullptr;
		if (parent) {
			parent->removeThing(current, current->getItemCount());
		}
		original->setID(change.id);
		original->setSubType(change.subtype);
		original->setInstanceID(change.instanceId);
		const auto sourceIndex = change.source->getItem() ? INDEX_WHEREEVER : change.index;
		change.source->addThing(sourceIndex, original);
		const int32_t index = change.source->getThingIndex(original);
		if (index == -1) {
			LOG_CRITICAL("[House] Failed to restore item {} to its original cylinder.", change.id);
		} else {
			original->startDecaying();
		}
	}
}

bool House::transferToDepot(Player* player, HouseItemMoves* moves) const
{
	if (townId == 0 || owner == 0) {
		return false;
	}

	Inbox* targetInbox = player->getInbox(townId);
	if (!targetInbox) {
		targetInbox = player->getInbox();
		if (targetInbox) {
			LOG_WARN(fmt::format("[House::transferToDepot] Fallback to player default inbox for house {} (townId {})", id, townId));
		} else {
			LOG_WARN(fmt::format("[House::transferToDepot] No inbox found for player when transferring house {} items", id));
			return false;
		}
	}

	const auto wrap = [&](const std::shared_ptr<Item>& item, uint16_t wrapId) {
		return moves ? moves->wrap(item, wrapId) : g_game.getItemSharedRef(g_game.transformItem(item.get(), wrapId));
	};
	const auto move = [&](Item* item) {
		return (moves ? moves->move(item, targetInbox, FLAG_NOLIMIT) :
		                g_game.internalMoveItem(item->getParent(), targetInbox, INDEX_WHEREEVER,
		                    item, item->getItemCount(), nullptr, FLAG_NOLIMIT)) == RETURNVALUE_NOERROR;
	};

	// Detached-house callbacks may mutate the house tile registry.
	for (const auto& tile : getTilesSnapshot()) {
		const TileItemVector* items = tile->getItemList();
		if (!items) {
			continue;
		}

		std::vector<std::shared_ptr<Item>> toProcess;
		for (const auto& item : *items) {
			toProcess.push_back(item);
		}

		for (const auto& item : toProcess) {
			if (!item || item->isRemoved() || tile->getThingIndex(item.get()) == -1) {
				continue;
			}
			if (Container* container = item->getContainer()) {
				std::vector<std::shared_ptr<Container>> subContainers = {g_game.getContainerSharedRef(container)};
				size_t idx = 0;
				while (idx < subContainers.size()) {
					const auto current = subContainers[idx++];
					if (!current || current->isRemoved() || current->getTile() != tile.get()) {
						continue;
					}
					std::vector<std::shared_ptr<Item>> children;
					for (const auto& child : current->getItemList()) {
						children.push_back(child);
					}

					for (const auto& child : children) {
						if (!child || child->isRemoved() || child->getParent() != current.get()) {
							continue;
						}
						auto processedChild = child;
						if (child->hasAttribute(ITEM_ATTRIBUTE_WRAPID)) {
							uint16_t wrapId = static_cast<uint16_t>(child->getIntAttr(ITEM_ATTRIBUTE_WRAPID));
							if (wrapId != 0) {
								processedChild = wrap(child, wrapId);
								if (!processedChild) {
									return false;
								}
							}
						}

						if (!processedChild || processedChild->isRemoved()) {
							continue;
						}
						if (Container* sub = processedChild->getContainer()) {
							subContainers.push_back(g_game.getContainerSharedRef(sub));
						} else if (processedChild->isPickupable()) {
							if (!move(processedChild.get())) {
								return false;
							}
						}
					}
				}
			}

			auto processedItem = item;
			if (processedItem->isRemoved() || tile->getThingIndex(processedItem.get()) == -1) {
				continue;
			}
			if (processedItem->hasAttribute(ITEM_ATTRIBUTE_WRAPID)) {
				uint16_t wrapId = static_cast<uint16_t>(processedItem->getIntAttr(ITEM_ATTRIBUTE_WRAPID));
				if (wrapId != 0) {
					processedItem = wrap(processedItem, wrapId);
					if (!processedItem) {
						return false;
					}
				}
			}

			if (!processedItem || processedItem->isRemoved()) {
				continue;
			}
			if (processedItem->isPickupable()) {
				if (!move(processedItem.get())) {
					return false;
				}
			} else if (Container* container = processedItem->getContainer()) {
				std::vector<std::shared_ptr<Item>> contents;
				for (const auto& content : container->getItemList()) {
					contents.push_back(content);
				}
				for (const auto& content : contents) {
					if (content && !content->isRemoved() && content->getParent() == container) {
						if (!move(content.get())) {
							return false;
						}
					}
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

bool HouseTransferItem::executeTrade(HouseTransferItem* first, HouseTransferItem* second, Player* seller,
                                     Player* buyer, HouseItemMoves& tradeMoves)
{
	std::vector<House::OwnerChange> changes;
	std::vector<std::shared_ptr<House>> houses;
	const auto addChange = [&](HouseTransferItem* item, Player* previousOwner, Player* newOwner) {
		if (!item) {
			return true;
		}
		auto house = item->house.lock();
		if (!house || house->transferItem.lock().get() != item || item->getParent() != &house->transfer_container) {
			return false;
		}
		uint32_t target = newOwner->getGUID();
		if (house->getType() == HOUSE_TYPE_GUILDHALL) {
			const auto& oldGuild = previousOwner->getGuild();
			const auto& newGuild = newOwner->getGuild();
			if (!oldGuild || !newGuild || oldGuild->getId() != house->getOwner()) {
				return false;
			}
			target = newGuild->getId();
		} else if (previousOwner->getGUID() != house->getOwner()) {
			return false;
		}
		if (!target || target == house->getOwner()) {
			return false;
		}
		changes.push_back({house.get(), target, previousOwner});
		houses.push_back(std::move(house));
		return true;
	};
	return addChange(first, seller, buyer) && addChange(second, buyer, seller) &&
	       House::changeOwners(changes, {seller, buyer}, &tradeMoves);
}

void HouseTransferItem::onTradeEvent(TradeEvents_t, Player*)
{
	// Deeds represent ownership and never enter a player's inventory.
	if (auto h = house.lock()) {
		h->resetTransferItem();
	}
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

void Houses::settleAuctions() const
{
	const auto now = time(nullptr);
	Database& db = Database::getInstance();
	const auto expiredAuctions = fmt::format(
	    "`owner` = 0 AND `bid_end` > 0 AND `bid_end` <= {}", now);
	auto rows = db.storeQuery(fmt::format(
	    "SELECT `id`, `highest_bidder`, `last_bid` FROM `houses` WHERE {}", expiredAuctions));
	if (!rows) {
		return;
	}

	do {
		const auto houseId = rows->getNumber<uint32_t>("id");
		const auto bidder = rows->getNumber<uint32_t>("highest_bidder");
		const auto price = rows->getNumber<uint64_t>("last_bid");
		const auto house = houseMap.find(houseId);
		if (house == houseMap.end() || house->second->getOwner() != 0 ||
		    house->second->isPersistenceBlocked() || g_saveManager.hasFailedRecovery(bidder)) {
			continue;
		}

		const auto auction = fmt::format("{} AND `id` = {} AND `highest_bidder` = {} AND `last_bid` = {}",
		                                 expiredAuctions, houseId, bidder, price);
		auto player = db.storeQuery(fmt::format("SELECT `balance` FROM `players` WHERE `id` = {}", bidder));
		if (!player && db.getLastErrno() != 0) {
			continue;
		}

		uint32_t owner = bidder;
		if (house->second->getType() == HOUSE_TYPE_GUILDHALL) {
			auto membership = db.storeQuery(fmt::format(
			    "SELECT `guild_id` FROM `guild_membership` WHERE `player_id` = {}", bidder));
			if (!membership && db.getLastErrno() != 0) {
				continue;
			}
			owner = membership ? membership->getNumber<uint32_t>("guild_id") : 0;
		}

		if (!owner || !player || player->getNumber<uint64_t>("balance") < price) {
			db.executeQuery(fmt::format(
			    "UPDATE `houses` SET `bid` = 0, `bid_end` = 0, `last_bid` = 0, `highest_bidder` = 0 WHERE {}",
			    auction));
			continue;
		}

		// Startup runs after WAL recovery and before players can log in.
		const bool settled = House::changeOwners({{house->second.get(), owner, nullptr}}, {}, nullptr, [&] {
			if (!db.storeQuery(fmt::format("SELECT `id` FROM `houses` WHERE {} FOR UPDATE", auction))) {
				return false;
			}
			if (!price) {
				return true;
			}
			return db.executeQuery(fmt::format(
			           "UPDATE `players` SET `balance` = `balance` - {} WHERE `id` = {} AND `balance` >= {}",
			           price, bidder, price)) &&
			       db.getAffectedRows() == 1;
		});
		if (!settled) {
			LOG_WARN("[House] Could not settle the auction for house {}.", houseId);
		}
	} while (rows->next());
}

void Houses::payHouses(RentPeriod_t rentPeriod) const
{
	if (rentPeriod == RENTPERIOD_NEVER) {
		return;
	}

	const auto currentTime = time(nullptr);
	const auto nextPayment = increasePaidUntil(rentPeriod, currentTime);
	Database& db = Database::getInstance();
	using Result = SaveManager::AtomicSaveResult;
	for (const auto& entry : houseMap) {
		auto& house = *entry.second;
		const uint32_t owner = house.getOwner();
		const uint32_t rent = house.getRent();
		const auto paid = house.getPaidUntil();
		const uint32_t warnings = house.getPayRentWarnings();
		if (!owner || !rent || paid > currentTime || house.persistenceBlocked || house.ownerTransitionInProgress) {
			continue;
		}

		const Town* town = g_game.map.towns.getTown(house.getTownId());
		if (!town) {
			continue;
		}
		const auto updateRent = [&](time_t newPaid, uint32_t newWarnings) {
			auto row = db.storeQuery(fmt::format(
			    "SELECT `owner`, `paid`, `warnings` FROM `houses` WHERE `id` = {} FOR UPDATE", house.getId()));
			return row && row->getNumber<uint32_t>("owner") == owner && row->getNumber<time_t>("paid") == paid &&
			       row->getNumber<uint32_t>("warnings") == warnings &&
			       db.executeQuery(fmt::format("UPDATE `houses` SET `paid` = {}, `warnings` = {} WHERE `id` = {}",
			                                   newPaid, newWarnings, house.getId()));
		};
		const bool guildhall = house.getType() == HOUSE_TYPE_GUILDHALL;
		uint32_t playerId = owner;
		if (guildhall) {
			if (IOGuild::isBankBalanceBlocked(owner)) {
				continue;
			}
			const auto row = db.storeQuery(fmt::format("SELECT `ownerid`, `balance` FROM `guilds` WHERE `id` = {}", owner));
			if (!row) {
				if (db.getLastErrno() == 0) {
					house.setOwner(0);
				}
				continue;
			}
			playerId = row->getNumber<uint32_t>("ownerid");
			const uint64_t balance = row->getNumber<uint64_t>("balance");
			if (balance >= rent) {
				const auto result = g_saveManager.savePlayersAtomic({}, [&] {
					return updateRent(nextPayment, 0) &&
					       db.executeQuery(fmt::format("UPDATE `guilds` SET `balance` = `balance` - {} WHERE `id` = {} AND `balance` = {}",
					                                   rent, owner, balance)) &&
					       db.getAffectedRows() == 1 &&
					       db.executeQuery(fmt::format(
					           "INSERT INTO `guild_transactions` (`guild_id`, `type`, `category`, `balance`, `time`) "
					           "VALUES ({}, 'WITHDRAW', 'RENT', {}, {})", owner, rent, currentTime));
				});
				if (result == Result::Committed) {
					if (auto guild = g_game.getGuild(owner)) {
						guild->loadBankBalance(balance - rent);
					}
					house.setPaidUntil(nextPayment);
					house.setPayRentWarnings(0);
				} else if (result == Result::Indeterminate) {
					IOGuild::blockBankBalance(owner);
					house.blockPersistence();
				}
				continue;
			}
		}

		std::shared_ptr<Player> player;
		const auto lookup = findOwnerPlayer(playerId, player);
		if (lookup != OwnerLookup::Found) {
			if (lookup == OwnerLookup::Missing) {
				house.setOwner(0);
			}
			continue;
		}

		const auto balance = player->getBankBalance();
		if (!guildhall && balance >= rent) {
			player->setBankBalance(balance - rent);
			const auto result = g_saveManager.savePlayersAtomic({player.get()}, [&] { return updateRent(nextPayment, 0); });
			if (result == Result::Committed) {
				house.setPaidUntil(nextPayment);
				house.setPayRentWarnings(0);
			} else if (result == Result::Failed) {
				player->setBankBalance(balance);
			} else {
				house.blockPersistence();
			}
			continue;
		}

		if (warnings >= 7) {
			house.setOwner(0, true, player.get());
			continue;
		}

		Cylinder* inbox = guildhall ? static_cast<Cylinder*>(player->getDepotLocker(town->getID())) :
		                              static_cast<Cylinder*>(player->getInbox());
		auto letter = Item::CreateItem(ITEM_LETTER_STAMPED);
		if (!inbox || !letter) {
			continue;
		}
		const std::string_view property = guildhall ? "guildhall" : "house";
		letter->setText(fmt::format(
		    "Warning! \nThe {} rent of {} gold for your {} \"{}\" is payable. Have it within {} days or you will lose this {}.",
		    getRentPeriod(rentPeriod), rent, property, house.getName(), 7 - warnings, property));
		if (g_game.internalAddItem(inbox, letter.get(), INDEX_WHEREEVER, FLAG_NOLIMIT) != RETURNVALUE_NOERROR) {
			continue;
		}
		const auto result = g_saveManager.savePlayersAtomic({player.get()}, [&] { return updateRent(paid, warnings + 1); });
		if (result == Result::Committed) {
			house.setPayRentWarnings(warnings + 1);
		} else if (result == Result::Failed) {
			g_game.internalRemoveItem(letter.get(), 1);
		} else {
			house.blockPersistence();
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
	if (!player || persistenceBlocked) {
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
