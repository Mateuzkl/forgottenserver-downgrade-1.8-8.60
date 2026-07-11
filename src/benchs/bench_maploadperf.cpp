#include "../otpch.h"

#include "../iomap.h"

#include "../item.h"
#include "../items.h"
#include "../logger.h"
#include "../map.h"
#include "../zones.h"

#include <benchmark/benchmark.h>

#include <cstdlib>
#include <filesystem>

namespace {

std::filesystem::path findDataDir()
{
	if (const char* env = std::getenv("TFS_DATA_DIR")) {
		return std::filesystem::path{env};
	}
	// Walk up from cwd: works from the tree root, build/, or build/src/benchs (ctest)
	auto dir = std::filesystem::current_path();
	for (int depth = 0; depth < 8; ++depth) {
		if (std::filesystem::exists(dir / "data" / "world" / "world.otbm")) {
			return dir / "data";
		}
		if (dir.parent_path() == dir) {
			break;
		}
		dir = dir.parent_path();
	}
	return {};
}

bool initFixture(std::filesystem::path& otbmPath, std::string& error)
{
	static bool initialized = false;
	static bool initOk = false;
	static std::filesystem::path cachedOtbm;

	if (!initialized) {
		initialized = true;
		const auto dataDir = findDataDir();
		if (dataDir.empty()) {
			error = "data/ not found (run from tree root or set TFS_DATA_DIR)";
			return false;
		}
		// REQUIRED: iomap.cpp calls g_logger() directly, which throws unless
		// initLogger() ran. ERRORR keeps the per-load info/warn lines silent.
		initLogger(LogLevel::ERRORR);

		// items.otb only. items.xml is deliberately NOT loaded: parseItemNode can
		// dereference g_moveEvents/g_weapons (null outside full server bootstrap).
		// The OTB alone provides group/stackable/flags used by the map-load path.
		if (!Item::items.loadFromOtb((dataDir / "items" / "items.otb").string())) {
			error = "failed to load items.otb";
			return false;
		}
		cachedOtbm = dataDir / "world" / "world.otbm";
		initOk = true;
	}

	otbmPath = cachedOtbm;
	if (!initOk) {
		error = "fixture initialization failed";
	}
	return initOk;
}

void bench_load_world_otbm(benchmark::State& state)
{
	std::filesystem::path otbmPath;
	std::string error;
	if (!initFixture(otbmPath, error)) {
		state.SkipWithError(error.c_str());
		return;
	}

	double towns = 0.0;
	double waypoints = 0.0;

	for ([[maybe_unused]] auto _ : state) {
		state.PauseTiming();
		Zones::clear(); // Map::loadMap does this before every real load
		auto map = std::make_unique<Map>();
		IOMap io;
		state.ResumeTiming();

		// Timed: mmap + parseTree, getProps, MapCache dedup, quadtree insertion,
		// house tiles, towns/waypoints, and MapCache::flush() (inside loadMap).
		if (!io.loadMap(map.get(), otbmPath)) {
			state.SkipWithError(std::string{io.getLastErrorString()}.c_str());
			break;
		}
		towns = static_cast<double>(map->towns.getTowns().size());
		waypoints = static_cast<double>(map->waypoints.size());

		state.PauseTiming();
		map.reset(); // exclude quadtree destruction from the timing
		state.ResumeTiming();
	}

	// Sanity counters so runs across revisions can be verified to load the same map
	state.counters["towns"] = towns;
	state.counters["waypoints"] = waypoints;
}
BENCHMARK(bench_load_world_otbm)->Unit(benchmark::kMillisecond);

} // namespace

BENCHMARK_MAIN();
