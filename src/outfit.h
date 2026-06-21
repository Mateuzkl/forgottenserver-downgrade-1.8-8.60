// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_OUTFIT_H
#define FS_OUTFIT_H

#include "enums.h"
#include "observer_ptr.h"

struct OutfitEntry
{
	constexpr explicit OutfitEntry(uint16_t initLookType, uint8_t initAddons) :
	    lookType(initLookType), addons(initAddons) {}

	uint16_t lookType;
	uint8_t addons;
};

struct Outfit
{
	Outfit(std::string_view name, uint16_t lookType, PlayerSex_t sex, bool premium, bool unlocked) :
	    name{name}, lookType{lookType}, sex{sex}, premium{premium}, unlocked{unlocked} {
		std::memset(skills, 0, sizeof(skills));
		std::memset(stats,  0, sizeof(stats));
		std::memset(specialSkills, 0, sizeof(specialSkills));
	}

	bool operator==(const Outfit& otherOutfit) const
	{
		return name == otherOutfit.name && lookType == otherOutfit.lookType && sex == otherOutfit.sex &&
		       premium == otherOutfit.premium && unlocked == otherOutfit.unlocked;
	}

	std::string name;
	uint16_t lookType;
	PlayerSex_t sex;
	bool premium;
	bool unlocked;

	std::string from;
	bool manaShield  = false;
	bool invisible   = false;
	bool regeneration = false;

	int32_t speed       = 0;
	int32_t attackSpeed = 0;
	int32_t healthGain  = 0;
	int32_t healthTicks = 0;
	int32_t manaGain    = 0;
	int32_t manaTicks   = 0;

	int32_t specialSkills[SPECIALSKILL_LAST + 1];

	int32_t skills[SKILL_LAST + 1];
	int32_t stats[STAT_LAST + 1];
};

struct ProtocolOutfit
{
	ProtocolOutfit(std::string_view name, uint16_t lookType, uint8_t addons, uint8_t mode = 0, uint32_t storeOfferId = 0) :
	    name{name}, lookType{lookType}, addons{addons}, mode{mode}, storeOfferId{storeOfferId}
	{}

	std::string name;
	uint16_t lookType;
	uint8_t addons;
	uint8_t mode;
	uint32_t storeOfferId;
};

class Outfits
{
public:
	static Outfits& getInstance()
	{
		static Outfits instance;
		return instance;
	}

	bool reload();
	bool loadFromXml();

	const Outfit* getOutfitByLookType(uint16_t lookType) const;
	const Outfit* getOutfitByLookType(uint16_t lookType, PlayerSex_t sex) const;
	std::vector<ObserverPtr<const Outfit>> getOutfits(PlayerSex_t sex) const;
	uint32_t getOutfitId(PlayerSex_t sex, uint16_t lookType) const;

	bool addAttributes(uint32_t playerId, uint32_t outfitId, PlayerSex_t sex);
	bool removeAttributes(uint32_t playerId, uint32_t outfitId, PlayerSex_t sex);

private:
	std::unordered_map<uint16_t, Outfit> outfits;
};

#endif
