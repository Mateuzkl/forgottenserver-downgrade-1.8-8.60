#ifndef FS_FAMILIAR_H
#define FS_FAMILIAR_H

#include "player.h"
#include <optional>
#include <string>

namespace Familiar {

struct FamiliarInfo
{
	uint16_t lookType = 0;
	std::string name;
};

std::optional<FamiliarInfo> getFamiliarInfo(const Player* player);
std::string getFamiliarName(const Player* player);
bool dispellFamiliar(Player* player);
bool createFamiliar(Player* player, const std::string& familiarName, uint32_t timeLeft);
bool createFamiliarSpell(Player* player, uint32_t spellId);
void restoreFamiliarOnLogin(uint32_t playerId);
void onPlayerLogout(Player* player);

}

#endif
