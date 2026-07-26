#include "../otpch.h"

#include "../configmanager.h"
#include "../iomap.h"
#include "../item.h"
#include "../logger.h"
#include "../map.h"
#include "../scriptmanager.h"
#include "dat_test_fixture.h"
#include "test_support.h"

#include <chrono>
#include <filesystem>

namespace {

using tfs::tests::dat::makeDat;
using tfs::tests::dat::TempDatFile;

std::filesystem::path sourceRoot()
{
	return std::filesystem::weakly_canonical(std::filesystem::path{__FILE__}).parent_path().parent_path().parent_path();
}

class CurrentPathGuard
{
public:
	explicit CurrentPathGuard(const std::filesystem::path& path) : previous(std::filesystem::current_path())
	{
		std::filesystem::current_path(path);
	}

	~CurrentPathGuard() { std::filesystem::current_path(previous); }

private:
	std::filesystem::path previous;
};

class TempDirectory
{
public:
	TempDirectory() :
	    path(std::filesystem::temp_directory_path() /
	         ("tfs_dat_without_otb_" +
	          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
	{
		std::filesystem::create_directory(path);
	}

	~TempDirectory()
	{
		std::error_code error;
		std::filesystem::remove_all(path, error);
	}

	std::filesystem::path path;
};

void loadRealXml(Items& items)
{
	if (!items.loadFromXml(false, false)) {
		throw std::runtime_error(items.getLastError());
	}
	CHECK(items.isValidItemId(100));
	CHECK(items.isValidItemId(52947));
	CHECK(items.isValidItemId(65000));
	CHECK(items.isValidItemId(65001));
	CHECK(!items[65000].stackable);
	CHECK(items[65001].stackable);
}

void loadRealMap(const std::filesystem::path& root)
{
	Map map;
	IOMap loader;
	if (!loader.loadMap(&map, root / "data/world/world.otbm", false)) {
		throw std::runtime_error(std::string{loader.getLastErrorString()});
	}
	CHECK(map.getWidth() != 0);
	CHECK(map.getHeight() != 0);
}

} // namespace

TEST_CASE(real_otb_source_loads_xml_and_map)
{
	const auto root = sourceRoot();
	CurrentPathGuard currentPath(root);

	Item::items.clear();
	CHECK(Item::items.loadFromOtb((root / "data/items/items.otb").string()));
	loadRealXml(Item::items);
	loadRealMap(root);
	Item::items.clear();
}

TEST_CASE(generated_dat_source_loads_real_xml_without_otb)
{
	const auto root = sourceRoot();
	CurrentPathGuard currentPath(root);
	TempDatFile dat("tfs_item_source_real_xml", makeDat(52947));

	Item::items.clear();
	CHECK(Item::items.loadFromDat(dat.path.string()));
	loadRealXml(Item::items);
	Item::items.clear();
}

TEST_CASE(configured_otb_source_ignores_the_dat_path)
{
	const auto root = sourceRoot();
	CurrentPathGuard currentPath(root);

	ConfigManager::setBoolean(ConfigManager::USE_ASSETS_DAT, false);
	ConfigManager::setString(ConfigManager::ASSETS_DAT_PATH, "data/items/does-not-exist.dat");
	Items items;
	CHECK(items.loadFromConfiguredSource());
	CHECK(items.getLoadedSource() == Items::Source::OTB);
}

TEST_CASE(configured_dat_source_and_missing_file_failure_are_explicit)
{
	const auto root = sourceRoot();
	CurrentPathGuard currentPath(root);
	TempDatFile dat("tfs_item_source_configured", makeDat());

	ConfigManager::setBoolean(ConfigManager::USE_ASSETS_DAT, true);
	ConfigManager::setString(ConfigManager::ASSETS_DAT_PATH, dat.path.string());
	Items items;
	CHECK(items.loadFromConfiguredSource());
	CHECK(items.getLoadedSource() == Items::Source::DAT);

	ConfigManager::setString(ConfigManager::ASSETS_DAT_PATH, "data/items/does-not-exist.dat");
	Items missing;
	CHECK(!missing.loadFromConfiguredSource());
	CHECK(missing.getLastError().find("does-not-exist.dat") != std::string::npos);
	CHECK(missing.getLastError().find("Tibia.dat") != std::string::npos);
	CHECK(missing.getLastError().find("assets.dat") != std::string::npos);
}

TEST_CASE(configured_dat_source_does_not_require_otb)
{
	const auto root = sourceRoot();
	TempDirectory isolated;
	CurrentPathGuard currentPath(isolated.path);
	TempDatFile dat("tfs_item_source_without_otb", makeDat());

	ConfigManager::setBoolean(ConfigManager::USE_ASSETS_DAT, true);
	ConfigManager::setString(ConfigManager::ASSETS_DAT_PATH, dat.path.string());
	Items items;
	CHECK(items.loadFromConfiguredSource());
	CHECK(items.getLoadedSource() == Items::Source::DAT);
	CHECK(!std::filesystem::exists(isolated.path / "data/items/items.otb"));
}

TEST_CASE(truncated_dat_reload_preserves_the_previous_item_state)
{
	const auto root = sourceRoot();
	CurrentPathGuard currentPath(root);
	TempDatFile dat("tfs_item_source_reload", makeDat(52947));

	ConfigManager::setBoolean(ConfigManager::USE_ASSETS_DAT, true);
	ConfigManager::setString(ConfigManager::ASSETS_DAT_PATH, dat.path.string());
	Items items;
	CHECK(items.loadFromConfiguredSource());
	loadRealXml(items);
	const std::string originalName = items[100].name;
	const std::string originalPath{items.getLoadedSourcePath()};

	auto truncatedBytes = makeDat(52947);
	truncatedBytes.resize(64);
	TempDatFile truncated("tfs_item_source_reload_truncated", truncatedBytes);
	ConfigManager::setString(ConfigManager::ASSETS_DAT_PATH, truncated.path.string());
	CHECK(!items.reload());
	CHECK(items.getLoadedSource() == Items::Source::DAT);
	CHECK(items.getLoadedSourcePath() == originalPath);
	CHECK(items[100].name == originalName);
	CHECK(items.isValidItemId(52947));
}

TEST_CASE(successful_reload_switches_between_otb_and_dat)
{
	const auto root = sourceRoot();
	CurrentPathGuard currentPath(root);
	TempDatFile dat("tfs_item_source_switch", makeDat(52947));

	CHECK(ConfigManager::load());
	ConfigManager::setBoolean(ConfigManager::USE_ASSETS_DAT, false);
	CHECK(ScriptingManager::getInstance().loadPreItems());
	Item::items.clear();
	CHECK(Item::items.loadFromConfiguredSource());
	CHECK(Item::items.loadFromXml());
	CHECK(ScriptingManager::getInstance().loadScriptSystems());

	ConfigManager::setBoolean(ConfigManager::USE_ASSETS_DAT, true);
	ConfigManager::setString(ConfigManager::ASSETS_DAT_PATH, dat.path.string());
	CHECK(Item::items.reload());
	CHECK(Item::items.getLoadedSource() == Items::Source::DAT);

	ConfigManager::setBoolean(ConfigManager::USE_ASSETS_DAT, false);
	CHECK(Item::items.reload());
	CHECK(Item::items.getLoadedSource() == Items::Source::OTB);
	Item::items.clear();
}

int main()
{
	if (!initLogger(LogLevel::ERRORR)) {
		return EXIT_FAILURE;
	}
	const int result = ::tfs::tests::run();
	shutdownLogger();
	return result;
}
