// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "spectators.h"

#include "creature.h"

void SpectatorVec::partitionByType()
{
	vec.erase(std::remove_if(vec.begin(), vec.end(),
		[](const auto& c) { return !c; }), vec.end());

	auto playersEnd = std::partition(vec.begin(), vec.end(),
		[](const auto& c) { return c->isPlayer(); });
	auto monstersEnd = std::partition(playersEnd, vec.end(),
		[](const auto& c) { return c->isMonster(); });

	playerEnd_ = static_cast<size_t>(playersEnd - vec.begin());
	monsterEnd_ = static_cast<size_t>(monstersEnd - vec.begin());
	partitioned_ = true;
}
