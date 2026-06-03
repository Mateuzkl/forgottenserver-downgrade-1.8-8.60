-- Luacheck configuration for TFS/revscripts Lua files.
-- The server exposes many globals from C++ at runtime, so they must be
-- declared here to avoid false positives in GitHub Actions.

std = "lua54"

-- Do not fail CI for formatting/style-only warnings. Syntax is already checked
-- by luac in the separate "Lua syntax check" job.
ignore = {
	"212", -- unused argument (common in event callbacks)
	"213", -- unused loop variable
	"631", -- line is too long
}

-- Project globals created by Lua modules/revscripts.
globals = {
	"onUpdateDatabase",
	"registerMonsterType",
	"CustomBestiary",
	"CustomBosstiary",
	"HuntingTasks",
	"PreySystem",
	"PreyMonsters",
	"Player",
	"MonsterType",
}

-- Runtime globals provided by the TFS Lua environment.
read_globals = {
	"addEvent",
	"configManager",
	"configKeys",
	"db",
	"result",
	"Game",
	"Event",
	"CreatureEvent",
	"MonsterEvent",
	"PacketHandler",
	"NetworkMessage",
	"MonsterSpell",
	"Loot",
	"ItemType",
	"logger",

	-- Message constants
	"MESSAGE_STATUS_DEFAULT",
	"MESSAGE_STATUS_SMALL",
	"MESSAGE_EVENT_ADVANCE",
	"MESSAGE_STATUS_CONSOLE_BLUE",

	-- Condition constants
	"CONDITION_INFIGHT",
	"CONDITIONID_DEFAULT",
	"CONDITIONID_COMBAT",
}
