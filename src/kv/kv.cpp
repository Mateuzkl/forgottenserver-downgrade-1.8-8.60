// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"
#include "kv/kv.h"
#include "database.h"
#include "logger.h"
#include <fmt/format.h>

int64_t KV::lastTimestamp_ = 0;
uint64_t KV::counter_ = 0;
std::mutex KV::mutex_ = {};

std::string KV::generateUUID() {
	std::lock_guard<std::mutex> lock(mutex_);

	const auto now = std::chrono::system_clock::now().time_since_epoch();
	const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

	if (milliseconds != lastTimestamp_) {
		counter_ = 0;
		lastTimestamp_ = milliseconds;
	} else {
		++counter_;
	}

	std::stringstream ss;
	ss << std::setw(20) << std::setfill('0') << milliseconds << "-"
	   << std::setw(12) << std::setfill('0') << counter_;

	return ss.str();
}

// ============ KVStore ============

KVStore::KVStore() = default;

KVStore &KVStore::getInstance() {
	static KVStore instance;
	return instance;
}

void KVStore::set(const std::string &key, const std::initializer_list<ValueWrapper> &init_list) {
	const ValueWrapper wrappedInitList(init_list);
	set(key, wrappedInitList);
}

void KVStore::set(const std::string &key, const std::initializer_list<std::pair<const std::string, ValueWrapper>> &init_list) {
	const ValueWrapper wrappedInitList(init_list);
	set(key, wrappedInitList);
}

void KVStore::set(const std::string &key, const ValueWrapper &value) {
	{
		std::scoped_lock lock(mutex_);
		setLocked(key, value);
	}
	processEvictions();
}

void KVStore::setLocked(const std::string &key, const ValueWrapper &value) {
	auto it = store_.find(key);
	if (it != store_.end()) {
		it->second.first = value;
		lruQueue_.splice(lruQueue_.begin(), lruQueue_, it->second.second);
	} else {
		std::string evictKey;
		ValueWrapper evictValue;
		bool needsEviction = false;

		if (store_.size() >= MAX_SIZE && !lruQueue_.empty()) {
			auto last = std::prev(lruQueue_.end());
			evictKey = *last;
			evictValue = store_[*last].first;
			needsEviction = true;
			store_.erase(*last);
			lruQueue_.pop_back();
		}

		lruQueue_.push_front(key);
		store_.try_emplace(key, std::make_pair(value, lruQueue_.begin()));

		if (needsEviction) {
			pendingEvictions_.emplace_back(evictKey, evictValue);
		}
	}
}

std::optional<ValueWrapper> KVStore::get(const std::string &key, bool forceLoad) {
	{
		std::scoped_lock lock(mutex_);
		if (!forceLoad) {
			auto it = store_.find(key);
			if (it != store_.end()) {
				auto &[value, lruIt] = it->second;
				if (value.isDeleted()) {
					lruQueue_.splice(lruQueue_.end(), lruQueue_, lruIt);
					return std::nullopt;
				}
				lruQueue_.splice(lruQueue_.begin(), lruQueue_, lruIt);
				return value;
			}
		}
	}

	auto value = load(key);
	if (value) {
		{
			std::scoped_lock lock(mutex_);
			if (store_.find(key) == store_.end()) {
				setLocked(key, *value);
			}
		}
		processEvictions();
	}
	return value;
}

std::unordered_set<std::string> KVStore::keys(const std::string &prefix) {
	std::unordered_set<std::string> keys;

	{
		std::scoped_lock lock(mutex_);
		for (const auto &[key, value] : store_) {
			if (key.find(prefix) == 0 && !value.first.isDeleted()) {
				std::string suffix = key.substr(prefix.size());
				keys.insert(suffix);
			}
		}
	}

	for (const auto &key : loadPrefix(prefix)) {
		{
			std::scoped_lock lock(mutex_);
			auto it = store_.find(prefix + key);
			if (it != store_.end() && it->second.first.isDeleted()) {
				continue;
			}
		}
		keys.insert(key);
	}

	return keys;
}

void KV::remove(const std::string &key) {
	set(key, ValueWrapper::deleted());
}

std::shared_ptr<KV> KVStore::scoped(const std::string &scope) {
	return std::make_shared<ScopedKV>(*this, scope);
}

bool KVStore::processEvictions() {
	std::vector<std::pair<std::string, ValueWrapper>> evictions;
	{
		std::scoped_lock lock(mutex_);
		if (!pendingEvictions_.empty()) {
			evictions = std::move(pendingEvictions_);
			pendingEvictions_.clear();
		}
	}

	bool success = true;
	for (const auto &[key, value] : evictions) {
		if (!save(key, value)) {
			success = false;
			std::scoped_lock lock(mutex_);
			pendingEvictions_.emplace_back(key, value);
		}
	}
	return success;
}

void KVStore::flush() {
	std::vector<std::pair<std::string, ValueWrapper>> snapshot;
	{
		std::scoped_lock lock(mutex_);
		snapshot.reserve(store_.size() + pendingEvictions_.size());
		for (const auto &[k, v] : store_) {
			snapshot.emplace_back(k, v.first);
		}
		snapshot.insert(snapshot.end(), pendingEvictions_.begin(), pendingEvictions_.end());
	}

	bool allSaved = true;
	for (const auto &[k, v] : snapshot) {
		if (!save(k, v)) {
			allSaved = false;
		}
	}

	if (allSaved) {
		std::scoped_lock lock(mutex_);
		store_.clear();
		pendingEvictions_.clear();
	}
}

KVStore::StoreMap KVStore::getStore() {
	std::scoped_lock lock(mutex_);
	StoreMap copy;
	for (const auto &[key, value] : store_) {
		copy.try_emplace(key, value);
	}
	return copy;
}

// ============ SQL Persistence ============

std::optional<ValueWrapper> KVStore::load(const std::string &key) {
	Database &db = Database::getInstance();
	const auto query = fmt::format("SELECT `key_name`, `timestamp`, `value` FROM `kv_store` WHERE `key_name` = {}", db.escapeString(key));
	const auto result = db.storeQuery(query);
	if (result == nullptr) {
		return std::nullopt;
	}

	unsigned long size = 0;
	auto data = result->getStream("value", size);
	if (data.data() == nullptr || size == 0) {
		return std::nullopt;
	}

	auto timestamp = result->getNumber<uint64_t>("timestamp");
	return ValueWrapper::deserialize(data.data(), size, timestamp);
}

std::vector<std::string> KVStore::loadPrefix(const std::string &prefix) {
	std::vector<std::string> keys;
	Database &db = Database::getInstance();

	// Escape LIKE metacharacters
	std::string escaped = prefix;
	for (auto pos = escaped.find_first_of(R"(\%_)"); pos != std::string::npos; pos = escaped.find_first_of(R"(\%_)", pos + 2)) {
		escaped.insert(pos, "\\");
	}

	std::string keySearch = db.escapeString(escaped + "%");
	const auto query = fmt::format("SELECT `key_name` FROM `kv_store` WHERE `key_name` LIKE {} ESCAPE '\\\\'", keySearch);
	const auto result = db.storeQuery(query);
	if (result == nullptr) {
		return keys;
	}

	do {
		std::string key(result->getString("key_name"));
		key.erase(0, prefix.size());
		keys.push_back(key);
	} while (result->next());

	return keys;
}

bool KVStore::save(const std::string &key, const ValueWrapper &value) {
	auto update = dbUpdate();
	if (!prepareSave(key, value, update)) {
		return false;
	}
	return update.execute();
}

bool KVStore::prepareSave(const std::string &key, const ValueWrapper &value, DBInsert &update) const {
	Database &db = Database::getInstance();

	if (value.isDeleted()) {
		const auto query = fmt::format("DELETE FROM `kv_store` WHERE `key_name` = {}", db.escapeString(key));
		return db.executeQuery(query);
	}

	const auto serialized = value.serialize();
	update.addRow(fmt::format("{}, {}, {}", db.escapeString(key), value.getTimestamp(), db.escapeBlob(serialized.data(), static_cast<uint32_t>(serialized.size()))));
	return true;
}

bool KVStore::saveAll() {
	// Drain pending evictions before the main cache snapshot.
	if (!processEvictions()) {
		g_logger().error("KVStore::saveAll() - Error saving pending KV evictions");
		return false;
	}

	auto store = getStore();

	DBTransaction transaction;
	if (!transaction.begin()) {
		g_logger().error("KVStore::saveAll() - Failed to start database transaction");
		return false;
	}

	auto update = dbUpdate();
	bool success = true;
	for (const auto &[key, value] : store) {
		if (!prepareSave(key, value.first, update)) {
			success = false;
			break;
		}
	}
	if (success) {
		success = update.execute();
	}
	if (success) {
		success = transaction.commit();
	}

	if (!success) {
		g_logger().error("KVStore::saveAll() - Error saving KV data");
	}

	return success;
}

DBInsert KVStore::dbUpdate() {
	auto insert = DBInsert("INSERT INTO `kv_store` (`key_name`, `timestamp`, `value`) VALUES");
	insert.upsert({ "key_name", "timestamp", "value" });
	return insert;
}
