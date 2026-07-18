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

std::string stripAnsi(std::string_view message)
{
	if (message.find('\x1b') == std::string_view::npos) {
		return std::string(message);
	}

	std::string plain;
	plain.reserve(message.size());
	for (size_t i = 0; i < message.size(); ++i) {
		if (message[i] != '\x1b' || i + 1 >= message.size() || message[i + 1] != '[') {
			plain.push_back(message[i]);
			continue;
		}

		i += 2;
		while (i < message.size()) {
			const unsigned char ch = static_cast<unsigned char>(message[i]);
			if (ch >= 0x40 && ch <= 0x7E) {
				break;
			}
			++i;
		}
	}
	return plain;
}

std::string_view normalizeConsoleMessage(std::string_view message)
{
	while (!message.empty() && std::isspace(static_cast<unsigned char>(message.front()))) {
		message.remove_prefix(1);
	}
	if (message.starts_with(">>")) {
		message.remove_prefix(2);
		while (!message.empty() && std::isspace(static_cast<unsigned char>(message.front()))) {
			message.remove_prefix(1);
		}
	}
	return message;
}

class LogWithSpdLog final : public Logger
{
public:
	LogWithSpdLog(std::string_view filePath, size_t rotateSize, size_t rotateFiles, bool logToFile)
	{
		try {
			consoleSink_ = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			consoleSink_->set_pattern("%^%v%$");
			consoleLogger_ = std::make_shared<spdlog::logger>("tfs-console", consoleSink_);
			consoleLogger_->set_level(spdlog::level::trace);
			consoleLogger_->flush_on(spdlog::level::err);

			if (logToFile) {
				timestampedPath_ = generateLogFileName(filePath);

				if (!checkDiskSpace(timestampedPath_)) {
					fmt::print(stderr, "Warning: Low disk space for logging\n");
				}

				auto fileSink =
				    std::make_shared<spdlog::sinks::rotating_file_sink_mt>(timestampedPath_, rotateSize, rotateFiles);
				fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");
				fileSink->set_level(spdlog::level::trace);
				fileLogger_ = std::make_shared<spdlog::logger>("tfs-file", fileSink);
				fileLogger_->set_level(spdlog::level::trace);
				fileLogger_->flush_on(spdlog::level::err);
			}

			flush();

		} catch (const std::exception& e) {
			fmt::print(stderr, "Error creating logger: {}\n", e.what());
			throw;
		}
	}

	~LogWithSpdLog() override
	{
		try {
			if (!shutdownInProgress.load()) {
				if (consoleLogger_) {
					consoleLogger_->info("[INFO    ] === TFS Logger Shutdown ===");
				}
				if (fileLogger_) {
					fileLogger_->info("[INFO    ] === TFS Logger Shutdown ===");
				}
				flush();
			}
		} catch (...) {
			// Safe destructor - no exceptions
		}
	}

	void setLevel(LogLevel level) override
	{
		if (consoleLogger_) {
			consoleLogger_->set_level(toSpd(level));
		}
		if (fileLogger_) {
			fileLogger_->set_level(toSpd(level));
		}
	}

	void setConsoleLevel(LogLevel level) override
	{
		if (consoleSink_) {
			consoleSink_->set_level(toSpd(level));
		}
	}

	void setConsoleColors(bool enabled) override
	{
		if (consoleSink_) {
			consoleSink_->set_color_mode(enabled ? spdlog::color_mode::automatic : spdlog::color_mode::never);
		}
	}

	LogLevel getLevel() const override { return consoleLogger_ ? fromSpd(consoleLogger_->level()) : LogLevel::INFO; }

	bool isEnabled(LogLevel level) const override
	{
		return consoleLogger_ && consoleLogger_->should_log(toSpd(level));
	}

	void writeConsoleBlock(const std::function<void()>& writer, std::string_view persistedMessage) override
	{
		if (!writer) {
			return;
		}

		try {
			std::scoped_lock lock(outputMutex_);
			writer();
			std::fflush(stdout);

			if (fileLogger_ && !persistedMessage.empty()) {
				fileLogger_->log(spdlog::level::err, "[{:<8}] {}", "ERROR", stripAnsi(persistedMessage));
				fileLogger_->flush();
			}
		} catch (const std::exception& e) {
			fmt::print(stderr, "[LOGGER ERROR] Failed to write console block: {}\n", e.what());
		}
	}

	void flush()
	{
		if (consoleLogger_) {
			consoleLogger_->flush();
		}
		if (fileLogger_) {
			fileLogger_->flush();
		}
	}

	void stats(std::string_view msg) override
	{
		logCategory(LogLevel::INFO, "STATS", msg);
	}

	void statsWarning(std::string_view msg) override
	{
		logCategory(LogLevel::WARNING, "WARNING", fmt::format("[STATS] {}", msg));
	}

	void mapCache(std::string_view msg) override
	{
		logCategory(LogLevel::INFO, "MAPCACHE", msg);
	}

	void network(std::string_view msg) override
	{
		logCategory(LogLevel::INFO, "NETWORK", msg);
	}

	void raid(std::string_view msg) override
	{
		logCategory(LogLevel::WARNING, "RAID", msg);
	}

	void threadPool(std::string_view msg) override
	{
		logCategory(LogLevel::INFO, "THREAD", msg);
	}

	void reactor(std::string_view msg) override
	{
		logCategory(LogLevel::INFO, "REACTOR", msg);
	}

protected:
	void log(LogLevel level, std::string_view msg) override
	{
		std::string_view category;
		switch (level) {
			case LogLevel::TRACE: category = "TRACE"; break;
			case LogLevel::DEBUG: category = "DEBUG"; break;
			case LogLevel::INFO: category = "INFO"; break;
			case LogLevel::WARNING: category = "WARNING"; break;
			case LogLevel::ERRORR:
			case LogLevel::CRITICAL: category = "ERROR"; break;
			case LogLevel::MIGRATION: category = "DATABASE"; break;
		}
		logCategory(level == LogLevel::MIGRATION ? LogLevel::INFO : level, category, msg);
	}

	void logCategory(LogLevel level, std::string_view category, std::string_view msg) override
	{
		logCategory(level, category, msg, msg);
	}

	void logCategory(LogLevel level, std::string_view category, std::string_view msg,
	                 std::string_view persistedMsg) override
	{
		if (!isEnabled(level)) {
			return;
		}

		try {
			std::scoped_lock lock(outputMutex_);
			if (level >= LogLevel::ERRORR && !timestampedPath_.empty() && !checkDiskSpace(timestampedPath_)) {
				fmt::print(stderr, "[DISK FULL] {}\n", msg);
			}

			std::string plainStorage;
			std::string_view plainMessage = persistedMsg;
			if (persistedMsg.find('\x1b') != std::string_view::npos) {
				plainStorage = stripAnsi(persistedMsg);
				plainMessage = plainStorage;
			}

			const std::string_view consoleMessage = normalizeConsoleMessage(msg);
			if (category == "LOGIN") {
				consoleLogger_->log(toSpd(level), "   ♟[LOGIN ] {}", consoleMessage);
			} else if (category == "LOGOUT") {
				consoleLogger_->log(toSpd(level), "   ♟[LOGOUT] {}", consoleMessage);
			} else if (level >= LogLevel::ERRORR) {
				consoleLogger_->log(toSpd(level), "    [ERROR   ] {}", consoleMessage);
			} else if (level >= LogLevel::WARNING) {
				consoleLogger_->log(toSpd(level), "    [WARNING ] {}", consoleMessage);
			} else if (category == "STATS") {
				consoleLogger_->log(toSpd(level), "    [STATS] {}", consoleMessage);
			} else if (category == "DATABASE") {
				consoleLogger_->log(toSpd(level), "    [DATABASE] {}", consoleMessage);
			} else if (category == "NETWORK") {
				consoleLogger_->log(toSpd(level), "    [NETWORK ] {}", consoleMessage);
			} else if ((consoleMessage.starts_with("SIG") &&
			            consoleMessage.find("shutting game server down") != std::string_view::npos) ||
			           consoleMessage.starts_with("Saving game state")) {
				consoleLogger_->log(toSpd(level), "   ⚠[INFO  ] {}", consoleMessage);
			} else {
				consoleLogger_->log(toSpd(level), "    [INFO    ] {}", consoleMessage);
			}

			if (fileLogger_) {
				fileLogger_->log(toSpd(level), "[{:<8}] {}", category.substr(0, 8), plainMessage);
			}

			if (level >= LogLevel::ERRORR) {
				flush();
			}
		} catch (const std::exception& e) {
			fmt::print(stderr, "[LOGGER ERROR] {}: {}\n", e.what(), msg);
		}
	}

private:
	std::shared_ptr<spdlog::logger> consoleLogger_;
	std::shared_ptr<spdlog::logger> fileLogger_;
	std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> consoleSink_;
	std::string timestampedPath_;
	std::mutex outputMutex_;
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

bool initLogger(LogLevel level, std::string_view filePath, size_t rotateSize, size_t rotateFiles, bool logToFile)
{
	std::scoped_lock lock(loggerMutex);

	if (loggerInitialized.load(std::memory_order_acquire)) {
		if (loggerInstance) {
			loggerInstance->setLevel(level);
		}
		return true;
	}

	try {
		loggerInstance = std::make_unique<LogWithSpdLog>(filePath, rotateSize, rotateFiles, logToFile);
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
