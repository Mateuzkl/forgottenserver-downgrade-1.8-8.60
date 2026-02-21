-- gerado por Spell Converter
-- script original
local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_DEATHDAMAGE)
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_MORTAREA)
combat:setParameter(COMBAT_PARAM_DISTANCEEFFECT, CONST_ANI_SUDDENDEATH)

local function callback(player, level, magicLevel)
	local min = (level / 5) + (magicLevel * 4.3) + 32
	local max = (level / 5) + (magicLevel * 7.4) + 48
	return -min, -max
end

combat:setCallback(CallBackParam.LEVELMAGICVALUE, callback)

local spell = Spell("rune")
function spell.onCastSpell(creature, variant, isHotkey)
	return combat:execute(creature, variant)
end


spell:group("attack")
spell:id(3155)
spell:runeId(3155)
spell:name("Sudden Death Rune")
spell:level(45)
spell:needCasterTargetOrDirection(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:allowFarUse(true)
spell:magicLevel(15)
spell:charges(3)
spell:isBlocking(true) -- True = Solid / False = Creature
spell:register()
