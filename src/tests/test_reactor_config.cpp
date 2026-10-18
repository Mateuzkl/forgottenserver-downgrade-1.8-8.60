#include "../otpch.h"

#include "../configmanager.h"
#include "../reactor.h"
#include "test_support.h"

#include <fstream>

TEST_CASE(reactor_startup_fallback_matches_admission_default_and_honors_override)
{
	const auto previousDirectory = std::filesystem::current_path();
	const auto directory =
	    std::filesystem::temp_directory_path() /
	    ("tfs-reactor-config-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	CHECK(std::filesystem::create_directory(directory));
	struct Cleanup
	{
		std::filesystem::path previous, temporary;
		~Cleanup()
		{
			std::filesystem::current_path(previous);
			std::error_code ignored;
			std::filesystem::remove_all(temporary, ignored);
		}
	} cleanup{previousDirectory, directory};
	std::filesystem::current_path(directory);
	ConfigManager::setString(ConfigManager::CONFIG_FILE, "config.lua");
	std::ofstream("config.lua") << "-- Deliberately omit reactorMaxInboxSize\n";
	CHECK(ConfigManager::load());
	CHECK(ConfigManager::getInteger(ConfigManager::REACTOR_MAX_INBOX_SIZE) == REACTOR_MAX_INBOX_SIZE);
	std::ofstream("config.lua") << "reactorMaxInboxSize = 1234\n";
	CHECK(ConfigManager::load());
	CHECK(ConfigManager::getInteger(ConfigManager::REACTOR_MAX_INBOX_SIZE) == 1234);
}

TFS_TEST_MAIN()
