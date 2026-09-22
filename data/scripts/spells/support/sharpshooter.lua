local WHEEL_AUGMENT_DEBUG = true
local SHARPSHOOTER_GRADE_II_DISTANCE = 5
local SHARPSHOOTER_FLAT_SUBID = 86063

local combat = Combat()
combat:setParameter(COMBAT_PARAM_EFFECT, 5)
combat:setParameter(COMBAT_PARAM_AGGRESSIVE, false)

local function getSharpshooterGrade(player)
	if not player.upgradeSpellsWOD then
		return 0
	end
	return player:upgradeSpellsWOD("Sharpshooter")
end

local function getEffectiveDistance(player)
	if player.getEffectiveSkillLevel then
		return player:getEffectiveSkillLevel(SKILL_DISTANCE)
	end
	return player:getSkillLevel(SKILL_DISTANCE)
end

local function isSharpshooterActive(player)
	return player:getCondition(CONDITION_ATTRIBUTES, CONDITIONID_COMBAT, AttrSubId_Sharpshooter) ~= nil
end

local function buildSharpshooterPercentCondition()
	local condition = Condition(CONDITION_ATTRIBUTES)
	condition:setParameter(CONDITION_PARAM_SUBID, AttrSubId_Sharpshooter)
	condition:setParameter(CONDITION_PARAM_TICKS, -1)
	condition:setParameter(CONDITION_PARAM_SKILL_DISTANCEPERCENT, 140)
	condition:setParameter(CONDITION_PARAM_BUFF_SPELL, true)
	return condition
end

local function buildSharpshooterFlatCondition()
	local condition = Condition(CONDITION_ATTRIBUTES)
	condition:setParameter(CONDITION_PARAM_SUBID, SHARPSHOOTER_FLAT_SUBID)
	condition:setParameter(CONDITION_PARAM_TICKS, -1)
	condition:setParameter(CONDITION_PARAM_SKILL_DISTANCE, SHARPSHOOTER_GRADE_II_DISTANCE)
	condition:setParameter(CONDITION_PARAM_BUFF_SPELL, true)
	return condition
end

local function removeSharpshooterConditions(player)
	player:removeCondition(CONDITION_ATTRIBUTES, CONDITIONID_COMBAT, AttrSubId_Sharpshooter)
	player:removeCondition(CONDITION_ATTRIBUTES, CONDITIONID_COMBAT, SHARPSHOOTER_FLAT_SUBID)
end

local function deactivateSharpshooter(player, reason)
	removeSharpshooterConditions(player)
	if player:getStance() == STANCE_SHARPSHOOTER then
		player:setStance(STANCE_NONE)
	end
	player:getPosition():sendMagicEffect(CONST_ME_POFF)

	if WHEEL_AUGMENT_DEBUG then
		print(string.format(
			"[wheel-aug][sharpshooter] player=%s action=%s grade=%d distance=%d",
			player:getName(),
			reason,
			getSharpshooterGrade(player),
			getEffectiveDistance(player)
		))
	end
end

local function applySharpshooterBuff(player, creature, variant)
	local grade = getSharpshooterGrade(player)
	local distanceBefore = getEffectiveDistance(player)

	if player:getStance() == STANCE_SHARPSHOOTER then
		player:setStance(STANCE_NONE)
	end

	removeSharpshooterConditions(player)
	combat:clearConditions()
	combat:addCondition(buildSharpshooterPercentCondition())
	if grade >= 2 then
		combat:addCondition(buildSharpshooterFlatCondition())
	end
	player:setStance(STANCE_SHARPSHOOTER)

	if not combat:execute(creature, variant) then
		removeSharpshooterConditions(player)
		player:setStance(STANCE_NONE)
		return false
	end

	if WHEEL_AUGMENT_DEBUG then
		print(string.format(
			"[wheel-aug][sharpshooter] player=%s action=activate grade=%d distance %d -> %d (+40%%%s)",
			player:getName(),
			grade,
			distanceBefore,
			getEffectiveDistance(player),
			grade >= 2 and string.format(" +%d flat", SHARPSHOOTER_GRADE_II_DISTANCE) or ""
		))
	end

	return true
end

local spell = Spell("instant")

function spell.onCastSpell(creature, variant)
	local player = creature:getPlayer()
	if not player then
		return false
	end

	if isSharpshooterActive(player) then
		deactivateSharpshooter(player, "deactivate")
		return true
	end

	return applySharpshooterBuff(player, creature, variant)
end

SharpshooterWheel = SharpshooterWheel or {}

function SharpshooterWheel.refreshActive(player)
	if not player or not isSharpshooterActive(player) then
		return
	end

	local variant = Variant(player:getId())
	applySharpshooterBuff(player, player, variant)
end

spell:name("Sharpshooter")
spell:words("utori con")
spell:group("support", "stance")
spell:vocation("paladin;true", "royal paladin;true")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_SHARPSHOOTER)
spell:id(313)
spell:cooldown(10 * 1000)
spell:groupCooldown(2 * 1000, 10 * 1000)
spell:level(60)
spell:mana(450)
spell:needLearn(false)
spell:isSelfTarget(true)
spell:isAggressive(false)
spell:isPremium(false)

spell:register()
