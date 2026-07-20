-- Isolated local smoke configuration for the MiniBot integration.
-- It inherits the complete project defaults and only redirects external
-- services and listeners to the disposable test environment documented in
-- docs/MINIBOT_PORT_REPORT.md.
dofile('config.lua.dist')

ip = '127.0.0.1'
bindOnlyConfiguredIpAddress = true
loginProtocolPort = 37171
gameProtocolPort = 37172
statusProtocolPort = 37173
maxPlayers = 1

mysqlHost = '127.0.0.1'
mysqlUser = 'forgottenserver'
mysqlPass = 'CodexUserSmoke_860'
mysqlDatabase = 'forgottenserver'
mysqlPort = 33306

startupDatabaseOptimization = false
globalSaveEnabled = false
serverSaveCleanMap = false
