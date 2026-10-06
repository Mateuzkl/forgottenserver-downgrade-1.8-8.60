-- Exercise the real migration's upgrade/failure contract without a game DB.
local migration = 'data/migrations/68.lua'
local function run(columnExists, failCount, failAlter, failCreate)
    local queries, freed = {}, false
    local env = {
        logMigration = function() end,
        db = {
            storeQuery = function(query)
                assert(query:find('information_schema', 1, true))
                return not failCount and 1 or false
            end,
            query = function(query)
                queries[#queries + 1] = query
                if query:find('ALTER TABLE', 1, true) then return not failAlter end
                assert(query:find('CREATE TABLE IF NOT EXISTS `player_save_journal`', 1, true))
                assert(query:find('ENGINE=InnoDB', 1, true))
                return not failCreate
            end
        },
        result = {
            getNumber = function() return columnExists and 1 or 0 end,
            free = function() freed = true end
        }
    }
    local chunk = assert(loadfile(migration, 't', env))
    chunk()
    local success = env.onUpdateDatabase()
    for _, query in ipairs(queries) do
        assert(not query:find('player_save_async_pending', 1, true), 'Legacy WAL must remain untouched')
        assert(not query:match('^%s*DELETE'), 'Migration must not discard journal evidence')
    end
    return success, #queries, freed
end

local success, count, freed = run(false)
assert(success and count == 2 and freed)
success, count, freed = run(true)
assert(success and count == 1 and freed)
success, count, freed = run(false, true)
assert(not success and count == 0 and not freed)
success, count, freed = run(false, false, true)
assert(not success and count == 1 and freed)
success, count = run(true, false, false, true)
assert(not success and count == 1)
-- Retrying after ALTER succeeded but CREATE failed must succeed without another ALTER.
success, count = run(true)
assert(success and count == 1)
print('Production save journal migration regression passed')
