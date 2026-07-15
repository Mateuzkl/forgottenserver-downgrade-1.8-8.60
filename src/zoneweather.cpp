// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "zoneweather.h"

#include "game.h"
#include "logger.h"
#include "tile.h"
#include "tools.h"

#include <charconv>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>

extern Game g_game;

namespace {

using WeatherByZone = std::unordered_map<ZoneId, WeatherState>;

WeatherByZone weatherByZone;

template <typename T>
bool parseIntegerAttribute(const pugi::xml_node& node, const char* name, int64_t minimum, int64_t maximum, T& value)
{
	const pugi::xml_attribute attribute = node.attribute(name);
	if (!attribute) {
		LOG_ERROR("[ZoneWeather] Missing '{}' attribute on zone entry.", name);
		return false;
	}

	const std::string_view text = attribute.value();
	int64_t parsed = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
	if (error != std::errc{} || end != text.data() + text.size() || parsed < minimum || parsed > maximum) {
		LOG_ERROR("[ZoneWeather] Invalid '{}' value '{}' (expected {}..{}).", name, text, minimum, maximum);
		return false;
	}

	value = static_cast<T>(parsed);
	return true;
}

bool parseWeatherType(const pugi::xml_node& node, WeatherType& type)
{
	const pugi::xml_attribute attribute = node.attribute("type");
	if (!attribute) {
		LOG_ERROR("[ZoneWeather] Missing 'type' attribute on zone entry.");
		return false;
	}

	const std::string value = asLowerCaseString(attribute.as_string());
	if (value == "none") {
		type = WeatherType::None;
	} else if (value == "rain") {
		type = WeatherType::Rain;
	} else if (value == "snow") {
		type = WeatherType::Snow;
	} else if (value == "sand") {
		type = WeatherType::Sand;
	} else {
		LOG_ERROR("[ZoneWeather] Unknown weather type '{}'.", value);
		return false;
	}
	return true;
}

} // namespace

static_assert(std::is_trivially_copyable_v<WeatherState>);

bool ZoneWeather::load(std::string_view path)
{
	const std::string fileName(path);
	pugi::xml_document document;
	const pugi::xml_parse_result result = document.load_file(fileName.c_str());
	if (!result) {
		printXMLError("Error - ZoneWeather::load", fileName, result);
		return false;
	}

	const pugi::xml_node root = document.child("zoneWeather");
	if (!root) {
		LOG_ERROR("[ZoneWeather] Missing <zoneWeather> root in {}.", fileName);
		return false;
	}

	WeatherByZone parsedWeather;
	for (const pugi::xml_node zoneNode : root.children("zone")) {
		ZoneId zoneId = 0;
		WeatherState state;
		if (!parseIntegerAttribute(zoneNode, "id", 1, std::numeric_limits<ZoneId>::max(), zoneId) ||
		    !parseWeatherType(zoneNode, state.type) ||
		    !parseIntegerAttribute(zoneNode, "intensity", 0, 100, state.intensity) ||
		    !parseIntegerAttribute(zoneNode, "windX", std::numeric_limits<int8_t>::min(),
		                           std::numeric_limits<int8_t>::max(), state.windX) ||
		    !parseIntegerAttribute(zoneNode, "windY", std::numeric_limits<int8_t>::min(),
		                           std::numeric_limits<int8_t>::max(), state.windY) ||
		    !parseIntegerAttribute(zoneNode, "transitionMs", 0, std::numeric_limits<uint16_t>::max(),
		                           state.transitionMs) ||
		    !parseIntegerAttribute(zoneNode, "priority", 0, std::numeric_limits<uint8_t>::max(), state.priority)) {
			return false;
		}

		if (state.type == WeatherType::None) {
			state.intensity = 0;
			state.windX = 0;
			state.windY = 0;
		}

		if (!parsedWeather.emplace(zoneId, state).second) {
			LOG_ERROR("[ZoneWeather] Duplicate configuration for zone id {}.", zoneId);
			return false;
		}
	}

	weatherByZone.swap(parsedWeather);
	LOG_INFO(">> Loaded {} zone weather configuration{}", weatherByZone.size(), weatherByZone.size() == 1 ? "" : "s");
	return true;
}

bool ZoneWeather::reload()
{
	return load(DEFAULT_CONFIG_PATH);
}

void ZoneWeather::clear()
{
	weatherByZone.clear();
}

WeatherState ZoneWeather::getState(const Position& position)
{
	const Tile* tile = g_game.map.getTile(position);
	if (!tile) {
		return {};
	}
	return getState(std::span<const ZoneId>(tile->getZoneIds()));
}

WeatherState ZoneWeather::getState(std::span<const ZoneId> zoneIds)
{
	WeatherState selectedState;
	ZoneId selectedZoneId = std::numeric_limits<ZoneId>::max();
	bool found = false;

	for (const ZoneId zoneId : zoneIds) {
		const auto it = weatherByZone.find(zoneId);
		if (it == weatherByZone.end()) {
			continue;
		}

		const WeatherState& candidate = it->second;
		// Highest priority wins; the lower ZoneId is the deterministic tie-breaker.
		if (!found || candidate.priority > selectedState.priority ||
		    (candidate.priority == selectedState.priority && zoneId < selectedZoneId)) {
			selectedState = candidate;
			selectedZoneId = zoneId;
			found = true;
		}
	}

	return selectedState;
}

size_t ZoneWeather::count()
{
	return weatherByZone.size();
}
