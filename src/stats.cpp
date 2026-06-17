#include <fmt/color.h>

#include "otpch.h"

#include "configmanager.h"
#include "logger.h"
#include "stats.h"
#include "tasks.h"
#include "tools.h"

// extern ConfigManager g_config;

int64_t Stats::DUMP_INTERVAL = 30000; // 30 sec
uint32_t Stats::SLOW_EXECUTION_TIME = 10000000; // 10 ms
uint32_t Stats::VERY_SLOW_EXECUTION_TIME = 50000000; // 50 ms

namespace {
constexpr uint64_t MAX_REASONABLE_STAT_NS = 5ULL * 60ULL * 1000ULL * 1000ULL * 1000ULL;

double clampPercent(double value) noexcept
{
	if (value <= 0.) {
		return 0.;
	}
	return std::min(value, 100.);
}

int64_t elapsedDumpMilliseconds(int64_t lastDump, int64_t now) noexcept
{
	return std::max<int64_t>(1, now - lastDump);
}

bool isValidStatSample(const Stat& stat, const char* queueName)
{
	if (stat.executionTime <= MAX_REASONABLE_STAT_NS) {
		return true;
	}

	LOG_STATS_WARNING("Ignoring invalid {} stats sample: {} ms - {} - {}", queueName, stat.executionTime / 1000000,
	                  stat.description, stat.extraDescription);
	return false;
}

void addStatSample(statsMap& stats, const Stat& stat)
{
	auto it = stats.emplace(stat.description, statsData(0, 0, stat.extraDescription)).first;
	it->second.calls += 1;
	it->second.executionTime += stat.executionTime;
}
}

Stats::Stats()
{
	configureDispatchers(1);
}

Stats::~Stats() = default;

thread_local AutoStatRecursive* AutoStatRecursive::activeStat = nullptr;

AutoStat::AutoStat(const std::string& description, const std::string& extraDescription)
{
	time_point = std::chrono::steady_clock::now();
	if (g_stats.isEnabled()) {
		stat = std::make_unique<Stat>(0, description, extraDescription);
	}
}

AutoStat::~AutoStat() noexcept
{
	try {
		if (!stat) {
			return;
		}

		stat->executionTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - time_point).count();
		if (stat->executionTime > minusTime) {
			stat->executionTime -= minusTime;
		} else {
			stat->executionTime = 0;
		}

		if (g_stats.isEnabled() && g_stats.isRunning()) {
			g_stats.addSpecialStats(std::move(stat));
		}
	} catch (...) {
	}
}

AutoStatRecursive::AutoStatRecursive(const std::string& description, const std::string& extraDescription) :
	AutoStat(description, extraDescription)
{
	parent = activeStat;
	activeStat = this;
}

AutoStatRecursive::~AutoStatRecursive() noexcept
{
	activeStat = parent;
	if (parent) {
		parent->minusTime += std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - time_point).count();
	}
}

void Stats::threadMain() {
	std::unique_lock<std::mutex> taskLockUnique(statsLock, std::defer_lock);
	bool last_iteration = false;
	lua.lastDump = sql.lastDump = special.lastDump = OTSYS_TIME();
	playersOnline = 0;
	for(auto& dispatcher : dispatchers) {
		dispatcher.waitTime = 0;
		dispatcher.lastDump = OTSYS_TIME();
	}
	while(true) {
		taskLockUnique.lock();

		DUMP_INTERVAL = ConfigManager::getInteger(ConfigManager::STATS_DUMP_INTERVAL) * 1000;
		SLOW_EXECUTION_TIME = ConfigManager::getInteger(ConfigManager::STATS_SLOW_LOG_TIME) * 1000000;
		VERY_SLOW_EXECUTION_TIME = ConfigManager::getInteger(ConfigManager::STATS_VERY_SLOW_LOG_TIME) * 1000000;

		std::vector<std::forward_list<std::unique_ptr<Stat>>> dispatcherStats;
		dispatcherStats.reserve(dispatchers.size());
		for (auto &dispatcher : dispatchers) {
			dispatcherStats.push_back(std::move(dispatcher.queue));
			dispatcher.queue.clear();
		}
		std::forward_list<std::unique_ptr<Stat>> lua_stats(std::move(lua.queue));
		lua.queue.clear();
		std::forward_list<std::unique_ptr<Stat>> sql_stats(std::move(sql.queue));
		sql.queue.clear();
		std::forward_list<std::unique_ptr<Stat>> special_stats(std::move(special.queue));
		special.queue.clear();
		taskLockUnique.unlock();

		parseDispatchersQueue(std::move(dispatcherStats));
		parseLuaQueue(lua_stats);
		parseSqlQueue(sql_stats);
		parseSpecialQueue(special_stats);

		int threadId = 0;
		for (auto &dispatcher : dispatchers) {
			if (DUMP_INTERVAL <= 0) {
				dispatcher.stats.clear();
				dispatcher.waitTime.store(0, std::memory_order_relaxed);
				dispatcher.lastDump = OTSYS_TIME();
				continue;
			}
			const int64_t now = OTSYS_TIME();
			if (dispatcher.lastDump + DUMP_INTERVAL < now || last_iteration) {
				uint64_t executionTime = 0;
				for (auto &it : dispatcher.stats) {
					executionTime += it.second.executionTime;
				}

				const uint64_t waitTime = dispatcher.waitTime.exchange(0, std::memory_order_relaxed);
				const int64_t elapsedMs = elapsedDumpMilliseconds(dispatcher.lastDump, now);
				const double cpu = clampPercent(static_cast<double>(executionTime) / (static_cast<double>(elapsedMs) * 10000.));
				const double idle = clampPercent(static_cast<double>(waitTime) / (static_cast<double>(elapsedMs) * 10000.));
				const double other = std::max(0., 100. - cpu - idle);
				auto msg = fmt::format("Thread: {} Cpu usage: {:.5f}% Idle: {:.5f}% Other: {:.5f}% Players online: {}\n",
					++threadId,
					cpu,
					idle,
					other,
					playersOnline.load());
				if(waitTime > 0 || !dispatcher.stats.empty())
					writeStats("dispatcher.log", dispatcher.stats, msg, elapsedMs);
				dispatcher.stats.clear();
				dispatcher.lastDump = now;
			}
		}
		const int64_t now = OTSYS_TIME();
		if(DUMP_INTERVAL <= 0) {
			lua.stats.clear();
			sql.stats.clear();
			special.stats.clear();
			lua.lastDump = sql.lastDump = special.lastDump = now;
		} else if(lua.lastDump + DUMP_INTERVAL < now || last_iteration) {
			const int64_t elapsedMs = elapsedDumpMilliseconds(lua.lastDump, now);
			writeStats("lua.log", lua.stats, "", elapsedMs);
			lua.stats.clear();
			lua.lastDump = now;
		}
		if(DUMP_INTERVAL > 0 && (sql.lastDump + DUMP_INTERVAL < now || last_iteration)) {
			const int64_t elapsedMs = elapsedDumpMilliseconds(sql.lastDump, now);
			writeStats("sql.log", sql.stats, "", elapsedMs);
			sql.stats.clear();
			sql.lastDump = now;
		}
		if(DUMP_INTERVAL > 0 && (special.lastDump + DUMP_INTERVAL < now || last_iteration)) {
			const int64_t elapsedMs = elapsedDumpMilliseconds(special.lastDump, now);
			writeStats("special.log", special.stats, "", elapsedMs);
			special.stats.clear();
			special.lastDump = now;
		}

		if(last_iteration)
			break;
		if(getState() == THREAD_STATE_TERMINATED) {
			last_iteration = true;
			continue;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

void Stats::configureDispatchers(std::size_t count) {
	if (isRunning()) {
		LOG_STATS_WARNING("Stats::configureDispatchers must be called before Stats::start()");
		return;
	}

	std::scoped_lock lockClass(statsLock);
	dispatchers.clear();
	dispatchers.resize(std::max<std::size_t>(1, count));
}

void Stats::addDispatcherStat(std::size_t index, std::unique_ptr<Stat> stat) {
	if (!stat) {
		return;
	}

	std::scoped_lock lockClass(statsLock);
	if (index >= dispatchers.size()) {
		LOG_STATS_WARNING("Stats dispatcher index {} is out of range (size: {})", index, dispatchers.size());
		return;
	}
	dispatchers[index].queue.push_front(std::move(stat));
}

void Stats::addDispatcherWaitTime(std::size_t index, uint64_t waitTime) noexcept {
	if (index >= dispatchers.size()) {
		return;
	}
	dispatchers[index].waitTime.fetch_add(waitTime, std::memory_order_relaxed);
}

void Stats::addLuaStats(std::unique_ptr<Stat> stats) {
	if (!stats) {
		return;
	}

	std::scoped_lock lockClass(statsLock);
	lua.queue.push_front(std::move(stats));
}

void Stats::addSqlStats(std::unique_ptr<Stat> stats) {
	if (!stats) {
		return;
	}

	std::scoped_lock lockClass(statsLock);
	sql.queue.push_front(std::move(stats));
}

void Stats::addSpecialStats(std::unique_ptr<Stat> stats) {
	if (!stats) {
		return;
	}

	std::scoped_lock lockClass(statsLock);
	special.queue.push_front(std::move(stats));
}

void Stats::parseDispatchersQueue(std::vector<std::forward_list<std::unique_ptr<Stat>>>&& queues) {
	for(std::size_t i = 0; i < dispatchers.size() && i < queues.size(); ++i) {
		auto& dispatcher = dispatchers[i];
		for(const auto& stat : queues[i]) {
			if (!stat || !isValidStatSample(*stat, "dispatcher")) {
				continue;
			}

			addStatSample(dispatcher.stats, *stat);
			if(VERY_SLOW_EXECUTION_TIME > 0 && stat->executionTime > VERY_SLOW_EXECUTION_TIME) {
				writeSlowInfo("dispatcher_very_slow.log", stat->executionTime, stat->description, stat->extraDescription);
			} else if(SLOW_EXECUTION_TIME > 0 && stat->executionTime > SLOW_EXECUTION_TIME) {
				writeSlowInfo("dispatcher_slow.log", stat->executionTime, stat->description, stat->extraDescription);
			}
		}
	}
}

void Stats::parseLuaQueue(std::forward_list<std::unique_ptr<Stat>>& queue) {
	for(const auto& stats : queue) {
		if (!stats || !isValidStatSample(*stats, "lua")) {
			continue;
		}

		addStatSample(lua.stats, *stats);

		if(VERY_SLOW_EXECUTION_TIME > 0 && stats->executionTime > VERY_SLOW_EXECUTION_TIME) {
			writeSlowInfo("lua_very_slow.log", stats->executionTime, stats->description, stats->extraDescription);
		} else if(SLOW_EXECUTION_TIME > 0 && stats->executionTime > SLOW_EXECUTION_TIME) {
			writeSlowInfo("lua_slow.log", stats->executionTime, stats->description, stats->extraDescription);
		}
	}
}

void Stats::parseSqlQueue(std::forward_list<std::unique_ptr<Stat>>& queue) {
	for(const auto& stats : queue) {
		if (!stats || !isValidStatSample(*stats, "sql")) {
			continue;
		}

		addStatSample(sql.stats, *stats);

		if(VERY_SLOW_EXECUTION_TIME > 0 && stats->executionTime > VERY_SLOW_EXECUTION_TIME) {
			writeSlowInfo("sql_very_slow.log", stats->executionTime, stats->description, stats->extraDescription);
		} else if(SLOW_EXECUTION_TIME > 0 && stats->executionTime > SLOW_EXECUTION_TIME) {
			writeSlowInfo("sql_slow.log", stats->executionTime, stats->description, stats->extraDescription);
		}
	}
}

void Stats::parseSpecialQueue(std::forward_list<std::unique_ptr<Stat>>& queue) {
	for(const auto& stats : queue) {
		if (!stats || !isValidStatSample(*stats, "special")) {
			continue;
		}

		addStatSample(special.stats, *stats);

		if(VERY_SLOW_EXECUTION_TIME > 0 && stats->executionTime > VERY_SLOW_EXECUTION_TIME) {
			writeSlowInfo("special_very_slow.log", stats->executionTime, stats->description, stats->extraDescription);
		} else if(SLOW_EXECUTION_TIME > 0 && stats->executionTime > SLOW_EXECUTION_TIME) {
			writeSlowInfo("special_slow.log", stats->executionTime, stats->description, stats->extraDescription);
		}
	}
}

void Stats::writeSlowInfo(const std::string& file, uint64_t executionTime, const std::string& description, const std::string& extraDescription) {
	if (!ConfigManager::getBoolean(ConfigManager::LOG_TO_FILE)) {
		LOG_STATS("Execution time: {} ms - {} - {}", (executionTime / 1000000), description, extraDescription);
		return;
	}

	std::ofstream out(std::string("data/logs/stats/") + file, std::ofstream::out | std::ofstream::app);
	if (!out.is_open()) {
		LOG_STATS_WARNING("Can't open {} (check if directory exists)", (std::string("data/logs/stats/") + file));
		return;
	}
	
	// File log includes manual timestamp
	std::string fileMessage = fmt::format("[{}] Execution time: {} ms - {} - {}\n", formatDateShort(time(nullptr)), (executionTime / 1000000), description, extraDescription);
	out << fileMessage;
	out.flush();
	out.close();

	// Console log uses system Logger (timestamp handled by logger)
	LOG_STATS("Execution time: {} ms - {} - {}", (executionTime / 1000000), description, extraDescription);
}

void Stats::writeStats(const std::string& file, const statsMap& stats, const std::string& extraInfo, int64_t intervalMs) {
	if (DUMP_INTERVAL <= 0) {
		return;
	}

	if (!extraInfo.empty()) {
		// Use LOG_INFO for console summary, removing the trailing newline if present as logger adds it
		std::string infoCopy = extraInfo;
		if (!infoCopy.empty() && infoCopy.back() == '\n') {
			infoCopy.pop_back();
		}
		LOG_STATS("{}", infoCopy);
	}

	if (!ConfigManager::getBoolean(ConfigManager::LOG_TO_FILE)) {
		return;
	}

	const int64_t effectiveIntervalMs = std::max<int64_t>(1, intervalMs > 0 ? intervalMs : DUMP_INTERVAL);

	std::ofstream out(std::string("data/logs/stats/") + file, std::ofstream::out | std::ofstream::app);
	if (!out.is_open()) {
		LOG_STATS_WARNING("Can't open {} (check if directory exists)", (std::string("data/logs/stats/") + file));
		return;
	}
	if(stats.empty()) {
		out.close();
		return;
	}
	out << "[" << formatDateShort(time(nullptr)) << "]\n";

	std::vector<std::pair<std::string, statsData>> pairs;
	for (auto& it : stats)
		pairs.emplace_back(it);

	std::ranges::sort(pairs, [](const auto& a, const auto& b) {
		return a.second.executionTime > b.second.executionTime;
	});

	out << extraInfo;
	uint64_t total_time = 0;
	out << std::setw(10) << "Time (ms)" << std::setw(10) << "Calls"
		<< std::setw(15) << "Rel usage " << "%" << std::setw(15) << "Real usage " << "%" << " " << "Description" << "\n";
	for(auto& it : pairs) {
		total_time += it.second.executionTime;
	}
	for(auto& it : pairs) {
		double percent = total_time > 0 ? 100. * static_cast<double>(it.second.executionTime) / static_cast<double>(total_time) : 0.;
		double realPercent = static_cast<double>(it.second.executionTime) / (static_cast<double>(effectiveIntervalMs) * 10000.);
		if(percent > 0.1)
			out << std::setw(10) << it.second.executionTime / 1000000 << std::setw(10) << it.second.calls
				<< std::setw(15) << std::setprecision(5) << std::fixed << percent << "%" << std::setw(15) << std::setprecision(5) << std::fixed << realPercent << "%" << " " << it.first << "\n";
	}
	out << "\n";
	out.flush();
	out.close();
}
