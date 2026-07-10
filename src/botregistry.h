// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_BOTREGISTRY_H
#define FS_BOTREGISTRY_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tfs::bot {

// Parses a strictly numeric, non-zero player guid. Returns nullopt for empty
// input, non-digit characters, zero, or values that overflow uint32_t.
std::optional<uint32_t> parseGuid(std::string_view text);

// In-memory bookkeeping for active bot players. The registry holds the owning
// shared_ptr that keeps a socketless bot Player alive while it is placed on
// the map (Game only keeps weak_ptr entries for players). Templated on the
// player type so tests and benchmarks can exercise it with lightweight stubs
// that only provide isRemoved().
template <typename PlayerT>
class Registry
{
public:
	bool insert(uint32_t guid, std::shared_ptr<PlayerT> player)
	{
		if (guid == 0 || !player) {
			return false;
		}
		return bots.emplace(guid, std::move(player)).second;
	}

	std::shared_ptr<PlayerT> find(uint32_t guid) const
	{
		auto it = bots.find(guid);
		if (it == bots.end() || !it->second || it->second->isRemoved()) {
			return nullptr;
		}
		return it->second;
	}

	bool contains(uint32_t guid) const { return find(guid) != nullptr; }

	bool erase(uint32_t guid) { return bots.erase(guid) != 0; }

	// Drops entries whose player is gone or already removed from the game and
	// returns their guids so the caller can persist the despawn timestamps.
	std::vector<uint32_t> sweepRemoved()
	{
		std::vector<uint32_t> swept;
		for (auto it = bots.begin(); it != bots.end();) {
			if (!it->second || it->second->isRemoved()) {
				swept.push_back(it->first);
				it = bots.erase(it);
			} else {
				++it;
			}
		}
		return swept;
	}

	std::vector<std::shared_ptr<PlayerT>> snapshot() const
	{
		std::vector<std::shared_ptr<PlayerT>> out;
		out.reserve(bots.size());
		for (const auto& [guid, player] : bots) {
			if (player && !player->isRemoved()) {
				out.push_back(player);
			}
		}
		return out;
	}

	std::vector<uint32_t> guids() const
	{
		std::vector<uint32_t> out;
		out.reserve(bots.size());
		for (const auto& [guid, player] : bots) {
			if (player && !player->isRemoved()) {
				out.push_back(guid);
			}
		}
		return out;
	}

	size_t size() const noexcept { return bots.size(); }

private:
	std::unordered_map<uint32_t, std::shared_ptr<PlayerT>> bots;
};

} // namespace tfs::bot

#endif // FS_BOTREGISTRY_H
