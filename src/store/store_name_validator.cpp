// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "store/store_name_validator.h"

#include "database.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fmt/core.h>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace CharacterNameValidator {

namespace {

// Exact same forbidden words as the current Lua implementation.
const std::unordered_set<std::string>& forbiddenWords()
{
	static const std::unordered_set<std::string> words = {
	    "gm", "adm", "tutor", "god", "cm", "admin", "owner", "administrator", "senior", "xangel", "x-angel",
	};
	return words;
}

std::string toLowerStr(std::string_view sv)
{
	std::string result(sv);
	std::transform(result.begin(), result.end(), result.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

size_t countWords(std::string_view name)
{
	size_t count = 0;
	bool inWord = false;
	for (char c : name) {
		if (std::isalpha(static_cast<unsigned char>(c))) {
			if (!inWord) {
				++count;
				inWord = true;
			}
		} else {
			inWord = false;
		}
	}
	return count;
}

} // namespace

std::string formatName(std::string_view rawName)
{
	std::string result;
	bool capitalize = true;

	for (char c : rawName) {
		if (std::isspace(static_cast<unsigned char>(c))) {
			if (!result.empty() && result.back() != ' ') {
				result += ' ';
				capitalize = true;
			}
		} else {
			result += capitalize ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
			                     : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			capitalize = false;
		}
	}

	if (!result.empty() && result.back() == ' ') {
		result.pop_back();
	}

	return result;
}

std::string validate(std::string_view name)
{
	if (name.empty() || name.size() < 2 || name.size() > MaxNameLength) {
		return "You cannot use this character name.";
	}

	// Check for double spaces.
	if (name.find("  ") != std::string_view::npos) {
		return "You cannot use this character name.";
	}

	if (countWords(name) > MaxNameWords) {
		return "You cannot use this character name.";
	}

	// Check spaced-out forbidden names (exact match from Lua).
	const std::string lowered = toLowerStr(name);
	if (lowered == "g m" || lowered == "g o d" || lowered == "a d m" || lowered == "c m") {
		return "You cannot use this character name.";
	}

	// Only allow letters and spaces.
	for (char c : name) {
		if (!std::isalpha(static_cast<unsigned char>(c)) && c != ' ') {
			return "You cannot use this character name.";
		}
	}

	// Check forbidden words.
	const auto& forbidden = forbiddenWords();
	std::istringstream iss(lowered);
	std::string word;
	while (iss >> word) {
		if (forbidden.count(word)) {
			return "You cannot use this character name.";
		}
	}

	return ""; // Valid.
}

bool nameExistsInDB(std::string_view name)
{
	if (name.empty()) {
		return false;
	}
	Database& db = Database::getInstance();
	auto result = db.storeQuery(fmt::format(
	    "SELECT `id` FROM `players` WHERE LOWER(`name`) = LOWER({:s}) LIMIT 1",
	    db.escapeString(std::string(name))));
	return result != nullptr;
}

} // namespace CharacterNameValidator
