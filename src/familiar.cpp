#include "otpch.h"

#include "familiar.h"
#include "configmanager.h"
#include "events.h"
#include "game.h"
#include "monster.h"
#include "condition.h"
#include "scheduler.h"
#include "scriptmanager.h"
#include "luascript.h"
#include "spells.h"
#include <map>
#include <vector>
#include <algorithm>

namespace Familiar {

struct FamDef { uint32_t id; std::string name; };

static const std::map<uint32_t, FamDef> FAMILIAR_ID = {
    {1, {994, "Sorcerer familiar"}},
    {2, {993, "Druid familiar"}},
    {3, {992, "Paladin familiar"}},
    {4, {991, "Knight familiar"}},
    {9, {990, "Monk familiar"}},
};

struct FamTimer { int32_t storage; uint32_t countdown; std::string message; };
static const std::vector<FamTimer> FAMILIAR_TIMER = {
    {STORAGE_FAMILIAR_TIMER_10, 10, "10 seconds"},
    {STORAGE_FAMILIAR_TIMER_60, 60, "one minute"},
};

static void ClearFamiliarTimerEvents(Player* player, bool stopEvents)
{
    if (!player) {
        return;
    }

    for (const auto& t : FAMILIAR_TIMER) {
        const auto eventId = player->getStorageValue(static_cast<uint32_t>(t.storage));
        if (stopEvents && eventId && *eventId > 0) {
            g_scheduler.stopEvent(static_cast<uint32_t>(*eventId));
        }
        player->setStorageValue(static_cast<uint32_t>(t.storage), std::optional<int64_t>(-1));
    }
}

static void SendMessageFunction(uint32_t playerId, const std::string& message)
{
    if (auto player = g_game.getPlayerByID(playerId)) {
        player->sendTextMessage(MESSAGE_INFO_DESCR, "Your summon will disappear in less than " + message);
    }
}

static void RemoveFamiliar(uint32_t creatureId, uint32_t playerId)
{
	auto creatureRef = g_game.getCreatureByIDShared(creatureId);
	Creature* creature = creatureRef.get();
	auto playerRef = g_game.getPlayerByID(playerId);
	Player* player = playerRef.get();
	if (!creature || !player) {
		if (player) {
			ClearFamiliarTimerEvents(player, false);
			player->setStorageValue(STORAGE_FAMILIAR_SUMMON_TIME, std::optional<int64_t>(-1));
		}
		return;
	}

	Monster* monster = creature->getMonster();
	std::shared_ptr<Creature> master;
	if (monster) {
		master = monster->getMaster();
	}
	if (master && master.get() == player) {
		g_game.removeCreature(creature);
		ClearFamiliarTimerEvents(player, false);
		player->setStorageValue(STORAGE_FAMILIAR_SUMMON_TIME, std::optional<int64_t>(-1));
	}
}
std::optional<FamiliarInfo> getFamiliarInfo(const Player* player)
{
    if (!ConfigManager::getBoolean(ConfigManager::FAMILIAR_SYSTEM_ENABLED)) return std::nullopt;
    if (!player || !player->getVocation()) return std::nullopt;
    uint32_t base = player->getVocation()->getFromVocation();
    if (base == 0) base = player->getVocation()->getId();
    auto it = FAMILIAR_ID.find(base);
    if (it == FAMILIAR_ID.end()) return std::nullopt;
    return FamiliarInfo{static_cast<uint16_t>(it->second.id), it->second.name};
}

std::string getFamiliarName(const Player* player)
{
    const auto familiar = getFamiliarInfo(player);
    return familiar ? familiar->name : std::string{};
}

bool dispellFamiliar(Player* player)
{
    if (!player) return false;
    if (!ConfigManager::getBoolean(ConfigManager::FAMILIAR_SYSTEM_ENABLED)) {
        ClearFamiliarTimerEvents(player, true);
        player->setStorageValue(STORAGE_FAMILIAR_SUMMON_TIME, std::optional<int64_t>(-1));
        return false;
    }

    const auto& summons = player->getSummons();
    std::string famName = getFamiliarName(player);
    if (famName.empty()) return false;
    for (const auto& s : summons) {
        if (auto summon = s.lock()) {
            std::string sname = summon->getName();
            std::transform(sname.begin(), sname.end(), sname.begin(), ::tolower);
            std::string fname = famName;
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            if (sname == fname) {
                g_game.addMagicEffect(player->getPosition(), CONST_ME_MAGIC_BLUE, player->getInstanceID());
                g_game.addMagicEffect(summon->getPosition(), CONST_ME_POFF, summon->getInstanceID());
                g_game.removeCreature(summon.get());
                ClearFamiliarTimerEvents(player, true);
                player->setStorageValue(STORAGE_FAMILIAR_SUMMON_TIME, std::optional<int64_t>(-1));
                return true;
            }
        }
    }
    return false;
}

bool createFamiliar(Player* player, const std::string& familiarName, uint32_t timeLeft)
{
    if (player && !ConfigManager::getBoolean(ConfigManager::FAMILIAR_SYSTEM_ENABLED)) {
        player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
        g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
        return false;
    }

    if (!player || familiarName.empty()) {
        if (player) {
            player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
            g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
        }
        return false;
    }

    auto monsterUnique = Monster::createMonster(familiarName);
    if (!monsterUnique) {
        player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
        g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
        return false;
    }

    auto monster = std::shared_ptr<Monster>(std::move(monsterUnique));
    const Position& pos = player->getPosition();
    monster->setInstanceID(player->getInstanceID());

    if (!g_events->eventMonsterOnSpawn(monster.get(), pos, false, true) ||
        !g_game.placeCreature(monster.get(), pos, true, false, CONST_ME_TELEPORT)) {
        player->sendCancelMessage(RETURNVALUE_NOTENOUGHROOM);
        return false;
    }

    monster->setMaster(player);
    // mark summon with guild emblem (ally = green badge)
    monster->setGuildEmblem(GUILDEMBLEM_ALLY);
    monster->setIcon("familiar", CreatureIcon(CreatureIconQuests_Familiar));
    int32_t delta = static_cast<int32_t>(player->getSpeed()) - static_cast<int32_t>(monster->getBaseSpeed());
    if (delta < 0) delta = 0;
    g_game.changeSpeed(monster.get(), delta);

    g_game.addMagicEffect(player->getPosition(), CONST_ME_MAGIC_BLUE, player->getInstanceID());
    g_game.addMagicEffect(monster->getPosition(), CONST_ME_TELEPORT, monster->getInstanceID());

    int64_t expireAt = static_cast<int64_t>(OTSYS_TIME()) + (static_cast<int64_t>(timeLeft) * 1000LL);
    player->setStorageValue(STORAGE_FAMILIAR_SUMMON_TIME, std::optional<int64_t>(expireAt));

    // schedule removal
    g_scheduler.addEvent(timeLeft * 1000, [creatureId = monster->getID(), playerId = player->getID()]() {
        RemoveFamiliar(creatureId, playerId);
    });

    // schedule warning messages and store event ids
    for (const auto& t : FAMILIAR_TIMER) {
        if (timeLeft > t.countdown) {
            uint32_t eventId = g_scheduler.addEvent((timeLeft - t.countdown) * 1000, [playerId = player->getID(), msg = t.message]() {
                SendMessageFunction(playerId, msg);
            });
            player->setStorageValue(static_cast<uint32_t>(t.storage), std::optional<int64_t>(static_cast<int64_t>(eventId)));
        } else {
            player->setStorageValue(static_cast<uint32_t>(t.storage), std::optional<int64_t>(-1));
        }
    }

    return true;
}

bool createFamiliarSpell(Player* player, uint32_t spellId)
{
    if (!player) return false;
    if (!ConfigManager::getBoolean(ConfigManager::FAMILIAR_SYSTEM_ENABLED)) {
        g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
        player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
        return false;
    }

    if (!player->isPremium()) {
        g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
        player->sendCancelMessage("You need a premium account.");
        return false;
    }

    if (player->getLevel() < 200) {
        player->sendCancelMessage("You need to be at least level 200.");
        g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
        return false;
    }

    for (const auto& summonWeak : player->getSummons()) {
        auto summon = summonWeak.lock();
        if (!summon) {
            continue;
        }
        Monster* monster = summon->getMonster();
        if (monster && monster->isFamiliar()) {
            player->sendCancelMessage("You already have a familiar.");
            g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
            return false;
        }
    }

    if (player->getSummons().size() >= 1 && player->getAccountType() < ACCOUNT_TYPE_GOD) {
        player->sendCancelMessage("You can't have other summons.");
        g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
        return false;
    }

    std::string famName = getFamiliarName(player);
    if (famName.empty()) {
        player->sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
        g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
        return false;
    }

    uint32_t summonDuration = 15 * 60;
    uint32_t cooldown = summonDuration * 2;

    bool created = createFamiliar(player, famName, summonDuration);
    if (created) {
        auto condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_SPELLCOOLDOWN, static_cast<int32_t>(cooldown * 1000), 0, false, spellId);
        if (condition) player->addCondition(std::move(condition));
        return true;
    }

    return false;
}

void restoreFamiliarOnLogin(uint32_t playerId)
{
    auto playerRef = g_game.getPlayerByID(playerId);
    Player* player = playerRef.get();
    if (!player) {
        return;
    }

    if (!ConfigManager::getBoolean(ConfigManager::FAMILIAR_SYSTEM_ENABLED)) {
        ClearFamiliarTimerEvents(player, true);
        player->setStorageValue(STORAGE_FAMILIAR_SUMMON_TIME, std::optional<int64_t>(-1));
        return;
    }

    auto storedExpire = player->getStorageValue(STORAGE_FAMILIAR_SUMMON_TIME);
    if (!storedExpire || *storedExpire <= 0) {
        return;
    }

    int64_t expireAt = *storedExpire;
    int64_t now = static_cast<int64_t>(OTSYS_TIME());
    int64_t remainingMs = expireAt - now;

    if (remainingMs <= 0) {
        player->setStorageValue(STORAGE_FAMILIAR_SUMMON_TIME, std::optional<int64_t>(-1));
        return;
    }

    uint32_t remainingSecs = static_cast<uint32_t>(remainingMs / 1000LL);
    if (remainingSecs < 5) {
        player->setStorageValue(STORAGE_FAMILIAR_SUMMON_TIME, std::optional<int64_t>(-1));
        return;
    }

    for (const auto& summonWeak : player->getSummons()) {
        auto summon = summonWeak.lock();
        if (!summon) {
            continue;
        }
        Monster* monster = summon->getMonster();
        if (monster && monster->isFamiliar()) {
            return;
        }
    }

    std::string familiarName = getFamiliarName(player);
    if (familiarName.empty()) {
        return;
    }

    createFamiliar(player, familiarName, remainingSecs);
}

void onPlayerLogout(Player* player)
{
    if (!player) {
        return;
    }

    ClearFamiliarTimerEvents(player, true);

    for (const auto& weakSummon : player->getSummons()) {
        auto summon = weakSummon.lock();
        if (!summon) {
            continue;
        }
        Monster* m = summon->getMonster();
        if (m && m->isFamiliar()) {
            g_game.removeCreature(summon.get());
            break;
        }
    }
}

}
