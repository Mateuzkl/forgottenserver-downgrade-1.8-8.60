// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_LOGGER_H
#define FS_LOGGER_H

#include "position.h"

#include <fmt/format.h>
#include <functional>

enum class LogLevel
{
	TRACE,
	DEBUG,
	INFO,
	WARNING,
	ERRORR,
	CRITICAL,
	MIGRATION
};

class Logger
{
public:
	virtual ~Logger() = default;

	virtual void setLevel(LogLevel level) = 0;
	virtual void setConsoleLevel(LogLevel level) = 0;
	virtual void setConsoleColors(bool enabled) = 0;
	virtual LogLevel getLevel() const = 0;
	virtual bool isEnabled(LogLevel level) const = 0;
	virtual void writeConsoleBlock(const std::function<void()>& writer, std::string_view persistedMessage = {}) = 0;

	void trace([[maybe_unused]] std::string_view msg)
	{
		if (isEnabled(LogLevel::TRACE)) {
			log(LogLevel::TRACE, msg);
		}
	}

	void debug([[maybe_unused]] std::string_view msg)
	{
		if (isEnabled(LogLevel::DEBUG)) {
			log(LogLevel::DEBUG, msg);
		}
	}

	void info(std::string_view msg)
	{
		if (isEnabled(LogLevel::INFO)) {
			log(LogLevel::INFO, msg);
		}
	}

	void warn(std::string_view msg)
	{
		if (isEnabled(LogLevel::WARNING)) {
			log(LogLevel::WARNING, msg);
		}
	}

	void error(std::string_view msg)
	{
		if (isEnabled(LogLevel::ERRORR)) {
			log(LogLevel::ERRORR, msg);
		}
	}

	void critical(std::string_view msg)
	{
		if (isEnabled(LogLevel::CRITICAL)) {
			log(LogLevel::CRITICAL, msg);
		}
	}

	void migration(std::string_view msg)
	{
		if (isEnabled(LogLevel::MIGRATION)) {
			logCategory(LogLevel::INFO, "DATABASE", msg);
		}
	}

	void database(std::string_view msg) { logCategoryIfEnabled(LogLevel::INFO, "DATABASE", msg); }
	void login(std::string_view msg) { logCategoryIfEnabled(LogLevel::INFO, "LOGIN", msg); }
	void login(std::string_view msg, std::string_view persistedMsg)
	{
		logCategoryIfEnabled(LogLevel::INFO, "LOGIN", msg, persistedMsg);
	}
	void logout(std::string_view msg) { logCategoryIfEnabled(LogLevel::INFO, "LOGOUT", msg); }
	void logout(std::string_view msg, std::string_view persistedMsg)
	{
		logCategoryIfEnabled(LogLevel::INFO, "LOGOUT", msg, persistedMsg);
	}
	void lua(std::string_view msg) { logCategoryIfEnabled(LogLevel::INFO, "LUA", msg); }
	void startup(std::string_view msg) { logCategoryIfEnabled(LogLevel::INFO, "STARTUP", msg); }

	virtual void stats(std::string_view msg) = 0;
	virtual void statsWarning(std::string_view msg) = 0;
	virtual void mapCache(std::string_view msg) = 0;
	virtual void network(std::string_view msg) = 0;
	virtual void raid(std::string_view msg) = 0;
	virtual void threadPool(std::string_view msg) = 0;
	virtual void reactor(std::string_view msg) = 0;

	template <typename... Args>
	void trace(fmt::format_string<Args...> fmt, Args&&... args)
	{
		if (isEnabled(LogLevel::TRACE)) {
			log(LogLevel::TRACE, fmt::format(fmt, std::forward<Args>(args)...));
		}
	}

	template <typename... Args>
	void debug(fmt::format_string<Args...> fmt, Args&&... args)
	{
		if (isEnabled(LogLevel::DEBUG)) {
			log(LogLevel::DEBUG, fmt::format(fmt, std::forward<Args>(args)...));
		}
	}

	template <typename... Args>
	void info(fmt::format_string<Args...> fmt, Args&&... args)
	{
		if (isEnabled(LogLevel::INFO)) {
			log(LogLevel::INFO, fmt::format(fmt, std::forward<Args>(args)...));
		}
	}

	template <typename... Args>
	void warn(fmt::format_string<Args...> fmt, Args&&... args)
	{
		if (isEnabled(LogLevel::WARNING)) {
			log(LogLevel::WARNING, fmt::format(fmt, std::forward<Args>(args)...));
		}
	}

	template <typename... Args>
	void error(fmt::format_string<Args...> fmt, Args&&... args)
	{
		if (isEnabled(LogLevel::ERRORR)) {
			log(LogLevel::ERRORR, fmt::format(fmt, std::forward<Args>(args)...));
		}
	}

	template <typename... Args>
	void critical(fmt::format_string<Args...> fmt, Args&&... args)
	{
		if (isEnabled(LogLevel::CRITICAL)) {
			log(LogLevel::CRITICAL, fmt::format(fmt, std::forward<Args>(args)...));
		}
	}

	template <typename... Args>
	void migration(fmt::format_string<Args...> fmt, Args&&... args)
	{
		if (isEnabled(LogLevel::MIGRATION)) {
			logCategory(LogLevel::INFO, "DATABASE", fmt::format(fmt, std::forward<Args>(args)...));
		}
	}

	template <typename... Args>
	void database(fmt::format_string<Args...> fmt, Args&&... args)
	{
		logCategoryFormatted(LogLevel::INFO, "DATABASE", fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void login(fmt::format_string<Args...> fmt, Args&&... args)
	{
		logCategoryFormatted(LogLevel::INFO, "LOGIN", fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void logout(fmt::format_string<Args...> fmt, Args&&... args)
	{
		logCategoryFormatted(LogLevel::INFO, "LOGOUT", fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void lua(fmt::format_string<Args...> fmt, Args&&... args)
	{
		logCategoryFormatted(LogLevel::INFO, "LUA", fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void startup(fmt::format_string<Args...> fmt, Args&&... args)
	{
		logCategoryFormatted(LogLevel::INFO, "STARTUP", fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void stats(fmt::format_string<Args...> fmt, Args&&... args)
	{
		stats(fmt::format(fmt, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void statsWarning(fmt::format_string<Args...> fmt, Args&&... args)
	{
		statsWarning(fmt::format(fmt, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void mapCache(fmt::format_string<Args...> fmt, Args&&... args)
	{
		mapCache(fmt::format(fmt, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void network(fmt::format_string<Args...> fmt, Args&&... args)
	{
		network(fmt::format(fmt, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void raid(fmt::format_string<Args...> fmt, Args&&... args)
	{
		raid(fmt::format(fmt, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void threadPool(fmt::format_string<Args...> fmt, Args&&... args)
	{
		threadPool(fmt::format(fmt, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void reactor(fmt::format_string<Args...> fmt, Args&&... args)
	{
		reactor(fmt::format(fmt, std::forward<Args>(args)...));
	}

	template <typename F>
	auto profile(std::string_view name, F&& func)
	{
		if (!isEnabled(LogLevel::INFO)) {
			return std::forward<F>(func)();
		}

		auto start = std::chrono::steady_clock::now();
		try {
			auto result = std::forward<F>(func)();
			auto end = std::chrono::steady_clock::now();
			info("{} took {} ms", name, std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
			return result;
		}

		catch (...) {
			auto end = std::chrono::steady_clock::now();
			error("{} failed after {} ms", name,
			      std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
			throw;
		}
	}

protected:
	virtual void log(LogLevel level, std::string_view message) = 0;
	virtual void logCategory(LogLevel level, std::string_view category, std::string_view message) = 0;
	virtual void logCategory(LogLevel level, std::string_view category, std::string_view message,
	                         std::string_view persistedMessage) = 0;

private:
	void logCategoryIfEnabled(LogLevel level, std::string_view category, std::string_view message)
	{
		if (isEnabled(level)) {
			logCategory(level, category, message);
		}
	}

	void logCategoryIfEnabled(LogLevel level, std::string_view category, std::string_view message,
	                          std::string_view persistedMessage)
	{
		if (isEnabled(level)) {
			logCategory(level, category, message, persistedMessage);
		}
	}

	template <typename... Args>
	void logCategoryFormatted(LogLevel level, std::string_view category, fmt::format_string<Args...> format,
	                          Args&&... args)
	{
		if (isEnabled(level)) {
			logCategory(level, category, fmt::format(format, std::forward<Args>(args)...));
		}
	}
};

Logger& g_logger();
bool initLogger(LogLevel level = LogLevel::INFO, std::string_view filePath = "data/logs/server.log",
                size_t rotateSize = 5 * 1024 * 1024, size_t rotateFiles = 3, bool logToFile = false);
void shutdownLogger();
bool isLoggerInitialized();
LogLevel parseLogLevel(std::string_view level);

// Signal-safe shutdown functions
void setupLoggerSignalHandlers();
void loggerSignalHandler(int signal);

#define LOG_TRACE(...) \
	do { \
		if (isLoggerInitialized()) g_logger().trace(__VA_ARGS__); \
	} while (0)
#define LOG_DEBUG(...) \
	do { \
		if (isLoggerInitialized()) g_logger().debug(__VA_ARGS__); \
	} while (0)
#define LOG_INFO(...) \
	do { \
		if (isLoggerInitialized()) g_logger().info(__VA_ARGS__); \
	} while (0)
#define LOG_WARN(...) \
	do { \
		if (isLoggerInitialized()) g_logger().warn(__VA_ARGS__); \
	} while (0)
#define LOG_ERROR(...) \
	do { \
		if (isLoggerInitialized()) g_logger().error(__VA_ARGS__); \
	} while (0)
#define LOG_CRITICAL(...) \
	do { \
		if (isLoggerInitialized()) g_logger().critical(__VA_ARGS__); \
	} while (0)
#define LOG_MIGRATION(...) \
	do { \
		if (isLoggerInitialized()) g_logger().migration(__VA_ARGS__); \
	} while (0)
#define LOG_STATS(...) \
	do { \
		if (isLoggerInitialized()) g_logger().stats(__VA_ARGS__); \
	} while (0)
#define LOG_STATS_WARNING(...) \
	do { \
		if (isLoggerInitialized()) g_logger().statsWarning(__VA_ARGS__); \
	} while (0)
#define LOG_MAPCACHE(...) \
	do { \
		if (isLoggerInitialized()) g_logger().mapCache(__VA_ARGS__); \
	} while (0)
#define LOG_NETWORK(...) \
	do { \
		if (isLoggerInitialized()) g_logger().network(__VA_ARGS__); \
	} while (0)
#define LOG_RAID(...) \
	do { \
		if (isLoggerInitialized()) g_logger().raid(__VA_ARGS__); \
	} while (0)
#define LOG_THREADPOOL(...) \
	do { \
		if (isLoggerInitialized()) g_logger().threadPool(__VA_ARGS__); \
	} while (0)
#define LOG_REACTOR(...) \
	do { \
		if (isLoggerInitialized()) g_logger().reactor(__VA_ARGS__); \
	} while (0)
#define LOG_DATABASE(...) \
	do { \
		if (isLoggerInitialized()) g_logger().database(__VA_ARGS__); \
	} while (0)
#define LOG_LOGIN(...) \
	do { \
		if (isLoggerInitialized()) g_logger().login(__VA_ARGS__); \
	} while (0)
#define LOG_LOGOUT(...) \
	do { \
		if (isLoggerInitialized()) g_logger().logout(__VA_ARGS__); \
	} while (0)
#define LOG_LUA(...) \
	do { \
		if (isLoggerInitialized()) g_logger().lua(__VA_ARGS__); \
	} while (0)
#define LOG_STARTUP(...) \
	do { \
		if (isLoggerInitialized()) g_logger().startup(__VA_ARGS__); \
	} while (0)

template <typename T>
concept EnumLike = std::is_enum_v<T>;

template <EnumLike T>
struct fmt::formatter<T> : fmt::formatter<int>
{
	template <typename FormatContext>
	auto format(T value, FormatContext& ctx)
	{
		return fmt::formatter<int>::format(static_cast<int>(value), ctx);
	}
};

template <>
struct fmt::formatter<Position>
{
	constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

	template <typename FormatContext>
	auto format(const Position& pos, FormatContext& ctx) const
	{
		return fmt::format_to(ctx.out(), "({}, {}, {})", pos.x, pos.y, pos.z);
	}
};

#endif // FS_LOGGER_H
