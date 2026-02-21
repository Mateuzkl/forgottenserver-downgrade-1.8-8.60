-- gerado por Spell Converter
-- script original
local combat = Combat()
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_MAGIC_RED)

local condition = Condition(CONDITION_PARALYZE)
condition:setParameter(CONDITION_PARAM_TICKS, 20000)
condition:setFormula(-1, 80, -1, 80)
combat:addCondition(condition)

local spell = Spell("rune")
function spell.onCastSpell(creature, variant, isHotkey)
	if not combat:execute(creature, variant) then return false end

	creature:getPosition():sendMagicEffect(CONST_ME_MAGIC_GREEN)
	return true
end


spell:group("support")
spell:id(3165)
spell:runeId(3165)
spell:name("Paralyze Rune")
spell:level(54)
spell:mana(1400)
spell:needCasterTargetOrDirection(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:allowFarUse(true)
spell:magicLevel(18)
spell:charges(1)
spell:isBlocking(true) -- True = Solid / False = Creature
spell:vocation("druid", "elder druid")
spell:register()
