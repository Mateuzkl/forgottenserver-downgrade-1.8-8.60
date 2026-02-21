-- gerado por Spell Converter
-- script original
local spell = Spell("rune")
function spell.onCastSpell(creature, variant, isHotkey)
	local position = variant:getPosition()
	local tile = Tile(position)
	local field = tile and tile:getItemByType(ITEM_TYPE_MAGICFIELD)
	if field and table.contains(FIELDS, field:getId()) then
		field:remove()
		position:sendMagicEffect(CONST_ME_POFF)
		return true
	end

	creature:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
	creature:getPosition():sendMagicEffect(CONST_ME_POFF)
	return false
end


spell:group("support")
spell:id(3148)
spell:runeId(3148)
spell:name("Destroy Field Rune")
spell:level(17)
spell:range(5)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:isAggressive(false)
spell:allowFarUse(true)
spell:magicLevel(3)
spell:charges(3)
spell:register()
