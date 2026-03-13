// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_PLAYER_H
#define FS_PLAYER_H

#include "configmanager.h"
#include "container.h"
#include "creature.h"
#include "cylinder.h"
#include "depotchest.h"
#include "depotlocker.h"
#include "enums.h"
#include "groups.h"
#include "guild.h"
#include "outfit.h"
#include "party.h"
#include "protocolgame.h"
#include "protocolspectator.h"
#include "rewardchest.h"
#include "town.h"
#include "vocation.h"

#include <bitset>

enum VirtueMonk_t : uint8_t {
	VIRTUE_NONE = 0,
	VIRTUE_HARMONY = 1,
	VIRTUE_JUSTICE = 2,
	VIRTUE_SUSTAIN = 3,
};

class House;
class NetworkMessage;
class Weapon;
class ProtocolGame;
class ProtocolSpectator;
class Npc;
class Party;
class SchedulerTask;
class Bed;
class Guild;

enum skillsid_t
{
	SKILLVALUE_LEVEL = 0,
	SKILLVALUE_TRIES = 1,
	SKILLVALUE_PERCENT = 2,
};

enum fightMode_t : uint8_t
{
	FIGHTMODE_ATTACK = 1,
	FIGHTMODE_BALANCED = 2,
	FIGHTMODE_DEFENSE = 3,
};

enum tradestate_t : uint8_t
{
	TRADE_NONE,
	TRADE_INITIATED,
	TRADE_ACCEPT,
	TRADE_ACKNOWLEDGE,
	TRADE_TRANSFER,
};

struct VIPEntry
{
	VIPEntry(uint32_t guid, std::string_view name) : guid{guid}, name{name} {}

	uint32_t guid;
	std::string name;
};

struct OpenContainer
{
	Container* container;
	uint16_t index;
};

inline constexpr int16_t MINIMUM_SKILL_LEVEL = 10;

struct Skill
{
	uint64_t tries = 0;
	uint16_t level = MINIMUM_SKILL_LEVEL;
	uint8_t percent = 0;
};

using AutoLootMap = std::map<uint16_t, std::pair<uint16_t, bool>>;

struct AutoLootConfig
{
	AutoLootMap itemList;
	bool lootAnything = false;
	bool enabled = true;
	std::string text;
};

using MuteCountMap = std::map<uint32_t, uint32_t>;

inline constexpr int32_t PLAYER_MIN_SPEED = 10;
inline constexpr int32_t PLAYER_MAX_BLESSINGS = 5;

inline constexpr int32_t AVATAR_TIMER_STORAGE = 50099;

class Player final : public Creature, public Cylinder
{
public:
	explicit Player(ProtocolGame_ptr p);
	~Player();

	// non-copyable
	Player(const Player&) = delete;
	Player& operator=(const Player&) = delete;

	Player* getPlayer() override { return this; }
	const Player* getPlayer() const override { return this; }

	void setID() override
	{
		if (id == 0) {
			id = playerAutoID++;
		}
	}

	virtual void setLossSkill(bool _skillLoss);

	static MuteCountMap muteCountMap;

	const std::string& getName() const override { return name; }
	void setName(std::string_view name) { this->name = name; }
	const std::string& getNameDescription() const override { return name; }
	std::string getDescription(int32_t lookDistance) const override;

	CreatureType_t getType() const override { return CREATURETYPE_PLAYER; }

	uint16_t getRandomMount() const;
	uint16_t getCurrentMount() const;
	void setCurrentMount(uint16_t mountId);
	bool isMounted() const { return defaultOutfit.lookMount != 0; }
	bool toggleMount(bool mount);
	bool tameMount(uint16_t mountId);
	bool untameMount(uint16_t mountId);
	bool hasMount(const Mount* mount) const;
	bool hasMounts() const;
	void dismount();

	void doReset(); // reset system

	void sendFYIBox(std::string_view message)
	{
		if (client) {
			client->sendFYIBox(message);
		}
	}

	void setGUID(uint32_t guid) { this->guid = guid; }
	uint32_t getGUID() const { return guid; }
	bool canSeeInvisibility() const override { return hasFlag(PlayerFlag_CanSenseInvisibility) || group->access; }

	void removeList() override;
	void addList() override;
	void kickPlayer(bool displayEffect);

	static uint64_t getExpForLevel(const uint64_t lv)
	{
		return (((lv - 6ULL) * lv + 17ULL) * lv - 12ULL) / 6ULL * 100ULL;
	}

	uint16_t getStaminaMinutes() const { return staminaMinutes; }

	uint64_t getBankBalance() const { return bankBalance; }
	void setBankBalance(uint64_t balance) { bankBalance = balance; }

	// Offline Training
	static constexpr int32_t SKILL_OFFLINE_AUTO = 255;
	bool addOfflineTrainingTries(skills_t skill, uint64_t tries);
	void applyOfflineTraining(uint32_t trainingTime);

	void addOfflineTrainingTime(int32_t addTime)
	{
		offlineTrainingTime = std::min<int32_t>(12 * 3600 * 1000, offlineTrainingTime + addTime);
	}
	void removeOfflineTrainingTime(int32_t removeTime)
	{
		offlineTrainingTime = std::max<int32_t>(0, offlineTrainingTime - removeTime);
	}
	int32_t getOfflineTrainingTime() const { return offlineTrainingTime; }

	int32_t getOfflineTrainingSkill() const { return offlineTrainingSkill; }
	void setOfflineTrainingSkill(int32_t skill) { offlineTrainingSkill = skill; }

	Guild_ptr getGuild() const { return guild; }
	void setGuild(Guild_ptr guild);

	GuildRank_ptr getGuildRank() const { return guildRank; }
	void setGuildRank(GuildRank_ptr newGuildRank) { guildRank = newGuildRank; }

	bool isGuildMate(const Player* player) const;

	std::string_view getGuildNick() const { return guildNick; }
	void setGuildNick(std::string nick) { guildNick = nick; }

	bool isInWar(const Player* player) const;
	bool isInWarList(uint32_t guildId) const;

	void setLastWalkthroughAttempt(int64_t walkthroughAttempt) { lastWalkthroughAttempt = walkthroughAttempt; }
	void setLastWalkthroughPosition(Position walkthroughPosition) { lastWalkthroughPosition = walkthroughPosition; }

	uint16_t getClientIcons() const;

	const GuildWarVector& getGuildWarVector() const { return guildWarVector; }

	Vocation* getVocation() const { return vocation; }

	uint32_t getResetCount() const { return reset; }
	void addReset(uint32_t count = 1);
	void setResetCount(uint32_t count);
	double getResetExpReduction() const;

	OperatingSystem_t getOperatingSystem() const { return operatingSystem; }
	void setOperatingSystem(OperatingSystem_t clientos) { operatingSystem = clientos; }

	uint16_t getProtocolVersion() const
	{
		if (!client) {
			return 0;
		}

		return client->getVersion();
	}

	bool hasSecureMode() const { return secureMode; }

	void setParty(Party* party) { this->party = party; }
	Party* getParty() const { return party; }
	PartyShields_t getPartyShield(const Player* player) const;
	bool isInviting(const Player* player) const;
	bool isPartner(const Player* player) const;
	void sendPlayerPartyIcons(Player* player);
	bool addPartyInvitation(Party* party);
	void removePartyInvitation(Party* party);
	void clearPartyInvitations();

	GuildEmblems_t getGuildEmblem(const Player* player) const;
	void reloadWarList(bool updateVisuals = true);

	uint64_t getSpentMana() const { return manaSpent; }

	bool hasFlag(PlayerFlags value) const { return (group->flags & value) != 0; }

	BedItem* getBedItem() { return bedItem; }
	void setBedItem(BedItem* b) { bedItem = b; }

	void addBlessing(uint8_t blessing) { blessings.set(blessing); }
	void removeBlessing(uint8_t blessing) { blessings.reset(blessing); }
	bool hasBlessing(uint8_t blessing) const { return blessings.test(blessing); }

	uint8_t getHarmony() const { return m_harmony; }
	void setHarmony(uint8_t value) {
		uint8_t minHarmony = (getVirtue() == VIRTUE_HARMONY) ? 1 : 0;
		m_harmony = static_cast<uint8_t>(std::clamp<int>(value, minHarmony, 5));
	}
	void addHarmony(uint8_t value) { setHarmony(m_harmony + value); }
	void removeHarmony(uint8_t value) {
		int newVal = static_cast<int>(m_harmony) - static_cast<int>(value);
		setHarmony(static_cast<uint8_t>(std::max(newVal, 0)));
	}

	bool isSerene() const { return m_serene; }
	void setSerene(bool serene) { m_serene = serene; }

	uint64_t getSereneCooldown() const {
		uint64_t now = OTSYS_TIME();
		if (m_serene_cooldown > now) {
			return m_serene_cooldown - now;
		}
		return 0;
	}
	void setSereneCooldown(uint64_t addTime) {
		m_serene_cooldown = OTSYS_TIME() + addTime;
	}

	VirtueMonk_t getVirtue() const { return m_virtue; }
	void setVirtue(VirtueMonk_t virtue) {
		switch (virtue) {
			case VIRTUE_HARMONY:
			case VIRTUE_JUSTICE:
			case VIRTUE_SUSTAIN:
				m_virtue = virtue;
				break;
			default:
				m_virtue = VIRTUE_NONE;
				break;
		}
	}

	void clearCooldowns();

	bool isOffline() const { return (getID() == 0); }
	void disconnect()
	{
		if (client) {
			client->disconnect();
		}
	}
	uint32_t getIP() const;
	uint32_t getLastIP() const { return lastIP; }

	void addContainer(uint8_t cid, Container* container);
	void closeContainer(uint8_t cid);
	void setContainerIndex(uint8_t cid, uint16_t index);

	Container* getContainerByID(uint8_t cid);
	int8_t getContainerID(const Container* container) const;
	uint16_t getContainerIndex(uint8_t cid) const;

	bool canOpenCorpse(uint32_t ownerId) const;

	void setStorageValue(const uint32_t key, const std::optional<int64_t> value, const bool isSpawn = false) override;

	void setGroup(Group* newGroup) { group = newGroup; }
	Group* getGroup() const { return group; }

	void setLastDepotId(int16_t newId) { lastDepotId = newId; }
	int16_t getLastDepotId() const { return lastDepotId; }

	int32_t getIdleTime() const { return idleTime; }

	void resetIdleTime() { idleTime = 0; }

	bool isInGhostMode() const override { return ghostMode; }
	bool canSeeGhostMode(const Creature* creature) const override;
	void switchGhostMode() { ghostMode = !ghostMode; }

	uint32_t getAccount() const { return accountNumber; }
	AccountType_t getAccountType() const { return accountType; }
	uint32_t getLevel() const { return level; }
	uint32_t getReset() const { return reset; }
	void setReset(uint32_t newReset) { reset = newReset; }
	uint8_t getLevelPercent() const { return levelPercent; }
	uint32_t getMagicLevel() const { return std::max<int32_t>(0, magLevel + varStats[STAT_MAGICPOINTS]); }
	uint32_t getSpecialMagicLevel(CombatType_t type) const
	{
		return std::max<int32_t>(0, specialMagicLevelSkill[combatTypeToIndex(type)]);
	}
	int32_t getExperienceRate(ExperienceRateType type) const { return experienceRate[static_cast<size_t>(type)]; }
	uint32_t getBaseMagicLevel() const { return magLevel; }
	uint8_t getMagicLevelPercent() const { return magLevelPercent; }
	uint8_t getSoul() const { return soul; }
	bool isAccessPlayer() const { return group->access; }
	bool isPremium() const;
	void setPremiumTime(time_t premiumEndsAt);

	// Token Protection System
	bool isTokenProtected() const { return tokenProtected; }
	void setTokenProtected(bool protected_) { tokenProtected = protected_; }
	std::string_view getTokenHash() const { return tokenHash; }
	void setTokenHash(std::string_view hash) { tokenHash = hash; }
	bool canMoveOwnItems(const Item* item) const;
	bool isTokenLocked() const { return tokenLocked; }
	void setTokenLocked(bool locked) { tokenLocked = locked; }
	bool unlockWithToken(const std::string& token);

	bool setVocation(uint16_t vocId);
	uint16_t getVocationId() const { return vocation->getId(); }
	bool isSorcerer() const { return vocation->getId() == 1 || vocation->getFromVocation() == 1; }
	bool isDruid() const { return vocation->getId() == 2 || vocation->getFromVocation() == 2; }
	bool isPaladin() const { return vocation->getId() == 3 || vocation->getFromVocation() == 3; }
	bool isKnight() const { return vocation->getId() == 4 || vocation->getFromVocation() == 4; }
	bool isMonk() const { return vocation->getId() == 9 || vocation->getFromVocation() == 9; }

	PlayerSex_t getSex() const { return sex; }
	void setSex(PlayerSex_t);
	uint64_t getExperience() const { return experience; }

	time_t getLastLoginSaved() const { return lastLoginSaved; }

	time_t getLastLogout() const { return lastLogout; }

	const Position& getLoginPosition() const { return loginPosition; }
	const Position& getTemplePosition() const { return town->getTemplePosition(); }
	Town* getTown() const { return town; }
	void setTown(Town* town) { this->town = town; }

	void clearModalWindows();
	bool hasModalWindowOpen(uint32_t modalWindowId) const;
	void onModalWindowHandled(uint32_t modalWindowId);

	bool isPushable() const override;
	uint32_t isMuted() const;
	void addMessageBuffer();
	void removeMessageBuffer();

	bool removeItemOfType(uint16_t itemId, uint32_t amount, int32_t subType, bool ignoreEquipped = false) const;

	uint32_t getCapacity() const
	{
		if (hasFlag(PlayerFlag_CannotPickupItem)) {
			return 0;
		} else if (hasFlag(PlayerFlag_HasInfiniteCapacity)) {
			return std::numeric_limits<uint32_t>::max();
		}
		return capacity;
	}

	uint32_t getFreeCapacity() const
	{
		if (hasFlag(PlayerFlag_CannotPickupItem)) {
			return 0;
		} else if (hasFlag(PlayerFlag_HasInfiniteCapacity)) {
			return std::numeric_limits<uint32_t>::max();
		}
		return std::max<int32_t>(0, capacity - inventoryWeight);
	}

	int32_t getMaxHealth() const override { return std::max<int32_t>(1, healthMax + varStats[STAT_MAXHITPOINTS]); }
	uint32_t getMana() const { return mana; }
	uint32_t getMaxMana() const { return std::max<int32_t>(0, manaMax + varStats[STAT_MAXMANAPOINTS]); }

	Item* getInventoryItem(slots_t slot) const;

	bool isItemAbilityEnabled(slots_t slot) const { return inventoryAbilities[slot]; }
	void setItemAbility(slots_t slot, bool enabled) { inventoryAbilities[slot] = enabled; }

	void setVarSkill(skills_t skill, int32_t modifier) { varSkills[skill] += modifier; }

	void setVarSpecialSkill(SpecialSkills_t skill, int32_t modifier) { varSpecialSkills[skill] += modifier; }

	void setSpecialMagicLevelSkill(CombatType_t type, int16_t modifier)
	{
		specialMagicLevelSkill[combatTypeToIndex(type)] += modifier;
	}

	void setExperienceRate(ExperienceRateType type, int32_t rate) { experienceRate[static_cast<size_t>(type)] = rate; }
	void addExperienceRate(ExperienceRateType type, int32_t rate) { experienceRate[static_cast<size_t>(type)] += rate; }

	void setVarStats(stats_t stat, int32_t modifier);
	int32_t getDefaultStats(stats_t stat) const;

	void addConditionSuppressions(uint32_t conditions);
	void removeConditionSuppressions(uint32_t conditions);

	DepotChest* getDepotChest(uint32_t depotId, bool autoCreate);
	DepotLocker* getDepotLocker(uint32_t depotId);
	RewardChest& getRewardChest();
	Inbox* getInbox();
	void onReceiveMail() const;
	bool isNearDepotBox() const;

	bool canSee(const Position& pos) const override;
	bool canSeeCreature(const Creature* creature) const override;

	bool canWalkthrough(const Creature* creature) const;
	bool canWalkthroughEx(const Creature* creature) const;

	RaceType_t getRace() const override { return RACE_BLOOD; }

	uint64_t getMoney() const;

	// safe-trade functions
	void setTradeState(tradestate_t state) { tradeState = state; }
	tradestate_t getTradeState() const { return tradeState; }
	Item* getTradeItem() { return tradeItem; }

	// shop functions
	void setShopOwner(Npc* owner, int32_t onBuy, int32_t onSell)
	{
		shopOwner = owner;
		purchaseCallback = onBuy;
		saleCallback = onSell;
	}

	Npc* getShopOwner(int32_t& onBuy, int32_t& onSell)
	{
		onBuy = purchaseCallback;
		onSell = saleCallback;
		return shopOwner;
	}

	const Npc* getShopOwner(int32_t& onBuy, int32_t& onSell) const
	{
		onBuy = purchaseCallback;
		onSell = saleCallback;
		return shopOwner;
	}

	// V.I.P. functions
	void notifyStatusChange(Player* loginPlayer, VipStatus_t status);
	bool removeVIP(uint32_t vipGuid);
	bool addVIP(uint32_t vipGuid, std::string_view vipName, VipStatus_t status);
	bool addVIPInternal(uint32_t vipGuid);

	// follow functions
	bool setFollowCreature(Creature* creature) override;
	void goToFollowCreature() override;

	// follow events
	void onFollowCreature(const Creature* creature) override;

	// walk events
	using Creature::onWalk;
	void onWalk(Direction& dir) override;
	void onWalkAborted() override;
	void onWalkComplete() override;

	void stopWalk();
	void openShopWindow(const std::list<ShopInfo>& shop);
	bool closeShopWindow(bool sendCloseShopWindow = true);
	bool updateSaleShopList(const Item* item);
	bool hasShopItemForSale(uint32_t itemId, uint8_t subType) const;

	void setChaseMode(bool mode);
	void setFightMode(fightMode_t mode) { fightMode = mode; }
	void setSecureMode(bool mode) { secureMode = mode; }

	void setAttackSpeed(uint32_t speed) { attackSpeed = speed; }
	uint32_t getAttackSpeed() const
	{
		if (attackSpeed > 0) {
			return attackSpeed;
		}
		return vocation->getAttackSpeed();
	}

	// combat functions
	bool setAttackedCreature(Creature* creature) override;
	bool isImmune(CombatType_t type) const override;
	bool isImmune(ConditionType_t type) const override;
	bool hasShield() const;
	bool isAttackable() const override;
	static bool lastHitIsPlayer(Creature* lastHitCreature);

	void changeHealth(int32_t healthChange, bool sendHealthChange = true) override;
	void changeMana(int32_t manaChange);
	void changeSoul(int32_t soulChange);

	bool isPzLocked() const { return pzLocked; }
	BlockType_t blockHit(Creature* attacker, CombatType_t combatType, int32_t& damage, bool checkDefense = false,
	                     bool checkArmor = false, bool field = false, bool ignoreResistances = false) override;
	void doAttacking(uint32_t interval) override;
	bool hasExtraSwing() override { return lastAttack > 0 && ((OTSYS_TIME() - lastAttack) >= getAttackSpeed()); }
	void maintainAttackFlow();

	uint16_t getSpecialSkill(uint8_t skill) const
	{
		return static_cast<uint16_t>(std::max<int32_t>(0, varSpecialSkills[skill]));
	}
	uint16_t getSkillLevel(uint8_t skill) const
	{
		return static_cast<uint16_t>(std::max<int32_t>(0, skills[skill].level + varSkills[skill]));
	}
	uint16_t getSpecialMagicLevelSkill(CombatType_t type) const
	{
		return static_cast<uint16_t>(std::max<int32_t>(0, specialMagicLevelSkill[combatTypeToIndex(type)]));
	}
	uint16_t getBaseSkill(uint8_t skill) const { return skills[skill].level; }
	uint8_t getSkillPercent(uint8_t skill) const { return skills[skill].percent; }
	uint64_t getSkillTries(uint8_t skill) const { return skills[skill].tries; }

	bool getAddAttackSkill() const { return addAttackSkillPoint; }
	BlockType_t getLastAttackBlockType() const { return lastAttackBlockType; }

	Item* getWeapon(slots_t slot, bool ignoreAmmo) const;
	Item* getWeapon(bool ignoreAmmo = false) const;
	WeaponType_t getWeaponType() const;
	int32_t getWeaponSkill(const Item* item) const;
	void getShieldAndWeapon(const Item*& shield, const Item*& weapon) const;

	void drainHealth(Creature* attacker, int32_t damage) override;
	void drainMana(Creature* attacker, int32_t manaLoss);
	void addManaSpent(uint64_t amount, bool artificial = false);
	void removeManaSpent(uint64_t amount, bool notify = false);
	void addSkillAdvance(skills_t skill, uint64_t count, bool artificial = false);
	void removeSkillTries(skills_t skill, uint64_t count, bool notify = false);

	int32_t getArmor() const override;
	int32_t getDefense() const override;
	float getAttackFactor() const override;
	float getDefenseFactor() const override;

	void addCombatExhaust(uint32_t ticks);
	void addHealExhaust(uint32_t ticks);
	void addInFightTicks(bool pzlock = false);

	uint64_t getGainedExperience(Creature* attacker) const override;

	// combat event functions
	void onAddCondition(ConditionType_t type) override;
	void onAddCombatCondition(ConditionType_t type) override;
	void onEndCondition(ConditionType_t type) override;
	void onCombatRemoveCondition(Condition* condition) override;
	void onAttackedCreature(Creature* target, bool addFightTicks = true) override;
	void onAttacked() override;
	void onAttackedCreatureDrainHealth(Creature* target, int32_t points) override;
	void onTargetCreatureGainHealth(Creature* target, int32_t points) override;
	bool onKilledCreature(Creature* target, bool lastHit = true) override;
	void onGainExperience(uint64_t gainExp, Creature* target) override;
	void onGainSharedExperience(uint64_t gainExp, Creature* source);
	void onAttackedCreatureBlockHit(BlockType_t blockType) override;
	void onBlockHit() override;
	void onChangeZone(ZoneType_t zone) override;
	void onAttackedCreatureChangeZone(ZoneType_t zone) override;
	void onIdleStatus() override;
	void onPlacedCreature() override;

	LightInfo getCreatureLight() const override;

	Skulls_t getSkull() const override;
	Skulls_t getSkullClient(const Creature* creature) const override;
	int64_t getSkullTicks() const { return skullTicks; }
	void setSkullTicks(int64_t ticks) { skullTicks = ticks; }

	bool hasAttacked(const Player* attacked) const;
	void addAttacked(const Player* attacked);
	void removeAttacked(const Player* attacked);
	void clearAttacked();
	void addUnjustifiedDead(const Player* attacked);
	void sendCreatureSkull(const Creature* creature) const
	{
		if (client) {
			client->sendCreatureSkull(creature);
		}
	}
	void sendCreatureEmblem(Creature* creature) const
	{
		if (client) {
			client->sendCreatureEmblem(creature);
		}
	}

	void checkSkullTicks(int64_t ticks);

	bool canWear(uint32_t lookType, uint8_t addons) const;
	bool hasOutfit(uint32_t lookType, uint8_t addons);
	void addOutfit(uint16_t lookType, uint8_t addons);
	bool removeOutfit(uint16_t lookType);
	bool removeOutfitAddon(uint16_t lookType, uint8_t addons);
	bool getOutfitAddons(const Outfit& outfit, uint8_t& addons) const;

	size_t getMaxVIPEntries() const;
	size_t getMaxDepotItems() const;

	// tile
	// send methods
	void sendAddTileItem(const Tile* tile, const Position& pos, const Item* item)
	{
		if (client) {
			int32_t stackpos = tile->getStackposOfItem(this, item);
			if (stackpos != -1) {
				client->sendAddTileItem(pos, stackpos, item);
			}
		}
	}
	void sendUpdateTileItem(const Tile* tile, const Position& pos, const Item* item)
	{
		if (client) {
			int32_t stackpos = tile->getStackposOfItem(this, item);
			if (stackpos != -1) {
				client->sendUpdateTileItem(pos, stackpos, item);
			}
		}
	}
	void sendRemoveTileThing(const Position& pos, int32_t stackpos)
	{
		if (stackpos != -1 && client) {
			client->sendRemoveTileThing(pos, stackpos);
		}
	}
	void sendUpdateTileCreature(const Creature* creature)
	{
		if (client) {
			auto tile = creature->getTile();
			if (!tile) {
				return;
			}
			uint32_t stackpos = tile->getClientIndexOfCreature(this, creature);
			if (stackpos < 10) {
				client->sendUpdateTileCreature(creature->getPosition(), stackpos, creature);
			}
		}
	}
	void sendUpdateTile(const Tile* tile, const Position& pos)
	{
		if (client) {
			client->sendUpdateTile(tile, pos);
		}
	}

	void sendChannelMessage(std::string_view author, std::string_view text, SpeakClasses type, uint16_t channel)
	{
		if (client) {
			client->sendChannelMessage(author, text, type, channel);
		}
	}
	void sendCreatureAppear(const Creature* creature, const Position& pos,
	                        MagicEffectClasses magicEffect = CONST_ME_NONE)
	{
		if (client) {
			client->sendAddCreature(creature, pos, creature->getTile()->getClientIndexOfCreature(this, creature),
			                        magicEffect);
		}
	}
	void sendCreatureMove(const Creature* creature, const Position& newPos, int32_t newStackPos, const Position& oldPos,
	                      int32_t oldStackPos, bool teleport)
	{
		if (client) {
			client->sendMoveCreature(creature, newPos, newStackPos, oldPos, oldStackPos, teleport);
		}
	}
	void sendCreatureTurn(const Creature* creature)
	{
		if (client && canSeeCreature(creature)) {
			int32_t stackpos = creature->getTile()->getClientIndexOfCreature(this, creature);
			if (stackpos != -1) {
				client->sendCreatureTurn(creature, stackpos);
			}
		}
	}
	void sendCreatureSay(const Creature* creature, SpeakClasses type, std::string_view text,
	                     const Position* pos = nullptr)
	{
		if (client) {
			client->sendCreatureSay(creature, type, text, pos);
		}
	}
	void sendPrivateMessage(const Player* speaker, SpeakClasses type, std::string_view text)
	{
		if (client) {
			client->sendPrivateMessage(speaker, type, text);
		}
	}
	void sendCreatureSquare(const Creature* creature, SquareColor_t color)
	{
		if (client) {
			client->sendCreatureSquare(creature, color);
		}
	}
	void sendCreatureChangeOutfit(const Creature* creature, const Outfit_t& outfit)
	{
		if (client) {
			client->sendCreatureOutfit(creature, outfit);
		}
	}
	void sendCreatureChangeVisible(const Creature* creature, bool visible)
	{
		if (!client) {
			return;
		}

		if (creature->getPlayer()) {
			if (visible) {
				client->sendCreatureOutfit(creature, creature->getCurrentOutfit());
			} else {
				static Outfit_t outfit;
				client->sendCreatureOutfit(creature, outfit);
			}
		} else if (canSeeInvisibility()) {
			client->sendCreatureOutfit(creature, creature->getCurrentOutfit());
		} else {
			int32_t stackpos = creature->getTile()->getClientIndexOfCreature(this, creature);
			if (stackpos == -1) {
				return;
			}

			if (visible) {
				client->sendAddCreature(creature, creature->getPosition(), stackpos);
			} else {
				client->sendRemoveTileThing(creature->getPosition(), stackpos);
			}
		}
	}
	void sendCreatureLight(const Creature* creature)
	{
		if (client) {
			client->sendCreatureLight(creature);
		}
	}
	void sendCreatureWalkthrough(const Creature* creature, bool walkthrough)
	{
		if (client) {
			client->sendCreatureWalkthrough(creature, walkthrough);
		}
	}
	void sendCreatureShield(const Creature* creature)
	{
		if (client) {
			client->sendCreatureShield(creature);
		}
	}

	void sendAnimatedText(std::string_view message, const Position& pos, TextColor_t color)
	{
		if (client) {
			client->sendAnimatedText(message, pos, color);
		}
	}

	void sendSpellCooldown(uint8_t spellId, uint32_t time)
	{
		if (client) {
			client->sendSpellCooldown(spellId, time);
		}
	}
	void sendSpellGroupCooldown(SpellGroup_t groupId, uint32_t time)
	{
		if (client) {
			client->sendSpellGroupCooldown(groupId, time);
		}
	}
	void sendUseItemCooldown(uint32_t time)
	{
		if (client) {
			client->sendUseItemCooldown(time);
		}
	}

	void sendModalWindow(const ModalWindow& modalWindow);

	// container
	void sendAddContainerItem(const Container* container, const Item* item);
	void sendUpdateContainerItem(const Container* container, uint16_t slot, const Item* newItem);
	void sendRemoveContainerItem(const Container* container, uint16_t slot);
	void sendContainer(uint8_t cid, const Container* container, bool hasParent, uint16_t firstIndex)
	{
		if (client) {
			client->sendContainer(cid, container, hasParent, firstIndex);
		}
	}

	// inventory
	void sendInventoryItem(slots_t slot, const Item* item)
	{
		if (client) {
			client->sendInventoryItem(slot, item);
		}
	}

	// event methods
	void onUpdateTileItem(const Tile* tile, const Position& pos, const Item* oldItem, const ItemType& oldType,
	                      const Item* newItem, const ItemType& newType) override;
	void onRemoveTileItem(const Tile* tile, const Position& pos, const ItemType& iType, const Item* item) override;

	void onCreatureAppear(Creature* creature, bool isLogin) override;
	void onRemoveCreature(Creature* creature, bool isLogout) override;
	void onCreatureMove(Creature* creature, const Tile* newTile, const Position& newPos, const Tile* oldTile,
	                    const Position& oldPos, bool teleport) override;

	void onAttackedCreatureDisappear(bool isLogout) override;
	void onFollowCreatureDisappear(bool isLogout) override;

	// container
	void onAddContainerItem(const Item* item);
	void onUpdateContainerItem(const Container* container, const Item* oldItem, const Item* newItem);
	void onRemoveContainerItem(const Container* container, const Item* item);

	void onCloseContainer(const Container* container);
	void onSendContainer(const Container* container);
	void autoCloseContainers(const Container* container);

	// inventory
	void onUpdateInventoryItem(Item* oldItem, Item* newItem);
	void onRemoveInventoryItem(Item* item);

	void sendCancelMessage(std::string_view msg) const
	{
		if (client) {
			client->sendTextMessage(TextMessage(MESSAGE_STATUS_SMALL, msg));
		}
	}
	void sendCancelMessage(ReturnValue message) const;
	void sendCancelTarget() const
	{
		if (client) {
			client->sendCancelTarget();
		}
	}
	void sendCancelWalk() const
	{
		if (client) {
			client->sendCancelWalk();
		}
	}
	void sendChangeSpeed(const Creature* creature, uint32_t newSpeed) const
	{
		if (client) {
			client->sendChangeSpeed(creature, newSpeed);
		}
	}
	void sendCreatureHealth(const Creature* creature) const
	{
		if (client) {
			client->sendCreatureHealth(creature);
		}
	}
	void sendDistanceShoot(const Position& from, const Position& to, uint16_t type) const
	{
		if (client) {
			client->sendDistanceShoot(from, to, type);
		}
	}
	void sendHouseWindow(House* house, uint32_t listId) const;
	void sendCreatePrivateChannel(uint16_t channelId, std::string_view channelName)
	{
		if (client) {
			client->sendCreatePrivateChannel(channelId, channelName);
		}
	}
	void sendClosePrivate(uint16_t channelId);
	void sendIcons() const
	{
		if (client) {
			client->sendIcons(getClientIcons());
		}
	}
	void sendMagicEffect(const Position& pos, uint16_t type) const
	{
		if (client) {
			client->sendMagicEffect(pos, type);
		}
	}
	void sendPing();
	void sendStats();
	void sendSkills() const
	{
		if (client) {
			client->sendSkills();
		}
	}
	void sendTextMessage(MessageClasses mclass, std::string_view message) const
	{
		if (client) {
			client->sendTextMessage(TextMessage(mclass, message));
		}
	}
	void sendTextMessage(const TextMessage& message) const
	{
		if (client) {
			client->sendTextMessage(message);
		}
	}
	void sendReLoginWindow() const
	{
		if (client) {
			client->sendReLoginWindow();
		}
	}
	void sendTextWindow(Item* item, uint16_t maxlen, bool canWrite) const
	{
		if (client) {
			client->sendTextWindow(windowTextId, item, maxlen, canWrite);
		}
	}
	void sendTextWindow(uint16_t itemId, std::string_view text) const
	{
		if (client) {
			client->sendTextWindow(windowTextId, itemId, text);
		}
	}
	void sendToChannel(const Creature* creature, SpeakClasses type, std::string_view text, uint16_t channelId) const
	{
		if (client) {
			client->sendToChannel(creature, type, text, channelId);
		}
	}
	void sendShop() const
	{
		if (client) {
			client->sendShop(shopItemList);
		}
	}
	void sendSaleItemList() const
	{
		if (client) {
			client->sendSaleItemList(shopItemList);
		}
	}
	void sendCloseShop() const
	{
		if (client) {
			client->sendCloseShop();
		}
	}
	void sendTradeItemRequest(std::string_view traderName, const Item* item, bool ack) const
	{
		if (client) {
			client->sendTradeItemRequest(traderName, item, ack);
		}
	}
	void sendTradeClose() const
	{
		if (client) {
			client->sendCloseTrade();
		}
	}
	void sendWorldLight(LightInfo lightInfo)
	{
		if (client) {
			client->sendWorldLight(lightInfo);
		}
	}
	void sendChannelsDialog()
	{
		if (client) {
			client->sendChannelsDialog();
		}
	}
	void sendOpenPrivateChannel(std::string_view receiver)
	{
		if (client) {
			client->sendOpenPrivateChannel(receiver);
		}
	}
	void sendOutfitWindow()
	{
		if (client) {
			client->sendOutfitWindow();
		}
	}
	void sendCloseContainer(uint8_t cid)
	{
		if (client) {
			client->sendCloseContainer(cid);
		}
	}

	void sendChannel(uint16_t channelId, std::string_view channelName)
	{
		if (client) {
			client->sendChannel(channelId, channelName);
		}
	}
	void sendTutorial(uint8_t tutorialId)
	{
		if (client) {
			client->sendTutorial(tutorialId);
		}
	}
	void sendAddMarker(const Position& pos, uint8_t markType, std::string_view desc)
	{
		if (client) {
			client->sendAddMarker(pos, markType, desc);
		}
	}
	void sendFightModes()
	{
		if (client) {
			client->sendFightModes();
		}
	}
	void sendNetworkMessage(const NetworkMessage& message)
	{
		if (client) {
			client->writeToOutputBuffer(message);
		}
	}

	void receivePing() { lastPong = OTSYS_TIME(); }

	void onThink(uint32_t interval) override;

	void postAddNotification(Thing* thing, const Cylinder* oldParent, int32_t index,
	                         cylinderlink_t link = LINK_OWNER) override;
	void postRemoveNotification(Thing* thing, const Cylinder* newParent, int32_t index,
	                            cylinderlink_t link = LINK_OWNER) override;

	void setNextAction(int64_t time)
	{
		if (time > nextAction) {
			nextAction = time;
		}
	}
	bool canDoAction() const { return nextAction <= OTSYS_TIME(); }
	uint32_t getNextActionTime() const;

	Item* getWriteItem(uint32_t& windowTextId, uint16_t& maxWriteLen);
	void setWriteItem(Item* item, uint16_t maxWriteLen = 0);

	House* getEditHouse(uint32_t& windowTextId, uint32_t& listId);
	void setEditHouse(House* house, uint32_t listId = 0);

	void learnInstantSpell(std::string_view spellName);
	void forgetInstantSpell(const std::string& spellName);
	bool hasLearnedInstantSpell(std::string_view spellName) const;

	// Autoloot
	void sendAutoLootWindow() const;
	void parseAutoLootWindow(const std::string& text);
	Container* findNonEmptyContainer(uint16_t itemId);
	void lootCorpse(Container* container);

	void updateRegeneration();

	const std::map<uint8_t, OpenContainer>& getOpenContainers() const { return openContainers; }

	uint16_t getProtectionTime() const { return protectionTime; }
	void setProtectionTime(uint16_t newProtectionTime) { protectionTime = newProtectionTime; }

	ProtocolSpectator_ptr client;

	void manageAccount(const std::string& text);
	bool isAccountManager() const { return accountManager != ACCOUNT_MANAGER_NONE; }
	AccountManagerMode getAccountManagerMode() const { return accountManager; }
	void setAccountManagerMode(AccountManagerMode mode) { accountManager = mode; }
	void setAccountManagerData(uint32_t accId) { managerData.accountId = accId; }



	// for lua module
	void setAccountType(AccountType_t newType) { accountType = newType; }

	void setCapacity(uint32_t newCapacity) { capacity = newCapacity; }

	double getLostPercent() const;

	void addExperience(Creature* source, uint64_t exp, bool sendText = false);
	void removeExperience(uint64_t exp, bool sendText = false);

	void setMana(uint32_t newMana) { mana = std::min<uint32_t>(newMana, manaMax); }
	void setMaxMana(uint32_t newMaxMana) { manaMax = newMaxMana; }

	int32_t getBaseMaxHealth() const { return healthMax; }
	int32_t getBaseMaxMana() const { return manaMax; }

	uint32_t getItemTypeCount(uint16_t itemId, int32_t subType = -1) const override;

	void setStaminaMinutes(uint16_t newStamina) { staminaMinutes = std::min<uint16_t>(2520, newStamina); }

	void updateStaminaRegen(int64_t timePassed);
	void stopStaminaPzRegen() { staminaPzActive = false; }
	void stopStaminaTrainerRegen() { staminaTrainerActive = false; }
	bool isTrainerTarget(Creature* creature) const;

	void incrementWindowTextId() { windowTextId++; }
	void setRawWriteItem(Item* item) { writeItem = item; }
	void setMaxWriteLen(uint16_t len) { maxWriteLen = len; }
	uint32_t getWindowTextId() const { return windowTextId; }

	Thing* getThing(size_t index) const override;

	auto getPremiumEndsAt() const { return premiumEndsAt; }

	void setLoginPosition(const Position& loginPos) { loginPosition = loginPos; }

	bool getChaseMode() const { return chaseMode; }
	bool getSecureMode() const { return secureMode; }
	auto getFightMode() const { return fightMode; }

	bool hasDebugAssertSent() const { return client ? client->debugAssertSent : false; }
	bool isOTCv8() const { return client ? client->isOTCv8 : false; }

	static uint32_t playerAutoID;

	uint32_t totalReduceSkillLoss = 0;

private:
	std::forward_list<Condition*> getMuteConditions() const;

	void checkTradeState(const Item* item);
	bool hasCapacity(const Item* item, uint32_t count) const;

	void handleNamelockManager(const std::string& text, std::ostringstream& msg, bool& shouldShowHelp);
	void handleAccountManager(const std::string& text, std::ostringstream& msg, bool& shouldShowHelp);
	void handleNewAccountManager(const std::string& text, std::ostringstream& msg, bool& shouldShowHelp);
	bool checkText(std::string_view text, std::string_view match) const;
	void resetTalkState(size_t from = 0, size_t to = 15);
	void setManagerTalkState(size_t index, bool value) { managerTalkState[index] = value; }

	void gainExperience(uint64_t gainExp, Creature* source);

	void updateInventoryWeight();

	void setNextWalkActionTask(SchedulerTask* task);
	void setNextWalkTask(SchedulerTask* task);
	void setNextActionTask(SchedulerTask* task, bool resetIdleTime = true);

	void death(Creature* lastHitCreature) override;
	bool dropCorpse(Creature* lastHitCreature, Creature* mostDamageCreature, bool lastHitUnjustified,
	                bool mostDamageUnjustified) override;
	Item* getCorpse(Creature* lastHitCreature, Creature* mostDamageCreature) override;

	// cylinder implementations
	ReturnValue queryAdd(int32_t index, const Thing& thing, uint32_t count, uint32_t flags,
	                     Creature* actor = nullptr) const override;
	ReturnValue queryMaxCount(int32_t index, const Thing& thing, uint32_t count, uint32_t& maxQueryCount,
	                          uint32_t flags) const override;
	ReturnValue queryRemove(const Thing& thing, uint32_t count, uint32_t flags,
	                        Creature* actor = nullptr) const override;
	Cylinder* queryDestination(int32_t& index, const Thing& thing, Item** destItem, uint32_t& flags) override;

	void addThing(Thing*) override {}
	void addThing(int32_t index, Thing* thing) override;

	void updateThing(Thing* thing, uint16_t itemId, uint32_t count) override;
	void replaceThing(uint32_t index, Thing* thing) override;

	void removeThing(Thing* thing, uint32_t count) override;

	int32_t getThingIndex(const Thing* thing) const override;
	size_t getFirstIndex() const override;
	size_t getLastIndex() const override;
	std::map<uint32_t, uint32_t>& getAllItemTypeCount(std::map<uint32_t, uint32_t>& countMap) const override;

	void internalAddThing(Thing* thing) override;
	void internalAddThing(uint32_t index, Thing* thing) override;

	std::unordered_set<uint32_t> attackedSet;
	std::unordered_set<uint32_t> VIPList;

	std::map<uint8_t, OpenContainer> openContainers;
	std::map<uint32_t, DepotLocker_ptr> depotLockerMap;
	std::map<uint32_t, DepotChest*> depotChests;

	std::unordered_map<uint16_t, uint8_t> outfits;
	std::unordered_set<uint16_t> mounts;
	GuildWarVector guildWarVector;

	std::list<ShopInfo> shopItemList;

	std::forward_list<Party*> invitePartyList;
	std::forward_list<uint32_t> modalWindows;
	std::forward_list<std::string> learnedInstantSpellList;

	static std::forward_list<Condition*>
	    storedConditionList; // TODO: This variable is only temporarily used when logging in, get rid of it somehow

	std::string name;
	std::string guildNick;

	Skill skills[SKILL_LAST + 1];
	LightInfo itemsLight;
	Position loginPosition;
	Position lastWalkthroughPosition;
	AutoLootConfig autolootConfig;

	time_t lastLoginSaved = 0;
	time_t lastLogout = 0;
	time_t premiumEndsAt = 0;

	uint64_t experience = 0;
	uint64_t manaSpent = 0;
	uint64_t lastAttack = 0;
	uint64_t bankBalance = 0;
	int64_t lastFailedFollow = 0;
	int64_t skullTicks = 0;
	int64_t lastWalkthroughAttempt = 0;
	int64_t lastToggleMount = 0;
	int64_t lastPing;
	int64_t lastPong;
	int64_t lastDllCheck = 0;
	int64_t ghostModeStartTime = 0;
	int64_t nextAction = 0;

	uint32_t lastIP = 0;
	BedItem* bedItem = nullptr;
	Guild_ptr guild = nullptr;
	GuildRank_ptr guildRank = nullptr;
	Group* group = nullptr;
	Item* tradeItem = nullptr;
	Item* inventory[CONST_SLOT_LAST + 1] = {};
	Item* writeItem = nullptr;
	House* editHouse = nullptr;
	Npc* shopOwner = nullptr;
	Party* party = nullptr;
	Player* tradePartner = nullptr;
	SchedulerTask* walkTask = nullptr;
	Town* town = nullptr;
	Vocation* vocation = nullptr;
	std::shared_ptr<RewardChest> rewardChest = nullptr;

	uint32_t attackSpeed = 0;
	uint32_t inventoryWeight = 0;
	uint32_t capacity = 40000;
	uint32_t damageImmunities = 0;
	uint32_t conditionImmunities = 0;
	uint32_t conditionSuppressions = 0;
	uint32_t level = 1;
	uint32_t reset = 0; // reset system
	uint32_t magLevel = 0;
	uint32_t actionTaskEvent = 0;
	uint32_t nextStepEvent = 0;
	uint32_t walkTaskEvent = 0;
	uint32_t MessageBufferTicks = 0;
	uint32_t accountNumber = 0;
	uint32_t guid = 0;
	uint32_t windowTextId = 0;
	uint32_t editListId = 0;
	uint32_t mana = 0;
	uint32_t manaMax = 0;
	int32_t varSpecialSkills[SPECIALSKILL_LAST + 1] = {};
	int32_t varSkills[SKILL_LAST + 1] = {};
	int32_t varStats[STAT_LAST + 1] = {};
	std::array<int16_t, COMBAT_COUNT> specialMagicLevelSkill = {0};
	std::array<int32_t, static_cast<size_t>(ExperienceRateType::STAMINA) + 1> experienceRate = {0};
	int32_t purchaseCallback = -1;
	int32_t saleCallback = -1;
	int32_t MessageBufferCount = 0;
	int32_t bloodHitCount = 0;
	int32_t shieldBlockCount = 0;
	int32_t offlineTrainingSkill = -1;
	int32_t offlineTrainingTime = 0;
	int32_t idleTime = 0;

	uint16_t lastStatsTrainingTime = 0;
	uint16_t staminaMinutes = 2520;
	uint16_t protectionTime = 10;
	uint16_t maxWriteLen = 0;
	int16_t lastDepotId = -1;

	int64_t staminaPzTicks = 0;
	int64_t staminaTrainerTicks = 0;

	uint32_t staminaPzOrangeDelayMs = 0;
	uint32_t staminaPzGreenDelayMs = 0;
	uint32_t staminaTrainerDelayMs = 0;

	uint8_t soul = 0;
	std::bitset<PLAYER_MAX_BLESSINGS + 1> blessings;
	uint8_t levelPercent = 0;
	uint8_t magLevelPercent = 0;

	PlayerSex_t sex = PLAYERSEX_FEMALE;
	OperatingSystem_t operatingSystem = CLIENTOS_NONE;
	BlockType_t lastAttackBlockType = BLOCK_NONE;
	tradestate_t tradeState = TRADE_NONE;
	fightMode_t fightMode = FIGHTMODE_ATTACK;
	AccountType_t accountType = ACCOUNT_TYPE_NORMAL;

	bool chaseMode = false;
	bool secureMode = false;
	bool ghostMode = false;
	bool wasMounted = false;
	bool pzLocked = false;
	bool isConnecting = false;
	bool addAttackSkillPoint = false;
	bool randomizeMount = false;
	bool inventoryAbilities[CONST_SLOT_LAST + 1] = {};
	bool tokenProtected = false;
	std::string tokenHash;
	bool tokenLocked = false;
	bool staminaPzActive = false;
	bool staminaTrainerActive = false;
	uint8_t m_harmony = 0;
	bool m_serene = false;
	uint64_t m_serene_cooldown = 0;
	VirtueMonk_t m_virtue = VIRTUE_NONE;

	AccountManagerMode accountManager{ACCOUNT_MANAGER_NONE};
	std::array<bool, 15> managerTalkState{};
	struct
	{
		PlayerSex_t sex{PLAYERSEX_FEMALE};
		uint32_t accountId{0};
		uint32_t vocationId{0};
		uint32_t townId{0};
		std::string string1;
		std::string string2;
		std::string accountName;
	} managerData;

	void updateItemsLight(bool internal = false);

	int32_t getStepSpeed() const override;
	void updateBaseSpeed();

	bool isPromoted() const;

	static uint16_t getBasisPointLevel(uint64_t count, uint64_t nextLevelCount);
	uint64_t getLostExperience() const override
	{
		return skillLoss ? static_cast<uint64_t>(experience * getLostPercent()) : 0;
	}
	uint32_t getDamageImmunities() const override { return damageImmunities; }
	uint32_t getConditionImmunities() const override { return conditionImmunities; }
	uint32_t getConditionSuppressions() const override { return conditionSuppressions; }
	uint16_t getLookCorpse() const override;
	void getPathSearchParams(const Creature* creature, FindPathParams& fpp) const override;

	friend class Game;
	friend class Npc;
	friend class LuaScriptInterface;
	friend class Map;
	friend class Actions;
	friend class IOLoginData;
	friend class ProtocolGame;
	friend class ProtocolSpectator;
};

#endif
