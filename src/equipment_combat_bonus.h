#ifndef FS_EQUIPMENT_COMBAT_BONUS_H
#define FS_EQUIPMENT_COMBAT_BONUS_H

#include <algorithm>
#include <cstdint>
#include <limits>

namespace EquipmentCombatBonus {

inline uint32_t applyAttackSpeedPercent(uint64_t interval, uint32_t percent)
{
	if (percent != 0) {
		const uint64_t denominator = 100ULL + percent;
		interval = (interval * 100ULL + denominator - 1) / denominator;
	}

	return static_cast<uint32_t>(std::clamp<uint64_t>(
	    interval, 100, std::numeric_limits<uint32_t>::max()));
}

inline int32_t increaseDamageByPercent(int32_t value, uint32_t percent)
{
	if (value <= 0 || percent == 0) {
		return value;
	}

	const uint64_t unsignedValue = static_cast<uint32_t>(value);
	const uint64_t increase = (unsignedValue / 100) * percent + ((unsignedValue % 100) * percent) / 100;
	const uint64_t result = unsignedValue + increase;
	return static_cast<int32_t>(std::min<uint64_t>(result, std::numeric_limits<int32_t>::max()));
}

inline int32_t reduceDamageByPercent(int32_t value, uint32_t percent)
{
	if (value <= 0 || percent == 0) {
		return value;
	}

	if (percent >= 100) {
		return 0;
	}

	return value - static_cast<int32_t>(static_cast<int64_t>(value) * percent / 100);
}

} // namespace EquipmentCombatBonus

#endif // FS_EQUIPMENT_COMBAT_BONUS_H
