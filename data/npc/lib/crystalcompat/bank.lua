-- crystalcompat/bank.lua
-- Restores the bank parsers that the crystalserver-structured NPCs expect.
-- The "migrate Crystal NPCs to crystalserver structure" step imported NPCs
-- (Suzy, Jessica, Tesha, Elgar, Lokur, Hireling...) that call Npc:parseBank /
-- :parseBankMessages / :parseGuildBank and a global NpcBankGreetCallback, but
-- those helpers were never ported -> every bank interaction crashed with
-- "attempt to call a nil value (method 'parseBank')".
-- This reimplements them on top of the classic npcsystem NpcHandler (say + focus).

if _G.CrystalBankCompatLoaded then return end
_G.CrystalBankCompatLoaded = true

local state = {} -- [cid] = { action, amount, name, awaiting }

local function tell(npcHandler, creature, text)
	npcHandler:say(text, creature)
end

local function clear(cid)
	state[cid] = nil
end

-- Trailing amount for "deposit all" / "withdraw 100"; nil if none.
local function inlineAmount(msg, keyword, bankBalance, money)
	local sub = msg:match(keyword .. "%s+(.+)$")
	if not sub then
		return nil
	end
	sub = sub:gsub("%s+", "")
	if sub == "all" then
		return (keyword == "withdraw") and bankBalance or money
	end
	return tonumber(sub)
end

function Npc.parseBank(self, message, npc, creature, npcHandler)
	if not creature then
		return false
	end
	local player = Player(creature)
	if not player then
		return false
	end
	local cid = creature:getId()
	local msg = tostring(message):lower()
	local st = state[cid]

	-- Multi-step dialog in progress
	if st then
		if st.awaiting == "amount" then
			local amount = (msg == "all")
				and (st.action == "withdraw" and player:getBankBalance() or player:getMoney())
				or tonumber(msg)
			if not isValidMoney(amount) then
				clear(cid)
				tell(npcHandler, creature, "That is not a valid amount.")
				return true
			end
			st.amount = amount
			st.awaiting = "confirm"
			tell(npcHandler, creature, "Do you want to " .. st.action .. " " .. amount .. " gold coins? Say {yes} or {no}.")
			return true
		elseif st.awaiting == "confirm" then
			if msgcontains(msg, "yes") then
				local action, amount, name = st.action, st.amount, st.name
				clear(cid)
				if action == "deposit" then
					if player:getMoney() < amount then
						tell(npcHandler, creature, "You don't have enough money.")
					else
						player:depositMoney(amount)
						tell(npcHandler, creature, "You have deposited " .. amount .. " gold coins into your bank account.")
					end
				elseif action == "withdraw" then
					if player:getBankBalance() < amount then
						tell(npcHandler, creature, "You don't have that much money in your bank.")
					elseif not player:canCarryMoney(amount) then
						tell(npcHandler, creature, "You can't carry that much money.")
					else
						player:withdrawMoney(amount)
						tell(npcHandler, creature, "You have withdrawn " .. amount .. " gold coins.")
					end
				elseif action == "transfer" then
					local receiver = Player.getPlayerDatabaseInfo(name)
					if receiver and player:transferMoneyTo(receiver, amount) then
						tell(npcHandler, creature, "You have transferred " .. amount .. " gold coins to " .. name .. ".")
					else
						tell(npcHandler, creature, "The transfer failed. Check the amount and the name.")
					end
				end
				return true
			elseif msgcontains(msg, "no") then
				clear(cid)
				tell(npcHandler, creature, "As you wish.")
				return true
			end
			-- not yes/no: fall through so a fresh keyword can start over
		end
	end

	if msgcontains(msg, "balance") then
		tell(npcHandler, creature, "Your account balance is " .. player:getBankBalance() .. " gold.")
		return true
	end

	if msgcontains(msg, "deposit") then
		local amount = inlineAmount(msg, "deposit", player:getBankBalance(), player:getMoney())
		if amount and isValidMoney(amount) then
			state[cid] = { action = "deposit", amount = amount, awaiting = "confirm" }
			tell(npcHandler, creature, "Do you want to deposit " .. amount .. " gold coins? Say {yes} or {no}.")
		else
			state[cid] = { action = "deposit", awaiting = "amount" }
			tell(npcHandler, creature, "How much money would you like to deposit?")
		end
		return true
	end

	if msgcontains(msg, "withdraw") then
		local amount = inlineAmount(msg, "withdraw", player:getBankBalance(), player:getMoney())
		if amount and isValidMoney(amount) then
			state[cid] = { action = "withdraw", amount = amount, awaiting = "confirm" }
			tell(npcHandler, creature, "Do you want to withdraw " .. amount .. " gold coins? Say {yes} or {no}.")
		else
			state[cid] = { action = "withdraw", awaiting = "amount" }
			tell(npcHandler, creature, "How much money would you like to withdraw?")
		end
		return true
	end

	if msgcontains(msg, "transfer") then
		local amount = tonumber(msg:match("(%d+)"))
		local name = message:match("[Tt]o%s+(.+)$")
		if amount and isValidMoney(amount) and name and name ~= "" then
			state[cid] = { action = "transfer", amount = amount, name = name, awaiting = "confirm" }
			tell(npcHandler, creature, "Do you want to transfer " .. amount .. " gold coins to " .. name .. "? Say {yes} or {no}.")
		else
			tell(npcHandler, creature, "Please phrase it as: transfer <amount> to <name>.")
		end
		return true
	end

	return false
end

-- Guild bank is not supported by this compat; no-op avoids the nil-method crash.
function Npc.parseGuildBank(self, message, npc, creature, playerId, npcHandler)
	return false
end

-- Extra bank small talk (help/keywords) is left to each NPC's keyword tree.
function Npc.parseBankMessages(self, message, npc, creature, npcHandler)
	return false
end

-- Greet callback referenced by the bank NPCs. Called as callback(cid) by the
-- npcsystem NpcHandler. Clears any stale pending transaction so a player can't
-- leave a confirmation half-open, walk off, re-greet and say "yes" to fire the
-- old transaction. Returns true so the greeting proceeds normally.
function NpcBankGreetCallback(cid)
	if cid then
		state[cid] = nil
	end
	return true
end
