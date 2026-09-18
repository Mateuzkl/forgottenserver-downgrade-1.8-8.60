// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_KV_H
#define FS_KV_H

#include <string>
#include <mutex>
#include <initializer_list>
#include <optional>
#include <unordered_set>
#include <list>
#include <utility>
#include <vector>
#include <memory>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <unordered_map>

#include "kv/value_wrapper.h"

class DBInsert;

class KV : public std::enable_shared_from_this<KV> {
public:
	virtual ~KV() = default;

	virtual void set(const std::string &key, const std::initializer_list<ValueWrapper> &init_list) = 0;
	virtual void set(const std::string &key, const std::initializer_list<std::pair<const std::string, ValueWrapper>> &init_list) = 0;
	virtual void set(const std::string &key, const ValueWrapper &value) = 0;

	virtual std::optional<ValueWrapper> get(const std::string &key, bool forceLoad = false) = 0;

	virtual bool saveAll() {
		return true;
	}

	virtual std::shared_ptr<KV> scoped(const std::string &scope) = 0;

	virtual std::unordered_set<std::string> keys(const std::string &prefix = "") = 0;

	void remove(const std::string &key);

	virtual void flush() {
		saveAll();
	}

	static std::string generateUUID();

private:
	static int64_t lastTimestamp_;
	static uint64_t counter_;
	static std::mutex mutex_;
};

class KVStore : public KV {
public:
	static constexpr size_t MAX_SIZE = 1000000;
	static KVStore &getInstance();

	KVStore();

	// KV interface
	void set(const std::string &key, const std::initializer_list<ValueWrapper> &init_list) override;
	void set(const std::string &key, const std::initializer_list<std::pair<const std::string, ValueWrapper>> &init_list) override;
	void set(const std::string &key, const ValueWrapper &value) override;

	std::optional<ValueWrapper> get(const std::string &key, bool forceLoad = false) override;

	void flush() override;
	bool saveAll() override;

	std::shared_ptr<KV> scoped(const std::string &scope) final;
	std::unordered_set<std::string> keys(const std::string &prefix = "") override;

	// SQL persistence
	std::optional<ValueWrapper> load(const std::string &key);
	bool save(const std::string &key, const ValueWrapper &value);
	[[nodiscard]] bool buildBatchSaveQuery(const std::vector<std::pair<std::string, ValueWrapper>> &entries,
	                                     std::string &query) const;
	std::vector<std::string> loadPrefix(const std::string &prefix = "");

protected:
	using StoreEntry = std::pair<ValueWrapper, std::list<std::string>::iterator>;
	using StoreMap = std::unordered_map<std::string, StoreEntry>;
	StoreMap getStore();

private:
	void setLocked(const std::string &key, const ValueWrapper &value);
	bool processEvictions();

	bool prepareSave(const std::string &key, const ValueWrapper &value, DBInsert &update) const;
	DBInsert dbUpdate() const;

	StoreMap store_;
	std::list<std::string> lruQueue_;
	std::mutex mutex_;
	std::vector<std::pair<std::string, ValueWrapper>> pendingEvictions_;
};

class ScopedKV final : public KV {
public:
	ScopedKV(KVStore &rootKV, std::string prefix) :
		rootKV_(rootKV), prefix_(std::move(prefix)) { }

	void set(const std::string &key, const std::initializer_list<ValueWrapper> &init_list) override {
		rootKV_.set(buildKey(key), init_list);
	}
	void set(const std::string &key, const std::initializer_list<std::pair<const std::string, ValueWrapper>> &init_list) override {
		rootKV_.set(buildKey(key), init_list);
	}
	void set(const std::string &key, const ValueWrapper &value) override {
		rootKV_.set(buildKey(key), value);
	}

	std::optional<ValueWrapper> get(const std::string &key, bool forceLoad = false) override {
		return rootKV_.get(buildKey(key), forceLoad);
	}

	template <typename T>
	T get(const std::string &key, bool forceLoad = false) {
		const auto optValue = rootKV_.get(buildKey(key), forceLoad);
		if (optValue.has_value()) {
			return optValue->get<T>();
		}
		return T {};
	}

	bool saveAll() override {
		return rootKV_.saveAll();
	}

	std::shared_ptr<KV> scoped(const std::string &scope) override {
		return std::make_shared<ScopedKV>(rootKV_, buildKey(scope));
	}

	std::unordered_set<std::string> keys(const std::string &prefix = "") override {
		return rootKV_.keys(buildListKey(prefix));
	}

private:
	std::string buildKey(const std::string &key) const {
		if (key.empty()) return prefix_;
		return fmt::format("{}.{}", prefix_, key);
	}

	std::string buildListKey(const std::string &key) const {
		std::string listKey = buildKey(key);
		if (!listKey.empty() && listKey.back() != '.') {
			listKey.push_back('.');
		}
		return listKey;
	}

	KVStore &rootKV_;
	std::string prefix_;
};

#endif // FS_KV_H
