-- Ethereal Barrage (Paladin) - Vocation Adjustment
-- Crosshair/target spell: needTarget + setArea so the engine resolves the
-- target creature and applies a diamond burst (AREA_CIRCLE2X2, 13-square
-- diamond-arrow-size) centered on the target's tile.
-- Physical damage, scales with DISTANCE FIGHTING like Strong Ethereal Spear
-- (SKILLVALUE callback: skill = effective distance skill, attack = weapon attack).

local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_PHYSICALDAMAGE)
combat:setParameter(COMBAT_PARAM_EFFECT, 320)
combat:setParameter(COMBAT_PARAM_DISTANCEEFFECT, CONST_ANI_ETHEREALSPEAR)
combat:setParameter(COMBAT_PARAM_BLOCKARMOR, 1)
combat:setArea(createCombatArea(AREA_CIRCLE2X2))

-- Base 40. Scales with distance fighting (and a little with level) like
-- Strong Ethereal Spear, tuned to a ~40 base hit.
function onGetFormulaValues(player, skill, attack, factor)
	local levelTotal = player:getLevel() / 5
	local min = ((2 * skill + attack / 2500) * 1.20) + levelTotal + 7
	local max = ((2 * skill + attack / 1875) * 2.00) + levelTotal + 13
	return -min, -max
end

combat:setCallback(CALLBACK_PARAM_SKILLVALUE, "onGetFormulaValues")

local spell = Spell("instant")

function spell.onCastSpell(creature, var)
	-- needPosition: var is the clicked tile (cursor/crosshair); the area is cast there.
	return combat:execute(creature, var)
end

spell:group("attack")
spell:id(303)
spell:name("Ethereal Barrage")
spell:words("exori dir moe")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_STRONG_ETHEREAL_SPEAR)
spell:impactSound(SOUND_EFFECT_TYPE_SPELL_STRONG_ETHEREAL_SPEAR)
spell:level(60)
spell:mana(135)
spell:isPremium(true)
spell:isAggressive(true)
spell:range(7)
spell:needPosition(true) -- cast at the clicked tile (cursor/crosshair); var is VARIANT_POSITION
spell:blockWalls(true)
spell:needLearn(false)
spell:cooldown(4 * 1000)
spell:groupCooldown(2 * 1000)

spell:vocation("paladin;true", "royal paladin;true")
spell:register()
