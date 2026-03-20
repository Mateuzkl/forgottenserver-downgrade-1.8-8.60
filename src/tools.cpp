// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "tools.h"

#include "configmanager.h"
#include "logger.h"

#include <chrono>
#include <iomanip>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

void printXMLError(std::string_view where, std::string_view fileName, const pugi::xml_parse_result& result)
{
	LOG_ERROR(fmt::format("[{}] Failed to load {}: {}", where, fileName, result.description()));

	FILE* file = fopen(fileName.data(), "rb");
	if (!file) {
		return;
	}

	char buffer[32768];
	uint32_t currentLine = 1;
	std::string line;

	auto offset = static_cast<size_t>(result.offset);
	size_t lineOffsetPosition = 0;
	size_t index = 0;
	size_t bytes;
	do {
		bytes = fread(buffer, 1, 32768, file);
		for (size_t i = 0; i < bytes; ++i) {
			char ch = buffer[i];
			if (ch == '\n') {
				if ((index + i) >= offset) {
					lineOffsetPosition = line.length() - ((index + i) - offset);
					bytes = 0;
					break;
				}
				++currentLine;
				line.clear();
			} else {
				line.push_back(ch);
			}
		}
		index += bytes;
	} while (bytes == 32768);
	fclose(file);

	LOG_ERROR(fmt::format("Line {}:\n{}\n{}^", currentLine, line, std::string(lineOffsetPosition, ' ')));
}

std::string transformToSHA1(std::string_view input)
{
	std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx{EVP_MD_CTX_new(), EVP_MD_CTX_free};
	if (!ctx) {
		throw std::runtime_error("Failed to create EVP context");
	}

	std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> md{EVP_MD_fetch(nullptr, "SHA1", nullptr), EVP_MD_free};
	if (!md) {
		throw std::runtime_error("Failed to fetch SHA1");
	}

	if (!EVP_DigestInit_ex(ctx.get(), md.get(), nullptr)) {
		throw std::runtime_error("Message digest initialization failed");
	}

	if (!EVP_DigestUpdate(ctx.get(), input.data(), input.size())) {
		throw std::runtime_error("Message digest update failed");
	}

	unsigned int len = EVP_MD_size(md.get());
	std::string digest(static_cast<size_t>(len), '\0');
	if (!EVP_DigestFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(digest.data()), &len)) {
		throw std::runtime_error("Message digest finalization failed");
	}

	return digest;
}

std::string transformToSHA1Hex(std::string_view input)
{
	std::string digest = transformToSHA1(input);
	std::ostringstream hexStream;
	hexStream << std::hex << std::setfill('0');
	for (unsigned char c : digest) {
		hexStream << std::setw(2) << static_cast<unsigned int>(c);
	}
	return hexStream.str();
}

std::string hashPasswordSHA256(std::string_view password)
{
	// Generate 16-byte random salt
	unsigned char saltBytes[16];
	if (RAND_bytes(saltBytes, sizeof(saltBytes)) != 1) {
		throw std::runtime_error("Failed to generate random salt");
	}

	std::ostringstream saltHex;
	saltHex << std::hex << std::setfill('0');
	for (unsigned char c : saltBytes) {
		saltHex << std::setw(2) << static_cast<unsigned int>(c);
	}
	std::string salt = saltHex.str();

	// Compute SHA256(salt + password)
	std::string salted = salt + std::string(password);

	std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx{EVP_MD_CTX_new(), EVP_MD_CTX_free};
	if (!ctx) {
		throw std::runtime_error("Failed to create EVP context");
	}

	std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> md{EVP_MD_fetch(nullptr, "SHA256", nullptr), EVP_MD_free};
	if (!md) {
		throw std::runtime_error("Failed to fetch SHA256");
	}

	if (!EVP_DigestInit_ex(ctx.get(), md.get(), nullptr)) {
		throw std::runtime_error("SHA256 digest init failed");
	}

	if (!EVP_DigestUpdate(ctx.get(), salted.data(), salted.size())) {
		throw std::runtime_error("SHA256 digest update failed");
	}

	unsigned int len = EVP_MD_size(md.get());
	std::string digest(static_cast<size_t>(len), '\0');
	if (!EVP_DigestFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(digest.data()), &len)) {
		throw std::runtime_error("SHA256 digest finalize failed");
	}

	std::ostringstream hashHex;
	hashHex << std::hex << std::setfill('0');
	for (unsigned char c : digest) {
		hashHex << std::setw(2) << static_cast<unsigned int>(c);
	}

	// Format: $SHA256$<salt>$<hash>  (8 + 32 + 1 + 64 = 105 chars)
	return "$SHA256$" + salt + "$" + hashHex.str();
}

bool verifyPassword(std::string_view password, std::string_view storedHash)
{
	constexpr std::string_view SHA256_PREFIX = "$SHA256$";

	if (storedHash.size() > SHA256_PREFIX.size() && storedHash.substr(0, SHA256_PREFIX.size()) == SHA256_PREFIX) {
		// New format: $SHA256$<32-hex-salt>$<64-hex-hash>
		auto rest = storedHash.substr(SHA256_PREFIX.size());
		auto dollarPos = rest.find('$');
		if (dollarPos == std::string_view::npos) {
			return false;
		}

		std::string salt(rest.substr(0, dollarPos));
		std::string expectedHash(rest.substr(dollarPos + 1));

		std::string salted = salt + std::string(password);

		std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx{EVP_MD_CTX_new(), EVP_MD_CTX_free};
		if (!ctx) {
			return false;
		}

		std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> md{EVP_MD_fetch(nullptr, "SHA256", nullptr), EVP_MD_free};
		if (!md) {
			return false;
		}

		if (!EVP_DigestInit_ex(ctx.get(), md.get(), nullptr)) {
			return false;
		}

		if (!EVP_DigestUpdate(ctx.get(), salted.data(), salted.size())) {
			return false;
		}

		unsigned int len = EVP_MD_size(md.get());
		std::string digest(static_cast<size_t>(len), '\0');
		if (!EVP_DigestFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(digest.data()), &len)) {
			return false;
		}

		std::ostringstream hashHex;
		hashHex << std::hex << std::setfill('0');
		for (unsigned char c : digest) {
			hashHex << std::setw(2) << static_cast<unsigned int>(c);
		}

		return hashHex.str() == expectedHash;
	}

	// Legacy SHA1 format (40-char hex)
	return storedHash == transformToSHA1Hex(password);
}

std::string hmac(std::string_view algorithm, std::string_view key, std::string_view message)
{
	std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx{EVP_MD_CTX_new(), EVP_MD_CTX_free};
	if (!ctx) {
		throw std::runtime_error("Failed to create EVP context");
	}

	std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> md{EVP_MD_fetch(nullptr, algorithm.data(), nullptr), EVP_MD_free};
	if (!md) {
		throw std::runtime_error(fmt::format("Failed to fetch {:s}", algorithm));
	}

	std::array<unsigned char, EVP_MAX_MD_SIZE> result;
	unsigned int len;

	if (!HMAC(md.get(), key.data(), key.size(), reinterpret_cast<const unsigned char*>(message.data()), message.size(),
	          result.data(), &len)) {
		throw std::runtime_error("HMAC failed");
	}

	return {reinterpret_cast<char*>(result.data()), len};
}

std::string generateToken(std::string_view key, uint64_t counter, size_t length /*= AUTHENTICATOR_DIGITS*/)
{
	std::string mac(8, 0);
	for (uint8_t i = 8; --i; counter >>= 8) {
		mac[i] = static_cast<char>(counter % 256);
	}

	mac = hmac("SHA1", key, mac);

	// calculate hmac offset
	auto offset = mac.back() % 16u;

	// get truncated hash
	uint32_t p =
	    (static_cast<unsigned char>(mac[offset + 0]) << 24u) | (static_cast<unsigned char>(mac[offset + 1]) << 16u) |
	    (static_cast<unsigned char>(mac[offset + 2]) << 8u) | (static_cast<unsigned char>(mac[offset + 3]) << 0u);

	auto token = std::to_string(p & 0x7fffffff);
	return token.substr(token.size() - length);
}

std::string generateRecoveryKey(int32_t fieldCount, int32_t fieldLength, bool mixCase /* = false*/)
{
	std::string key;
	key.reserve(fieldCount * (fieldLength + 1));
	for (int32_t i = 0; i < fieldCount; ++i) {
		if (i > 0) {
			key += '-';
		}
		for (int32_t j = 0; j < fieldLength; ++j) {
			char ch;
			int32_t charType = uniform_random(0, mixCase ? 2 : 1);
			if (charType == 0) {
				ch = '0' + uniform_random(2, 9);
			} else if (charType == 1 || !mixCase) {
				ch = 'A' + uniform_random(0, 25);
			} else {
				ch = 'a' + uniform_random(0, 25);
			}
			key += ch;
		}
	}
	return key;
}

std::string generateSecurePassword(int32_t length /*= 12*/)
{
	const char* uppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const char* lowercase = "abcdefghijklmnopqrstuvwxyz";
	const char* numbers = "0123456789";
	const char* symbols = "!@#$%^&*-_+=";

	std::string password;
	password.reserve(length);

	password += uppercase[uniform_random(0, 25)];
	password += lowercase[uniform_random(0, 25)];
	password += numbers[uniform_random(0, 9)];
	password += symbols[uniform_random(0, 11)];

	const char* allChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*-_+=";
	const int32_t allCharsLen = 74;

	for (int32_t i = 4; i < length; ++i) {
		password += allChars[uniform_random(0, allCharsLen - 1)];
	}

	for (int32_t i = length - 1; i > 0; --i) {
		int32_t j = uniform_random(0, i);
		std::swap(password[i], password[j]);
	}

	return password;
}

bool validateAndFormatPlayerName(std::string& name)
{
	if (name.empty()) {
		return false;
	}

	size_t start = name.find_first_not_of(' ');
	size_t end = name.find_last_not_of(' ');
	if (start == std::string::npos) {
		return false;
	}

	name = name.substr(start, end - start + 1);

	if (name.length() < 3 || name.length() > 29) {
		return false;
	}

	if (!std::isalpha(static_cast<unsigned char>(name[0]))) {
		return false;
	}

	if (!std::isalpha(static_cast<unsigned char>(name[name.length() - 1]))) {
		return false;
	}

	char lastChar = ' ';
	bool capitalizeNext = true;

	for (size_t i = 0; i < name.length(); ++i) {
		char c = name[i];

		if (!std::isalpha(static_cast<unsigned char>(c)) && c != ' ' && c != '\'') {
			return false;
		}

		if ((c == ' ' || c == '\'') && (lastChar == ' ' || lastChar == '\'')) {
			return false;
		}

		if (capitalizeNext && std::isalpha(static_cast<unsigned char>(c))) {
			name[i] = std::toupper(static_cast<unsigned char>(c));
			capitalizeNext = false;
		} else if (std::isalpha(static_cast<unsigned char>(c))) {
			name[i] = std::tolower(static_cast<unsigned char>(c));
		} else if (c == ' ') {
			capitalizeNext = true;
		}

		lastChar = c;
	}

	return true;
}

bool caseInsensitiveEqual(std::string_view str1, std::string_view str2)
{
	return str1.size() == str2.size() &&
	       std::equal(str1.begin(), str1.end(), str2.begin(), [](char a, char b) { return tolower(a) == tolower(b); });
}

bool caseInsensitiveStartsWith(std::string_view str, std::string_view prefix)
{
	return str.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), str.begin(),
	                                                 [](char a, char b) { return tolower(a) == tolower(b); });
}

std::vector<std::string_view> explodeString(std::string_view inString, const std::string& separator,
                                            int32_t limit /* = -1*/)
{
	std::vector<std::string_view> returnVector;
	std::string_view::size_type start = 0, end = 0;

	while (--limit != -1 && (end = inString.find(separator, start)) != std::string_view::npos) {
		returnVector.push_back(inString.substr(start, end - start));
		start = end + separator.size();
	}

	returnVector.push_back(inString.substr(start));
	return returnVector;
}

IntegerVector vectorAtoi(const std::vector<std::string_view>& stringVector)
{
	IntegerVector returnVector;
	for (const auto& string : stringVector) {
		returnVector.push_back(std::stoi(string.data()));
	}
	return returnVector;
}

std::mt19937& getRandomGenerator()
{
	static std::random_device rd;
	static std::mt19937 generator(rd());
	return generator;
}

void toLowerCaseString(std::string& source) { std::transform(source.begin(), source.end(), source.begin(), tolower); }

std::string asLowerCaseString(std::string source)
{
	toLowerCaseString(source);
	return source;
}

int32_t uniform_random(int32_t minNumber, int32_t maxNumber)
{
	static std::uniform_int_distribution<int32_t> uniformRand;
	if (minNumber == maxNumber) {
		return minNumber;
	} else if (minNumber > maxNumber) {
		std::swap(minNumber, maxNumber);
	}
	return uniformRand(getRandomGenerator(), std::uniform_int_distribution<int32_t>::param_type(minNumber, maxNumber));
}

int32_t normal_random(int32_t minNumber, int32_t maxNumber)
{
	static std::normal_distribution<float> normalRand(0.5f, 0.25f);

	float v;
	do {
		v = normalRand(getRandomGenerator());
	} while (v < 0.0 || v > 1.0);

	auto&& [a, b] = std::minmax(minNumber, maxNumber);
	return a + std::lround(v * (b - a));
}

bool boolean_random(double probability /* = 0.5*/)
{
	static std::bernoulli_distribution booleanRand;
	return booleanRand(getRandomGenerator(), std::bernoulli_distribution::param_type(probability));
}

std::string convertIPToString(uint32_t ip)
{
	char buffer[17];

	int res = sprintf(buffer, "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24));
	if (res < 0) {
		return {};
	}

	return buffer;
}

std::string formatDateShort(time_t time)
{
#ifdef _MSC_VER
	return std::format("{:%d %b %Y}", std::chrono::system_clock::from_time_t(time));
#else
	const tm* tms = localtime(&time);
	if (!tms) {
		return {};
	}
	char buffer[32];
	strftime(buffer, sizeof(buffer), "%d %b %Y", tms);
	return buffer;
#endif
}

Position getNextPosition(Direction direction, Position pos)
{
	switch (direction) {
		case DIRECTION_NORTH:
			pos.y--;
			break;

		case DIRECTION_SOUTH:
			pos.y++;
			break;

		case DIRECTION_WEST:
			pos.x--;
			break;

		case DIRECTION_EAST:
			pos.x++;
			break;

		case DIRECTION_SOUTHWEST:
			pos.x--;
			pos.y++;
			break;

		case DIRECTION_NORTHWEST:
			pos.x--;
			pos.y--;
			break;

		case DIRECTION_NORTHEAST:
			pos.x++;
			pos.y--;
			break;

		case DIRECTION_SOUTHEAST:
			pos.x++;
			pos.y++;
			break;

		default:
			break;
	}

	return pos;
}

Direction getDirectionTo(const Position& from, const Position& to, bool extended /* = true*/)
{
	if (from == to) {
		return DIRECTION_NONE;
	}

	Direction dir;

	int32_t x_offset = from.getOffsetX(to);
	if (x_offset < 0) {
		dir = DIRECTION_EAST;
		x_offset = std::abs(x_offset);
	} else {
		dir = DIRECTION_WEST;
	}

	int32_t y_offset = from.getOffsetY(to);
	if (y_offset >= 0) {
		if (y_offset > x_offset) {
			dir = DIRECTION_NORTH;
		} else if (y_offset == x_offset && extended) {
			if (dir == DIRECTION_EAST) {
				dir = DIRECTION_NORTHEAST;
			} else {
				dir = DIRECTION_NORTHWEST;
			}
		}
	} else {
		y_offset = std::abs(y_offset);
		if (y_offset > x_offset) {
			dir = DIRECTION_SOUTH;
		} else if (y_offset == x_offset && extended) {
			if (dir == DIRECTION_EAST) {
				dir = DIRECTION_SOUTHEAST;
			} else {
				dir = DIRECTION_SOUTHWEST;
			}
		}
	}
	return dir;
}

using MagicEffectNames = std::unordered_map<std::string, MagicEffectClasses>;
using ShootTypeNames = std::unordered_map<std::string, ShootType_t>;
using CombatTypeNames = std::unordered_map<CombatType_t, std::string, std::hash<int32_t>>;
using AmmoTypeNames = std::unordered_map<std::string, Ammo_t>;
using WeaponActionNames = std::unordered_map<std::string, WeaponAction_t>;
using SkullNames = std::unordered_map<std::string, Skulls_t>;

MagicEffectNames magicEffectNames = {{"redspark", CONST_ME_DRAWBLOOD},
                                     {"bluebubble", CONST_ME_LOSEENERGY},
                                     {"poff", CONST_ME_POFF},
                                     {"yellowspark", CONST_ME_BLOCKHIT},
                                     {"explosionarea", CONST_ME_EXPLOSIONAREA},
                                     {"explosion", CONST_ME_EXPLOSIONHIT},
                                     {"firearea", CONST_ME_FIREAREA},
                                     {"yellowbubble", CONST_ME_YELLOW_RINGS},
                                     {"greenbubble", CONST_ME_GREEN_RINGS},
                                     {"blackspark", CONST_ME_HITAREA},
                                     {"teleport", CONST_ME_TELEPORT},
                                     {"energy", CONST_ME_ENERGYHIT},
                                     {"blueshimmer", CONST_ME_MAGIC_BLUE},
                                     {"redshimmer", CONST_ME_MAGIC_RED},
                                     {"greenshimmer", CONST_ME_MAGIC_GREEN},
                                     {"fire", CONST_ME_HITBYFIRE},
                                     {"greenspark", CONST_ME_HITBYPOISON},
                                     {"mortarea", CONST_ME_MORTAREA},
                                     {"greennote", CONST_ME_SOUND_GREEN},
                                     {"rednote", CONST_ME_SOUND_RED},
                                     {"poison", CONST_ME_POISONAREA},
                                     {"yellownote", CONST_ME_SOUND_YELLOW},
                                     {"purplenote", CONST_ME_SOUND_PURPLE},
                                     {"bluenote", CONST_ME_SOUND_BLUE},
                                     {"whitenote", CONST_ME_SOUND_WHITE},
                                     {"bubbles", CONST_ME_BUBBLES},
                                     {"dice", CONST_ME_CRAPS},
                                     {"giftwraps", CONST_ME_GIFT_WRAPS},
                                     {"yellowfirework", CONST_ME_FIREWORK_YELLOW},
                                     {"redfirework", CONST_ME_FIREWORK_RED},
                                     {"bluefirework", CONST_ME_FIREWORK_BLUE},
                                     {"stun", CONST_ME_STUN},
                                     {"sleep", CONST_ME_SLEEP},
                                     {"watercreature", CONST_ME_WATERCREATURE},
                                     {"groundshaker", CONST_ME_GROUNDSHAKER},
                                     {"hearts", CONST_ME_HEARTS},
                                     {"fireattack", CONST_ME_FIREATTACK},
                                     {"energyarea", CONST_ME_ENERGYAREA},
                                     {"smallclouds", CONST_ME_SMALLCLOUDS},
                                     {"holydamage", CONST_ME_HOLYDAMAGE},
                                     {"bigclouds", CONST_ME_BIGCLOUDS},
                                     {"icearea", CONST_ME_ICEAREA},
                                     {"icetornado", CONST_ME_ICETORNADO},
                                     {"iceattack", CONST_ME_ICEATTACK},
                                     {"stones", CONST_ME_STONES},
                                     {"smallplants", CONST_ME_SMALLPLANTS},
                                     {"carniphila", CONST_ME_CARNIPHILA},
                                     {"purpleenergy", CONST_ME_PURPLEENERGY},
                                     {"yellowenergy", CONST_ME_YELLOWENERGY},
                                     {"holyarea", CONST_ME_HOLYAREA},
                                     {"bigplants", CONST_ME_BIGPLANTS},
                                     {"cake", CONST_ME_CAKE},
                                     {"giantice", CONST_ME_GIANTICE},
                                     {"watersplash", CONST_ME_WATERSPLASH},
                                     {"plantattack", CONST_ME_PLANTATTACK},
                                     {"tutorialarrow", CONST_ME_TUTORIALARROW},
                                     {"tutorialsquare", CONST_ME_TUTORIALSQUARE},
                                     {"mirrorhorizontal", CONST_ME_MIRRORHORIZONTAL},
                                     {"mirrorvertical", CONST_ME_MIRRORVERTICAL},
                                     {"skullhorizontal", CONST_ME_SKULLHORIZONTAL},
                                     {"skullvertical", CONST_ME_SKULLVERTICAL},
                                     {"assassin", CONST_ME_ASSASSIN},
                                     {"stepshorizontal", CONST_ME_STEPSHORIZONTAL},
                                     {"bloodysteps", CONST_ME_BLOODYSTEPS},
                                     {"stepsvertical", CONST_ME_STEPSVERTICAL},
                                     {"yalaharighost", CONST_ME_YALAHARIGHOST},
                                     {"bats", CONST_ME_BATS},
                                     {"smoke", CONST_ME_SMOKE},
                                     {"insects", CONST_ME_INSECTS},
                                     {"dragonhead", CONST_ME_DRAGONHEAD},
                                     {"orcshaman", CONST_ME_ORCSHAMAN},
                                     {"orcshamanfire", CONST_ME_ORCSHAMAN_FIRE},
                                     {"thunder", CONST_ME_THUNDER},
                                     {"ferumbras", CONST_ME_FERUMBRAS},
                                     {"confettihorizontal", CONST_ME_CONFETTI_HORIZONTAL},
                                     {"confettivertical", CONST_ME_CONFETTI_VERTICAL},
                                     {"blacksmoke", CONST_ME_BLACKSMOKE},
                                     {"redsmoke", CONST_ME_REDSMOKE},
                                     {"yellowsmoke", CONST_ME_YELLOWSMOKE},
                                     {"greensmoke", CONST_ME_GREENSMOKE},
                                     {"purplesmoke", CONST_ME_PURPLESMOKE},
                                     {"lightning", CONST_ME_LIGHTNING},
                                     {"ragiazbonecapsule", CONST_ME_RAGIAZ_BONE_CAPSULE},
                                     {"criticalhit", CONST_ME_CRITICAL_HIT},
                                     {"plungingfish", CONST_ME_PLUNGING_FISH},
                                     {"blueenergyspark", CONST_ME_BLUE_ENERGY_SPARK},
                                     {"orangeenergyspark", CONST_ME_ORANGE_ENERGY_SPARK},
                                     {"greenenergyspark", CONST_ME_GREEN_ENERGY_SPARK},
                                     {"pinkenergyspark", CONST_ME_PINK_ENERGY_SPARK},
                                     {"whiteenergyspark", CONST_ME_WHITE_ENERGY_SPARK},
                                     {"yellowenergyspark", CONST_ME_YELLOW_ENERGY_SPARK},
                                     {"magicpowder", CONST_ME_MAGIC_POWDER},
                                     {"pixieexplosion", CONST_ME_PIXIE_EXPLOSION},
                                     {"pixiecoming", CONST_ME_PIXIE_COMING},
                                     {"pixiegoing", CONST_ME_PIXIE_GOING},
                                     {"storm", CONST_ME_STORM},
                                     {"stonestorm", CONST_ME_STONE_STORM},
                                     {"blueghost", CONST_ME_BLUE_GHOST},
                                     {"pinkvortex", CONST_ME_PINK_VORTEX},
                                     {"tresauremap", CONST_ME_TREASURE_MAP},
                                     {"pinkbeam", CONST_ME_PINK_BEAM},
                                     {"greenfireworks", CONST_ME_GREEN_FIREWORKS},
                                     {"orangefireworks", CONST_ME_ORANGE_FIREWORKS},
                                     {"pinkfireworks", CONST_ME_PINK_FIREWORKS},
                                     {"bluefireworks", CONST_ME_BLUE_FIREWORKS},
                                     {"blackblood", CONST_ME_BLACK_BLOOD},
                                     {"supremecube", CONST_ME_SUPREME_CUBE},
                                     {"prismaticspark", CONST_ME_PRISMATIC_SPARK},
                                     {"thaian", CONST_ME_THAIAN},
                                     {"thaianghost", CONST_ME_THAIAN_GHOST},
                                     {"ghostsmoke", CONST_ME_GHOST_SMOKE},
                                     {"waterblockfloating", CONST_ME_WATER_BLOCK_FLOATING},
                                     {"waterblock", CONST_ME_WATER_BLOCK},
                                     {"roots", CONST_ME_ROOTS},
                                     {"ghostlyscratch", CONST_ME_GHOSTLY_SCRATCH},
                                     {"ghostlybite", CONST_ME_GHOSTLY_BITE},
                                     {"bigscratch", CONST_ME_BIG_SCRATCH},
                                     {"slash", CONST_ME_SLASH},
                                     {"bite", CONST_ME_BITE},
                                     {"chivalriouschallenge", CONST_ME_CHIVALRIOUS_CHALLENGE},
                                     {"divinedazzle", CONST_ME_DIVINE_DAZZLE},
                                     {"eletricalspark", CONST_ME_ELECTRICALSPARK},
                                     {"purpleteleport", CONST_ME_PURPLETELEPORT},
                                     {"redteleport", CONST_ME_REDTELEPORT},
                                     {"orangeteleport", CONST_ME_ORANGETELEPORT},
                                     {"greyteleport", CONST_ME_GREYTELEPORT},
                                     {"lightblueteleport", CONST_ME_LIGHTBLUETELEPORT},
                                     {"fatal", CONST_ME_FATAL},
                                     {"dodge", CONST_ME_DODGE},
                                     {"hourglass", CONST_ME_HOURGLASS},
                                     {"dazzling", CONST_ME_DAZZLING},
                                     {"ferumbras1", CONST_ME_FERUMBRAS_1},
                                     {"gazharagoth", CONST_ME_GAZHARAGOTH},
                                     {"madmage", CONST_ME_MAD_MAGE},
                                     {"horestis", CONST_ME_HORESTIS},
                                     {"devovorga", CONST_ME_DEVOVORGA},
                                     {"ferumbras2", CONST_ME_FERUMBRAS_2},
                                     {"whitesmoke", CONST_ME_WHITE_SMOKE},
                                     {"whitesmokes", CONST_ME_WHITE_SMOKES},
                                     {"waterdrop", CONST_ME_WATER_DROP},
                                     {"avatarappear", CONST_ME_AVATAR_APPEAR},
                                     {"divinegrenade", CONST_ME_DIVINE_GRENADE},
                                     {"divineempowerment", CONST_ME_DIVINE_EMPOWERMENT},
                                     {"waterfloatingthrash", CONST_ME_WATER_FLOATING_THRASH},
                                     {"agony", CONST_ME_AGONY},
                                     {"loothighlight", CONST_ME_LOOT_HIGHLIGHT},
                                     {"meltingcream", CONST_ME_MELTING_CREAM},
                                     {"reaper", CONST_ME_REAPER},
                                     {"powerfulhearts", CONST_ME_POWERFUL_HEARTS},
                                     {"cream", CONST_ME_CREAM},
                                     {"gentlebubble", CONST_ME_GENTLE_BUBBLE},
                                     {"startburst", CONST_ME_STARBURST},
                                     {"siurp", CONST_ME_SIURP},
                                     {"cacao", CONST_ME_CACAO},
                                     {"candyfloss", CONST_ME_CANDY_FLOSS}};

ShootTypeNames shootTypeNames = {{"spear", CONST_ANI_SPEAR},
                                 {"bolt", CONST_ANI_BOLT},
                                 {"arrow", CONST_ANI_ARROW},
                                 {"fire", CONST_ANI_FIRE},
                                 {"energy", CONST_ANI_ENERGY},
                                 {"poisonarrow", CONST_ANI_POISONARROW},
                                 {"burstarrow", CONST_ANI_BURSTARROW},
                                 {"throwingstar", CONST_ANI_THROWINGSTAR},
                                 {"throwingknife", CONST_ANI_THROWINGKNIFE},
                                 {"smallstone", CONST_ANI_SMALLSTONE},
                                 {"death", CONST_ANI_DEATH},
                                 {"largerock", CONST_ANI_LARGEROCK},
                                 {"snowball", CONST_ANI_SNOWBALL},
                                 {"powerbolt", CONST_ANI_POWERBOLT},
                                 {"poison", CONST_ANI_POISON},
                                 {"infernalbolt", CONST_ANI_INFERNALBOLT},
                                 {"huntingspear", CONST_ANI_HUNTINGSPEAR},
                                 {"enchantedspear", CONST_ANI_ENCHANTEDSPEAR},
                                 {"redstar", CONST_ANI_REDSTAR},
                                 {"greenstar", CONST_ANI_GREENSTAR},
                                 {"royalspear", CONST_ANI_ROYALSPEAR},
                                 {"sniperarrow", CONST_ANI_SNIPERARROW},
                                 {"onyxarrow", CONST_ANI_ONYXARROW},
                                 {"piercingbolt", CONST_ANI_PIERCINGBOLT},
                                 {"whirlwindsword", CONST_ANI_WHIRLWINDSWORD},
                                 {"whirlwindaxe", CONST_ANI_WHIRLWINDAXE},
                                 {"whirlwindclub", CONST_ANI_WHIRLWINDCLUB},
                                 {"etherealspear", CONST_ANI_ETHEREALSPEAR},
                                 {"ice", CONST_ANI_ICE},
                                 {"earth", CONST_ANI_EARTH},
                                 {"holy", CONST_ANI_HOLY},
                                 {"suddendeath", CONST_ANI_SUDDENDEATH},
                                 {"flasharrow", CONST_ANI_FLASHARROW},
                                 {"flammingarrow", CONST_ANI_FLAMMINGARROW},
                                 {"shiverarrow", CONST_ANI_SHIVERARROW},
                                 {"energyball", CONST_ANI_ENERGYBALL},
                                 {"smallice", CONST_ANI_SMALLICE},
                                 {"smallholy", CONST_ANI_SMALLHOLY},
                                 {"smallearth", CONST_ANI_SMALLEARTH},
                                 {"eartharrow", CONST_ANI_EARTHARROW},
                                 {"explosion", CONST_ANI_EXPLOSION},
                                 {"cake", CONST_ANI_CAKE},
                                 {"envenomedarrow", CONST_ANI_ENVENOMEDARROW},
                                 {"gloothspear", CONST_ANI_GLOOTHSPEAR},
                                 {"simplearrow", CONST_ANI_SIMPLEARROW},
                                 {"leafstar", CONST_ANI_LEAFSTAR},
                                 {"prismaticbolt", CONST_ANI_PRISMATICBOLT},
                                 {"drillbolt", CONST_ANI_DRILLBOLT},
                                 {"vortexbolt", CONST_ANI_VORTEXBOLT},
                                 {"tarsalarrow", CONST_ANI_TARSALARROW},
                                 {"crystallinearrow", CONST_ANI_CRYSTALLINEARROW},
                                 {"diamondarrow", CONST_ANI_DIAMONDARROW},
                                 {"spectralbolt", CONST_ANI_SPECTRALBOLT},
                                 {"royalstar", CONST_ANI_ROYALSTAR},
                                 {"candycane", CONST_ANI_CANDYCANE},
                                 {"cherrybomb", CONST_ANI_CHERRYBOMB}};

CombatTypeNames combatTypeNames = {
    {COMBAT_PHYSICALDAMAGE, "physical"}, {COMBAT_ENERGYDAMAGE, "energy"},       {COMBAT_EARTHDAMAGE, "earth"},
    {COMBAT_FIREDAMAGE, "fire"},         {COMBAT_UNDEFINEDDAMAGE, "undefined"}, {COMBAT_LIFEDRAIN, "lifedrain"},
    {COMBAT_MANADRAIN, "manadrain"},     {COMBAT_HEALING, "healing"},           {COMBAT_DROWNDAMAGE, "drown"},
    {COMBAT_ICEDAMAGE, "ice"},           {COMBAT_HOLYDAMAGE, "holy"},           {COMBAT_DEATHDAMAGE, "death"},
};

AmmoTypeNames ammoTypeNames = {
    {"spear", AMMO_SPEAR},
    {"bolt", AMMO_BOLT},
    {"arrow", AMMO_ARROW},
    {"poisonarrow", AMMO_ARROW},
    {"burstarrow", AMMO_ARROW},
    {"throwingstar", AMMO_THROWINGSTAR},
    {"throwingknife", AMMO_THROWINGKNIFE},
    {"smallstone", AMMO_STONE},
    {"largerock", AMMO_STONE},
    {"snowball", AMMO_SNOWBALL},
    {"powerbolt", AMMO_BOLT},
    {"infernalbolt", AMMO_BOLT},
    {"huntingspear", AMMO_SPEAR},
    {"enchantedspear", AMMO_SPEAR},
    {"royalspear", AMMO_SPEAR},
    {"sniperarrow", AMMO_ARROW},
    {"onyxarrow", AMMO_ARROW},
    {"piercingbolt", AMMO_BOLT},
    {"etherealspear", AMMO_SPEAR},
    {"flasharrow", AMMO_ARROW},
    {"flammingarrow", AMMO_ARROW},
    {"shiverarrow", AMMO_ARROW},
    {"eartharrow", AMMO_ARROW},
};

WeaponActionNames weaponActionNames = {
    {"move", WEAPONACTION_MOVE},
    {"removecharge", WEAPONACTION_REMOVECHARGE},
    {"removecount", WEAPONACTION_REMOVECOUNT},
};

SkullNames skullNames = {
    {"none", SKULL_NONE},   {"yellow", SKULL_YELLOW}, {"green", SKULL_GREEN},
    {"white", SKULL_WHITE}, {"red", SKULL_RED},       {"black", SKULL_BLACK},
};

MagicEffectClasses getMagicEffect(const std::string& strValue)
{
	auto magicEffect = magicEffectNames.find(strValue);
	if (magicEffect != magicEffectNames.end()) {
		return magicEffect->second;
	}
	return CONST_ME_NONE;
}

ShootType_t getShootType(const std::string& strValue)
{
	auto shootType = shootTypeNames.find(strValue);
	if (shootType != shootTypeNames.end()) {
		return shootType->second;
	}
	return CONST_ANI_NONE;
}

std::string getCombatName(CombatType_t combatType)
{
	auto combatName = combatTypeNames.find(combatType);
	if (combatName != combatTypeNames.end()) {
		return combatName->second;
	}
	return "unknown";
}

Ammo_t getAmmoType(const std::string& strValue)
{
	auto ammoType = ammoTypeNames.find(strValue);
	if (ammoType != ammoTypeNames.end()) {
		return ammoType->second;
	}
	return AMMO_NONE;
}

WeaponAction_t getWeaponAction(const std::string& strValue)
{
	auto weaponAction = weaponActionNames.find(strValue);
	if (weaponAction != weaponActionNames.end()) {
		return weaponAction->second;
	}
	return WEAPONACTION_NONE;
}

Skulls_t getSkullType(const std::string& strValue)
{
	auto skullType = skullNames.find(strValue);
	if (skullType != skullNames.end()) {
		return skullType->second;
	}
	return SKULL_NONE;
}

std::string getSkillName(uint8_t skillid)
{
	switch (skillid) {
		case SKILL_FIST:
			return "fist fighting";

		case SKILL_CLUB:
			return "club fighting";

		case SKILL_SWORD:
			return "sword fighting";

		case SKILL_AXE:
			return "axe fighting";

		case SKILL_DISTANCE:
			return "distance fighting";

		case SKILL_SHIELD:
			return "shielding";

		case SKILL_FISHING:
			return "fishing";

		case SKILL_MAGLEVEL:
			return "magic level";

		case SKILL_LEVEL:
			return "level";

		default:
			return "unknown";
	}
}

uint32_t adlerChecksum(const uint8_t* data, size_t length)
{
	if (length > NETWORKMESSAGE_MAXSIZE) {
		return 0;
	}

	const uint16_t adler = 65521;

	uint32_t a = 1, b = 0;

	while (length > 0) {
		size_t tmp = length > 5552 ? 5552 : length;
		length -= tmp;

		do {
			a += *data++;
			b += a;
		} while (--tmp);

		a %= adler;
		b %= adler;
	}

	return (b << 16) | a;
}

std::string ucfirst(std::string str)
{
	for (char& i : str) {
		if (i != ' ') {
			i = static_cast<char>(toupper(i));
			break;
		}
	}
	return str;
}

std::string ucwords(std::string str)
{
	size_t strLength = str.length();
	if (strLength == 0) {
		return str;
	}

	str[0] = static_cast<char>(toupper(str.front()));
	for (size_t i = 1; i < strLength; ++i) {
		if (str[i - 1] == ' ') {
			str[i] = static_cast<char>(toupper(str[i]));
		}
	}

	return str;
}

bool booleanString(std::string_view str)
{
	if (str.empty()) {
		return false;
	}

	char ch = static_cast<char>(tolower(str.front()));
	return ch != 'f' && ch != 'n' && ch != '0';
}

std::string getWeaponName(WeaponType_t weaponType)
{
	switch (weaponType) {
		case WEAPON_SWORD:
			return "sword";
		case WEAPON_CLUB:
			return "club";
		case WEAPON_AXE:
			return "axe";
		case WEAPON_DISTANCE:
			return "distance";
		case WEAPON_WAND:
			return "wand";
		case WEAPON_AMMO:
			return "ammunition";
		case WEAPON_FIST:
			return "fist";
		default:
			return std::string();
	}
}

size_t combatTypeToIndex(CombatType_t combatType)
{
	switch (combatType) {
		case COMBAT_PHYSICALDAMAGE:
			return 0;
		case COMBAT_ENERGYDAMAGE:
			return 1;
		case COMBAT_EARTHDAMAGE:
			return 2;
		case COMBAT_FIREDAMAGE:
			return 3;
		case COMBAT_UNDEFINEDDAMAGE:
			return 4;
		case COMBAT_LIFEDRAIN:
			return 5;
		case COMBAT_MANADRAIN:
			return 6;
		case COMBAT_HEALING:
			return 7;
		case COMBAT_DROWNDAMAGE:
			return 8;
		case COMBAT_ICEDAMAGE:
			return 9;
		case COMBAT_HOLYDAMAGE:
			return 10;
		case COMBAT_DEATHDAMAGE:
			return 11;
		default:
			return 0;
	}
}

CombatType_t indexToCombatType(size_t v) { return static_cast<CombatType_t>(1 << v); }

uint8_t serverFluidToClient(uint8_t serverFluid)
{
	uint8_t size = sizeof(clientToServerFluidMap) / sizeof(uint8_t);
	for (uint8_t i = 0; i < size; ++i) {
		if (clientToServerFluidMap[i] == serverFluid) {
			return i;
		}
	}
	return 0;
}

uint8_t clientFluidToServer(uint8_t clientFluid)
{
	uint8_t size = sizeof(clientToServerFluidMap) / sizeof(uint8_t);
	if (clientFluid >= size) {
		return 0;
	}
	return clientToServerFluidMap[clientFluid];
}

itemAttrTypes stringToItemAttribute(std::string_view str)
{
	if (str == "aid") {
		return ITEM_ATTRIBUTE_ACTIONID;
	} else if (str == "uid") {
		return ITEM_ATTRIBUTE_UNIQUEID;
	} else if (str == "description") {
		return ITEM_ATTRIBUTE_DESCRIPTION;
	} else if (str == "text") {
		return ITEM_ATTRIBUTE_TEXT;
	} else if (str == "date") {
		return ITEM_ATTRIBUTE_DATE;
	} else if (str == "writer") {
		return ITEM_ATTRIBUTE_WRITER;
	} else if (str == "name") {
		return ITEM_ATTRIBUTE_NAME;
	} else if (str == "article") {
		return ITEM_ATTRIBUTE_ARTICLE;
	} else if (str == "pluralname") {
		return ITEM_ATTRIBUTE_PLURALNAME;
	} else if (str == "weight") {
		return ITEM_ATTRIBUTE_WEIGHT;
	} else if (str == "attack") {
		return ITEM_ATTRIBUTE_ATTACK;
	} else if (str == "defense") {
		return ITEM_ATTRIBUTE_DEFENSE;
	} else if (str == "extradefense") {
		return ITEM_ATTRIBUTE_EXTRADEFENSE;
	} else if (str == "armor") {
		return ITEM_ATTRIBUTE_ARMOR;
	} else if (str == "hitchance") {
		return ITEM_ATTRIBUTE_HITCHANCE;
	} else if (str == "shootrange") {
		return ITEM_ATTRIBUTE_SHOOTRANGE;
	} else if (str == "owner") {
		return ITEM_ATTRIBUTE_OWNER;
	} else if (str == "duration") {
		return ITEM_ATTRIBUTE_DURATION;
	} else if (str == "decaystate") {
		return ITEM_ATTRIBUTE_DECAYSTATE;
	} else if (str == "corpseowner") {
		return ITEM_ATTRIBUTE_CORPSEOWNER;
	} else if (str == "charges") {
		return ITEM_ATTRIBUTE_CHARGES;
	} else if (str == "fluidtype") {
		return ITEM_ATTRIBUTE_FLUIDTYPE;
	} else if (str == "doorid") {
		return ITEM_ATTRIBUTE_DOORID;
	} else if (str == "decayto") {
		return ITEM_ATTRIBUTE_DECAYSTATE;
	} else if (str == "wrapid") {
		return ITEM_ATTRIBUTE_WRAPID;
	} else if (str == "storeitem") {
		return ITEM_ATTRIBUTE_STOREITEM;
	} else if (str == "attackspeed") {
		return ITEM_ATTRIBUTE_ATTACK_SPEED;
	} else if (str == "classification") {
		return ITEM_ATTRIBUTE_CLASSIFICATION;
	} else if (str == "tier") {
		return ITEM_ATTRIBUTE_TIER;
	} else if (str == "rewardid") {
		return ITEM_ATTRIBUTE_REWARDID;
	}
	return ITEM_ATTRIBUTE_NONE;
}

std::string getFirstLine(std::string_view str)
{
	std::string firstLine;
	firstLine.reserve(str.length());
	for (const char c : str) {
		if (c == '\n') {
			break;
		}
		firstLine.push_back(c);
	}
	return firstLine;
}

std::string getStringLine(std::string_view str, const int lineNumber)
{
	std::istringstream iss(str.data());
	std::string line;
	for (int i = 1; i < lineNumber; ++i) {
		std::getline(iss, line);
	}
	return std::getline(iss, line) ? line : std::string{};
}

std::string_view getReturnMessage(ReturnValue value)
{
	switch (value) {
		case RETURNVALUE_DESTINATIONOUTOFREACH:
			return "Destination is out of range.";

		case RETURNVALUE_NOTMOVEABLE:
			return "You cannot move this object.";

		case RETURNVALUE_CREATURENOTMOVEABLE:
			return "You cannot move this creature.";

		case RETURNVALUE_DROPTWOHANDEDITEM:
			return "Drop the double-handed object first.";

		case RETURNVALUE_BOTHHANDSNEEDTOBEFREE:
			return "Both hands need to be free.";

		case RETURNVALUE_CANNOTBEDRESSED:
			return "You cannot dress this object there.";

		case RETURNVALUE_PUTTHISOBJECTINYOURHAND:
			return "Put this object in your hand.";

		case RETURNVALUE_PUTTHISOBJECTINBOTHHANDS:
			return "Put this object in both hands.";

		case RETURNVALUE_CANONLYUSEONEWEAPON:
			return "You may only use one weapon.";

		case RETURNVALUE_TOOFARAWAY:
			return "You are too far away.";

		case RETURNVALUE_FIRSTGODOWNSTAIRS:
			return "First go downstairs.";

		case RETURNVALUE_FIRSTGOUPSTAIRS:
			return "First go upstairs.";

		case RETURNVALUE_NOTENOUGHCAPACITY:
			return "This object is too heavy for you to carry.";

		case RETURNVALUE_CONTAINERNOTENOUGHROOM:
			return "You cannot put more objects in this container.";

		case RETURNVALUE_NEEDEXCHANGE:
		case RETURNVALUE_NOTENOUGHROOM:
			return "There is not enough room.";

		case RETURNVALUE_CANNOTPICKUP:
			return "You cannot take this object.";

		case RETURNVALUE_CANNOTTHROW:
			return "You cannot throw there.";

		case RETURNVALUE_THEREISNOWAY:
			return "There is no way.";

		case RETURNVALUE_THISISIMPOSSIBLE:
			return "This is impossible.";

		case RETURNVALUE_PLAYERISPZLOCKED:
			return "You can not enter a protection zone after attacking another player.";

		case RETURNVALUE_PLAYERISNOTINVITED:
			return "You are not invited.";

		case RETURNVALUE_CANNOTMOVEEXERCISEWEAPON:
			return "You cannot trade or drop this training weapon.";

		case RETURNVALUE_CANNOTMOVEGOLDPOUCH:
			return "You cannot trade or drop the Gold Pouch.";

		case RETURNVALUE_ONLYGUILDMEMBERSMAYENTER:
			return "Only members of this guild may enter.";

		case RETURNVALUE_CREATUREDOESNOTEXIST:
			return "Creature does not exist.";

		case RETURNVALUE_DEPOTISFULL:
			return "You cannot put more items in this depot.";

		case RETURNVALUE_CANNOTUSETHISOBJECT:
			return "You cannot use this object.";

		case RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE:
			return "A player with this name is not online.";

		case RETURNVALUE_NOTREQUIREDLEVELTOUSERUNE:
			return "You do not have the required magic level to use this rune.";

		case RETURNVALUE_YOUAREALREADYTRADING:
			return "You are already trading. Finish this trade first.";

		case RETURNVALUE_THISPLAYERISALREADYTRADING:
			return "This player is already trading.";

		case RETURNVALUE_YOUMAYNOTLOGOUTDURINGAFIGHT:
			return "You may not logout during or immediately after a fight!";

		case RETURNVALUE_DIRECTPLAYERSHOOT:
			return "You are not allowed to shoot directly on players.";

		case RETURNVALUE_NOTENOUGHLEVEL:
			return "Your level is too low.";

		case RETURNVALUE_NOTENOUGHMAGICLEVEL:
			return "You do not have enough magic level.";

		case RETURNVALUE_NOTENOUGHMANA:
			return "You do not have enough mana.";

		case RETURNVALUE_NOTENOUGHSOUL:
			return "You do not have enough soul.";

		case RETURNVALUE_YOUAREEXHAUSTED:
			return "You are exhausted.";

		case RETURNVALUE_YOUCANNOTUSEOBJECTSTHATFAST:
			return "You cannot use objects that fast.";

		case RETURNVALUE_CANONLYUSETHISRUNEONCREATURES:
			return "You can only use it on creatures.";

		case RETURNVALUE_PLAYERISNOTREACHABLE:
			return "Player is not reachable.";

		case RETURNVALUE_CREATUREISNOTREACHABLE:
			return "Creature is not reachable.";

		case RETURNVALUE_ACTIONNOTPERMITTEDINPROTECTIONZONE:
			return "This action is not permitted in a protection zone.";

		case RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER:
			return "You may not attack this person.";

		case RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE:
			return "You may not attack this creature.";

		case RETURNVALUE_YOUMAYNOTATTACKAPERSONINPROTECTIONZONE:
			return "You may not attack a person in a protection zone.";

		case RETURNVALUE_YOUMAYNOTATTACKAPERSONWHILEINPROTECTIONZONE:
			return "You may not attack a person while you are in a protection zone.";

		case RETURNVALUE_YOUCANONLYUSEITONCREATURES:
			return "You can only use it on creatures.";

		case RETURNVALUE_TURNSECUREMODETOATTACKUNMARKEDPLAYERS:
			return "Turn secure mode off if you really want to attack unmarked players.";

		case RETURNVALUE_YOUNEEDPREMIUMACCOUNT:
			return "You need a premium account.";

		case RETURNVALUE_YOUNEEDTOLEARNTHISSPELL:
			return "You must learn this spell first.";

		case RETURNVALUE_YOURVOCATIONCANNOTUSETHISSPELL:
			return "You have the wrong vocation to cast this spell.";

		case RETURNVALUE_YOUNEEDAWEAPONTOUSETHISSPELL:
			return "You need to equip a weapon to use this spell.";

		case RETURNVALUE_PLAYERISPZLOCKEDLEAVEPVPZONE:
			return "You can not leave a pvp zone after attacking another player.";

		case RETURNVALUE_PLAYERISPZLOCKEDENTERPVPZONE:
			return "You can not enter a pvp zone after attacking another player.";

		case RETURNVALUE_ACTIONNOTPERMITTEDINANOPVPZONE:
			return "This action is not permitted in a non pvp zone.";

		case RETURNVALUE_YOUCANNOTLOGOUTHERE:
			return "You can not logout here.";

		case RETURNVALUE_YOUNEEDAMAGICITEMTOCASTSPELL:
			return "You need a magic item to cast this spell.";

		case RETURNVALUE_CANNOTCONJUREITEMHERE:
			return "You cannot conjure items here.";

		case RETURNVALUE_YOUNEEDTOSPLITYOURSPEARS:
			return "You need to split your spears first.";

		case RETURNVALUE_NAMEISTOOAMBIGUOUS:
			return "Player name is ambiguous.";

		case RETURNVALUE_CANONLYUSEONESHIELD:
			return "You may use only one shield.";

		case RETURNVALUE_NOPARTYMEMBERSINRANGE:
			return "No party members in range.";

		case RETURNVALUE_YOUARENOTTHEOWNER:
			return "You are not the owner.";

		case RETURNVALUE_NOSUCHRAIDEXISTS:
			return "No such raid exists.";

		case RETURNVALUE_ANOTHERRAIDISALREADYEXECUTING:
			return "Another raid is already executing.";

		case RETURNVALUE_TRADEPLAYERFARAWAY:
			return "Trade player is too far away.";

		case RETURNVALUE_YOUDONTOWNTHISHOUSE:
			return "You don't own this house.";

		case RETURNVALUE_TRADEPLAYERALREADYOWNSAHOUSE:
			return "Trade player already owns a house.";

		case RETURNVALUE_TRADEPLAYERHIGHESTBIDDER:
			return "Trade player is currently the highest bidder of an auctioned house.";

		case RETURNVALUE_YOUCANNOTTRADETHISHOUSE:
			return "You can not trade this house.";

		case RETURNVALUE_YOUDONTHAVEREQUIREDPROFESSION:
			return "You don't have the required profession.";

		case RETURNVALUE_ITEMCANNOTBEMOVEDTHERE:
			return "This item cannot be moved there.";

		case RETURNVALUE_YOUCANNOTUSETHISBED:
			return "This bed can't be used, but Premium Account players can rent houses and sleep in beds there to regain health and mana.";

		case RETURNVALUE_TRADEPLAYERNOTINAGUILD:
			return "Trade player is not in a guild.";

		case RETURNVALUE_TRADEGUILDALREADYOWNSAHOUSE:
			return "Trade guild already owns a guildhall.";

		case RETURNVALUE_TRADEPLAYERNOTGUILDLEADER:
			return "Trade player is not a guild leader.";

		case RETURNVALUE_YOUARENOTGUILDLEADER:
			return "You are not a guild leader.";

		case RETURNVALUE_CANNOTTHROWONTELEPORT:
			return "You cannot throw items on teleports!";

		case RETURNVALUE_REWARDCHESTEMPTY:
			return "The chest is currently empty. You did not\ntake part in any battles in the last seven\ndays or already claimed your reward.";

		case RETURNVALUE_CANNOTMOVEITEMISPROTECTED:
			return "You cannot move this item. Only the house owner or authorized guests can move items in this protected house.";

		case RETURNVALUE_NOTENOUGHRESET:
			return "You do not have enough resets";

		case RETURNVALUE_CANNOTADDMOREITEMSONTILE:
			return "You cannot add more items on this tile.";

		case RETURNVALUE_ITEMSTOKENPROTECTED:
			return "Your items are token protected. Use !token off to disable protection.";

		case RETURNVALUE_QUIVERAMMOONLY:
			return "This quiver only holds arrows and bolts.";

		default: // RETURNVALUE_NOTPOSSIBLE, etc
			return "Sorry, not possible.";
	}
}

int64_t OTSYS_TIME()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
	    .count();
}

int64_t OTSYS_NANOTIME()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
	           std::chrono::high_resolution_clock::now().time_since_epoch())
	    .count();
}

SpellGroup_t stringToSpellGroup(std::string_view value)
{
	auto tmpStr = boost::algorithm::to_lower_copy<std::string>(std::string{value});
	if (tmpStr == "attack" || tmpStr == "1") {
		return SPELLGROUP_ATTACK;
	} else if (tmpStr == "healing" || tmpStr == "2") {
		return SPELLGROUP_HEALING;
	} else if (tmpStr == "support" || tmpStr == "3") {
		return SPELLGROUP_SUPPORT;
	} else if (tmpStr == "special" || tmpStr == "4") {
		return SPELLGROUP_SPECIAL;
	}

	return SPELLGROUP_NONE;
}

const std::vector<Direction>& getShuffleDirections()
{
	static std::vector<Direction> dirList{DIRECTION_NORTH, DIRECTION_WEST, DIRECTION_EAST, DIRECTION_SOUTH};
	std::shuffle(dirList.begin(), dirList.end(), getRandomGenerator());
	return dirList;
}

std::string getVocationShortName(uint8_t vocationId)
{
	std::stringstream ss;
	switch (vocationId) {
		case 1:
		case 5:
			ss << "MS";
			break;
		case 2:
		case 6:
			ss << "ED";
			break;
		case 3:
		case 7:
			ss << "RP";
			break;
		case 4:
		case 8:
			ss << "EK";
			break;
		case 9:
		case 10:
			ss << "MK";
			break;
		default:
			ss << "-";
			break;
	}
	return ss.str();
}