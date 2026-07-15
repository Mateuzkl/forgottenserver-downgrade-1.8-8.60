#include "../otpch.h"

#include "../game.h"
#include "../tile.h"
#include "../zoneweather.h"

#include <benchmark/benchmark.h>

#include <array>
#include <filesystem>
#include <fstream>

namespace {

void loadWeatherFixture()
{
	static const bool loaded = [] {
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "tfs_bench_zone_weather.xml";
		{
			std::ofstream output(path, std::ios::trunc);
			output << R"xml(<zoneWeather>
	<zone id="100" type="rain" intensity="70" windX="-12" windY="90" transitionMs="400" priority="10"/>
	<zone id="200" type="snow" intensity="45" windX="4" windY="28" transitionMs="700" priority="20"/>
	<zone id="300" type="sand" intensity="65" windX="45" windY="8" transitionMs="500" priority="15"/>
</zoneWeather>)xml";
		}

		const bool result = ZoneWeather::load(path.string());
		std::error_code error;
		std::filesystem::remove(path, error);
		return result;
	}();

	if (!loaded) {
		throw std::runtime_error("failed to load zone weather benchmark fixture");
	}
}

void loadWeatherTile()
{
	static const bool loaded = [] {
		constexpr Position WEATHER_POSITION(32000, 32000, 7);
		auto tile = std::make_unique<DynamicTile>(WEATHER_POSITION.x, WEATHER_POSITION.y, WEATHER_POSITION.z);
		tile->setZoneIds({100, 300, 200});
		g_game.map.setTile(WEATHER_POSITION, std::move(tile));
		return g_game.map.getTile(WEATHER_POSITION) != nullptr;
	}();

	if (!loaded) {
		throw std::runtime_error("failed to create zone weather benchmark tile");
	}
}

void bench_zone_weather_lookup(benchmark::State& state)
{
	loadWeatherFixture();
	constexpr std::array<ZoneId, 8> ZONE_IDS = {999, 100, 998, 300, 997, 200, 996, 995};
	const size_t count = static_cast<size_t>(state.range(0));
	const std::span<const ZoneId> zoneIds(ZONE_IDS.data(), count);

	for ([[maybe_unused]] auto _ : state) {
		WeatherState weather = ZoneWeather::getState(zoneIds);
		benchmark::DoNotOptimize(weather);
	}
	state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(count));
}
BENCHMARK(bench_zone_weather_lookup)->Arg(1)->Arg(3)->Arg(8);

void bench_zone_weather_position_lookup(benchmark::State& state)
{
	loadWeatherFixture();
	loadWeatherTile();
	constexpr Position WEATHER_POSITION(32000, 32000, 7);

	for ([[maybe_unused]] auto _ : state) {
		WeatherState weather = ZoneWeather::getState(WEATHER_POSITION);
		benchmark::DoNotOptimize(weather);
	}
	state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(bench_zone_weather_position_lookup);

} // namespace

BENCHMARK_MAIN();
