Rarity = {}

function Rarity:onAttackProc(player, target, item, statKey, combatType, damage)
	if hasEvent.onAttackProc then
		return Event.onAttackProc(player, target, item, statKey, combatType, damage)
	end
	return true
end

function Rarity:onHitProc(player, target, item, statKey, combatType, damage)
	if hasEvent.onHitProc then
		return Event.onHitProc(player, target, item, statKey, combatType, damage)
	end
	return true
end

function Rarity:onDoubleDamage(player)
	if hasEvent.onDoubleDamage then
		return Event.onDoubleDamage(player)
	end
	return true
end

function Rarity:onElementalDamage(player, item, fireDmg)
	if hasEvent.onElementalDamage then
		return Event.onElementalDamage(player, item, fireDmg)
	end
	return true
end

function Rarity:onKillProc(player, target, item, statKey, value)
	if hasEvent.onKillProc then
		return Event.onKillProc(player, target, item, statKey, value)
	end
	return true
end
