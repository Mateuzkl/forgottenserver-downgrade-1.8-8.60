#include <fmt/color.h>

#include "otpch.h"

#include "configmanager.h"
#include "logger.h"
#include "stats.h"
#include "tasks.h"
#include "tools.h"

#include <cerrno>
#include <system_error>

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

std::error_code currentStreamError()
{
	return errno != 0 ? std::error_code(errno, std::generic_category()) :
	                    std::make_error_code(std::io_errc::stream);
}
}

Stats::Stats()
{
	configureDispatchers(1);
}

Stats::~Stats() = default;

void Stats::prepareFileLogging()
{
	fileLoggingRequested.store(ConfigManager::getBoolean(ConfigManager::LOG_TO_FILE), std::memory_order_release);
	fileLoggingAvailable.store(false, std::memory_order_release);

	std::error_code error;
	auto absoluteDirectory = std::filesystem::absolute(statsDirectory, error);
	if (error) {
		absoluteDirectory = statsDirectory;
		error.clear();
	}

	{
		std::scoped_lock lock(fileLoggingMutex);
		absoluteStatsDirectory = std::move(absoluteDirectory);
		fileLoggingFailureReason.clear();
	}

	std::filesystem::create_directories(statsDirectory, error);
	if (!error && !std::filesystem::is_directory(statsDirectory, error)) {
		error = std::make_error_code(std::errc::not_a_directory);
	}
	if (error) {
		std::scoped_lock lock(fileLoggingMutex);
		fileLoggingFailureReason = error.message();
		return;
	}

	if (!fileLoggingRequested.load(std::memory_order_acquire)) {
		return;
	}

	for (std::string_view fileName : {"dispatcher.log", "lua.log", "sql.log", "special.log"}) {
		errno = 0;
		std::ofstream output(statsDirectory / fileName, std::ofstream::out | std::ofstream::app);
		if (!output.is_open()) {
			const std::error_code openError = errno != 0 ? std::error_code(errno, std::generic_category()) :
			                                             std::make_error_code(std::io_errc::stream);
			std::scoped_lock lock(fileLoggingMutex);
			fileLoggingFailureReason = openError.message();
			return;
		}
	}

	fileLoggingAvailable.store(true, std::memory_order_release);
}

StatsFileLoggingStatus Stats::getFileLoggingStatus() const
{
	std::scoped_lock lock(fileLoggingMutex);
	return StatsFileLoggingStatus{
	    fileLoggingRequested.load(std::memory_order_acquire),
	    fileLoggingAvailable.load(std::memory_order_acquire),
	    statsDirectory,
	    absoluteStatsDirectory,
	    fileLoggingFailureReason,
	};
}

void Stats::disableFileLogging(std::string reason)
{
	bool expected = true;
	if (!fileLoggingAvailable.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
		return;
	}

	std::filesystem::path absoluteDirectory;
	{
		std::scoped_lock lock(fileLoggingMutex);
		fileLoggingFailureReason = std::move(reason);
		absoluteDirectory = absoluteStatsDirectory;
	}

	LOG_STATS_WARNING(
	    "OTS STATISTICS\n"
	    "    Log directory       unavailable\n"
	    "    Path                {}\n"
	    "    Reason              {}\n"
	    "    File logging        disabled for this session",
	    absoluteDirectory.string(), getFileLoggingStatus().reason);
}

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
	if (ConfigManager::getBoolean(ConfigManager::LOG_TO_FILE)) {
		std::error_code error;
		std::filesystem::create_directories("data/logs/stats", error);
		if (error) {
			LOG_STATS_WARNING("Can't create data/logs/stats: {}", error.message());
		}
	}
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
				const int currentThreadId = ++threadId;
				const auto fileSummary = fmt::format(
				    "Thread: {} Cpu usage: {:.5f}% Idle: {:.5f}% Other: {:.5f}% Players online: {}\n",
				    currentThreadId, cpu, idle, other, playersOnline.load());
				const auto consoleSummary = fmt::format(
				    "Thread {} | CPU {:.2f}% | Idle {:.2f}% | Other {:.2f}% | Players {}",
				    currentThreadId, cpu, idle, other, playersOnline.load());
				if(waitTime > 0 || !dispatcher.stats.empty())
					writeStats("dispatcher.log", dispatcher.stats, fileSummary, elapsedMs, consoleSummary);
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
	if (!stat || !isEnabled() || !isRunning()) {
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
	if (!isEnabled() || index >= dispatchers.size()) {
		return;
	}
	dispatchers[index].waitTime.fetch_add(waitTime, std::memory_order_relaxed);
}

void Stats::addLuaStats(std::unique_ptr<Stat> stats) {
	if (!stats || !isEnabled() || !isRunning()) {
		return;
	}

	std::scoped_lock lockClass(statsLock);
	lua.queue.push_front(std::move(stats));
}

void Stats::addSqlStats(std::unique_ptr<Stat> stats) {
	if (!stats || !isEnabled() || !isRunning()) {
		return;
	}

	std::scoped_lock lockClass(statsLock);
	sql.queue.push_front(std::move(stats));
}

void Stats::addSpecialStats(std::unique_ptr<Stat> stats) {
	if (!stats || !isEnabled() || !isRunning()) {
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
	if (!fileLoggingRequested.load(std::memory_order_acquire) ||
	    !fileLoggingAvailable.load(std::memory_order_acquire)) {
		LOG_STATS("Execution time: {} ms - {} - {}", (executionTime / 1000000), description, extraDescription);
		return;
	}

	errno = 0;
	std::ofstream out(statsDirectory / file, std::ofstream::out | std::ofstream::app);
	if (!out.is_open()) {
		disableFileLogging(currentStreamError().message());
		LOG_STATS("Execution time: {} ms - {} - {}", (executionTime / 1000000), description, extraDescription);
		return;
	}
	
	// File log includes manual timestamp
	std::string fileMessage = fmt::format("[{}] Execution time: {} ms - {} - {}\n", formatDateShort(time(nullptr)), (executionTime / 1000000), description, extraDescription);
	errno = 0;
	out << fileMessage;
	if (!out) {
		disableFileLogging(currentStreamError().message());
	} else {
		errno = 0;
		out.flush();
		if (!out) {
			disableFileLogging(currentStreamError().message());
		}
	}

	// Console log uses system Logger (timestamp handled by logger)
	LOG_STATS("Execution time: {} ms - {} - {}", (executionTime / 1000000), description, extraDescription);
}

void Stats::writeStats(const std::string& file, const statsMap& stats, const std::string& extraInfo,
                       int64_t intervalMs, std::string_view consoleSummary) {
	if (DUMP_INTERVAL <= 0) {
		return;
	}

	if (!consoleSummary.empty()) {
		LOG_STATS("{}", consoleSummary);
	}

	if (!fileLoggingRequested.load(std::memory_order_acquire) ||
	    !fileLoggingAvailable.load(std::memory_order_acquire)) {
		return;
	}

	const int64_t effectiveIntervalMs = std::max<int64_t>(1, intervalMs > 0 ? intervalMs : DUMP_INTERVAL);

	errno = 0;
	std::ofstream out(statsDirectory / file, std::ofstream::out | std::ofstream::app);
	if (!out.is_open()) {
		disableFileLogging(currentStreamError().message());
		return;
	}
	if(stats.empty()) {
		out.close();
		return;
	}
	errno = 0;
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
	if (!out) {
		disableFileLogging(currentStreamError().message());
		return;
	}

	errno = 0;
	out.flush();
	if (!out) {
		disableFileLogging(currentStreamError().message());
	}
}
