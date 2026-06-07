// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_DATABASE_H
#define FS_DATABASE_H

#include "pugicast.h"
#include <mysql/mysql.h>
#include "logger.h"
#include <fmt/format.h>
#include <cassert>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

class DBResult;
using DBResult_ptr = std::shared_ptr<DBResult>;

namespace tfs::detail {

struct MysqlDeleter
{
	void operator()(MYSQL* handle) const { mysql_close(handle); }
	void operator()(MYSQL_RES* handle) const { mysql_free_result(handle); }
};

using Mysql_ptr = std::unique_ptr<MYSQL, MysqlDeleter>;
using MysqlResult_ptr = std::unique_ptr<MYSQL_RES, MysqlDeleter>;

} // namespace tfs::detail

class Database
{
public:
	/**
	 * Singleton implementation.
	 *
	 * @return database connection handler singleton
	 */
	static Database& getInstance()
	{
		static Database instance;
		return instance;
	}

	/**
	 * Connects to the database
	 *
	 * @return true on successful connection, false on error
	 */
	bool connect();

	/**
	 * Executes command.
	 *
	 * Executes query which doesn't generates results (eg. INSERT, UPDATE, DELETE...).
	 *
	 * @param query command
	 * @return true on success, false on error
	 */
	bool executeQuery(std::string_view query);

	/**
	 * Queries database.
	 *
	 * Executes query which generates results (mostly SELECT).
	 *
	 * @return results object (nullptr on error)
	 */
	DBResult_ptr storeQuery(std::string_view query);

	/**
	 * Escapes string for query.
	 *
	 * Prepares string to fit SQL queries including quoting it.
	 *
	 * @param s string to be escaped
	 * @return quoted string
	 */
	std::string escapeString(std::string_view s) const;

	/**
	 * Escapes binary stream for query.
	 *
	 * Prepares binary stream to fit SQL queries.
	 *
	 * @param s binary stream
	 * @param length stream length
	 * @return quoted string
	 */
	std::string escapeBlob(const char* s, uint32_t length) const;

	/**
	 * Retrieve id of last inserted row
	 *
	 * @return id on success, 0 if last query did not result on any rows with auto_increment keys
	 */
	uint64_t getLastInsertId() const;

	/**
	 * Get database engine version
	 *
	 * @return the database engine version
	 */
	static const char* getClientVersion() { return mysql_get_client_info(); }

	uint64_t getMaxPacketSize() const;

	unsigned int getLastErrno() const { return getContext().lastErrno; }

	uint64_t getAffectedRows() const;

	[[nodiscard]] bool lastQueryWasDeadlock() const;

	void beginQueryCapture(std::vector<std::string>* buffer);
	void endQueryCapture();

	/**
	 * Shutdown the database connection and cleanup MySQL library.
	 * Should be called before program termination.
	 */
	static void shutdown();

	bool beginTransaction();
	bool rollback();
	bool commit();
	[[nodiscard]] bool isInTransaction() const;

	struct ConnectionParams
	{
		std::string host;
		std::string user;
		std::string password;
		std::string database;
		std::string socket;
		int port = 0;
	};

private:
	struct ConnectionContext
	{
		ConnectionContext() = default;
		ConnectionContext(const ConnectionContext&) = delete;
		ConnectionContext& operator=(const ConnectionContext&) = delete;
		ConnectionContext(ConnectionContext&&) = delete;
		ConnectionContext& operator=(ConnectionContext&&) = delete;
		~ConnectionContext() = default;

		tfs::detail::Mysql_ptr handle = nullptr;
		uint64_t maxPacketSize = 1048576;
		unsigned int lastErrno = 0;
		bool inTransaction = false;
	};

	ConnectionContext& getContext() const;
	bool establishConnection(ConnectionContext& ctx, bool retryIfError) const;
	/**
	 * Reconnects the calling thread's database context using saved credentials.
	 * Replaces that thread's handle on success.
	 * @return true on successful reconnect, false on error.
	 */
	bool reconnect(ConnectionContext& ctx) const;

	mutable std::optional<ConnectionParams> connectionParams;
	mutable std::mutex connectionsMutex;
	mutable std::vector<std::unique_ptr<ConnectionContext>> connections;
	bool libraryInitialized = false;

	friend class DBTransaction;
};

class QueryCaptureScope
{
public:
	explicit QueryCaptureScope(std::vector<std::string>& buffer)
	{
		Database::getInstance().beginQueryCapture(&buffer);
	}

	~QueryCaptureScope()
	{
		Database::getInstance().endQueryCapture();
	}

	QueryCaptureScope(const QueryCaptureScope&) = delete;
	QueryCaptureScope& operator=(const QueryCaptureScope&) = delete;
};

class DBResult
{
public:
	explicit DBResult(tfs::detail::MysqlResult_ptr&& res);

	// non-copyable
	DBResult(const DBResult&) = delete;
	DBResult& operator=(const DBResult&) = delete;

	template <typename T>
	T getNumber(const std::string& s) const
	{
		auto it = listNames.find(s);
		if (it == listNames.end()) {
			LOG_ERROR(fmt::format("[Error - DBResult::getNumber] Column '{}' doesn't exist in the result set", s));
			return {};
		}

		if (row[it->second] == nullptr) {
			return {};
		}

		return pugi::cast<T>(row[it->second]);
	}

	std::string_view getString(std::string_view column) const;
	std::string_view getStream(std::string_view column, unsigned long& size) const;

	bool hasNext() const;
	bool next();

private:
	tfs::detail::MysqlResult_ptr handle;
	MYSQL_ROW row;

	std::map<std::string_view, size_t> listNames;

	friend class Database;
};

/**
 * INSERT statement.
 */
class DBInsert
{
public:
	explicit DBInsert(std::string_view query);
	bool addRow(std::string_view row);
	bool addRow(std::ostringstream& row);
	bool execute();

	void upsert(const std::vector<std::string>& columns);

private:
	std::string query;
	std::string values;
	std::string upsertClause;
	size_t length;
};

class DBTransaction
{
public:
	constexpr DBTransaction() = default;

	~DBTransaction()
	{
		if (state == STATE_START) {
			Database::getInstance().rollback();
		}
	}

	// non-copyable
	DBTransaction(const DBTransaction&) = delete;
	DBTransaction& operator=(const DBTransaction&) = delete;

	bool begin()
	{
		if (!Database::getInstance().beginTransaction()) {
			state = STATE_NO_START;
			return false;
		}
		state = STATE_START;
		return true;
	}

	bool commit()
	{
		if (state != STATE_START) {
			return false;
		}

		if (!Database::getInstance().commit()) {
			return false;
		}

		state = STATE_COMMIT;
		return true;
	}

	bool rollback()
	{
		if (state != STATE_START) {
			return false;
		}

		state = STATE_NO_START;
		return Database::getInstance().rollback();
	}

	static constexpr uint8_t TRANSACTION_MAX_ATTEMPTS = 3;

	// DBTransaction may run callback up to TRANSACTION_MAX_ATTEMPTS when
	// Database::lastQueryWasDeadlock() reports a deadlock or lock timeout.
	// The callback must be side-effect free or idempotent: only re-applicable DB
	// statements, no external I/O, and no non-DB state mutations.
	template <typename Func>
	static bool executeWithinTransactionRollbackOnFailure(const Func& callback)
	{
		for (uint8_t attempt = 1; attempt <= TRANSACTION_MAX_ATTEMPTS; ++attempt) {
			DBTransaction transaction;
			if (!transaction.begin()) {
				LOG_ERROR("[DBTransaction] Failed to begin transaction.");
				return false;
			}

			try {
				if (!callback()) {
					transaction.rollback();
					if (Database::getInstance().lastQueryWasDeadlock() && attempt < TRANSACTION_MAX_ATTEMPTS) {
						LOG_WARN(fmt::format("[DBTransaction] Transaction deadlock/lock timeout, retrying ({}/{})",
						                     attempt, TRANSACTION_MAX_ATTEMPTS));
						continue;
					}
					return false;
				}

				if (transaction.commit()) {
					return true;
				}

				if (Database::getInstance().lastQueryWasDeadlock() && attempt < TRANSACTION_MAX_ATTEMPTS) {
					LOG_WARN(fmt::format("[DBTransaction] Transaction commit deadlock/lock timeout, retrying ({}/{})",
					                     attempt, TRANSACTION_MAX_ATTEMPTS));
					continue;
				}
				return false;
			} catch (const std::exception& e) {
				transaction.rollback();
				LOG_ERROR(fmt::format("[DBTransaction] Exception during transaction: {}", e.what()));
				return false;
			}
		}

		return false;
	}

private:
	enum TransactionStates_t
	{
		STATE_NO_START,
		STATE_START,
		STATE_COMMIT,
	};

	TransactionStates_t state = STATE_NO_START;
};

#endif
