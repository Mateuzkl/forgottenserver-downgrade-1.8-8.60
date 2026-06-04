// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "spectators.h"

#include "creature.h"
#include "player.h"

void SpectatorVec::partitionByType()
{
	vec.erase(std::remove_if(vec.begin(), vec.end(),
		[](const auto& c) { return !c; }), vec.end());

	auto playersEnd = std::partition(vec.begin(), vec.end(),
		[](const auto& c) { return c->getPlayer() != nullptr; });
	auto monstersEnd = std::partition(playersEnd, vec.end(),
		[](const auto& c) { return c->getMonster() != nullptr; });

	playerEnd_ = static_cast<size_t>(playersEnd - vec.begin());
	monsterEnd_ = static_cast<size_t>(monstersEnd - vec.begin());
	partitioned_ = true;
}

void SpectatorVec::filterPlayers(std::function<bool(const Player*)> pred)
{
	if (!partitioned_) {
		partitionByType();
	}
	assert(partitioned_);

	size_t writeIndex = 0;
	for (size_t readIndex = 0; readIndex < playerEnd_; ++readIndex) {
		const Player* player = vec[readIndex]->getPlayer();
		if (!pred(player)) {
			continue;
		}

		if (writeIndex != readIndex) {
			std::swap(vec[writeIndex], vec[readIndex]);
		}
		++writeIndex;
	}

	const size_t removedPlayers = playerEnd_ - writeIndex;
	if (removedPlayers == 0) {
		return;
	}

	vec.erase(vec.begin() + writeIndex, vec.begin() + playerEnd_);
	playerEnd_ = writeIndex;
	monsterEnd_ -= removedPlayers;
}
