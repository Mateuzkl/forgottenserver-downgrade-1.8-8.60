-- gerado por Spell Converter
-- script original
local spell = Spell("rune")
function spell.onCastSpell(creature, variant, isHotkey)
	local target = Creature(variant:getNumber())
	if not target or not target:isMonster() then
		creature:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		creature:getPosition():sendMagicEffect(CONST_ME_POFF)
		return false
	end

	local monsterType = target:getType()
	if not creature:hasFlag(PlayerFlag_CanConvinceAll) then
		if not monsterType:isConvinceable() then
			creature:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
			creature:getPosition():sendMagicEffect(CONST_ME_POFF)
			return false
		end

		if #creature:getSummons() >= 2 then
			creature:sendCancelMessage("You cannot control more creatures.")
			creature:getPosition():sendMagicEffect(CONST_ME_POFF)
			return false
		end
	end

	local manaCost = target:getType():getManaCost()
	if creature:getMana() < manaCost and
		not creature:hasFlag(PlayerFlag_HasInfiniteMana) then
		creature:sendCancelMessage(RETURNVALUE_NOTENOUGHMANA)
		creature:getPosition():sendMagicEffect(CONST_ME_POFF)
		return false
	end

	creature:addMana(-manaCost)
	creature:addManaSpent(manaCost)
	creature:addSummon(target)
	creature:getPosition():sendMagicEffect(CONST_ME_MAGIC_BLUE)
	return true
end


spell:group("support")
spell:id(3177)
spell:runeId(3177)
spell:name("Convince Creature Rune")
spell:level(16)
spell:needCasterTargetOrDirection(true)
spell:cooldown(2 * 1000)
spell:groupCooldown(2 * 1000)
spell:needLearn(false)
spell:allowFarUse(true)
spell:magicLevel(5)
spell:charges(1)
spell:blockType("solid")
spell:register()
