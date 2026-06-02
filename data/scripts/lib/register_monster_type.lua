registerMonsterType = {}
setmetatable(registerMonsterType, {
	__call = function(self, mtype, mask)
		for _, parse in pairs(self) do parse(mtype, mask) end
	end
})

local function isInteger(n)
	return (type(n) == "number") and (math.floor(n) == n)
end

local function validateLevelRange(monsterName, level)
	if type(level) ~= "table" then
		print("[Warning] Monster: \"" .. tostring(monsterName) .. "\" invalid level range (expected table)")
		return nil, nil
	end

	local minLevel = level.min
	local maxLevel = level.max
	if isInteger(minLevel) and isInteger(maxLevel) and minLevel > 0 and maxLevel > 0 and minLevel <= maxLevel then
		return minLevel, maxLevel
	end

	print("[Warning] Monster: \"" .. tostring(monsterName) .. "\" invalid level range (min=" ..
		tostring(minLevel) .. ", max=" .. tostring(maxLevel) .. ")")
	return nil, nil
end

local function isBossScriptSource()
	if not debug or not debug.getinfo then return false end

	local info = debug.getinfo(3, "S")
	local source = info and info.source
	if not source then return false end

	source = source:lower():gsub("\\", "/")
	return source:find("/bosses/", 1, true) ~= nil
end

MonsterType.register = function(self, mask)
	local result = registerMonsterType(self, mask)
	if isBossScriptSource() then self:isBoss(true) end
	local registeredBestiary = CustomBestiary and CustomBestiary.registerMonster and CustomBestiary.registerMonster(self, mask)
	local registeredBosstiary = CustomBosstiary and CustomBosstiary.registerMonster and CustomBosstiary.registerMonster(self, mask)
	if registeredBestiary or registeredBosstiary then
		self:registerEvent("CustomBestiaryKill")
	end
	return result
end

MonsterType.setLevel = function(self, level)
	local minLevel, maxLevel = validateLevelRange(self:name(), level)
	if not minLevel then
		return false
	end

	self:minLevel(minLevel)
	self:maxLevel(maxLevel)
	return true
end

registerMonsterType.name = function(mtype, mask)
	if mask.name then mtype:name(mask.name) end
end
registerMonsterType.raceId = function(mtype, mask)
	if mask.raceId then mtype:raceId(mask.raceId) end
end
registerMonsterType.description = function(mtype, mask)
	if mask.description then mtype:nameDescription(mask.description) end
end
registerMonsterType.faction = function(mtype, mask)
	if mask.faction then mtype:faction(mask.faction) end
end
registerMonsterType.enemyFactions = function(mtype, mask)
	if mask.enemyFactions then mtype:enemyFactions(mask.enemyFactions) end
end
registerMonsterType.experience = function(mtype, mask)
	if mask.experience then mtype:experience(mask.experience) end
end
registerMonsterType.skull = function(mtype, mask)
	if mask.skull then mtype:skull(mask.skull) end
end
registerMonsterType.level = function(mtype, mask)
	if mask.level then
		local minLevel, maxLevel = validateLevelRange(mtype:name(), mask.level)
		if minLevel then
			mtype:minLevel(minLevel)
			mtype:maxLevel(maxLevel)
		end
	end
end
registerMonsterType.emblem = function(mtype, mask)
	if mask.emblem then mtype:emblem(mask.emblem) end
end
registerMonsterType.outfit = function(mtype, mask)
	if mask.outfit then mtype:outfit(mask.outfit) end
end
registerMonsterType.maxHealth = function(mtype, mask)
	if mask.maxHealth then
		mtype:maxHealth(mask.maxHealth)
		mtype:health(math.min(mtype:health(), mask.maxHealth))
	end
end
registerMonsterType.health = function(mtype, mask)
	if mask.health then
		mtype:health(mask.health)
		mtype:maxHealth(math.max(mask.health, mtype:maxHealth()))
	end
end
registerMonsterType.maxSummons = function(mtype, mask)
	if mask.maxSummons then
		mtype:maxSummons(mask.maxSummons)
	elseif mask.summon and mask.summon.maxSummons then
		mtype:maxSummons(mask.summon.maxSummons)
	end
end
registerMonsterType.race = function(mtype, mask)
	if mask.race then mtype:race(mask.race) end
end
registerMonsterType.bosstiary = function(mtype, mask)
	if type(mask.bosstiary) == "table" then mtype:isBoss(true) end
end
registerMonsterType.manaCost = function(mtype, mask)
	if mask.manaCost then mtype:manaCost(mask.manaCost) end
end
registerMonsterType.speed = function(mtype, mask)
	if mask.speed then mtype:baseSpeed(mask.speed) end
end
registerMonsterType.corpse = function(mtype, mask)
	if mask.corpse then mtype:corpseId(mask.corpse) end
end
registerMonsterType.flags = function(mtype, mask)
	if mask.flags then
		if mask.flags.attackable ~= nil then
			mtype:isAttackable(mask.flags.attackable)
		end
		if mask.flags.healthHidden ~= nil then
			mtype:isHealthHidden(mask.flags.healthHidden)
		end
		if mask.flags.boss ~= nil then mtype:isBoss(mask.flags.boss) end
		if mask.flags.challengeable ~= nil then
			mtype:isChallengeable(mask.flags.challengeable)
		end
		if mask.flags.convinceable ~= nil then
			mtype:isConvinceable(mask.flags.convinceable)
		end
		if mask.flags.summonable ~= nil then
			mtype:isSummonable(mask.flags.summonable)
		end
		if mask.flags.isBlockable ~= nil then
			mtype:isBlockable(mask.flags.isBlockable)
		elseif mask.flags.ignoreSpawnBlock ~= nil then
			mtype:isBlockable(not mask.flags.ignoreSpawnBlock)
		end
		if mask.flags.illusionable ~= nil then
			mtype:isIllusionable(mask.flags.illusionable)
		end
		if mask.flags.hostile ~= nil then mtype:isHostile(mask.flags.hostile) end
		if mask.flags.pushable ~= nil then mtype:isPushable(mask.flags.pushable) end
		if mask.flags.canPushItems ~= nil then
			mtype:canPushItems(mask.flags.canPushItems)
		end
		if mask.flags.canPushCreatures ~= nil then
			mtype:canPushCreatures(mask.flags.canPushCreatures)
		end
		-- if a monster can push creatures,
		-- it should not be pushable
		if mask.flags.canPushCreatures then mtype:isPushable(false) end
		if mask.flags.targetDistance then
			mtype:targetDistance(mask.flags.targetDistance)
		end
		if mask.flags.runHealth then
			mtype:runHealth(mask.flags.runHealth)
		end
		if mask.flags.staticAttackChance then
			mtype:staticAttackChance(mask.flags.staticAttackChance)
		end
		if mask.flags.canWalkOnEnergy ~= nil then
			mtype:canWalkOnEnergy(mask.flags.canWalkOnEnergy)
		end
		if mask.flags.canWalkOnFire ~= nil then
			mtype:canWalkOnFire(mask.flags.canWalkOnFire)
		end
		if mask.flags.canWalkOnPoison ~= nil then
			mtype:canWalkOnPoison(mask.flags.canWalkOnPoison)
		end
		if mask.flags.rewardBoss ~= nil or mask.flags.isrewardboss ~= nil then
			mtype:isRewardBoss(mask.flags.rewardBoss or mask.flags.isrewardboss)
		end
	end
end
registerMonsterType.light = function(mtype, mask)
	if mask.light then mtype:light(mask.light.color or 0, mask.light.level or 0) end
end
registerMonsterType.changeTarget = function(mtype, mask)
	if mask.changeTarget then
		if mask.changeTarget.chance then
			mtype:changeTargetChance(mask.changeTarget.chance)
		end
		if mask.changeTarget.interval then
			mtype:changeTargetSpeed(mask.changeTarget.interval)
		end
	end
end
registerMonsterType.strategiesTarget = function(mtype, mask)
	if mask.strategiesTarget then
		mtype:strategiesTarget(mask.strategiesTarget)
	end
end
registerMonsterType.voices = function(mtype, mask)
	if type(mask.voices) == "table" then
		local interval, chance
		if mask.voices.interval then interval = mask.voices.interval end
		if mask.voices.chance then chance = mask.voices.chance end
		for k, v in pairs(mask.voices) do
			if type(v) == "table" then mtype:addVoice(v.text, interval, chance, v.yell) end
		end
	end
end
registerMonsterType.summons = function(mtype, mask)
	local summonData = mask.summons or (mask.summon and mask.summon.summons)
	if type(summonData) == "table" then
		for k, v in pairs(summonData) do
			mtype:addSummon(v.name, v.interval, v.chance, v.count, v.effect, v.masterEffect)
		end
	end
end
registerMonsterType.events = function(mtype, mask)
	if type(mask.events) == "table" then
		for k, v in pairs(mask.events) do mtype:registerEvent(v) end
	end
end
registerMonsterType.loot = function(mtype, mask)
	if type(mask.loot) == "table" then
		local lootError = false
		for _, loot in pairs(mask.loot) do
			local parent <close> = Loot()
			if loot.name then
				if not parent:setIdFromName(loot.name) then lootError = true end
			elseif loot.id and isInteger(loot.id) and loot.id > 0 then
				parent:setId(loot.id)
			else
				lootError = true
			end

			if loot.subType or loot.charges then
				parent:setSubType(loot.subType or loot.charges)
			else
				local lType = ItemType(loot.name or loot.id)
				if lType and lType:getCharges() > 1 then
					parent:setSubType(lType:getCharges())
				end
			end
			if loot.chance then parent:setChance(loot.chance) end
			if loot.maxCount then parent:setMaxCount(loot.maxCount) end
			if loot.minCount then parent:setMinCount(loot.minCount) end
			if loot.aid or loot.actionId then
				parent:setActionId(loot.aid or loot.actionId)
			end
			if loot.unique then
				parent:setUnique(loot.unique)
			end
			if loot.text or loot.description then
				parent:setDescription(loot.text or loot.description)
			end
			if loot.child then
				for _, children in pairs(loot.child) do
					local child <close> = Loot()
					if children.name then
						if not child:setIdFromName(children.name) then lootError = true end
					elseif children.id and isInteger(children.id) and children.id > 0 then
						child:setId(children.id)
					else
						lootError = true
					end

					if children.subType or children.charges then
						child:setSubType(children.subType or children.charges)
					else
						local cType = ItemType(children.name or children.id)
						if cType and cType:getCharges() > 1 then
							child:setSubType(cType:getCharges())
						end
					end
					if children.chance then child:setChance(children.chance) end
					if children.maxCount then child:setMaxCount(children.maxCount) end
					if children.aid or children.actionId then
						child:setActionId(children.aid or children.actionId)
					end
					if children.unique then
						child:setUnique(children.unique)
					end
					if children.text or children.description then
						child:setDescription(children.text or children.description)
					end
					parent:addChildLoot(child)
				end
			end
			mtype:addLoot(parent)
		end
		if lootError then
			print("[Warning - end] Monster: \"" .. mtype:name() ..
				      "\" loot could not correctly be load.")
		end
	end
end
registerMonsterType.elements = function(mtype, mask)
	if type(mask.elements) == "table" then
		for _, element in pairs(mask.elements) do
			if element.type and element.percent then
				mtype:addElement(element.type, element.percent)
			end
		end
	end
end
registerMonsterType.immunities = function(mtype, mask)
	if type(mask.immunities) == "table" then
		for _, immunity in pairs(mask.immunities) do
			if immunity.type and immunity.combat then
				mtype:combatImmunities(immunity.type)
			end
			if immunity.type and immunity.condition then
				mtype:conditionImmunities(immunity.type)
			end
		end
	end
end
local function AbilityTableToSpell(ability)
	local spell = MonsterSpell()
	if ability.name then
		if ability.name == "melee" then
			spell:setType("melee")
			if ability.attack and ability.skill then
				spell:setAttackValue(ability.attack, ability.skill)
			end
			if ability.minDamage and ability.maxDamage then
				spell:setCombatValue(ability.minDamage, ability.maxDamage)
			end
			if ability.interval then spell:setInterval(ability.interval) end
			if ability.effect then spell:setCombatEffect(ability.effect) end
		else
			spell:setType(ability.name)
			if ability.type then
				if ability.name == "condition" then
					spell:setConditionType(ability.type)
				else
					spell:setCombatType(ability.type)
				end
			end
			if ability.interval then spell:setInterval(ability.interval) end
			if ability.chance then spell:setChance(ability.chance) end
			if ability.range then spell:setRange(ability.range) end
			if ability.duration then spell:setConditionDuration(ability.duration) end
			local speed = ability.speed or ability.speedChange or ability.minChange
			if speed then
				if type(speed) ~= "table" then
					spell:setConditionSpeedChange(speed, ability.maxChange or speed)
				elseif type(speed) == "table" then
					if speed.min and speed.max then
						spell:setConditionSpeedChange(speed.min, speed.max)
					end
				end
			end
			if ability.target then spell:setNeedTarget(ability.target) end
			if ability.length then spell:setCombatLength(ability.length) end
			if ability.spread then spell:setCombatSpread(ability.spread) end
			if ability.radius then spell:setCombatRadius(ability.radius) end
			if ability.ring then spell:setCombatRing(ability.ring) end
			if ability.minDamage and ability.maxDamage then
				spell:setCombatValue(ability.minDamage, ability.maxDamage)
				if ability.name == "condition" then
					spell:setConditionDamage(ability.minDamage, ability.maxDamage, ability.startDamage or 0)
				end
			end
			if ability.totalDamage and ability.name == "condition" then
				spell:setConditionDamage(ability.totalDamage, ability.totalDamage, ability.startDamage or 0)
			end
			if ability.effect then spell:setCombatEffect(ability.effect) end
			if ability.outfit and spell.setOutfit then
				spell:setOutfit(ability.outfit)
			end
			if (ability.monster or ability.outfitMonster) and spell.setOutfitMonster then
				spell:setOutfitMonster(ability.monster or ability.outfitMonster)
			end
			if ability.item and spell.setOutfitItem then
				spell:setOutfitItem(ability.item)
			end
			if ability.shootEffect then spell:setCombatShootEffect(ability.shootEffect) end
			if ability.drunkenness then
				spell:setConditionDrunkenness(ability.drunkenness)
			end
		end
		if ability.condition then
			if ability.condition.type then
				spell:setConditionType(ability.condition.type)
			end
			local startDamage = 0
			if ability.condition.startDamage then
				startDamage = ability.condition.startDamage
			end
			if ability.condition.minDamage and ability.condition.maxDamage then
				spell:setConditionDamage(ability.condition.minDamage, ability.condition.maxDamage, startDamage)
			elseif ability.condition.totalDamage then
				spell:setConditionDamage(ability.condition.totalDamage, ability.condition.totalDamage, startDamage)
			end
			if ability.condition.duration then
				spell:setConditionDuration(ability.condition.duration)
			end
			if ability.condition.interval then
				spell:setConditionTickInterval(ability.condition.interval)
			end
		end
	elseif ability.script then
		spell:setScriptName(ability.script)
		if ability.interval then spell:setInterval(ability.interval) end
		if ability.chance then spell:setChance(ability.chance) end
		if ability.minDamage and ability.maxDamage then
			spell:setCombatValue(ability.minDamage, ability.maxDamage)
		end
		if ability.target then spell:setNeedTarget(ability.target) end
		if ability.direction then spell:setNeedDirection(ability.direction) end
	end
	return spell
end
registerMonsterType.attacks = function(mtype, mask)
	if type(mask.attacks) == "table" then
		for _, attack in pairs(mask.attacks) do
			local spell <close> = AbilityTableToSpell(attack)
			mtype:addAttack(spell)
		end
	end
end
registerMonsterType.defenses = function(mtype, mask)
	if type(mask.defenses) == "table" then
		if mask.defenses.defense then mtype:defense(mask.defenses.defense) end
		if mask.defenses.armor then mtype:armor(mask.defenses.armor) end
		if mask.defenses.mitigation then mtype:mitigation(mask.defenses.mitigation) end
		for _, defense in pairs(mask.defenses) do
			if type(defense) == "table" then
				local spell <close> = AbilityTableToSpell(defense)
				mtype:addDefense(spell)
			end
		end
	end
end
