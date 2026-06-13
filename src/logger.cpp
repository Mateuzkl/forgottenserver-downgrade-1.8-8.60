// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "logger.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#ifdef _WIN32
#include <io.h>
#define write _write
#define STDERR_FILENO 2
using ssize_t = ptrdiff_t;
#else
#include <unistd.h>
#endif

namespace {

std::mutex loggerMutex;
std::atomic<bool> loggerInitialized{false};
std::atomic<bool> shutdownInProgress{false};

spdlog::level::level_enum toSpd(LogLevel level)
{
	switch (level) {
		case LogLevel::TRACE:
			return spdlog::level::trace;
		case LogLevel::DEBUG:
			return spdlog::level::debug;
		case LogLevel::INFO:
			return spdlog::level::info;
		case LogLevel::WARNING:
			return spdlog::level::warn;
		case LogLevel::ERRORR:
			return spdlog::level::err;
		case LogLevel::CRITICAL:
			return spdlog::level::critical;
		case LogLevel::MIGRATION:
			return spdlog::level::info;
	}
	return spdlog::level::info;
}

LogLevel fromSpd(spdlog::level::level_enum level)
{
	switch (level) {
		case spdlog::level::trace:
			return LogLevel::TRACE;
		case spdlog::level::debug:
			return LogLevel::DEBUG;
		case spdlog::level::info:
			return LogLevel::INFO;
		case spdlog::level::warn:
			return LogLevel::WARNING;
		case spdlog::level::err:
			return LogLevel::ERRORR;
		case spdlog::level::critical:
			return LogLevel::CRITICAL;
		default:
			return LogLevel::INFO;
	}
}

std::string generateLogFileName(std::string_view basePath)
{
	auto now = std::chrono::system_clock::now();
	auto time_t = std::chrono::system_clock::to_time_t(now);

	std::stringstream ss;
	ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");

	std::filesystem::path path(basePath);
	std::string directory = path.parent_path().string();
	std::string baseName = path.stem().string();
	std::string extension = path.extension().string();

	if (!directory.empty()) {
		try {
			std::filesystem::create_directories(directory);
		} catch (const std::filesystem::filesystem_error& e) {
			fmt::print(stderr, "Failed to create log directory: {}\n", e.what());
			throw;
		}
	}

	return directory + "/" + baseName + "_" + ss.str() + extension;
}

bool checkDiskSpace(const std::string& path, size_t minSpaceBytes = 50 * 1024 * 1024)
{
	try {
		auto space = std::filesystem::space(std::filesystem::path(path).parent_path());
		return space.available > minSpaceBytes;
	} catch (const std::filesystem::filesystem_error&) {
		return true;
	}
}

class LogWithSpdLog final : public Logger
{
public:
	LogWithSpdLog(std::string_view filePath, size_t rotateSize, size_t rotateFiles)
	{
		try {
			timestampedPath_ = generateLogFileName(filePath);

			if (!checkDiskSpace(timestampedPath_)) {
				fmt::print(stderr, "Warning: Low disk space for logging\n");
			}

			auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

			auto file_sink =
			    std::make_shared<spdlog::sinks::rotating_file_sink_mt>(timestampedPath_, rotateSize, rotateFiles);
			file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
			file_sink->set_level(spdlog::level::trace);

			std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
			logger_ = std::make_shared<spdlog::logger>("tfs", sinks.begin(), sinks.end());
			logger_->set_level(spdlog::level::trace);
			logger_->flush_on(spdlog::level::info);

			auto console_sink_stats = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			console_sink_stats->set_pattern("[%H:%M:%S] %v");

			statsLoggerConsole_ = std::make_shared<spdlog::logger>("tfs_stats_console", console_sink_stats);
			statsLoggerConsole_->set_level(spdlog::level::info);

			auto console_sink_migrations = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			console_sink_migrations->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");

			migrationsLogger_ = std::make_shared<spdlog::logger>("tfs_migrations", console_sink_migrations);
			migrationsLogger_->set_level(spdlog::level::info);

			auto console_sink_stats_warning = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			console_sink_stats_warning->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");

			statsWarningLogger_ = std::make_shared<spdlog::logger>("tfs_stats_warning", console_sink_stats_warning);
			statsWarningLogger_->set_level(spdlog::level::info);

			auto console_sink_mapcache = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			console_sink_mapcache->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");

			mapCacheLogger_ = std::make_shared<spdlog::logger>("tfs_mapcache", console_sink_mapcache);
			mapCacheLogger_->set_level(spdlog::level::info);

			auto console_sink_network = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			console_sink_network->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");

			networkLogger_ = std::make_shared<spdlog::logger>("tfs_network", console_sink_network);
			networkLogger_->set_level(spdlog::level::info);

			auto console_sink_raid = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			console_sink_raid->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");

			raidLogger_ = std::make_shared<spdlog::logger>("tfs_raid", console_sink_raid);
			raidLogger_->set_level(spdlog::level::info);

		auto console_sink_threadpool = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		console_sink_threadpool->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");

		threadPoolLogger_ = std::make_shared<spdlog::logger>("tfs_threadpool", console_sink_threadpool);
		threadPoolLogger_->set_level(spdlog::level::info);

		auto console_sink_reactor = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		console_sink_reactor->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");

		reactorLogger_ = std::make_shared<spdlog::logger>("tfs_reactor", console_sink_reactor);
		reactorLogger_->set_level(spdlog::level::info);

			logger_->info("=== TFS Logger Initialized ===");
			logger_->info("Log file: {}", timestampedPath_);
			logger_->flush();

		} catch (const std::exception& e) {
			fmt::print(stderr, "Error creating logger: {}\n", e.what());
			throw;
		}
	}

	~LogWithSpdLog() override
	{
		try {
			if (logger_ && !shutdownInProgress.load()) {
				logger_->info("=== TFS Logger Shutdown ===");
				logger_->flush();
			}
		} catch (...) {
			// Safe destructor - no exceptions
		}
	}

	void setLevel(LogLevel level) override
	{
		if (logger_) {
			logger_->set_level(toSpd(level));
		}
	}

	LogLevel getLevel() const override { return logger_ ? fromSpd(logger_->level()) : LogLevel::INFO; }

	bool isEnabled(LogLevel level) const override { return logger_ && logger_->should_log(toSpd(level)); }

	void flush()
	{
		if (logger_) {
			logger_->flush();
		}
	}

	void stats(std::string_view msg) override
	{
		if (statsLoggerConsole_) {
			statsLoggerConsole_->info("\033[38;5;208m[STATS]\033[0m {}", msg);
		}
		writeToMainFileSink(spdlog::level::info, fmt::format("[STATS] {}", msg));
	}

	void statsWarning(std::string_view msg) override
	{
		if (statsWarningLogger_) {
			statsWarningLogger_->info("{} {}", fmt::format(fg(fmt::color::yellow), "[WARNING STATS]"), msg);
		}
		writeToMainFileSink(spdlog::level::warn, fmt::format("[WARNING STATS] {}", msg));
	}

	void mapCache(std::string_view msg) override
	{
		if (mapCacheLogger_) {
			mapCacheLogger_->info("{}", msg);
		}
		writeToMainFileSink(spdlog::level::info, fmt::format("{}", msg));
	}

	void network(std::string_view msg) override
	{
		if (networkLogger_) {
			networkLogger_->info("\033[38;5;135m[Network]\033[0m {}", msg);
		}
		writeToMainFileSink(spdlog::level::info, fmt::format("[Network] {}", msg));
	}

	void raid(std::string_view msg) override
	{
		if (raidLogger_) {
			raidLogger_->info("\033[31m[RAIDS]\033[0m \033[37m{}\033[0m", msg);
		}
		writeToMainFileSink(spdlog::level::warn, fmt::format("[RAIDS] {}", msg));
	}

	void threadPool(std::string_view msg) override
	{
		if (threadPoolLogger_) {
			threadPoolLogger_->info("\033[36m[ThreadPool]\033[0m \033[37m{}\033[0m", msg);
		}
		writeToMainFileSink(spdlog::level::info, fmt::format("[ThreadPool] {}", msg));
	}

	void reactor(std::string_view msg) override
	{
		if (reactorLogger_) {
			reactorLogger_->info("\033[33m[TaskReactor]\033[0m >> {}", msg);
		}
		writeToMainFileSink(spdlog::level::info, fmt::format("[TaskReactor] >> {}", msg));
	}

protected:
	void log(LogLevel level, std::string_view msg) override
	{
		if (level == LogLevel::MIGRATION) {
			if (migrationsLogger_) {
				migrationsLogger_->info("\033[36m[migrations]\033[0m {}", msg);
			}
			writeToMainFileSink(spdlog::level::info, fmt::format("[migrations] {}", msg));
			return;
		}

		if (!logger_ || !logger_->should_log(toSpd(level))) return;

		try {
			if (level >= LogLevel::ERRORR && !checkDiskSpace(timestampedPath_)) {
				fmt::print(stderr, "[DISK FULL] {}\n", msg);
			}

			logger_->log(toSpd(level), msg);

			if (level >= LogLevel::ERRORR) {
				logger_->flush();
			}
		} catch (const std::exception& e) {
			fmt::print(stderr, "[LOGGER ERROR] {}: {}\n", e.what(), msg);
		}
	}

private:
	std::shared_ptr<spdlog::logger> logger_;
	std::shared_ptr<spdlog::logger> statsLoggerConsole_;
	std::shared_ptr<spdlog::logger> statsWarningLogger_;
	std::shared_ptr<spdlog::logger> migrationsLogger_;
	std::shared_ptr<spdlog::logger> mapCacheLogger_;
	std::shared_ptr<spdlog::logger> networkLogger_;
	std::shared_ptr<spdlog::logger> raidLogger_;
	std::shared_ptr<spdlog::logger> threadPoolLogger_;
	std::shared_ptr<spdlog::logger> reactorLogger_;
	std::string timestampedPath_;

	void writeToMainFileSink(spdlog::level::level_enum level, std::string_view formattedMsg)
	{
		if (!logger_) {
			return;
		}
		for (auto& sink : logger_->sinks()) {
			auto fileSink = std::dynamic_pointer_cast<spdlog::sinks::rotating_file_sink_mt>(sink);
			if (fileSink) {
				try {
					spdlog::details::log_msg logMsg("tfs", level, formattedMsg);
					fileSink->log(logMsg);
				} catch (const std::exception& e) {
					fmt::print(stderr, "[LOGGER] writeToMainFileSink({}) I/O error: {}\n", static_cast<int>(level), e.what());
				} catch (...) {
					fmt::print(stderr, "[LOGGER] writeToMainFileSink({}) unknown I/O error\n", static_cast<int>(level));
				}
			}
		}
	}
};

static std::unique_ptr<Logger> loggerInstance;

} // namespace

Logger& g_logger()
{
	if (loggerInitialized.load(std::memory_order_acquire) && loggerInstance) {
		return *loggerInstance;
	}

	std::scoped_lock lock(loggerMutex);
	if (!loggerInitialized.load(std::memory_order_acquire) || !loggerInstance) {
		throw std::runtime_error("Logger not initialized. Call initLogger() first.");
	}
	return *loggerInstance;
}

bool initLogger(LogLevel level, std::string_view filePath, size_t rotateSize, size_t rotateFiles)
{
	std::scoped_lock lock(loggerMutex);

	if (loggerInitialized.load(std::memory_order_acquire)) {
		if (loggerInstance) {
			loggerInstance->setLevel(level);
		}
		return true;
	}

	try {
		loggerInstance = std::make_unique<LogWithSpdLog>(filePath, rotateSize, rotateFiles);
		loggerInstance->setLevel(level);
		loggerInitialized.store(true, std::memory_order_release);

		return true;
	}

	catch (const std::exception& e) {
		fmt::print(stderr, "Failed to initialize logger: {}\n", e.what());
		return false;
	}
}

void shutdownLogger()
{
	std::scoped_lock lock(loggerMutex);
	if (loggerInitialized.load(std::memory_order_acquire)) {
		shutdownInProgress.store(true, std::memory_order_release);

		if (loggerInstance) {
			loggerInstance->info("=== TFS Server Shutdown ===");
			loggerInstance->info(">> Shutdown initiated at {}", std::chrono::duration_cast<std::chrono::seconds>(
			                                                        std::chrono::system_clock::now().time_since_epoch())
			                                                        .count());
		}

		loggerInstance.reset();
		loggerInitialized.store(false, std::memory_order_release);

		try {
			// spdlog::shutdown();
		} catch (...) {
			// Ignore shutdown failure
		}
	}
}

bool isLoggerInitialized() { return loggerInitialized.load(std::memory_order_acquire); }

LogLevel parseLogLevel(std::string_view level)
{
	if (level == "trace") return LogLevel::TRACE;
	if (level == "debug") return LogLevel::DEBUG;
	if (level == "info") return LogLevel::INFO;
	if (level == "warning" || level == "warn") return LogLevel::WARNING;
	if (level == "error") return LogLevel::ERRORR;
	if (level == "critical") return LogLevel::CRITICAL;
	return LogLevel::INFO;
}

void loggerSignalHandler(int signal)
{
	// Signal handlers must only use async-signal-safe functions.
	// write() is async-signal-safe, while fprintf, g_logger(), etc. are not.
	const char* signalName = "UNKNOWN";
	switch (signal) {
		case SIGSEGV:
			signalName = "SIGSEGV";
			break;
		case SIGABRT:
			signalName = "SIGABRT";
			break;
		default:
			return;
	}

	// Use write() for signal-safe output to stderr
	const char prefix[] = "[CRITICAL] Signal received: ";
	const char suffix[] = ", >> shutting down\n";
	[[maybe_unused]] ssize_t r1 = write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
	[[maybe_unused]] ssize_t r2 = write(STDERR_FILENO, signalName, strlen(signalName));
	[[maybe_unused]] ssize_t r3 = write(STDERR_FILENO, suffix, sizeof(suffix) - 1);

	std::signal(signal, SIG_DFL);
	std::raise(signal);
}

void setupLoggerSignalHandlers()
{
	std::signal(SIGSEGV, loggerSignalHandler);
	std::signal(SIGABRT, loggerSignalHandler);
}