// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found
// in the LICENSE file.

#ifndef FS_INSTANCE_UTILS_H
#define FS_INSTANCE_UTILS_H

#include "creature.h"
#include "item.h"
#include "player.h"
#include "spectators.h"

#include <cstdint>

namespace InstanceUtils {

inline bool canSeeItemInInstance(uint32_t viewerInstanceId, const Item *item)
{
    if (!item) {
        return false;
    }

    const uint32_t itemInstanceId = item->getInstanceID();

    if (itemInstanceId == viewerInstanceId) {
        return true;
    }

    return itemInstanceId == 0 && item->isLoadedFromMap();
}

inline bool isPlayerInSameInstance(const Creature* player,
                                   uint32_t objectInstanceId)
{
    if (objectInstanceId == 0) {
        return true;
    }

    return player && player->compareInstance(objectInstanceId);
}

inline void filterByInstanceInPlace(SpectatorVec &spectators,
                                     uint32_t instanceId)
{
    spectators.filterPlayers([instanceId](const Player* player) {
        return player && player->compareInstance(instanceId);
    });
}

inline SpectatorVec filterByInstance(const SpectatorVec &spectators,
                                     uint32_t instanceId)
{
    SpectatorVec filtered = spectators;
    filterByInstanceInPlace(filtered, instanceId);
    return filtered;
}

inline bool canInteract(const Creature *a, const Creature *b)
{
    return a && b && a->compareInstance(b->getInstanceID());
}

inline void sendMagicEffectToInstance(const SpectatorVec &spectators,
                                       const Position &pos, uint8_t effect,
                                       uint32_t instanceId)
{
    for (const auto& spectator : spectators.players()) {
        Player *p = static_cast<Player*>(spectator.get());
        if (p->compareInstance(instanceId)) {
            p->sendMagicEffect(pos, effect);
        }
    }
}

void sendMagicEffectToInstance(const Position &pos, uint32_t instanceId,
                               uint8_t effect);

inline void sendDistanceEffectToInstance(const SpectatorVec &spectators,
                                         const Position &from,
                                         const Position &to, uint8_t effect,
                                         uint32_t instanceId)
{
    for (const auto& spectator : spectators.players()) {
        Player *p = static_cast<Player*>(spectator.get());
        if (p->compareInstance(instanceId)) {
            p->sendDistanceShoot(from, to, effect);
        }
    }
}

} // namespace InstanceUtils

#endif // FS_INSTANCE_UTILS_H
