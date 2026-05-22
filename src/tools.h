// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_TOOLS_H
#define FS_TOOLS_H

#include "enums.h"
#include "position.h"

#include <random>

void printXMLError(std::string_view where, std::string_view fileName, const pugi::xml_parse_result& result);

std::string transformToSHA1(std::string_view input);
std::string transformToSHA1Hex(std::string_view input);

std::string generateToken(std::string_view key, uint64_t counter, size_t length = 6);
std::string generateRecoveryKey(int32_t fieldCount, int32_t fieldLength, bool mixCase = false);
std::string generateSecurePassword(int32_t length = 12);

bool validateAndFormatPlayerName(std::string& name);
bool caseInsensitiveEqual(std::string_view str1, std::string_view str2);
bool caseInsensitiveStartsWith(std::string_view str, std::string_view prefix);

std::vector<std::string_view> explodeString(std::string_view inString, std::string_view separator,
                                            int32_t limit = -1);
using IntegerVector = std::vector<int32_t>;
IntegerVector vectorAtoi(const std::vector<std::string_view>& stringVector);

std::mt19937& getRandomGenerator();
void toLowerCaseString(std::string& source);
std::string asLowerCaseString(std::string source);

int32_t uniform_random(int32_t minNumber, int32_t maxNumber);
int32_t normal_random(int32_t minNumber, int32_t maxNumber);
bool boolean_random(double probability = 0.5);

std::string convertIPToString(uint32_t ip);
std::string formatDateShort(time_t time);

Position getNextPosition(Direction direction, Position pos);
Direction getDirectionTo(const Position& from, const Position& to, bool extended = true);

MagicEffectClasses getMagicEffect(const std::string& strValue);
ShootType_t getShootType(const std::string& strValue);
std::string getCombatName(CombatType_t combatType);
TextColor_t getTextColorByName(std::string_view name, TextColor_t defaultColor);
Ammo_t getAmmoType(const std::string& strValue);
WeaponAction_t getWeaponAction(const std::string& strValue);
Skulls_t getSkullType(const std::string& strValue);
GuildEmblems_t getEmblemType(const std::string& strValue);
std::string getSkillName(uint8_t skillid);

uint32_t adlerChecksum(const uint8_t* data, size_t length);

std::string ucfirst(std::string str);
std::string ucwords(std::string str);
bool booleanString(std::string_view str);

std::string getWeaponName(WeaponType_t weaponType);

size_t combatTypeToIndex(CombatType_t combatType);
CombatType_t indexToCombatType(size_t v);

uint8_t serverFluidToClient(uint8_t serverFluid);
uint8_t clientFluidToServer(uint8_t clientFluid);

itemAttrTypes stringToItemAttribute(std::string_view str);

std::string getFirstLine(std::string_view str);
std::string getStringLine(std::string_view str, int lineNumber);
std::string formatValueK(int64_t value);

std::string_view getReturnMessage(ReturnValue value);

// OTSYS_TIME: returns milliseconds since steady_clock epoch.
// Cached once per dispatcher cycle via UPDATE_OTSYS_TIME().
// Falls back to a live clock call before the dispatcher initialises (cache == 0).
void UPDATE_OTSYS_TIME();
int64_t OTSYS_TIME();

// OTSYS_NANOTIME: nanosecond precision via high_resolution_clock.
// Not cached — only used in profiling paths, not hot game loops.
int64_t OTSYS_NANOTIME();

SpellGroup_t stringToSpellGroup(std::string_view value);

const std::vector<Direction>& getShuffleDirections();

std::string getVocationShortName(uint8_t vocationId);

constexpr uint8_t clientToServerFluidMap[] = {
    FLUID_NONE,
    FLUID_WATER,
    FLUID_MANA,
    FLUID_BEER,
    FLUID_WATER,
    FLUID_BLOOD,
    FLUID_SLIME,
    FLUID_LEMONADE,
    FLUID_MILK,
    FLUID_WATER,
    FLUID_WATER,
    FLUID_WATER,
    FLUID_WATER,
    FLUID_WATER,
    FLUID_WATER,
    FLUID_WATER,
    FLUID_WATER,
    FLUID_WATER,
    FLUID_WATER,
    FLUID_WINE,
    FLUID_WATER,
    FLUID_WATER,
    FLUID_WATER,
    FLUID_WATER,
    FLUID_HEALTH,
    FLUID_WATER,
    FLUID_URINE,
    FLUID_RUM,
    FLUID_FRUITJUICE,
    FLUID_WATER,
    FLUID_WATER,
    FLUID_WATER,
    FLUID_COCONUTMILK,
    FLUID_MEAD,
    FLUID_TEAORHERBS,
};

#endif
