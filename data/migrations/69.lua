-- Safe to resume after a partially completed ALTER; no existing rights are removed.
function onUpdateDatabase()
    logMigration("Updating database to version 70 (Market destination and bounded delivery progress)")
    local changes = {
        {"market_offers", "delivery_depot_id", "SMALLINT UNSIGNED NULL DEFAULT NULL"},
        {"market_delivery_pending", "delivered_amount", "INT UNSIGNED NOT NULL DEFAULT 0"},
        {"market_delivery_pending", "retry_after", "BIGINT UNSIGNED NOT NULL DEFAULT 0"}
    }
    for _, change in ipairs(changes) do
        local query = db.storeQuery("SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = " ..
            db.escapeString(change[1]) .. " AND COLUMN_NAME = " .. db.escapeString(change[2]))
        if query then
            result.free(query)
        elseif not db.query("ALTER TABLE `" .. change[1] .. "` ADD COLUMN `" .. change[2] .. "` " .. change[3]) then
            logMigration("Failed to add " .. change[1] .. "." .. change[2])
            return false
        end
    end
    return true
end
