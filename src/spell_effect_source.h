// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License.
#pragma once

class Creature;

// Attribute synchronous Lua spell effects without changing existing scripts.
// Nested casts restore the previous actor; no pointer survives the callback.
class SpellEffectSourceScope
{
public:
	explicit SpellEffectSourceScope(Creature* actor) : previous(currentActor) { currentActor = actor; }
	~SpellEffectSourceScope() { currentActor = previous; }
	SpellEffectSourceScope(const SpellEffectSourceScope&) = delete;
	SpellEffectSourceScope& operator=(const SpellEffectSourceScope&) = delete;
	static Creature* getActor() { return currentActor; }

private:
	Creature* previous;
	inline static thread_local Creature* currentActor = nullptr;
};
