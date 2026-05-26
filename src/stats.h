#ifndef TFS_STATS_H
#define TFS_STATS_H

#include "observer_ptr.h"
#include "thread_holder_base.h"

#ifdef STATS_ENABLED
#define createTask(function) createTaskWithStats(function, #function, __FUNCTION__)
#define createTimedTask(delay, function) createTaskWithStats(delay, function, #function, __FUNCTION__)
#define createSchedulerTask(delay, function) createSchedulerTaskWithStats(delay, function, #function, __FUNCTION__)
#define addGameTask(function, ...) addGameTaskWithStats(function, #function, __FUNCTION__, __VA_ARGS__)
#define addGameTaskTimed(delay, function, ...) addGameTaskTimedWithStats(delay, function, #function, __FUNCTION__, __VA_ARGS__)
#else
#define createTask(function) createTaskWithStats(function, "", "")
#define createTimedTask(delay, function) createTaskWithStats(delay, function, "", "")
#define createSchedulerTask(delay, function) createSchedulerTaskWithStats(delay, function, "", "")
#define addGameTask(function, ...) addGameTaskWithStats(function, "", "", __VA_ARGS__)
#define addGameTaskTimed(delay, function, ...) addGameTaskTimedWithStats(delay, function, "", "", __VA_ARGS__)
#endif

struct Stat {
	Stat(uint64_t _executionTime, std::string _description, std::string _extraDescription) :
			executionTime(_executionTime), description(std::move(_description)), extraDescription(std::move(_extraDescription)) {}
	uint64_t executionTime = 0;
	std::string description;
	std::string extraDescription;
};

struct statsData {
	statsData(uint32_t _calls, uint64_t _executionTime, std::string _extraInfo) :
			calls(_calls), executionTime(_executionTime), extraInfo(std::move(_extraInfo)) {}
	uint32_t calls = 0;
	uint64_t executionTime = 0;
	std::string extraInfo;
};

using statsMap = std::unordered_map<std::string, statsData>;

class Stats : public ThreadHolder<Stats> {
public:
	Stats();
	~Stats();

	void threadMain();
	void shutdown() {
		setState(THREAD_STATE_TERMINATED);
	}

	void setEnabled(bool enabled) {
		this->enabled = enabled;
	}

	bool isEnabled() const {
		return enabled;
	}

	bool isRunning() const { return getState() == THREAD_STATE_RUNNING; }

	void configureDispatchers(std::size_t count);
	void addDispatcherStat(std::size_t index, std::unique_ptr<Stat> stat);
	void addDispatcherWaitTime(std::size_t index, uint64_t waitTime) noexcept;
	void addLuaStats(std::unique_ptr<Stat> stats);
	void addSqlStats(std::unique_ptr<Stat> stats);
	void addSpecialStats(std::unique_ptr<Stat> stats);

	static uint32_t SLOW_EXECUTION_TIME;
	static uint32_t VERY_SLOW_EXECUTION_TIME;
	static int64_t DUMP_INTERVAL;

	std::atomic<uint32_t> playersOnline;

private:
	void parseDispatchersQueue(std::vector<std::forward_list<std::unique_ptr<Stat>>>&& queues);
	void parseLuaQueue(std::forward_list<std::unique_ptr<Stat>>& queue);
	void parseSqlQueue(std::forward_list<std::unique_ptr<Stat>>& queue);
	void parseSpecialQueue(std::forward_list<std::unique_ptr<Stat>>& queue);
	static void writeSlowInfo(const std::string& file, uint64_t executionTime, const std::string& description, const std::string& extraDescription);
	static void writeStats(const std::string& file, const statsMap& stats, const std::string& extraInfo = "", int64_t intervalMs = 0);

	std::mutex statsLock;
	struct DispatcherStats {
		std::forward_list<std::unique_ptr<Stat>> queue;
		statsMap stats;
		std::atomic<uint64_t> waitTime{0};
		int64_t lastDump = 0;

		DispatcherStats() = default;
		DispatcherStats(const DispatcherStats&) = delete;
		DispatcherStats& operator=(const DispatcherStats&) = delete;
		DispatcherStats(DispatcherStats&& other) noexcept :
			queue(std::move(other.queue)),
			stats(std::move(other.stats)),
			waitTime(other.waitTime.load(std::memory_order_relaxed)),
			lastDump(other.lastDump) {}
		DispatcherStats& operator=(DispatcherStats&& other) noexcept {
			if (this == &other) {
				return *this;
			}
			queue = std::move(other.queue);
			stats = std::move(other.stats);
			waitTime.store(other.waitTime.load(std::memory_order_relaxed), std::memory_order_relaxed);
			lastDump = other.lastDump;
			return *this;
		}
	};
	std::vector<DispatcherStats> dispatchers;
	struct {
		std::forward_list<std::unique_ptr<Stat>> queue;
		statsMap stats;
		int64_t lastDump;
	} lua, sql, special;

	std::atomic<bool> enabled{true};
};

extern Stats g_stats;

class AutoStat {
public:
	AutoStat(const std::string& description, const std::string& extraDescription = "");
	~AutoStat() noexcept;

protected:
	uint64_t minusTime = 0;
	std::chrono::steady_clock::time_point time_point;

private:
	std::unique_ptr<Stat> stat;
};

class AutoStatRecursive : public AutoStat {
public:
	AutoStatRecursive(const std::string& description, const std::string& extraDescription = "");
	~AutoStatRecursive() noexcept;

private:
	static thread_local ObserverPtr<AutoStatRecursive> activeStat;
	ObserverPtr<AutoStatRecursive> parent = nullptr;
};

#endif
