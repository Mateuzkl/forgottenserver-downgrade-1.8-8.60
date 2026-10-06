// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License in the LICENSE file.
#ifndef FS_SAVE_JOURNAL_H
#define FS_SAVE_JOURNAL_H

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tfs::save {

// A versioned, length-delimited payload. SQL is never split on semicolons or NULs.
// The journal also stores SHA-256 separately and checks it before decoding/replay.
inline std::optional<std::string> encode(uint32_t guid, uint64_t generation, const std::vector<std::string>& queries)
{
	if (guid == 0 || generation == 0 || queries.empty() || queries.size() > 100000) {
		return {};
	}
	std::string payload = "TFSSAVE1";
	auto append = [&payload](uint64_t value, unsigned bytes) {
		for (unsigned i = 0; i < bytes; ++i) {
			payload.push_back(static_cast<char>(value >> (i * 8)));
		}
	};
	append(guid, 4);
	append(generation, 8);
	append(queries.size(), 4);
	for (const auto& query : queries) {
		if (query.empty() || query.size() > std::numeric_limits<uint32_t>::max()) {
			return {};
		}
		append(query.size(), 4);
		payload.append(query);
	}
	return payload;
}

inline std::optional<std::vector<std::string>> decode(std::string_view payload, uint32_t guid, uint64_t generation)
{
	if (!payload.starts_with("TFSSAVE1") || guid == 0 || generation == 0) {
		return {};
	}
	payload.remove_prefix(8);
	auto take = [&payload](unsigned bytes) -> std::optional<uint64_t> {
		if (payload.size() < bytes) {
			return {};
		}
		uint64_t value = 0;
		for (unsigned i = 0; i < bytes; ++i) {
			value |= static_cast<uint64_t>(static_cast<unsigned char>(payload[i])) << (i * 8);
		}
		payload.remove_prefix(bytes);
		return value;
	};
	const auto storedGuid = take(4);
	const auto storedGeneration = take(8);
	const auto count = take(4);
	if (storedGuid != guid || storedGeneration != generation || !count || *count == 0 || *count > 100000) {
		return {};
	}
	std::vector<std::string> queries;
	for (uint64_t i = 0; i < *count; ++i) {
		const auto size = take(4);
		if (!size || *size == 0 || *size > payload.size()) {
			return {};
		}
		queries.emplace_back(payload.substr(0, *size));
		payload.remove_prefix(*size);
	}
	if (!payload.empty()) {
		return {};
	}
	return queries;
}

} // namespace tfs::save
#endif
