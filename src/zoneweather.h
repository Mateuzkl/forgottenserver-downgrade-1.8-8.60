// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_ZONEWEATHER_H
#define FS_ZONEWEATHER_H

#include "position.h"
#include "zones.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

enum class WeatherType : uint8_t {
	None = 0,
	Rain = 1,
	Snow = 2,
	Sand = 3,
};

struct WeatherState {
	WeatherType type = WeatherType::None;
	uint8_t intensity = 0;
	int8_t windX = 0;
	int8_t windY = 0;
	uint16_t transitionMs = 0;
	uint8_t priority = 0;

	bool operator==(const WeatherState& other) const
	{
		return type == other.type && intensity == other.intensity && windX == other.windX && windY == other.windY &&
		       transitionMs == other.transitionMs && priority == other.priority;
	}
};

class ZoneWeather final
{
public:
	static constexpr std::string_view DEFAULT_CONFIG_PATH = "data/XML/zone_weather.xml";

	static bool load(std::string_view path = DEFAULT_CONFIG_PATH);
	static bool reload();
	static void clear();

	static WeatherState getState(const Position& position);
	static WeatherState getState(std::span<const ZoneId> zoneIds);
	static size_t count();
};

#endif // FS_ZONEWEATHER_H
