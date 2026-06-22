local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_HEALING)
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_MAGIC_GREEN)
combat:setParameter(COMBAT_PARAM_DISPEL, CONDITION_PARALYZE)
combat:setParameter(COMBAT_PARAM_AGGRESSIVE, false)

local function callback(player, level, magicLevel)
	local min = (level / 5) + (magicLevel * 6.3) + 45
	local max = (level / 5) + (magicLevel * 14.4) + 90
	return min, max
end

combat:setCallback(CallBackParam.LEVELMAGICVALUE, callback)

local SHARED_CONSERVATION_RATIO = 0.30

local sharedConservationCombat = Combat()
sharedConservationCombat:setParameter(COMBAT_PARAM_TYPE, COMBAT_HEALING)
sharedConservationCombat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_MAGIC_BLUE)
sharedConservationCombat:setParameter(COMBAT_PARAM_AGGRESSIVE, false)

local function sharedConservationCallback(player, level, magicLevel)
	local min = ((level / 5) + (magicLevel * 6.3) + 45) * SHARED_CONSERVATION_RATIO
	local max = ((level / 5) + (magicLevel * 14.4) + 90) * SHARED_CONSERVATION_RATIO
	return min, max
end

sharedConservationCombat:setCallback(CallBackParam.LEVELMAGICVALUE, sharedConservationCallback)

local function shareConservationHeal(player, primaryTargetId)
	if not player or player:getStance() ~= STANCE_SHARED_CONSERVATION then
		return
	end

	local party = player:getParty()
	if not party then
		return
	end

	local members = party:getMembers()
	table.insert(members, party:getLeader())

	local origin = player:getPosition()
	local best, bestDist
	for _, member in ipairs(members) do
		if member and member:isPlayer() and member:getId() ~= primaryTargetId and member:getId() ~= player:getId() and member:getHealth() > 0 then
			local pos = member:getPosition()
			if pos.z == origin.z then
				local dist = math.max(math.abs(pos.x - origin.x), math.abs(pos.y - origin.y))
				if not bestDist or dist < bestDist then
					best, bestDist = member, dist
				end
			end
		end
	end

	if best then
		sharedConservationCombat:execute(player, Variant(best:getId()))
	end
end

local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	creature:getPosition():sendMagicEffect(CONST_ME_MAGIC_BLUE)
	local result = combat:execute(creature, variant)
	shareConservationHeal(creature:getPlayer(), variant:getNumber())
	return result
end


spell:group("healing")
spell:id(124)
spell:name("Heal Friend")
spell:words("exura sio")
spell:level(18)
spell:mana(140)
spell:isPremium(true)
spell:needCasterTargetOrDirection(true)
spell:blockWalls(true)
spell:cooldown(1 * 1000)
spell:groupCooldown(1 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:playerNameParam(true)
spell:hasParams(true)
spell:vocation("druid", "elder druid")
spell:register()
