#include "../otpch.h"

#include "../zoneweather.h"

#include "test_support.h"

#include <filesystem>
#include <fstream>

namespace {

class TemporaryWeatherXml final
{
public:
	explicit TemporaryWeatherXml(std::string_view contents)
	{
		path = std::filesystem::temp_directory_path() / "tfs_zone_weather_test.xml";
		std::ofstream output(path, std::ios::trunc);
		output << contents;
		if (!output) {
			throw std::runtime_error("failed to write temporary zone weather XML");
		}
	}

	~TemporaryWeatherXml()
	{
		std::error_code error;
		std::filesystem::remove(path, error);
	}

	std::string string() const { return path.string(); }

private:
	std::filesystem::path path;
};

TEST_CASE(test_zone_weather_priority_and_tie_break)
{
	TemporaryWeatherXml xml(R"xml(<?xml version="1.0"?>
<zoneWeather>
	<zone id="100" type="rain" intensity="70" windX="-12" windY="90" transitionMs="400" priority="10"/>
	<zone id="200" type="snow" intensity="45" windX="4" windY="28" transitionMs="700" priority="20"/>
	<zone id="50" type="sand" intensity="65" windX="45" windY="8" transitionMs="500" priority="20"/>
	<zone id="300" type="none" intensity="80" windX="-100" windY="100" transitionMs="300" priority="5"/>
</zoneWeather>)xml");

	CHECK(ZoneWeather::load(xml.string()));
	CHECK(ZoneWeather::count() == 4);

	const std::array<ZoneId, 2> priorityZones = {100, 200};
	const WeatherState priorityState = ZoneWeather::getState(priorityZones);
	CHECK(priorityState.type == WeatherType::Snow);
	CHECK(priorityState.intensity == 45);
	CHECK(priorityState.windX == 4);
	CHECK(priorityState.windY == 28);
	CHECK(priorityState.transitionMs == 700);
	CHECK(priorityState.priority == 20);

	// Equal priority is deterministic: the lower ZoneId wins regardless of input order.
	const std::array<ZoneId, 2> tiedZones = {200, 50};
	CHECK(ZoneWeather::getState(tiedZones).type == WeatherType::Sand);

	const std::array<ZoneId, 1> unknownZone = {999};
	CHECK(ZoneWeather::getState(unknownZone) == WeatherState{});

	const std::array<ZoneId, 1> clearZone = {300};
	const WeatherState clearState = ZoneWeather::getState(clearZone);
	CHECK(clearState.type == WeatherType::None);
	CHECK(clearState.intensity == 0);
	CHECK(clearState.windX == 0);
	CHECK(clearState.windY == 0);
	CHECK(clearState.transitionMs == 300);
	ZoneWeather::clear();
}

TEST_CASE(test_zone_weather_failed_reload_is_atomic)
{
	TemporaryWeatherXml validXml(R"xml(<zoneWeather>
	<zone id="10" type="rain" intensity="40" windX="0" windY="20" transitionMs="250" priority="1"/>
</zoneWeather>)xml");
	CHECK(ZoneWeather::load(validXml.string()));

	TemporaryWeatherXml invalidXml(R"xml(<zoneWeather>
	<zone id="10" type="rain" intensity="101" windX="0" windY="20" transitionMs="250" priority="1"/>
</zoneWeather>)xml");
	CHECK(!ZoneWeather::load(invalidXml.string()));

	const std::array<ZoneId, 1> zone = {10};
	const WeatherState state = ZoneWeather::getState(zone);
	CHECK(state.type == WeatherType::Rain);
	CHECK(state.intensity == 40);
	ZoneWeather::clear();
}

} // namespace

TFS_TEST_MAIN()
