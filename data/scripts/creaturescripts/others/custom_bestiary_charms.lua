if not CustomBestiary then
	return
end

local CHARM = {
	WOUND = 0,
	ENFLAME = 1,
	POISON = 2,
	FREEZE = 3,
	ZAP = 4,
	CURSE = 5,
	CRIPPLE = 6,
	PARRY = 7,
	DODGE = 8,
	ADRENALINE = 9,
	NUMB = 10,
	CLEANSE = 11,
	BLESS = 12,
	SCAVENGE = 13,
	GUT = 14,
	LOW_BLOW = 15,
	DIVINE_WRATH = 16,
	VAMPIRIC = 17,
	VOID_CALL = 18,
	SAVAGE = 19,
	FATAL_HOLD = 20,
	VOID_INVERSION = 21,
	CARNAGE = 22,
	OVERPOWER = 23,
	OVERFLUX = 24
}

local CHARM_CATEGORY = {
	MAJOR = "major",
	MINOR = "minor"
}

local charmCategories = {
	[CHARM.WOUND] = CHARM_CATEGORY.MAJOR,
	[CHARM.ENFLAME] = CHARM_CATEGORY.MAJOR,
	[CHARM.POISON] = CHARM_CATEGORY.MAJOR,
	[CHARM.FREEZE] = CHARM_CATEGORY.MAJOR,
	[CHARM.ZAP] = CHARM_CATEGORY.MAJOR,
	[CHARM.CURSE] = CHARM_CATEGORY.MAJOR,
	[CHARM.CRIPPLE] = CHARM_CATEGORY.MINOR,
	[CHARM.PARRY] = CHARM_CATEGORY.MAJOR,
	[CHARM.DODGE] = CHARM_CATEGORY.MAJOR,
	[CHARM.ADRENALINE] = CHARM_CATEGORY.MINOR,
	[CHARM.NUMB] = CHARM_CATEGORY.MINOR,
	[CHARM.CLEANSE] = CHARM_CATEGORY.MINOR,
	[CHARM.BLESS] = CHARM_CATEGORY.MINOR,
	[CHARM.SCAVENGE] = CHARM_CATEGORY.MINOR,
	[CHARM.GUT] = CHARM_CATEGORY.MINOR,
	[CHARM.LOW_BLOW] = CHARM_CATEGORY.MAJOR,
	[CHARM.DIVINE_WRATH] = CHARM_CATEGORY.MAJOR,
	[CHARM.VAMPIRIC] = CHARM_CATEGORY.MINOR,
	[CHARM.VOID_CALL] = CHARM_CATEGORY.MINOR,
	[CHARM.SAVAGE] = CHARM_CATEGORY.MAJOR,
	[CHARM.FATAL_HOLD] = CHARM_CATEGORY.MINOR,
	[CHARM.VOID_INVERSION] = CHARM_CATEGORY.MINOR,
	[CHARM.CARNAGE] = CHARM_CATEGORY.MAJOR,
	[CHARM.OVERPOWER] = CHARM_CATEGORY.MAJOR,
	[CHARM.OVERFLUX] = CHARM_CATEGORY.MAJOR
}

local damageCharms = {
	[CHARM.WOUND] = COMBAT_PHYSICALDAMAGE,
	[CHARM.ENFLAME] = COMBAT_FIREDAMAGE,
	[CHARM.POISON] = COMBAT_EARTHDAMAGE,
	[CHARM.FREEZE] = COMBAT_ICEDAMAGE,
	[CHARM.ZAP] = COMBAT_ENERGYDAMAGE,
	[CHARM.CURSE] = COMBAT_DEATHDAMAGE,
	[CHARM.DIVINE_WRATH] = COMBAT_HOLYDAMAGE
}

local negativeConditions = {
	CONDITION_POISON,
	CONDITION_FIRE,
	CONDITION_ENERGY,
	CONDITION_BLEEDING,
	CONDITION_PARALYZE,
	CONDITION_DROWN,
	CONDITION_FREEZING,
	CONDITION_DAZZLED,
	CONDITION_CURSED
}

local playerCharmCache = {}
local playerSpecials = {}
local adrenalineBoosts = {}
local charmDamageGuard = false

local function getCharmDefinition(charmId)
	return CustomBestiary and CustomBestiary.charmById and CustomBestiary.charmById[charmId] or nil
end

local function getCharmTierValue(charmId, tier, field, fallback)
	local charm = getCharmDefinition(charmId)
	tier = math.max(1, math.min(tonumber(tier) or 1, 3))
	if charm and type(charm[field]) == "table" and charm[field][tier] ~= nil then
		return charm[field][tier]
	end
	return fallback
end

local function getCharmChance(charmId, tier, fallback)
	return getCharmTierValue(charmId, tier, "bonuses", fallback or 0)
end

local function getCharmBonus(charmId, tier, fallback)
	return getCharmTierValue(charmId, tier, "bonuses", fallback or 0)
end

local function getCharmPercent(charmId, fallback)
	local charm = getCharmDefinition(charmId)
	return tonumber(charm and charm.percent) or fallback or 0
end

local function getPlayerFromCreature(creature)
	if not creature then
		return nil
	end
	if creature:isPlayer() then
		return creature
	end
	local master = creature:getMaster()
	if master and master:isPlayer() then
		return master
	end
	return nil
end

local function getRaceId(creature)
	if not creature or not creature:isMonster() then
		return 0
	end
	local monsterType = creature:getType()
	return monsterType and monsterType:raceId() or 0
end

local function getPlayerCharms(player)
	local guid = player:getGuid()
	if playerCharmCache[guid] then
		return playerCharmCache[guid]
	end

	local charms = { byRace = {} }
	local resultId = db.storeQuery("SELECT `charm_id`, `unlocked`, `raceid` FROM `player_bestiary_charms` WHERE `player_id` = " ..
		guid .. " AND `unlocked` > 0 AND `raceid` > 0")
	if resultId ~= false then
		repeat
			local charmId = result.getDataInt(resultId, "charm_id")
			local tier = math.max(1, math.min(result.getDataInt(resultId, "unlocked"), 3))
			local raceId = result.getDataInt(resultId, "raceid")
			local category = charmCategories[charmId]
			if category then
				charms.byRace[raceId] = charms.byRace[raceId] or {}
				charms.byRace[raceId][category] = { id = charmId, tier = tier }
			end
		until not result.next(resultId)
		result.free(resultId)
	end

	playerCharmCache[guid] = charms
	return charms
end

local function getCharmForRace(player, raceId, category)
	if not player or raceId <= 0 then
		return nil
	end
	local entry = getPlayerCharms(player).byRace[raceId]
	if type(entry) == "table" then
		local charm = category and entry[category]
		if not category then
			charm = entry[CHARM_CATEGORY.MAJOR] or entry[CHARM_CATEGORY.MINOR]
		end
		if type(charm) == "table" then
			return charm.id, charm.tier
		end
		return charm, 1
	end
	return entry, 1
end

local function removeSpecials(player)
	local guid = player:getGuid()
	local applied = playerSpecials[guid]
	if not applied then
		return
	end
	for skill, value in pairs(applied) do
		if value ~= 0 then
			player:addSpecialSkill(skill, -value)
		end
	end
	playerSpecials[guid] = nil
end

local function applySpecials(player)
	removeSpecials(player)
end

function CustomBestiary.refreshPlayerCharms(player)
	if not player then
		return
	end
	playerCharmCache[player:getGuid()] = nil
	applySpecials(player)
end

function CustomBestiary.getToolCharmBonuses(player, corpseId)
	local raceId = CustomBestiary.corpseRaceById[tonumber(corpseId) or 0] or 0
	local charmId, tier = getCharmForRace(player, raceId, CHARM_CATEGORY.MINOR)
	return {
		scavenge = charmId == CHARM.SCAVENGE and getCharmBonus(charmId, tier, 60) or 0,
		scavengeCharmId = charmId == CHARM.SCAVENGE and charmId or 0,
		gut = charmId == CHARM.GUT and getCharmBonus(charmId, tier, 6) or 0,
		gutCharmId = charmId == CHARM.GUT and charmId or 0
	}
end

local function roll(chancePercent)
	return (math.random() * 100) <= (tonumber(chancePercent) or 0)
end

local function notifyCharmActivated(player, charmId)
	if not player or not charmId then
		return
	end
	if MiscAnalyzer and MiscAnalyzer.sendCharm and MiscAnalyzer.sendCharm(player, charmId) then
		return
	end
	if player.sendCharmActivated then
		player:sendCharmActivated(charmId)
	end
end

local function isDamage(value, combatType)
	return value ~= 0 and combatType ~= COMBAT_HEALING and combatType ~= COMBAT_NONE
end

local function doCharmDamage(caster, target, combatType, amount, effect)
	if not caster or not target or amount <= 0 or charmDamageGuard then
		return
	end
	charmDamageGuard = true
	doTargetCombat(caster, target, combatType, -amount, -amount, effect or CONST_ME_NONE, ORIGIN_NONE)
	charmDamageGuard = false
end

local function addParalyze(target, ticks)
	if not target then
		return
	end
	local condition = Condition(CONDITION_PARALYZE)
	condition:setParameter(CONDITION_PARAM_TICKS, ticks)
	condition:setFormula(-0.45, 0, -0.8, 0)
	target:addCondition(condition)
	target:getPosition():sendMagicEffect(CONST_ME_STUN)
end

local function addAdrenaline(player)
	local guid = player:getGuid()
	local boost = adrenalineBoosts[guid]
	if boost then
		boost.token = boost.token + 1
		local token = boost.token
		addEvent(function(playerId, delayedToken)
			local delayedPlayer = Player(playerId)
			local delayedBoost = delayedPlayer and adrenalineBoosts[delayedPlayer:getGuid()]
			if delayedPlayer and delayedBoost and delayedBoost.token == delayedToken then
				delayedPlayer:changeSpeed(-delayedBoost.delta)
				adrenalineBoosts[delayedPlayer:getGuid()] = nil
			end
		end, 10000, player:getId(), token)
		player:getPosition():sendMagicEffect(CONST_ME_MAGIC_GREEN)
		return
	end

	local delta = math.max(1, math.floor(player:getBaseSpeed() * 1.5))
	adrenalineBoosts[guid] = { delta = delta, token = 1 }
	player:changeSpeed(delta)
	player:getPosition():sendMagicEffect(CONST_ME_MAGIC_GREEN)

	addEvent(function(playerId, token)
		local delayedPlayer = Player(playerId)
		local delayedBoost = delayedPlayer and adrenalineBoosts[delayedPlayer:getGuid()]
		if delayedPlayer and delayedBoost and delayedBoost.token == token then
			delayedPlayer:changeSpeed(-delayedBoost.delta)
			adrenalineBoosts[delayedPlayer:getGuid()] = nil
		end
	end, 10000, player:getId(), 1)
end

local function cleanse(player)
	local removable = {}
	for _, conditionType in ipairs(negativeConditions) do
		if player:getCondition(conditionType) then
			removable[#removable + 1] = conditionType
		end
	end
	if #removable == 0 then
		return
	end
	local conditionType = removable[math.random(#removable)]
	player:removeCondition(conditionType)
	player:addConditionSuppressions(conditionType)
	addEvent(function(playerId, suppressedType)
		local delayedPlayer = Player(playerId)
		if delayedPlayer then
			delayedPlayer:removeConditionSuppressions(suppressedType)
		end
	end, 11000, player:getId(), conditionType)
	player:getPosition():sendMagicEffect(CONST_ME_MAGIC_BLUE)
end

local charmHealth = CreatureEvent("CustomBestiaryCharmHealth")
function charmHealth.onHealthChange(creature, attacker, primaryDamage, primaryType, secondaryDamage, secondaryType, origin)
	if charmDamageGuard or not CustomBestiary then
		return primaryDamage, primaryType, secondaryDamage, secondaryType
	end

	if creature and creature:isMonster() then
		local player = getPlayerFromCreature(attacker)
		local raceId = getRaceId(creature)
		if isDamage(primaryDamage, primaryType) then
			local majorCharmId, majorTier = getCharmForRace(player, raceId, CHARM_CATEGORY.MAJOR)
			local combatType = damageCharms[majorCharmId]
			if player and combatType and roll(getCharmChance(majorCharmId, majorTier, 5)) then
				local entry = CustomBestiary.getMonster(raceId)
				local baseHealth = entry and entry.health or creature:getMaxHealth()
				local capByLevel = math.ceil(player:getLevel() * 2)
				local damage = math.min(capByLevel, math.ceil(baseHealth * (getCharmPercent(majorCharmId, 5) / 100)))
				notifyCharmActivated(player, majorCharmId)
				doCharmDamage(player, creature, combatType, math.max(1, damage), CONST_ME_DRAWBLOOD)
			elseif player and majorCharmId == CHARM.OVERPOWER and roll(getCharmChance(majorCharmId, majorTier, 5)) then
				local targetCap = math.ceil(creature:getMaxHealth() * 0.08)
				local damage = math.min(targetCap, math.ceil(player:getMaxHealth() * (getCharmPercent(majorCharmId, 5) / 100)))
				notifyCharmActivated(player, majorCharmId)
				doCharmDamage(player, creature, COMBAT_PHYSICALDAMAGE, math.max(1, damage), CONST_ME_DRAWBLOOD)
			elseif player and majorCharmId == CHARM.OVERFLUX and roll(getCharmChance(majorCharmId, majorTier, 5)) then
				local targetCap = math.ceil(creature:getMaxHealth() * 0.08)
				local damage = math.min(targetCap, math.ceil(player:getMaxMana() * (getCharmPercent(majorCharmId, 2.5) / 100)))
				notifyCharmActivated(player, majorCharmId)
				doCharmDamage(player, creature, COMBAT_ENERGYDAMAGE, math.max(1, damage), CONST_ME_ENERGYHIT)
			end

			local minorCharmId, minorTier = getCharmForRace(player, raceId, CHARM_CATEGORY.MINOR)
			if minorCharmId == CHARM.CRIPPLE and roll(getCharmChance(minorCharmId, minorTier, 6)) then
				notifyCharmActivated(player, minorCharmId)
				addParalyze(creature, 10000)
			elseif minorCharmId == CHARM.FATAL_HOLD and roll(getCharmChance(minorCharmId, minorTier, 30)) and creature.blockFleeing then
				notifyCharmActivated(player, minorCharmId)
				creature:blockFleeing(30000)
				creature:getPosition():sendMagicEffect(CONST_ME_WHITE_TIGERCLASH)
			end
		end
	elseif creature and creature:isPlayer() then
		local raceId = getRaceId(attacker)
		if isDamage(primaryDamage, primaryType) then
			local majorCharmId, majorTier = getCharmForRace(creature, raceId, CHARM_CATEGORY.MAJOR)
			if majorCharmId == CHARM.DODGE and roll(getCharmChance(majorCharmId, majorTier, 5)) then
				notifyCharmActivated(creature, majorCharmId)
				creature:getPosition():sendMagicEffect(CONST_ME_WHITE_EXPLOSIONHIT)
				return 0, primaryType, 0, secondaryType
			elseif majorCharmId == CHARM.PARRY and roll(getCharmChance(majorCharmId, majorTier, 5)) and attacker then
				local reflected = math.abs(primaryDamage) + math.abs(secondaryDamage)
				notifyCharmActivated(creature, majorCharmId)
				doCharmDamage(creature, attacker, primaryType ~= COMBAT_NONE and primaryType or COMBAT_PHYSICALDAMAGE, reflected, CONST_ME_DRAWBLOOD)
			end

			local minorCharmId, minorTier = getCharmForRace(creature, raceId, CHARM_CATEGORY.MINOR)
			if minorCharmId == CHARM.ADRENALINE and roll(getCharmChance(minorCharmId, minorTier, 6)) then
				notifyCharmActivated(creature, minorCharmId)
				addAdrenaline(creature)
			elseif minorCharmId == CHARM.NUMB and roll(getCharmChance(minorCharmId, minorTier, 6)) then
				notifyCharmActivated(creature, minorCharmId)
				addParalyze(attacker, 10000)
			elseif minorCharmId == CHARM.CLEANSE and roll(getCharmChance(minorCharmId, minorTier, 6)) then
				notifyCharmActivated(creature, minorCharmId)
				cleanse(creature)
			end
		end
	end

	return primaryDamage, primaryType, secondaryDamage, secondaryType
end
charmHealth:register()

local charmMana = CreatureEvent("CustomBestiaryCharmMana")
function charmMana.onManaChange(creature, attacker, primaryDamage, primaryType, secondaryDamage, secondaryType, origin)
	if creature and creature:isPlayer() and primaryType == COMBAT_MANADRAIN then
		local charmId, tier = getCharmForRace(creature, getRaceId(attacker), CHARM_CATEGORY.MINOR)
		if charmId == CHARM.VOID_INVERSION and primaryDamage < 0 and roll(getCharmChance(charmId, tier, 20)) then
			notifyCharmActivated(creature, charmId)
			creature:getPosition():sendMagicEffect(CONST_ME_MAGIC_BLUE)
			return math.abs(primaryDamage), COMBAT_MANADRAIN, math.abs(secondaryDamage), secondaryType
		end
	end
	return primaryDamage, primaryType, secondaryDamage, secondaryType
end
charmMana:register()

local charmDeath = CreatureEvent("CustomBestiaryCharmDeath")
function charmDeath.onDeath(creature, corpse, killer, mostDamageKiller, lastHitUnjustified, mostDamageUnjustified)
	local player = getPlayerFromCreature(killer) or getPlayerFromCreature(mostDamageKiller)
	if not player or not creature or not creature:isMonster() then
		return true
	end
	local charmId, tier = getCharmForRace(player, getRaceId(creature), CHARM_CATEGORY.MAJOR)
	if charmId == CHARM.CARNAGE and roll(getCharmChance(charmId, tier, 10)) then
		local capByLevel = player:getLevel() * 6
		local damage = math.max(1, math.min(capByLevel, math.ceil(creature:getMaxHealth() * (getCharmPercent(charmId, 15) / 100))))
		local position = creature:getPosition()
		local spectators = Game.getSpectators(position, false, false, 1, 1, 1, 1)
		notifyCharmActivated(player, charmId)
		creature:getPosition():sendMagicEffect(CONST_ME_EXPLOSIONAREA)
		for _, spectator in ipairs(spectators) do
			local spectatorPosition = spectator:getPosition()
			local distance = math.abs(spectatorPosition.x - position.x) + math.abs(spectatorPosition.y - position.y)
			if spectator:isMonster() and spectator ~= creature and spectatorPosition.z == position.z and distance == 1 then
				spectator:getPosition():sendMagicEffect(CONST_ME_EXPLOSIONAREA)
				doCharmDamage(player, spectator, COMBAT_PHYSICALDAMAGE, damage, CONST_ME_NONE)
			end
		end
	end
	return true
end
charmDeath:register()

local charmPrepareDeath = CreatureEvent("CustomBestiaryCharmPrepareDeath")
function charmPrepareDeath.onPrepareDeath(player, killer)
	local charmId, tier = getCharmForRace(player, getRaceId(killer), CHARM_CATEGORY.MINOR)
	if player and player:isPlayer() and charmId == CHARM.BLESS and player.setTemporaryDeathLossReduction then
		notifyCharmActivated(player, charmId)
		player:setTemporaryDeathLossReduction(getCharmBonus(charmId, tier, 6))
	end
	return true
end
charmPrepareDeath:register()

local charmLogin = CreatureEvent("CustomBestiaryCharmLogin")
function charmLogin.onLogin(player)
	player:registerEvent("CustomBestiaryCharmHealth")
	player:registerEvent("CustomBestiaryCharmMana")
	player:registerEvent("CustomBestiaryCharmPrepareDeath")
	player:registerEvent("CustomBestiaryCharmLogout")
	CustomBestiary.refreshPlayerCharms(player)
	return true
end
charmLogin:register()

local charmLogout = CreatureEvent("CustomBestiaryCharmLogout")
function charmLogout.onLogout(player)
	removeSpecials(player)
	local boost = adrenalineBoosts[player:getGuid()]
	if boost then
		player:changeSpeed(-boost.delta)
		adrenalineBoosts[player:getGuid()] = nil
	end
	playerCharmCache[player:getGuid()] = nil
	return true
end
charmLogout:register()

local charmSpawn = MonsterEvent and MonsterEvent("CustomBestiaryCharmSpawn") or Event()
function charmSpawn.onSpawn(monster)
	if monster then
		monster:registerEvent("CustomBestiaryCharmHealth")
		monster:registerEvent("CustomBestiaryCharmDeath")
	end
	return true
end
charmSpawn:register()

local charmTarget = Event()
function charmTarget.onTargetCombat(creature, target)
	if creature and creature:isPlayer() then
		creature:registerEvent("CustomBestiaryCharmHealth")
		creature:registerEvent("CustomBestiaryCharmMana")
		creature:registerEvent("CustomBestiaryCharmPrepareDeath")
	end
	if target and target:isPlayer() then
		target:registerEvent("CustomBestiaryCharmHealth")
		target:registerEvent("CustomBestiaryCharmMana")
		target:registerEvent("CustomBestiaryCharmPrepareDeath")
	end
	if target and target:isMonster() then
		target:registerEvent("CustomBestiaryCharmHealth")
		target:registerEvent("CustomBestiaryCharmDeath")
	end
	return RETURNVALUE_NOERROR
end
charmTarget:register()
