local WHEEL_AUGMENT_DEBUG = true
local spellDuration = 10000
local SWIFT_FOOT_DAMAGE_SUBID = 86064

local combat = Combat()
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_MAGIC_GREEN)
combat:setParameter(COMBAT_PARAM_AGGRESSIVE, 0)

local hasteCondition = Condition(CONDITION_HASTE)
hasteCondition:setParameter(CONDITION_PARAM_TICKS, spellDuration)
hasteCondition:setFormula(1.8, 72, 1.8, 72)
combat:addCondition(hasteCondition)

local function getSwiftFootGrade(player)
	if not player or not player.upgradeSpellsWOD then
		return 0
	end
	return player:upgradeSpellsWOD("Swift Foot")
end

local function getDamageDealtPercent(grade)
	if grade >= 2 then
		return nil
	elseif grade >= 1 then
		return 50
	end
	return 70
end

local function removeSwiftFootDamageDebuff(creature)
	creature:removeCondition(CONDITION_ATTRIBUTES, CONDITIONID_COMBAT, SWIFT_FOOT_DAMAGE_SUBID)
end

local function applySwiftFootDamageDebuff(creature, grade)
	removeSwiftFootDamageDebuff(creature)

	local damagePercent = getDamageDealtPercent(grade)
	if not damagePercent then
		return
	end

	local damageDebuff = Condition(CONDITION_ATTRIBUTES)
	damageDebuff:setParameter(CONDITION_PARAM_SUBID, SWIFT_FOOT_DAMAGE_SUBID)
	damageDebuff:setParameter(CONDITION_PARAM_TICKS, spellDuration)
	damageDebuff:setParameter(CONDITION_PARAM_BUFF_DAMAGEDEALT, damagePercent)
	damageDebuff:setParameter(CONDITION_PARAM_BUFF_SPELL, true)
	creature:addCondition(damageDebuff)
end

local function applyFamiliarHaste(creature)
	local summons = creature:getSummons()
	if not summons or type(summons) ~= "table" or #summons == 0 then
		return
	end

	for i = 1, #summons do
		local summon = summons[i]
		local summonType = summon:getType()
		if summonType and summonType:familiar() then
			local deltaSpeed = math.max(creature:getBaseSpeed() - summon:getBaseSpeed(), 0)
			local familiarSpeed = ((summon:getBaseSpeed() + deltaSpeed) * 0.8) - 72
			local familiarHaste = Condition(CONDITION_HASTE)
			familiarHaste:setParameter(CONDITION_PARAM_TICKS, spellDuration)
			familiarHaste:setParameter(CONDITION_PARAM_SPEED, familiarSpeed)
			summon:addCondition(familiarHaste)
		end
	end
end

local spell = Spell("instant")

function spell.onCastSpell(creature, var)
	local player = creature:getPlayer()
	local grade = player and getSwiftFootGrade(player) or 0

	applyFamiliarHaste(creature)

	if not combat:execute(creature, var) then
		return false
	end

	applySwiftFootDamageDebuff(creature, grade)

	if WHEEL_AUGMENT_DEBUG and player then
		local damageText = "none"
		local damagePercent = getDamageDealtPercent(grade)
		if damagePercent then
			damageText = string.format("%d%% dealt (-%d%% penalty)", damagePercent, 100 - damagePercent)
		end
		print(string.format(
			"[wheel-aug][swift-foot] player=%s grade=%d damageDealt=%s",
			player:getName(),
			grade,
			damageText
		))
	end

	return true
end

SwiftFootWheel = SwiftFootWheel or {}

function SwiftFootWheel.refreshActive(player)
	if not player then
		return
	end

	local grade = getSwiftFootGrade(player)
	if player:getCondition(CONDITION_ATTRIBUTES, CONDITIONID_COMBAT, SWIFT_FOOT_DAMAGE_SUBID) then
		applySwiftFootDamageDebuff(player, grade)
	elseif WHEEL_AUGMENT_DEBUG then
		print(string.format(
			"[wheel-aug][swift-foot] player=%s refresh skipped (Swift Foot not active)",
			player:getName()
		))
	end
end

spell:name("Swift Foot")
spell:words("utamo tempo san")
spell:group("support", "focus")
spell:vocation("paladin;true", "royal paladin;true")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_SWIFT_FOOT)
spell:id(134)
spell:cooldown(4 * 1000)
spell:groupCooldown(2 * 1000, 2 * 1000)
spell:level(55)
spell:mana(400)
spell:isSelfTarget(true)
spell:isAggressive(false)
spell:isPremium(true)
spell:needLearn(false)

spell:register()
