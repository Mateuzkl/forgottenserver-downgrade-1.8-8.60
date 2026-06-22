local AttrSubId_PrimaryStance = 3100

local combat = Combat()
combat:setParameter(COMBAT_PARAM_EFFECT, 5)
combat:setParameter(COMBAT_PARAM_AGGRESSIVE, false)

local skill = Condition(CONDITION_ATTRIBUTES)
skill:setParameter(CONDITION_PARAM_SUBID, AttrSubId_PrimaryStance)
skill:setParameter(CONDITION_PARAM_TICKS, -1)
skill:setParameter(CONDITION_PARAM_SKILL_DISTANCEPERCENT, 140)
skill:setParameter(CONDITION_PARAM_BUFF_SPELL, true)
combat:addCondition(skill)

local spell = Spell("instant")
function spell.onCastSpell(creature, variant)
	local player = creature:getPlayer()
	if player and player:getStance() == STANCE_SHARPSHOOTER then
		player:removeCondition(CONDITION_ATTRIBUTES, CONDITIONID_COMBAT, AttrSubId_PrimaryStance)
		player:setStance(STANCE_NONE)
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		return true
	end

	if player then
		player:setStance(STANCE_SHARPSHOOTER)
	end
	return combat:execute(creature, variant)
end


spell:group("support", "stance")
spell:id(144)
spell:name("Sharpshooter")
spell:words("utito tempo san")
spell:level(60)
spell:mana(450)
spell:isPremium(true)
spell:isSelfTarget(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:vocation("paladin", "royal paladin")
spell:register()
