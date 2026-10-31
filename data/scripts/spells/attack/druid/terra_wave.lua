local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_EARTHDAMAGE)
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_SMALLPLANTS)
combat:setArea(createCombatArea(AREA_SQUAREWAVE5, AREADIAGONAL_SQUAREWAVE5))

local function callback(player, level, magicLevel)
	local min = (level / 5) + (magicLevel * 3.25) + 5
	local max = (level / 5) + (magicLevel * 6.75) + 30
	return -min, -max
end

combat:setCallback(CallBackParam.LEVELMAGICVALUE, callback)

local spell = Spell("instant")
function spell.onCastSpell(creature, variant) return combat:execute(creature, variant) end


spell:group("attack")
spell:id(119)
spell:name("Terra Wave")
spell:castSound(SOUND_EFFECT_TYPE_SPELL_TERRA_WAVE)
spell:words("exevo tera hur")
spell:level(38)
spell:mana(210)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:needDirection(true)
spell:vocation("druid", "elder druid")
spell:register()
