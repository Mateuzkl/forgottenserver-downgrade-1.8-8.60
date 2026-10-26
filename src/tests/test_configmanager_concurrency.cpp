#include "../otpch.h"
#include "../configmanager.h"
#include "test_support.h"

#include <atomic>
#include <barrier>
#include <thread>

TEST_CASE(config_scalar_settings_can_be_read_during_runtime_updates)
{
	constexpr auto integerKey = ConfigManager::STATS_SLOW_LOG_TIME;
	constexpr auto booleanKey = ConfigManager::LOG_TO_FILE;
	constexpr auto floatKey = ConfigManager::POWER_LAW_EXPONENT;
	const auto oldInteger = ConfigManager::getInteger(integerKey);
	const auto oldBoolean = ConfigManager::getBoolean(booleanKey);
	const auto oldFloat = ConfigManager::getFloat(floatKey);
	constexpr int64_t first = 0x1111222233334444;
	constexpr int64_t second = 0x5555666677778888;
	ConfigManager::setInteger(integerKey, first);
	ConfigManager::setBoolean(booleanKey, false);
	ConfigManager::setFloat(floatKey, 0.25f);
	std::barrier start{2};
	std::atomic_bool valid{true};
	std::thread writer([&] {
		start.arrive_and_wait();
		for (int i = 0; i < 20000; ++i) {
			ConfigManager::setInteger(integerKey, (i & 1) ? first : second);
			ConfigManager::setBoolean(booleanKey, (i & 1) != 0);
			ConfigManager::setFloat(floatKey, (i & 1) ? 0.25f : 0.5f);
		}
	});
	start.arrive_and_wait();
	for (int i = 0; i < 20000; ++i) {
		const auto integer = ConfigManager::getInteger(integerKey);
		const auto floating = ConfigManager::getFloat(floatKey);
		(void)ConfigManager::getBoolean(booleanKey);
		if ((integer != first && integer != second) || (floating != 0.25f && floating != 0.5f)) {
			valid.store(false, std::memory_order_relaxed);
		}
	}
	writer.join();
	ConfigManager::setInteger(integerKey, oldInteger);
	ConfigManager::setBoolean(booleanKey, oldBoolean);
	ConfigManager::setFloat(floatKey, oldFloat);
	CHECK(valid.load(std::memory_order_relaxed));
}

TFS_TEST_MAIN()
