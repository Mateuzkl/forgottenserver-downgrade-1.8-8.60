-- gerado por Spell Converter
-- script original
local combat = Combat()
combat:setParameter(COMBAT_PARAM_DISTANCEEFFECT, CONST_ANI_EARTH)
combat:setParameter(COMBAT_PARAM_CREATEITEM, ITEM_WILDGROWTH)

local spell = Spell("rune")
function spell.onCastSpell(creature, variant, isHotkey)
	return combat:execute(creature, variant)
end


spell:group("support")
spell:id(3156)
spell:runeId(3156)
spell:name("Wild Growth Rune")
spell:level(27)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:allowFarUse(true)
spell:magicLevel(8)
spell:charges(2)
spell:isBlocking(true, true) -- blockType("all") - bloqueia solid e creature
spell:vocation("druid", "elder druid")
spell:register()
