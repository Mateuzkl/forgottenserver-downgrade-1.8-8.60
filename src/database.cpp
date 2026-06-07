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

#ifndef ER_LOCK_DEADLOCK
#define ER_LOCK_DEADLOCK 1213
#endif
#ifndef ER_LOCK_WAIT_TIMEOUT
#define ER_LOCK_WAIT_TIMEOUT 1205
#endif
#include "logger.h"
#include <fmt/format.h>
#include <algorithm>
#include <cctype>

static constexpr int MAX_RECONNECT_ATTEMPTS = 10;
static constexpr unsigned int MYSQL_TIMEOUT_SECONDS = 30;
static constexpr uint64_t DB_INSERT_PACKET_SAFETY_MARGIN = 4096;

namespace {
thread_local std::vector<std::string>* tlsQueryCapture = nullptr;

#ifdef STATS_ENABLED
void recordSqlStats(std::string_view query, std::chrono::steady_clock::time_point start)
{
	uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
	g_stats.addSqlStats(std::make_unique<Stat>(ns, std::string(query.substr(0, 100)), std::string(query.substr(0, 256))));
}
#endif

struct ThreadCleanup
{
	bool init()
	{
		if (initialized) {
			return true;
		}

		if (mysql_thread_init() != 0) {
			return false;
		}

		initialized = true;
		return true;
	}

	void shutdown()
	{
		if (!initialized) {
			return;
		}

		mysql_thread_end();
		initialized = false;
	}

	~ThreadCleanup()
	{
		shutdown();
	}

private:
	bool initialized = false;
};

ThreadCleanup& getThreadCleanup()
{
	static thread_local ThreadCleanup cleanup;
	return cleanup;
}
} // namespace

static tfs::detail::Mysql_ptr connectToDatabase(const Database::ConnectionParams& params, const bool retryIfError)
{
	for (int retryCount = 0; ; ++retryCount) {
		if (retryCount > 0) {
			if (!retryIfError || retryCount > MAX_RECONNECT_ATTEMPTS) {
				if (retryIfError) {
					LOG_ERROR(fmt::format(">> Database: failed to connect after {} attempts. Giving up.", MAX_RECONNECT_ATTEMPTS));
				}
				return nullptr;
			}
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}

		tfs::detail::Mysql_ptr handle{mysql_init(nullptr)};
		if (!handle) {
			LOG_ERROR(">> Database: failed to initialize MySQL connection handle.");
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
		const char* socket = params.socket.empty() ? nullptr : params.socket.c_str();
		if (!mysql_real_connect(handle.get(), params.host.c_str(), params.user.c_str(), params.password.c_str(),
		                        params.database.c_str(), static_cast<unsigned int>(params.port), socket, 0)) {
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
	static std::once_flag libraryInitFlag;
	std::call_once(libraryInitFlag, [this]() {
		if (mysql_library_init(0, nullptr, nullptr) != 0) {
			LOG_ERROR("Failed to initialize the MySQL client library.");
			return;
		}

		libraryInitialized = true;
		LOG_INFO(">> Database running in per-thread connection mode (one MySQL connection per worker thread).");
	});

	if (!libraryInitialized) {
		return false;
	}

	connectionParams = ConnectionParams{
		std::string(getString(ConfigManager::MYSQL_HOST)),
		std::string(getString(ConfigManager::MYSQL_USER)),
		std::string(getString(ConfigManager::MYSQL_PASS)),
		std::string(getString(ConfigManager::MYSQL_DB)),
		std::string(getString(ConfigManager::MYSQL_SOCK)),
		static_cast<int>(getInteger(ConfigManager::SQL_PORT))
	};

	ConnectionContext& ctx = getContext();
	if (!ctx.handle) {
		return false;
	}

	return true;
}

bool Database::establishConnection(ConnectionContext& ctx, const bool retryIfError) const
{
	if (!connectionParams) {
		LOG_ERROR(">> Database: connection parameters not initialized.");
		return false;
	}

	if (!getThreadCleanup().init()) {
		LOG_ERROR(">> Database: failed to initialize MySQL thread state.");
		return false;
	}

	ctx.handle = connectToDatabase(*connectionParams, retryIfError);
	ctx.lastErrno = 0;
	ctx.inTransaction = false;
	if (!ctx.handle) {
		return false;
	}

	static constexpr std::string_view maxPacketQuery = "SHOW VARIABLES LIKE 'max_allowed_packet'";
	if (mysql_real_query(ctx.handle.get(), maxPacketQuery.data(), maxPacketQuery.size()) == 0) {
		tfs::detail::MysqlResult_ptr res{mysql_store_result(ctx.handle.get())};
		if (res) {
			DBResult result{std::move(res)};
			if (result.hasNext()) {
				ctx.maxPacketSize = result.getNumber<uint64_t>("Value");
			}
		}
	}

	return true;
}

Database::ConnectionContext& Database::getContext() const
{
	thread_local ConnectionContext* tlsContext = nullptr;
	thread_local std::unique_ptr<ConnectionContext> failedContext;
	if (tlsContext) {
		return *tlsContext;
	}

	auto context = std::make_unique<ConnectionContext>();
	ConnectionContext* contextPtr = context.get();
	if (!establishConnection(*contextPtr, false)) {
		failedContext = std::move(context);
		LOG_ERROR(">> Database: failed to open MySQL connection.");
		return *failedContext;
	}

	size_t connectionNumber = 0;
	{
		std::scoped_lock lock{connectionsMutex};
		connections.push_back(std::move(context));
		connectionNumber = connections.size();
	}

	tlsContext = contextPtr;
	LOG_INFO(fmt::format(">> Database: opened MySQL connection #{}.", connectionNumber));
	return *tlsContext;
}

bool Database::reconnect(ConnectionContext& ctx) const
{
	LOG_WARN(">> Database: lost connection, attempting reconnect...");

	ctx.handle.reset();
	const bool success = establishConnection(ctx, true);
	if (success) {
		LOG_INFO(">> Database: reconnected successfully.");
	}
	return success;
}

void Database::shutdown()
{
    Database& db = getInstance();
	{
		std::scoped_lock lock{db.connectionsMutex};
		if (!db.connections.empty()) {
			LOG_INFO(fmt::format(">> Database: closing {} MySQL connection(s).", db.connections.size()));
		}
		db.connections.clear();
	}

	if (db.libraryInitialized) {
		getThreadCleanup().shutdown();
		mysql_library_end();
		db.libraryInitialized = false;
	}
}

bool Database::beginTransaction()
{
	ConnectionContext& ctx = getContext();
	ctx.lastErrno = 0;
	const bool result = executeQuery("START TRANSACTION");
	if (result) {
		ctx.inTransaction = true;
	}
	return result;
}

bool Database::rollback()
{
	ConnectionContext& ctx = getContext();
	if (!ctx.handle) {
		LOG_ERROR(">> Database: not initialized.");
		return false;
	}

	const bool result = mysql_rollback(ctx.handle.get()) == 0;
	if (!result) {
		ctx.lastErrno = mysql_errno(ctx.handle.get());
		LOG_ERROR(fmt::format("[Error - mysql_rollback] Message: {}", mysql_error(ctx.handle.get())));
	}
	ctx.inTransaction = false;
	return result;
}

bool Database::commit()
{
	ConnectionContext& ctx = getContext();
	if (!ctx.handle) {
		LOG_ERROR(">> Database: not initialized.");
		return false;
	}

	const bool result = mysql_commit(ctx.handle.get()) == 0;
	if (!result) {
		ctx.lastErrno = mysql_errno(ctx.handle.get());
		LOG_ERROR(fmt::format("[Error - mysql_commit] Message: {}", mysql_error(ctx.handle.get())));
	}
	ctx.inTransaction = false;
	return result;
}

bool Database::executeQuery(std::string_view query)
{
	if (tlsQueryCapture) {
		tlsQueryCapture->emplace_back(query);
		return true;
	}

	ConnectionContext& ctx = getContext();
	if (!ctx.handle) {
		LOG_ERROR(">> Database: not initialized.");
		return false;
	}

#ifdef STATS_ENABLED
	auto time_point = std::chrono::steady_clock::now();
#endif

	bool success = ::executeQuery(ctx.handle, query, false);

	if (!success) {
		const unsigned int mysqlError = mysql_errno(ctx.handle.get());
		ctx.lastErrno = mysqlError;
		if (!ctx.inTransaction && isLostConnectionError(mysqlError)) {
			LOG_WARN(fmt::format(">> Database: lost connection during executeQuery (error {}), attempting reconnect...", mysqlError));
			if (reconnect(ctx)) {
				success = ::executeQuery(ctx.handle, query, false);
				if (!success) {
					ctx.lastErrno = mysql_errno(ctx.handle.get());
					logQueryError(ctx.handle, query);
				} else {
					ctx.lastErrno = 0;
				}
			}
		} else {
			logQueryError(ctx.handle, query);
		}
	} else {
		ctx.lastErrno = 0;
	}

	// executeQuery can be called with command that produces result (e.g. SELECT)
	// we have to store that result, even though we do not need it, otherwise handle will get blocked
	if (success) {
		auto mysql_res = mysql_store_result(ctx.handle.get());
		mysql_free_result(mysql_res);
	}

	// Track raw SQL transaction state to prevent reconnect during transactions
	if (success) {
		std::string_view q{query};
		auto isDelim = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';'; };
		while (!q.empty() && isDelim(q.front())) {
			q.remove_prefix(1);
		}

		auto wordEnd = std::string_view::npos;
		for (size_t i = 0; i < q.size(); ++i) {
			if (isDelim(q[i])) { wordEnd = i; break; }
		}
		std::string_view firstWord = (wordEnd == std::string_view::npos) ? q : q.substr(0, wordEnd);

		char upper[9];
		size_t len = std::min(firstWord.size(), sizeof(upper));
		for (size_t i = 0; i < len; ++i) {
			upper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(firstWord[i])));
		}
		std::string_view cmd{upper, len};

		if (cmd == "START" || cmd == "BEGIN") {
			ctx.inTransaction = true;
		} else if (cmd == "COMMIT") {
			ctx.inTransaction = false;
		} else if (cmd == "ROLLBACK") {
			// ROLLBACK TO SAVEPOINT does not end the transaction
			auto afterFirst = q.substr(firstWord.size());
			while (!afterFirst.empty() && isDelim(afterFirst.front())) {
				afterFirst.remove_prefix(1);
			}
			auto secondEnd = std::string_view::npos;
			for (size_t i = 0; i < afterFirst.size(); ++i) {
				if (isDelim(afterFirst[i])) { secondEnd = i; break; }
			}
			std::string_view secondWord = (secondEnd == std::string_view::npos) ? afterFirst : afterFirst.substr(0, secondEnd);
			char upper2[3];
			size_t len2 = std::min(secondWord.size(), sizeof(upper2));
			for (size_t i = 0; i < len2; ++i) {
				upper2[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(secondWord[i])));
			}
			if (len2 < 2 || std::string_view{upper2, len2} != "TO") {
				ctx.inTransaction = false;
			}
		}
	}

#ifdef STATS_ENABLED
	recordSqlStats(query, time_point);
#endif

	return success;
}

DBResult_ptr Database::storeQuery(std::string_view query)
{
	ConnectionContext& ctx = getContext();
	if (!ctx.handle) {
		LOG_ERROR(">> Database: not initialized.");
		return nullptr;
	}

#ifdef STATS_ENABLED
	auto time_point = std::chrono::steady_clock::now();
#endif

	tfs::detail::MysqlResult_ptr res;

	if (!::executeQuery(ctx.handle, query, false)) {
		const unsigned int mysqlError = mysql_errno(ctx.handle.get());
		ctx.lastErrno = mysqlError;
		if (ctx.inTransaction || !isLostConnectionError(mysqlError)) {
			logQueryError(ctx.handle, query);
			return nullptr;
		}
		// Lost connection: reconnect once and retry.
		LOG_WARN(fmt::format(">> Database: lost connection during storeQuery (error {}), attempting reconnect...", mysqlError));
		if (!reconnect(ctx)) {
			return nullptr;
		}
		if (!::executeQuery(ctx.handle, query, false)) {
			ctx.lastErrno = mysql_errno(ctx.handle.get());
			logQueryError(ctx.handle, query);
			return nullptr;
		}
		ctx.lastErrno = 0;
	} else {
		ctx.lastErrno = 0;
	}

	// we should call that every time as someone would call executeQuery('SELECT...')
	// as it is described in MySQL manual: "it doesn't hurt" :P
	res.reset(mysql_store_result(ctx.handle.get()));

	if (!res) {
		LOG_ERROR(fmt::format("[Error - mysql_store_result] Query: {}\nMessage: {}", query, mysql_error(ctx.handle.get())));
		return nullptr;
	}

#ifdef STATS_ENABLED
	recordSqlStats(query, time_point);
#endif

	// retrieving results of query
	DBResult_ptr result = std::make_shared<DBResult>(std::move(res));
	if (!result->hasNext()) {
		return nullptr;
	}
	return result;
}

uint64_t Database::getLastInsertId() const
{
	ConnectionContext& ctx = getContext();
	if (!ctx.handle) {
		LOG_ERROR(">> Database: connection not established, cannot get last insert id.");
		return 0;
	}
	return mysql_insert_id(ctx.handle.get());
}

uint64_t Database::getMaxPacketSize() const
{
	return getContext().maxPacketSize;
}

bool Database::isInTransaction() const
{
	return getContext().inTransaction;
}

uint64_t Database::getAffectedRows() const
{
	ConnectionContext& ctx = getContext();
	if (!ctx.handle) {
		return 0;
	}
	const auto rows = mysql_affected_rows(ctx.handle.get());
	if (rows == static_cast<std::remove_const_t<decltype(rows)>>(-1) || ctx.lastErrno != 0) {
		return 0;
	}
	return static_cast<uint64_t>(rows);
}

bool Database::lastQueryWasDeadlock() const
{
	const unsigned int error = getContext().lastErrno;
	return error == ER_LOCK_DEADLOCK || error == ER_LOCK_WAIT_TIMEOUT;
}

void Database::beginQueryCapture(std::vector<std::string>* buffer)
{
	assert(tlsQueryCapture == nullptr && "nested query capture is not supported");
	tlsQueryCapture = buffer;
}

void Database::endQueryCapture()
{
	assert(tlsQueryCapture != nullptr && "endQueryCapture without matching begin");
	tlsQueryCapture = nullptr;
}

std::string Database::escapeString(std::string_view s) const { return escapeBlob(s.data(), s.length()); }

std::string Database::escapeBlob(const char* s, uint32_t length) const
{
	ConnectionContext& ctx = getContext();
	if (!ctx.handle) {
		LOG_ERROR(">> Database: connection not established, cannot escape blob.");
		return "''";
	}

	// the worst case is 2n + 1
	size_t maxLength = (length * 2) + 1;

	std::string escaped;
	escaped.reserve(maxLength + 2);
	escaped.push_back('\'');

	if (length != 0) {
		std::vector<char> output(maxLength);
		const unsigned long escapedLength = mysql_real_escape_string(ctx.handle.get(), output.data(), s, length);
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
