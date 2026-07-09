local VOCATION_ALL = 0

ResetBonusConfig = {
	resetLevel    = 8,
	maxResets     = 225,
	resetCooldown = 0,

	damage = {
		enabled = true,
		[VOCATION_ALL] = {
			steps = {
				{ reset =  1, bonus = 1.0 },
				{ reset =  5, bonus = 1.0 },
				{ reset = 10, bonus = 1.0 },
			},
			default = 0.05,
		},
	},

	defense = {
		enabled = true,
		[VOCATION_ALL] = {
			steps = {
				{ reset =  1, bonus = 0.75 },
				{ reset =  5, bonus = 0.75 },
				{ reset = 10, bonus = 0.75 },
			},
			default = 0.04,
		},
	},

	experience = {
		enabled = true,
		[VOCATION_ALL] = {
			steps = {
				{ reset =  1, bonus = 3.0 },
				{ reset =  5, bonus = 2.0 },
				{ reset = 10, bonus = 2.0 },
			},
			default = 0.05,
		},
	},

	healing = {
		enabled = true,
		[VOCATION_ALL] = {
			steps = {
				{ reset =  1, bonus = 0.75 },
				{ reset =  5, bonus = 0.75 },
				{ reset = 10, bonus = 0.75 },
			},
			default = 0.04,
		},
	},

	attackSpeed = {
		enabled = true,
		[VOCATION_ALL] = {
			steps = {
				{ reset =  1, bonus = 5 },
				{ reset =  5, bonus = 5 },
				{ reset = 10, bonus = 5 },
			},
			default = 0.5,
		},
	},

	hp = {
		enabled = true,
		bonusMode = "percent",
		[VOCATION_ALL] = {
			ranges = {
				{ minReset =   1, maxReset =  50, bonus = 0.30 },
				{ minReset =  51, maxReset = 100, bonus = 0.20 },
				{ minReset = 101, maxReset = 150, bonus = 0.15 },
				{ minReset = 151, maxReset =   0, bonus = 0.10 },
			},
		},
	},

	mana = {
		enabled = true,
		bonusMode = "percent",
		[VOCATION_ALL] = {
			ranges = {
				{ minReset =   1, maxReset =  50, bonus = 0.25 },
				{ minReset =  51, maxReset = 100, bonus = 0.18 },
				{ minReset = 101, maxReset = 150, bonus = 0.12 },
				{ minReset = 151, maxReset =   0, bonus = 0.08 },
			},
		},
	},

	manaPotion = {
		enabled = true,
		[VOCATION_ALL] = {
			ranges = {
				{ minReset = 1, maxReset = 0, bonus = 0.08 },
			},
		},
		[1] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.18 } } },
		[2] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.18 } } },
		[5] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.18 } } },
		[6] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.18 } } },
		[3] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.12 } } },
		[7] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.12 } } },
		[4] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.10 } } },
		[8] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.10 } } },
	},

	manaSpell = {
		enabled = true,
		[VOCATION_ALL] = {
			ranges = {
				{ minReset = 1, maxReset = 0, bonus = 0.05 },
			},
		},
		[1] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.20 } } },
		[2] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.20 } } },
		[5] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.20 } } },
		[6] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.20 } } },
		[3] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.10 } } },
		[7] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.10 } } },
		[4] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.0 } } },
		[8] = { ranges = { { minReset = 1, maxReset = 0, bonus = 0.0 } } },
	},
}

function ResetBonusConfig.getTotalBonus(bonusType, resetCount, vocationId)
	if resetCount <= 0 then
		return 0
	end

	local config = ResetBonusConfig[bonusType]
	if not config then
		return 0
	end

	if config.enabled == false then
		return 0
	end

	local vocConfig = config[vocationId] or config[0]
	if not vocConfig then
		return 0
	end

	local total = 0
	local defaultBonus = vocConfig.default or 0

	if vocConfig.steps and #vocConfig.steps > 0 then
		local lastStepReset = 0
		local appliedSteps = 0
		local seenSteps = {}

		for _, step in ipairs(vocConfig.steps) do
			local stepReset = step.reset or 0
			lastStepReset = math.max(lastStepReset, stepReset)

			if stepReset > 0 and stepReset <= resetCount and not seenSteps[stepReset] then
				total = total + (step.bonus or 0)
				appliedSteps = appliedSteps + 1
				seenSteps[stepReset] = true
			end
		end

		local resetsWithinSteps = math.min(resetCount, lastStepReset)
		local gapsBeforeLastStep = math.max(0, resetsWithinSteps - appliedSteps)
		local beyond = math.max(0, resetCount - lastStepReset)
		return total + ((gapsBeforeLastStep + beyond) * defaultBonus)
	end

	if vocConfig.ranges and #vocConfig.ranges > 0 then
		for _, range in ipairs(vocConfig.ranges) do
			local effectiveMax = (range.maxReset == 0) and resetCount or range.maxReset
			local count = math.max(0, math.min(resetCount, effectiveMax) - range.minReset + 1)
			total = total + (count * (range.bonus or 0))
		end
		return total
	end

	return resetCount * defaultBonus
end

local function callPlayerMethod(player, methodName, ...)
	local method = player[methodName]
	if type(method) ~= "function" then
		return false
	end

	method(player, ...)
	return true
end

local function applyAttackSpeedBonus(player, vocation, bonus)
	if callPlayerMethod(player, "setResetAttackSpeedBonus", bonus) then
		return
	end

	if type(player.setAttackSpeed) ~= "function" then
		return
	end

	local baseSpeed = nil
	if vocation and type(vocation.getAttackSpeed) == "function" then
		baseSpeed = vocation:getAttackSpeed()
	elseif type(player.getAttackSpeed) == "function" then
		baseSpeed = player:getAttackSpeed()
	end

	if baseSpeed then
		player:setAttackSpeed(math.max(100, baseSpeed - bonus))
	end
end

function ResetBonusConfig.applyBonuses(player)
	local resets = player:getResetCount()
	local vocation = player:getVocation()
	local vocId = vocation and vocation:getId() or VOCATION_ALL

	local spd = 0
	if ResetBonusConfig.attackSpeed.enabled ~= false then
		spd = ResetBonusConfig.getTotalBonus("attackSpeed", resets, vocId)
	end
	applyAttackSpeedBonus(player, vocation, math.floor(spd))

	local dmg = 0
	if ResetBonusConfig.damage.enabled ~= false then
		dmg = ResetBonusConfig.getTotalBonus("damage", resets, vocId)
	end
	callPlayerMethod(player, "setResetDamageBonus", dmg)

	local def = 0
	if ResetBonusConfig.defense.enabled ~= false then
		def = ResetBonusConfig.getTotalBonus("defense", resets, vocId)
	end
	callPlayerMethod(player, "setResetDefenseBonus", def)

	local heal = 0
	if ResetBonusConfig.healing.enabled ~= false then
		heal = ResetBonusConfig.getTotalBonus("healing", resets, vocId)
	end
	callPlayerMethod(player, "setResetHealingBonus", heal)

	local hpConfig = ResetBonusConfig.hp
	if hpConfig and hpConfig.enabled ~= false then
		local hpTotal = ResetBonusConfig.getTotalBonus("hp", resets, vocId)
		if hpTotal > 0 then
			if hpConfig.bonusMode == "flat" then
				callPlayerMethod(player, "setResetHpBonus", math.floor(hpTotal))
			else
				callPlayerMethod(player, "setResetHpBonus", 0)
				local baseHp = player:getMaxHealth()
				callPlayerMethod(player, "setResetHpBonus", math.floor(baseHp * hpTotal / 100))
			end
		else
			callPlayerMethod(player, "setResetHpBonus", 0)
		end
	else
		callPlayerMethod(player, "setResetHpBonus", 0)
	end

	local manaConfig = ResetBonusConfig.mana
	if manaConfig and manaConfig.enabled ~= false then
		local manaTotal = ResetBonusConfig.getTotalBonus("mana", resets, vocId)
		if manaTotal > 0 then
			if manaConfig.bonusMode == "flat" then
				callPlayerMethod(player, "setResetManaBonus", math.floor(manaTotal))
			else
				callPlayerMethod(player, "setResetManaBonus", 0)
				local baseMana = player:getMaxMana()
				callPlayerMethod(player, "setResetManaBonus", math.floor(baseMana * manaTotal / 100))
			end
		else
			callPlayerMethod(player, "setResetManaBonus", 0)
		end
	else
		callPlayerMethod(player, "setResetManaBonus", 0)
	end

	local mpConfig = ResetBonusConfig.manaPotion
	if mpConfig and mpConfig.enabled ~= false then
		local mpBonus = ResetBonusConfig.getTotalBonus("manaPotion", resets, vocId)
		callPlayerMethod(player, "setResetManaPotionBonus", mpBonus)
	else
		callPlayerMethod(player, "setResetManaPotionBonus", 0)
	end

	local msConfig = ResetBonusConfig.manaSpell
	if msConfig and msConfig.enabled ~= false then
		local msBonus = ResetBonusConfig.getTotalBonus("manaSpell", resets, vocId)
		callPlayerMethod(player, "setResetManaSpellBonus", msBonus)
	else
		callPlayerMethod(player, "setResetManaSpellBonus", 0)
	end
end
