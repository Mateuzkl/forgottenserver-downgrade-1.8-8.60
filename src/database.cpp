// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "database.h"
#include "stats.h"

#include "configmanager.h"

#if __has_include(<mariadb/errmsg.h>)
#  include <mariadb/errmsg.h>
#else
#  include <mysql/errmsg.h>
#endif
#include "logger.h"
#include <fmt/format.h>
#include <algorithm>
#include <cctype>

static constexpr int MAX_RECONNECT_ATTEMPTS = 10;
static constexpr unsigned int MYSQL_TIMEOUT_SECONDS = 30;
static constexpr uint64_t DB_INSERT_PACKET_SAFETY_MARGIN = 4096;

static tfs::detail::Mysql_ptr connectToDatabase(const bool retryIfError)
{
	for (int retryCount = 0; ; ++retryCount) {
		if (retryCount > 0) {
			if (!retryIfError || retryCount > MAX_RECONNECT_ATTEMPTS) {
				if (retryIfError) {
					LOG_ERROR(fmt::format("[Database] Failed to connect after {} attempts. Giving up.", MAX_RECONNECT_ATTEMPTS));
				}
				return nullptr;
			}
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}

		tfs::detail::Mysql_ptr handle{mysql_init(nullptr)};
		if (!handle) {
			LOG_ERROR("Failed to initialize MySQL connection handle.");
			continue;
		}

		// Set query timeouts to prevent hanging
		{
			unsigned int readTimeout = MYSQL_TIMEOUT_SECONDS;
			unsigned int writeTimeout = MYSQL_TIMEOUT_SECONDS;
			mysql_options(handle.get(), MYSQL_OPT_READ_TIMEOUT, &readTimeout);
			mysql_options(handle.get(), MYSQL_OPT_WRITE_TIMEOUT, &writeTimeout);
		}

		// Disable SSL enforcement and verification
#if defined(_WIN32)
		{
			bool ssl_enforce = false;
			bool ssl_verify = false;
			mysql_options(handle.get(), MYSQL_OPT_SSL_ENFORCE, &ssl_enforce);
			mysql_options(handle.get(), MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &ssl_verify);
		}

#else
		{
			#if defined(MARIADB_VERSION_ID)
			bool ssl_enforce = false;
			bool ssl_verify  = false;
			mysql_options(handle.get(), MYSQL_OPT_SSL_ENFORCE, &ssl_enforce);
			mysql_options(handle.get(), MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &ssl_verify);
		    mysql_ssl_set(handle.get(), nullptr, nullptr, nullptr, nullptr, nullptr);
#else
			unsigned int ssl_mode = SSL_MODE_DISABLED;
			mysql_options(handle.get(), MYSQL_OPT_SSL_MODE, &ssl_mode);
#endif
		}
#endif

		// connects to database
		if (!mysql_real_connect(handle.get(), getString(ConfigManager::MYSQL_HOST).data(),
		                        getString(ConfigManager::MYSQL_USER).data(), getString(ConfigManager::MYSQL_PASS).data(),
		                        getString(ConfigManager::MYSQL_DB).data(), getInteger(ConfigManager::SQL_PORT),
		                        getString(ConfigManager::MYSQL_SOCK).data(), 0)) {
			LOG_ERROR(fmt::format("MySQL Error Message: {}", mysql_error(handle.get())));
			continue;
		}
		return handle;
	}
}

static bool isLostConnectionError(const unsigned error)
{
	return error == CR_SERVER_LOST || error == CR_SERVER_GONE_ERROR || error == CR_CONN_HOST_ERROR ||
	       error == 1053 /*ER_SERVER_SHUTDOWN*/ || error == CR_CONNECTION_ERROR;
}

static bool isValidSqlIdentifier(std::string_view identifier)
{
	if (identifier.empty()) {
		return false;
	}
	const auto isIdentifierHead = [](unsigned char ch) {
		return std::isalpha(ch) || ch == '_';
	};
	const auto isIdentifierTail = [](unsigned char ch) {
		return std::isalnum(ch) || ch == '_';
	};
	if (!isIdentifierHead(static_cast<unsigned char>(identifier.front()))) {
		return false;
	}
	return std::all_of(identifier.begin() + 1, identifier.end(), [isIdentifierTail](char ch) {
		return isIdentifierTail(static_cast<unsigned char>(ch));
	});
}

static void logQueryError(tfs::detail::Mysql_ptr& handle, std::string_view query)
{
	LOG_ERROR(fmt::format("[Error - mysql_real_query] Query: {}\nMessage: {}", query.substr(0, 256), mysql_error(handle.get())));
}

// Single-attempt query execution. Reconnect/retry is handled by the Database member methods.
static bool executeQuery(tfs::detail::Mysql_ptr& handle, std::string_view query, bool logError = true)
{
	if (mysql_real_query(handle.get(), query.data(), query.length()) != 0) {
		if (logError) {
			logQueryError(handle, query);
		}
		return false;
	}
	return true;
}

bool Database::connect()
{
	// Save credentials so reconnect() can restore the connection later
	dbHost   = getString(ConfigManager::MYSQL_HOST);
	dbUser   = getString(ConfigManager::MYSQL_USER);
	dbPass   = getString(ConfigManager::MYSQL_PASS);
	dbName   = getString(ConfigManager::MYSQL_DB);
	dbPort   = getInteger(ConfigManager::SQL_PORT);
	dbSocket = getString(ConfigManager::MYSQL_SOCK);

	auto newHandle = connectToDatabase(false);
	if (!newHandle) {
		return false;
	}

	handle = std::move(newHandle);
	DBResult_ptr result = storeQuery("SHOW VARIABLES LIKE 'max_allowed_packet'");
	if (result) {
		maxPacketSize = result->getNumber<uint64_t>("Value");
	}
	return true;
}

bool Database::reconnect()
{
	LOG_WARN("[Database::reconnect] Lost connection, attempting reconnect...");

	tfs::detail::Mysql_ptr newHandle{mysql_init(nullptr)};
	if (!newHandle) {
		LOG_ERROR("[Database::reconnect] mysql_init failed.");
		return false;
	}

	// Restore same timeouts as the initial connect
	{
		unsigned int readTimeout  = MYSQL_TIMEOUT_SECONDS;
		unsigned int writeTimeout = MYSQL_TIMEOUT_SECONDS;
		mysql_options(newHandle.get(), MYSQL_OPT_READ_TIMEOUT,  &readTimeout);
		mysql_options(newHandle.get(), MYSQL_OPT_WRITE_TIMEOUT, &writeTimeout);
	}

#if defined(_WIN32)
	{
		bool ssl_enforce = false;
		bool ssl_verify  = false;
		mysql_options(newHandle.get(), MYSQL_OPT_SSL_ENFORCE,             &ssl_enforce);
		mysql_options(newHandle.get(), MYSQL_OPT_SSL_VERIFY_SERVER_CERT,  &ssl_verify);
	}
#else
	{
#if defined(MARIADB_VERSION_ID)
		bool ssl_enforce = false;
		bool ssl_verify  = false;
		mysql_options(newHandle.get(), MYSQL_OPT_SSL_ENFORCE, &ssl_enforce);
		mysql_options(newHandle.get(), MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &ssl_verify);
		mysql_ssl_set(newHandle.get(), nullptr, nullptr, nullptr, nullptr, nullptr);
#else
		unsigned int ssl_mode = SSL_MODE_DISABLED;
		mysql_options(newHandle.get(), MYSQL_OPT_SSL_MODE, &ssl_mode);
#endif
	}
#endif

	const char* sock = dbSocket.empty() ? nullptr : dbSocket.c_str();
	if (!mysql_real_connect(newHandle.get(), dbHost.c_str(), dbUser.c_str(), dbPass.c_str(),
	                        dbName.c_str(), static_cast<unsigned int>(dbPort), sock, 0)) {
		LOG_ERROR(fmt::format("[Database::reconnect] failed: {}", mysql_error(newHandle.get())));
		return false;
	}

	handle = std::move(newHandle);
	LOG_INFO("[Database::reconnect] Reconnected successfully.");
	return true;
}

void Database::shutdown()
{
    Database& db = getInstance();
    db.handle.reset();
    mysql_library_end();
}

bool Database::beginTransaction()
{
	transactionLock = std::unique_lock(databaseLock);
	const bool result = executeQuery("START TRANSACTION");
	retryQueries = !result;
	if (!result) {
		transactionLock.unlock();
	}
	return result;
}

bool Database::rollback()
{
	const bool result = executeQuery("ROLLBACK");
	retryQueries = true;
	transactionLock.unlock();
	return result;
}

bool Database::commit()
{
	const bool result = executeQuery("COMMIT");
	retryQueries = true;
	transactionLock.unlock();
	return result;
}

bool Database::executeQuery(std::string_view query)
{
	std::scoped_lock lockGuard(databaseLock);
#ifdef STATS_ENABLED
	std::chrono::steady_clock::time_point time_point = std::chrono::steady_clock::now();
#endif

	bool success = ::executeQuery(handle, query, false);

	if (!success) {
		const unsigned int mysqlError = mysql_errno(handle.get());
		if (retryQueries && isLostConnectionError(mysqlError)) {
			LOG_WARN(fmt::format("[Database::executeQuery] Lost connection (error {}), attempting reconnect...", mysqlError));
			if (reconnect()) {
				success = ::executeQuery(handle, query, false);
				if (!success) {
					logQueryError(handle, query);
				}
			}
		} else {
			logQueryError(handle, query);
		}
	}

	// executeQuery can be called with command that produces result (e.g. SELECT)
	// we have to store that result, even though we do not need it, otherwise handle will get blocked
	auto mysql_res = mysql_store_result(handle.get());
	mysql_free_result(mysql_res);

#ifdef STATS_ENABLED
	uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - time_point).count();
	g_stats.addSqlStats(std::make_unique<Stat>(ns, std::string(query.substr(0, 100)), std::string(query.substr(0, 256))));
#endif

	return success;
}

DBResult_ptr Database::storeQuery(std::string_view query)
{
	std::scoped_lock lockGuard(databaseLock);

#ifdef STATS_ENABLED
	std::chrono::steady_clock::time_point time_point = std::chrono::steady_clock::now();
#endif

	tfs::detail::MysqlResult_ptr res;

	if (!::executeQuery(handle, query, false)) {
		const unsigned int mysqlError = mysql_errno(handle.get());
		if (!retryQueries || !isLostConnectionError(mysqlError)) {
			logQueryError(handle, query);
			return nullptr;
		}
		// Lost connection: reconnect once and retry.
		LOG_WARN(fmt::format("[Database::storeQuery] Lost connection (error {}), attempting reconnect...", mysqlError));
		if (!reconnect()) {
			return nullptr;
		}
		if (!::executeQuery(handle, query, false)) {
			logQueryError(handle, query);
			return nullptr;
		}
	}

	// we should call that every time as someone would call executeQuery('SELECT...')
	// as it is described in MySQL manual: "it doesn't hurt" :P
	res.reset(mysql_store_result(handle.get()));

	if (!res) {
		LOG_ERROR(fmt::format("[Error - mysql_store_result] Query: {}\nMessage: {}", query, mysql_error(handle.get())));
		return nullptr;
	}

#ifdef STATS_ENABLED
	uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - time_point).count();
	g_stats.addSqlStats(std::make_unique<Stat>(ns, std::string(query.substr(0, 100)), std::string(query.substr(0, 256))));
#endif

	// retrieving results of query
	DBResult_ptr result = std::make_shared<DBResult>(std::move(res));
	if (!result->hasNext()) {
		return nullptr;
	}
	return result;
}

std::string Database::escapeString(std::string_view s) const { return escapeBlob(s.data(), s.length()); }

std::string Database::escapeBlob(const char* s, uint32_t length) const
{
	// the worst case is 2n + 1
	size_t maxLength = (length * 2) + 1;

	std::string escaped;
	escaped.reserve(maxLength + 2);
	escaped.push_back('\'');

	if (length != 0) {
		std::vector<char> output(maxLength);
		const unsigned long escapedLength = mysql_real_escape_string(handle.get(), output.data(), s, length);
		escaped.append(output.data(), escapedLength);
	}

	escaped.push_back('\'');
	return escaped;
}

DBResult::DBResult(tfs::detail::MysqlResult_ptr&& res) : handle{std::move(res)}
{
	size_t i = 0;

	MYSQL_FIELD* field = mysql_fetch_field(handle.get());
	while (field) {
		listNames[field->name] = i++;
		field = mysql_fetch_field(handle.get());
	}

	row = mysql_fetch_row(handle.get());
}

std::string_view DBResult::getString(std::string_view column) const
{
	auto it = listNames.find(column);
	if (it == listNames.end()) {
		LOG_ERROR(fmt::format("[Error - DBResult::getString] Column '{}' does not exist in result set.", column));
		return {};
	}

	if (!row[it->second]) {
		return {};
	}

	auto size = mysql_fetch_lengths(handle.get())[it->second];
	return {row[it->second], size};
}

std::string_view DBResult::getStream(std::string_view column, unsigned long& size) const
{
	auto it = listNames.find(column);
	if (it == listNames.end()) {
		LOG_ERROR(fmt::format("[Error - DBResult::getStream] Column '{}' doesn't exist in the result set", column));
		size = 0;
		return {};
	}

	if (row[it->second] == nullptr) {
		size = 0;
		return {};
	}

	size = mysql_fetch_lengths(handle.get())[it->second];
	return row[it->second];
}

bool DBResult::hasNext() const { return row != nullptr; }

bool DBResult::next()
{
	row = mysql_fetch_row(handle.get());
	return row != nullptr;
}

DBInsert::DBInsert(std::string_view query) : query{query} { this->length = this->query.length(); }

bool DBInsert::addRow(std::string_view row)
{
	// adds new row to buffer
	const size_t rowLength = row.length();
	const uint64_t maxPacketSize = Database::getInstance().getMaxPacketSize();
	const uint64_t maxQueryLength = maxPacketSize > DB_INSERT_PACKET_SAFETY_MARGIN
	                                    ? maxPacketSize - DB_INSERT_PACKET_SAFETY_MARGIN
	                                    : maxPacketSize;

	const bool hasRows = !values.empty();
	const size_t projectedRowLength = rowLength + (hasRows ? 3 : 2);
	if (hasRows && static_cast<uint64_t>(length + projectedRowLength) > maxQueryLength && !execute()) {
		return false;
	}

	const bool firstRow = values.empty();
	if (values.empty()) {
		values.reserve(rowLength + 2);
		values.push_back('(');
		values.append(row);
		values.push_back(')');
	} else {
		values.reserve(values.length() + rowLength + 3);
		values.push_back(',');
		values.push_back('(');
		values.append(row);
		values.push_back(')');
	}
	length += rowLength + (firstRow ? 2 : 3);
	return true;
}

bool DBInsert::addRow(std::ostringstream& row)
{
	bool ret = addRow(row.str());
	row.str(std::string());
	return ret;
}

bool DBInsert::execute()
{
	if (values.empty()) {
		return true;
	}

	std::string fullQuery = query + " " + values + upsertClause;
	bool res = Database::getInstance().executeQuery(fullQuery);
	values.clear();
	length = query.length();
	return res;
}

void DBInsert::upsert(const std::vector<std::string>& columns)
{
	if (columns.empty()) {
		return;
	}

	for (const auto& column : columns) {
		if (!isValidSqlIdentifier(column)) {
			LOG_ERROR(fmt::format("[DBInsert::upsert] Invalid SQL column identifier: {}", column));
			upsertClause.clear();
			return;
		}
	}

	upsertClause = " ON DUPLICATE KEY UPDATE ";
	for (size_t i = 0; i < columns.size(); ++i) {
		upsertClause += fmt::format("`{}` = VALUES(`{}`)", columns[i], columns[i]);
		if (i < columns.size() - 1) {
			upsertClause += ", ";
		}
	}
}
