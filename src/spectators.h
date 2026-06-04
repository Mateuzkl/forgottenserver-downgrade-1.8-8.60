// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_SPECTATORS_H
#define FS_SPECTATORS_H

#include <absl/container/flat_hash_set.h>

#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <span>
#include <vector>

class Creature;
class Player;

class SpectatorVec
{
	using Vec = std::vector<std::shared_ptr<Creature>>;
	using Iterator = Vec::iterator;
	using ConstIterator = Vec::const_iterator;

public:
	SpectatorVec() { vec.reserve(32); }

	void addSpectators(const SpectatorVec& spectators)
	{
		absl::flat_hash_set<Creature*> existing;
		existing.reserve(vec.size() + spectators.vec.size());
		for (const auto& spectator : vec) {
			existing.insert(spectator.get());
		}

		for (const auto& spectator : spectators.vec) {
			if (existing.insert(spectator.get()).second) {
				vec.emplace_back(spectator);
			}
		}
		partitioned_ = false;
	}

	void erase(Creature* spectator)
	{
		auto it = std::find_if(vec.begin(), vec.end(),
			[spectator](const auto& sp) { return sp.get() == spectator; });
		if (it == vec.end()) {
			return;
		}
		std::iter_swap(it, vec.end() - 1);
		vec.pop_back();
		partitioned_ = false;
	}

	void partitionByType();
	void filterPlayers(std::function<bool(const Player*)> pred);

	std::span<std::shared_ptr<Creature>> players() {
		assert(partitioned_);
		return {vec.data(), playerEnd_};
	}
	std::span<std::shared_ptr<Creature>> monsters() {
		assert(partitioned_);
		return {vec.data() + playerEnd_, monsterEnd_ - playerEnd_};
	}
	std::span<std::shared_ptr<Creature>> npcs() {
		assert(partitioned_);
		return {vec.data() + monsterEnd_, vec.size() - monsterEnd_};
	}

	std::span<const std::shared_ptr<Creature>> players() const {
		assert(partitioned_);
		return {vec.data(), playerEnd_};
	}
	std::span<const std::shared_ptr<Creature>> monsters() const {
		assert(partitioned_);
		return {vec.data() + playerEnd_, monsterEnd_ - playerEnd_};
	}
	std::span<const std::shared_ptr<Creature>> npcs() const {
		assert(partitioned_);
		return {vec.data() + monsterEnd_, vec.size() - monsterEnd_};
	}

	size_t size() const { return vec.size(); }
	bool empty() const { return vec.empty(); }
	std::shared_ptr<Creature>& operator[](size_t index) { return vec[index]; }
	const std::shared_ptr<Creature>& operator[](size_t index) const { return vec[index]; }
	Iterator begin() { return vec.begin(); }
	ConstIterator begin() const { return vec.begin(); }
	Iterator end() { return vec.end(); }
	ConstIterator end() const { return vec.end(); }
	void emplace_back(std::shared_ptr<Creature> c) { if (c) { vec.emplace_back(std::move(c)); partitioned_ = false; } }
	void emplaceNull() { vec.emplace_back(); partitioned_ = false; }

private:
	Vec vec;
	size_t playerEnd_ = 0;
	size_t monsterEnd_ = 0;
	bool partitioned_ = false;
};

#endif // FS_SPECTATORS_H
