// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_TOOLS_H
#define FS_TOOLS_H

#include "const.h"
#include "enums.h"
#include "position.h"

#include <random>
#include <string_view>

void printXMLError(std::string_view where, std::string_view fileName, const pugi::xml_parse_result& result);

std::string transformToSHA1(std::string_view input);
std::string transformToSHA1Hex(std::string_view input);
std::string generateToken(const std::string& key, uint32_t ticks);
std::string generateRecoveryKey(int32_t fieldCount, int32_t fieldLength, bool mixCase = false);
std::string generateSecurePassword(int32_t length = 12);
bool validateAndFormatPlayerName(std::string& name);

// checks that str1 is equivalent to str2 ignoring letter case
bool caseInsensitiveEqual(std::string_view str1, std::string_view str2);

// checks that str1 starts with str2 ignoring letter case
bool caseInsensitiveStartsWith(std::string_view str, std::string_view prefix);

void toLowerCaseString(std::string& source);
std::string asLowerCaseString(std::string source);

using StringVector = std::vector<std::string>;
using IntegerVector = std::vector<int32_t>;

std::vector<std::string_view> explodeString(std::string_view inString, const std::string& separator,
                                            int32_t limit = -1);
IntegerVector vectorAtoi(const std::vector<std::string_view>& stringVector);
constexpr bool hasBitSet(uint32_t flag, uint32_t flags) { return (flags & flag) != 0; }

std::mt19937& getRandomGenerator();
int32_t uniform_random(int32_t minNumber, int32_t maxNumber);
int32_t normal_random(int32_t minNumber, int32_t maxNumber);
bool boolean_random(double probability = 0.5);

Position getNextPosition(Direction direction, Position pos);
Direction getDirectionTo(const Position& from, const Position& to, bool extended = true);

std::string getFirstLine(std::string_view str);
std::string getStringLine(std::string_view str, const int lineNumber);

std::string formatDateShort(time_t time);
std::string convertIPToString(uint32_t ip);

MagicEffectClasses getMagicEffect(const std::string& strValue);
ShootType_t getShootType(const std::string& strValue);
Ammo_t getAmmoType(const std::string& strValue);
WeaponAction_t getWeaponAction(const std::string& strValue);
Skulls_t getSkullType(const std::string& strValue);
std::string getCombatName(CombatType_t combatType);

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

std::string_view getReturnMessage(ReturnValue value);

int64_t OTSYS_TIME();
int64_t OTSYS_NANOTIME();

SpellGroup_t stringToSpellGroup(std::string_view value);

const std::vector<Direction>& getShuffleDirections();

std::string getVocationShortName(uint8_t vocationId);

namespace tfs {

#if __has_cpp_attribute(__cpp_lib_to_underlying)

inline constexpr auto to_underlying(auto e) noexcept { return std::to_underlying(e); }

#else

inline constexpr auto to_underlying(auto e) noexcept { return static_cast<std::underlying_type_t<decltype(e)>>(e); }

#endif

} // namespace tfs

#endif
