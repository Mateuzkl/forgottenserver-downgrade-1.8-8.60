--[[
================================================================================
  stress_db_pr69.lua  -  RevScript  (TFS 1.8 / 8.60 downgrade fork)
  Stress test para PR #69 - "Port threaded database login save"
  Repositório: Mateuzkl/forgottenserver-downgrade-1.8-8.60
================================================================================

  USO (apenas GMs com getAccess() == true):
    !stress start   - roda todas as 11 fases em sequência
    !stress 1-11    - roda uma fase individualmente
    !stress diag    - apenas diagnósticos InnoDB (Phase 10, não destrutivo)
    !stress info    - descreve cada fase e o que testa
    !stress clean   - dropa a tabela stress_pr69

  FASES E SQL EXERCITADO:
  ┌────┬────────────────────────────────┬────────────────────────────────────────┐
  │ Ph │ Área testada                   │ SQL / API                              │
  ├────┼────────────────────────────────┼────────────────────────────────────────┤
  │  1 │ ConnectionContext lazy         │ INSERT flood + db.escapeString()       │
  │    │ LAST_INSERT_ID / auto-commit   │ SELECT LAST_INSERT_ID()                │
  │  2 │ buildPlayerSave dirty snapshot │ WHERE key IN (...) + result:next()     │
  │  3 │ flushInFlight / pendingFlushes │ player:save() rapid-fire              │
  │  4 │ Deadlock retry ×3             │ SELECT...FOR UPDATE + Innodb_deadlocks │
  │  5 │ R/W misto / handle lifecycle   │ addEvent bursts (concurrent workers)   │
  │  6 │ Integridade count/dup/gap      │ EXPLAIN + ANALYZE TABLE                │
  │  7 │ Atomicidade de transação       │ START TRANSACTION + COMMIT + ROLLBACK  │
  │  8 │ DELETE + re-INSERT (save real) │ player_storage save pattern completo   │
  │  9 │ Batch INSERT em transação      │ multi-row VALUES() em um único query   │
  │ 10 │ InnoDB diagnostics             │ SHOW STATUS + SHOW ENGINE INNODB STATUS│
  │ 11 │ UPSERT (ON DUPLICATE KEY)      │ INSERT...ON DUPLICATE KEY UPDATE       │
  └────┴────────────────────────────────┴────────────────────────────────────────┘

  O QUE FOI CORRIGIDO/ADICIONADO vs versão anterior:
    + db.escapeString() em toda string parametrizada
    + result:next() loop - Ph2 de 60 SELECTs → 1 query com IN(...)
    + START TRANSACTION / COMMIT / ROLLBACK explícitos (Ph7)
    + ROLLBACK atomicity verification (Ph7b)
    + SELECT ... FOR UPDATE (Ph4) - bloqueio de linha correto
    + Innodb_deadlocks delta antes/após Phase 4
    + DELETE + re-INSERT transacional (Ph8) - espelho do player_storage save
    + Batch multi-row INSERT numa query só (Ph9)
    + SHOW STATUS + SHOW ENGINE INNODB STATUS (Ph10)
    + EXPLAIN para verificar uso de índice (Ph6/Ph10)
    + SELECT LAST_INSERT_ID() - verifica getLastInsertId() do PR (Ph1)
    + ON DUPLICATE KEY UPDATE - padrão real do IOLoginData (Ph11)
    + addEvent(0) burst concorrente no Ph5 (anteriormente era loop síncrono)

  SEGURANÇA:
    • Tabela isolada `stress_pr69` - nunca toca tabelas de produção.
    • Storage keys acima de STORAGE_BASE (95000) - limpas ao final de Ph2/Ph3.
    • run_id único por rodada (os.time() % 65535) - rodadas não se cruzam.
    • Requer player:getGroup():getAccess() == true.
================================================================================
--]]

-- ============================================================================
-- CONFIGURAÇÃO
-- ============================================================================
local STRESS_TABLE = "stress_pr69"
local STORAGE_BASE = 95000   -- mude se colidir com storage keys do seu server
local REPORT_DELAY = 1200    -- ms aguardados antes de verificar flushes assíncronos

local CFG = {
    -- Phase 1
    ph1_inserts        = 300,
    ph1_tx_batch       = 100,   -- INSERTs dentro da sub-transação explícita

    -- Phase 2
    ph2_storage_keys   = 60,    -- N keys; metade (pares) será removida

    -- Phase 3
    ph3_save_count     = 12,
    ph3_stagger_ms     = 15,

    -- Phase 4
    ph4_sentinel_rows  = 4,
    ph4_update_bursts  = 30,

    -- Phase 5
    ph5_event_bursts   = 50,    -- goroutinas addEvent(0) disparadas

    -- Phase 7
    ph7_commit_rows    = 80,    -- linhas no path COMMIT
    ph7_rollback_rows  = 25,    -- linhas no path ROLLBACK (devem sumir)

    -- Phase 8
    ph8_rows           = 60,    -- linhas v1; serão deletadas e re-inseridas como v2

    -- Phase 9
    ph9_batch_size     = 150,   -- linhas num único multi-row INSERT

    -- Phase 11
    ph11_upsert_rows   = 50,    -- linhas para o ON DUPLICATE KEY UPDATE
}

-- Constantes de mensagem podem mudar entre forks.
local MSG_BLUE = MESSAGE_STATUS_CONSOLE_BLUE or MESSAGE_EVENT_ADVANCE or 19
local MSG_RED = MESSAGE_STATUS_CONSOLE_RED or MESSAGE_STATUS_WARNING or MSG_BLUE

local activeRun = false

-- ============================================================================
-- UTILIDADES
-- ============================================================================

-- Códigos ANSI para cores no console
local COLOR_RESET = "\27[0m"
local COLOR_BLUE = "\27[94m"
local COLOR_GREEN = "\27[32m"
local COLOR_YELLOW = "\27[33m"
local COLOR_RED = "\27[31m"
local COLOR_ORANGE = "\27[38;5;208m"

local function log(player, msg)
    local coloredMsg = msg:gsub("(Phase %d+:)", COLOR_ORANGE .. "%1" .. COLOR_RESET)
    print(COLOR_BLUE .. "[StressDB]" .. COLOR_RESET .. " " .. coloredMsg)
    if player and player:isPlayer() then
        player:sendTextMessage(MSG_BLUE, "[StressDB] " .. msg)
    end
end

local function logFail(player, msg)
    local coloredMsg = msg:gsub("(Phase %d+:)", COLOR_ORANGE .. "%1" .. COLOR_RESET)
    print(COLOR_BLUE .. "[StressDB]" .. COLOR_RED .. "[FAIL]" .. COLOR_RESET .. " " .. coloredMsg)
    if player and player:isPlayer() then
        player:sendTextMessage(MSG_RED, "[StressDB][FAIL] " .. msg)
    end
end

local function logPass(player, msg)
    local coloredMsg = msg:gsub("(Phase %d+[abc]? [A-Z]+:)", COLOR_ORANGE .. "%1" .. COLOR_RESET)
    coloredMsg = coloredMsg:gsub("(Phase %d+[abc]?:)", COLOR_ORANGE .. "%1" .. COLOR_RESET)
    coloredMsg = coloredMsg:gsub("(Ph%d+ [A-Z]+:)", COLOR_ORANGE .. "%1" .. COLOR_RESET)
    coloredMsg = coloredMsg:gsub("(Ph%d+:)", COLOR_ORANGE .. "%1" .. COLOR_RESET)
    print(COLOR_BLUE .. "[StressDB]" .. COLOR_YELLOW .. "[PASS]" .. COLOR_RESET .. " " .. coloredMsg)
    if player and player:isPlayer() then
        player:sendTextMessage(MSG_BLUE, "[StressDB][PASS] " .. msg)
    end
end

local function logInfo(player, msg)
    local coloredMsg = msg:gsub("(Phase %d+)", COLOR_ORANGE .. "%1" .. COLOR_RESET)
    coloredMsg = coloredMsg:gsub("(Ph%d+ [A-Z]+:)", COLOR_ORANGE .. "%1" .. COLOR_RESET)
    coloredMsg = coloredMsg:gsub("(Ph%d+:)", COLOR_ORANGE .. "%1" .. COLOR_RESET)
    print(COLOR_BLUE .. "[StressDB]" .. COLOR_GREEN .. "[INFO]" .. COLOR_RESET .. " " .. coloredMsg)
    if player and player:isPlayer() then
        player:sendTextMessage(MSG_BLUE, "[StressDB][INFO] " .. msg)
    end
end

local function safePlayer(pid)
    local p = Player(pid)
    if not p then
        print("[StressDB] Player id=" .. tostring(pid) .. " desconectou durante teste.")
    end
    return p
end

-- Compatibilidade entre forks:
-- Em TFS normal db.escapeString("abc") ja retorna 'abc'.
-- Em alguns forks pode retornar apenas abc. Esta função sempre devolve string SQL com aspas.
local function sqlString(value)
    local escaped = db.escapeString(tostring(value or ""))
    if not escaped or escaped == "" then
        return "''"
    end

    local first = escaped:sub(1, 1)
    local last = escaped:sub(-1)
    if first == "'" and last == "'" then
        return escaped
    end
    return "'" .. escaped .. "'"
end

-- Wrapper para db.storeQuery que retorna um objeto Result compatível
-- Lê um STATUS variable do MySQL como número (0 se não encontrado)
local function readStatusVar(varName)
    local res = db.storeQuery("SHOW STATUS LIKE " .. sqlString(varName))
    if not res or res == false or res == nil then return 0 end
    local v = tonumber(result.getString(res, "Value")) or 0
    result.free(res)
    return v
end

-- Helper: executa storeQuery e chama callback se resultado for userdata
local function withQuery(query, callback)
    local res = db.storeQuery(query)
    if res and res ~= false and res ~= nil then
        local result = callback(res)
        result.free(res)
        return result
    end
    return nil
end

-- ============================================================================
-- SETUP / TEARDOWN
-- ============================================================================
local function setupTable()
    db.query("DROP TABLE IF EXISTS `" .. STRESS_TABLE .. "`")
    return db.query(string.format([[
        CREATE TABLE `%s` (
            `id`      INT UNSIGNED      NOT NULL AUTO_INCREMENT,
            `run_id`  SMALLINT UNSIGNED NOT NULL DEFAULT 0,
            `phase`   TINYINT UNSIGNED  NOT NULL DEFAULT 0,
            `seq`     INT UNSIGNED      NOT NULL DEFAULT 0,
            `label`   VARCHAR(128)      NOT NULL DEFAULT '',
            `counter` INT               NOT NULL DEFAULT 0,
            `ts`      BIGINT            NOT NULL DEFAULT 0,
            PRIMARY KEY (`id`),
            UNIQUE KEY `uq_run_phase_seq` (`run_id`, `phase`, `seq`),
            KEY `idx_run_phase` (`run_id`, `phase`)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
    ]], STRESS_TABLE))
end

local function teardownTable()
    return db.query("DROP TABLE IF EXISTS `" .. STRESS_TABLE .. "`")
end

-- ============================================================================
-- PHASE 1 - INSERT flood + db.escapeString + LAST_INSERT_ID
-- ============================================================================
--[[
  Testa a ConnectionContext thread_local do PR (dispatcher thread).
  Garante que a conexão é criada lazily e permanece aberta entre queries.
  NOVIDADES vs versão anterior:
    • db.escapeString() em TODOS os valores de string - evita SQL injection
      mesmo num script de teste e confirma que a API funciona corretamente.
    • Sub-teste com transação explícita: START TRANSACTION + ph1_tx_batch
      INSERTs + COMMIT - compara throughput com/sem auto-commit.
    • SELECT LAST_INSERT_ID() após INSERT - testa Database::getLastInsertId()
      que o PR mantém por ConnectionContext (affinity de conexão).
  FALHA ESPERADA SE: db.escapeString() retorna nil, LAST_INSERT_ID é 0,
  ou a throughput da transação for menor que a do auto-commit (indica overhead).
--]]
local function runPhase1(player, runId)
    local n  = CFG.ph1_inserts
    local nb = CFG.ph1_tx_batch

    log(player, string.format("Phase 1: INSERT flood - %d auto-commit + %d em transacao...", n, nb))

    -- ── 1a: auto-commit individual ──────────────────────────────────────────
    local t0 = os.clock()
    local ok, fail = 0, 0

    for i = 1, n do
        -- db.escapeString() em toda string parametrizada
        local safeLabel = sqlString("ph1_ac_" .. i)
        local q = string.format(
            "INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`ts`) VALUES (%d,1,%d,%s,%d)",
            STRESS_TABLE, runId, i, safeLabel, os.time()
        )
        if db.query(q) then ok = ok + 1 else fail = fail + 1 end
    end

    local elapsedAC = (os.clock() - t0)
    local qpsAC = ok / (elapsedAC + 1e-9)

    -- ── 1b: dentro de transação explícita ────────────────────────────────────
    -- Espelha o que flushPlayerSave faz: abre TX, reaplica N queries, COMMIT
    local t1 = os.clock()
    local txOk = true

    db.query("START TRANSACTION")
    for i = 1, nb do
        local safeLabel = sqlString("ph1_tx_" .. i)
        local q = string.format(
            "INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`ts`) VALUES (%d,1,%d,%s,%d)",
            STRESS_TABLE, runId, n + i, safeLabel, os.time()
        )
        if not db.query(q) then txOk = false end
    end
    if txOk then db.query("COMMIT") else db.query("ROLLBACK") end

    local elapsedTX = (os.clock() - t1)
    local qpsTX = nb / (elapsedTX + 1e-9)

    -- ── 1c: LAST_INSERT_ID - verifica affinity de conexão (worker thread) ──
    -- Runs on scheduler thread via addEvent to exercise per-thread ConnectionContext
    local playerId = player:getId()
    local runIdC = runId
    local failC = fail
    local txOkC = txOk
    local elapsedACC = elapsedAC
    local elapsedTXC = elapsedTX
    local qpsACC = qpsAC
    local qpsTXC = qpsTX
    local nC = n
    local nbC = nb

    addEvent(function(pId, rId, nVal, nbVal, failVal, txOkVal, elapsedAC, elapsedTX, qpsAC, qpsTX)
        local p = safePlayer(pId)
        if not p then return end

        local safeLabel2 = sqlString("ph1_lastid_probe")
        db.query(string.format(
            "INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`ts`) VALUES (%d,1,%d,%s,%d)",
            STRESS_TABLE, rId, nVal + nbVal + 1, safeLabel2, os.time()
        ))
        local lastId = db.lastInsertId()

        if failVal == 0 and txOkVal and lastId > 0 then
            logPass(p, string.format(
                "Phase 1: AC=%d/%d (%.0f q/s | %.1fms) | TX=%d (%.0f q/s | %.1fms) | LAST_INSERT_ID=%d",
                nVal, nVal, qpsAC, elapsedAC * 1000,
                nbVal, qpsTX, elapsedTX * 1000,
                lastId
            ))
        else
            if failVal > 0 then
                logFail(p, string.format("Phase 1: %d INSERTs falharam - verifique ConnectionContext.", failVal))
            end
            if not txOkVal then
                logFail(p, "Phase 1: INSERT dentro de transacao falhou - START TRANSACTION/COMMIT com problema.")
            end
            if lastId == 0 then
                logFail(p, "Phase 1: LAST_INSERT_ID=0 - getLastInsertId() pode estar retornando de conexao errada!")
            end
        end
    end, 1, playerId, runIdC, nC, nbC, failC, txOkC, elapsedACC, elapsedTXC, qpsACC, qpsTXC)

    -- Phase 1c runs async; return early (pass/fail reported via callback above)

    -- ── Resultado (Phases 1a + 1b) ────────────────────────────────────────────
    if fail == 0 and txOk then
        logInfo(player, string.format(
            "Phase 1a-b: AC=%d/%d (%.0f q/s | %.1fms) | TX=%d (%.0f q/s | %.1fms) | Phase 1c async...",
            ok, n, qpsAC, elapsedAC * 1000,
            nb, qpsTX, elapsedTX * 1000
        ))
    else
        if fail > 0 then
            logFail(player, string.format("Phase 1: %d INSERTs falharam - verifique ConnectionContext.", fail))
        end
        if not txOk then
            logFail(player, "Phase 1: INSERT dentro de transacao falhou - START TRANSACTION/COMMIT com problema.")
        end
    end

    return fail == 0 and txOk
end

-- ============================================================================
-- PHASE 2 - Storage dirty snapshot  (buildPlayerSave + flushPlayerSave)
-- ============================================================================
--[[
  Testa que buildPlayerSave() captura o snapshot correto de
  modifiedStorageKeys e removedStorageKeys, e que flushPlayerSave()
  replica o SQL em transação no worker.
  NOVIDADE vs versão anterior:
    • Verificação reescrita com UMA query usando WHERE key IN (...) +
      loop result:next() - antes eram 60 SELECTs individuais (1 por key).
      Isso também testa que result:next() funciona corretamente para
      resultado multi-row e que result:free() não vaza handles.
  FALHA ESPERADA SE: snapshot omitiu keys (faltam no banco), keys removidas
  ainda aparecem, result:next() para cedo demais, ou há valor errado.
--]]
local function runPhase2(player, runId)
    local n    = CFG.ph2_storage_keys
    local base = STORAGE_BASE + 2000

    log(player, string.format("Phase 2: Storage dirty snapshot - %d keys (verificacao com IN + next())...", n))

    -- Seta todas as keys; remove as pares
    local t0 = os.clock()
    for i = 1, n do
        player:setStorageValue(base + i, i * 7)
    end
    for i = 1, n do
        if i % 2 == 0 then
            player:setStorageValue(base + i, -1)  -- -1 = remove no TFS
        end
    end
    log(player, string.format(
        "Phase 2: %d setadas | %d para remocao | %.1fms | disparando save...",
        n, math.floor(n / 2), (os.clock() - t0) * 1000
    ))

    local saved = player:save()
    if not saved then
        logFail(player, "Phase 2: player:save() retornou false!")
        return false
    end

    local pid  = player:getId()
    local guid = player:getGuid()

    addEvent(function(playerId, playerGuid, nKeys, keyBase)
        local p = safePlayer(playerId)
        if not p then return end

        -- Monta a lista IN(key1, key2, ...) de todas as keys ímpares esperadas
        local presentKeys = {}
        for i = 1, nKeys do
            if i % 2 ~= 0 then
                presentKeys[#presentKeys + 1] = tostring(keyBase + i)
            end
        end
        local inList = table.concat(presentKeys, ",")

        -- Uma query só busca todas as keys esperadas como presentes
        local found = {}  -- found[key] = value
        local res = db.storeQuery(string.format(
            "SELECT `key`, `value` FROM `player_storage` WHERE `player_id`=%d AND `key` IN (%s)",
            playerGuid, inList
        ))
        if res and res ~= false and res ~= nil then
            repeat
                local k = result.getNumber(res, "key")
                local v = result.getNumber(res, "value")
                found[k] = v
            until not result.next(res)  -- ← result:next() itera multi-row
            result.free(res)
        end

        -- Verifica keys ímpares (devem estar presentes com valor i*7)
        local missingPresent, wrongValue = 0, 0
        for i = 1, nKeys do
            if i % 2 ~= 0 then
                local key = keyBase + i
                local v   = found[key]
                if v == nil then
                    missingPresent = missingPresent + 1
                elseif v ~= i * 7 then
                    wrongValue = wrongValue + 1
                end
            end
        end

        -- Verifica keys pares (NÃO devem estar no banco)
        local wronglyPresent = 0
        local evenKeys = {}
        for i = 1, nKeys do
            if i % 2 == 0 then
                evenKeys[#evenKeys + 1] = tostring(keyBase + i)
            end
        end
        local res2 = db.storeQuery(string.format(
            "SELECT COUNT(*) AS cnt FROM `player_storage` WHERE `player_id`=%d AND `key` IN (%s)",
            playerGuid, table.concat(evenKeys, ",")
        ))
        if res2 and res2 ~= false and res2 ~= nil then
            wronglyPresent = result.getNumber(res2, "cnt")
            result.free(res2)
        end

        local expPresent = math.ceil(nKeys / 2)
        local expAbsent  = math.floor(nKeys / 2)
        local total = missingPresent + wrongValue + wronglyPresent

        if total == 0 then
            logPass(p, string.format(
                "Phase 2: %d presentes OK | %d ausentes OK | result:next() iterou %d linhas corretamente",
                expPresent, expAbsent, #presentKeys
            ))
        else
            logFail(p, string.format(
                "Phase 2: %d faltando | %d valor errado | %d nao-removidas no banco",
                missingPresent, wrongValue, wronglyPresent
            ))
            logFail(p, "  -> Verifique buildPlayerSave snapshot + result:next() do storeQuery.")
        end

        -- Cleanup
        for i = 1, nKeys do
            p:setStorageValue(keyBase + i, -1)
        end
        p:save()

    end, REPORT_DELAY + 800, pid, guid, n, base)

    return true
end

-- ============================================================================
-- PHASE 3 - Save flood  (flushInFlight + pendingFlushes ordering)
-- ============================================================================
--[[
  (sem alterações estruturais - lógica já estava correta)
  Agenda N saves com intervalo stagger_ms. Cada save escreve um valor
  diferente na mesma storage key. O ÚLTIMO valor deve persistir.
--]]
local function runPhase3(player, runId)
    local n       = CFG.ph3_save_count
    local stagger = CFG.ph3_stagger_ms
    local key     = STORAGE_BASE + 3000
    local pid     = player:getId()
    local guid    = player:getGuid()

    log(player, string.format(
        "Phase 3: Save flood - %d saves | stagger=%dms | key=%d...",
        n, stagger, key
    ))

    for i = 1, n do
        addEvent(function(playerId, iteration, storageKey)
            local p = safePlayer(playerId)
            if not p then return end
            p:setStorageValue(storageKey, iteration * 99)
            p:save()
        end, i * stagger, pid, i, key)
    end

    local verifyDelay = (n * stagger) + REPORT_DELAY + 1000

    addEvent(function(playerId, playerGuid, storageKey, expectedVal, totalSaves)
        local p = safePlayer(playerId)
        if not p then return end

        local res = db.storeQuery(string.format(
            "SELECT `value` FROM `player_storage` WHERE `player_id`=%d AND `key`=%d",
            playerGuid, storageKey
        ))

        if res and res ~= false and res ~= nil then
            local dbVal = result.getNumber(res, "value")
            result.free(res)
            if dbVal == expectedVal then
                logPass(p, string.format(
                    "Phase 3: DB=%d (esperado %d) - flush ordering OK (%d saves)",
                    dbVal, expectedVal, totalSaves
                ))
            else
                logFail(p, string.format(
                    "Phase 3: DB=%d esperado=%d - ORDERING BUG em pendingFlushes!",
                    dbVal, expectedVal
                ))
                logFail(p, "  -> Verifique SaveManager::onPlayerFlushed + pendingFlushes drain.")
            end
        else
            logFail(p, string.format(
                "Phase 3: key %d nao encontrada no banco - save DESCARTADO!", storageKey
            ))
        end

        p:setStorageValue(storageKey, -1)
        p:save()

    end, verifyDelay, pid, guid, key, n * 99, n)

    log(player, string.format("Phase 3: %d saves agendados | resultado em ~%dms", n, verifyDelay))
    return true
end

-- ============================================================================
-- PHASE 4 - Lock contention + SELECT...FOR UPDATE + Innodb_deadlocks delta
-- ============================================================================
--[[
  Testa DBTransaction::executeWithinTransactionRollbackOnFailure (retry ×3).

  NOVIDADES vs versão anterior:
    • SELECT ... FOR UPDATE antes do UPDATE - bloqueio de linha explícito,
      padrão correto para gerar deadlock/lock-wait no InnoDB.
      Sem isso, UPDATEs em auto-commit raramente geram deadlock real.
    • Lê SHOW STATUS LIKE 'Innodb_deadlocks' ANTES e DEPOIS dos bursts.
      Imprime o delta real de deadlocks que o InnoDB registrou.
      Isso confirma se o retry path foi realmente ativado.
    • Lê 'Innodb_row_lock_waits' e 'Innodb_row_lock_time_avg' para
      mostrar latência de contenção.

  FALHA ESPERADA SE: counter < expected (update perdido sem retry),
  ou delta_deadlocks == 0 (nenhum deadlock real gerado - teste trivial).
--]]
local function runPhase4(player, runId)
    local rows  = CFG.ph4_sentinel_rows
    local burst = CFG.ph4_update_bursts
    local pid   = player:getId()

    log(player, string.format(
        "Phase 4: Lock contention + FOR UPDATE - %d linhas x %d bursts...",
        rows, burst
    ))

    -- Lê contadores InnoDB ANTES dos bursts
    local deadlocksBefore  = readStatusVar("Innodb_deadlocks")
    local lockWaitsBefore  = readStatusVar("Innodb_row_lock_waits")

    -- Insere sentinelas com counter=0
    local inserted = 0
    for r = 1, rows do
        local safeLabel = sqlString("sentinel_" .. r)
        if db.query(string.format(
            "INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`counter`,`ts`) VALUES (%d,4,%d,%s,0,%d)",
            STRESS_TABLE, runId, r, safeLabel, os.time()
        )) then
            inserted = inserted + 1
        end
    end

    if inserted == 0 then
        logFail(player, "Phase 4: Falha ao inserir sentinelas. Abortando.")
        return false
    end

    if inserted ~= rows then
        logFail(player, string.format(
            "Phase 4: Inconsistencia - inserted=%d != rows=%d. Abortando.", inserted, rows
        ))
        return false
    end

    -- Calcula total esperado e dispara bursts de UPDATE com FOR UPDATE
    -- NOTA: cada UPDATE afeta 2 rows (seq IN (first, second)), então totalExpected *= 2
    local totalExpected = 0
    for b = 1, burst do
        local inc = (b % 2 == 0) and 2 or 1
        totalExpected = totalExpected + (inc * inserted * 2)  -- Multiplica por 2

        for r = 1, rows do
            local seqA = r
            local seqB = (r % rows) + 1
            addEvent(function(tbl, rId, a, b, increment)
                -- SELECT ... FOR UPDATE em duas linhas com ordem de lock oposta
                -- para gerar deadlock real entre workers concorrentes.
                local first = increment % 2 == 1 and a or b
                local second = increment % 2 == 1 and b or a
                db.query("START TRANSACTION")
                local lockRes1 = db.storeQuery(string.format(
                    "SELECT `counter` FROM `%s` WHERE `run_id`=%d AND `phase`=4 AND `seq`=%d FOR UPDATE",
                    tbl, rId, first
                ))
                local lockRes2 = db.storeQuery(string.format(
                    "SELECT `counter` FROM `%s` WHERE `run_id`=%d AND `phase`=4 AND `seq`=%d FOR UPDATE",
                    tbl, rId, second
                ))
                if lockRes1 and lockRes1 ~= false
                   and lockRes2 and lockRes2 ~= false then
                    result.free(lockRes1)
                    result.free(lockRes2)
                    db.query(string.format(
                        "UPDATE `%s` SET `counter`=`counter`+%d, `ts`=%d WHERE `run_id`=%d AND `phase`=4 AND `seq` IN (%d,%d)",
                        tbl, increment, os.time(), rId, first, second
                    ))
                    db.query("COMMIT")
                else
                    if lockRes1 and lockRes1 ~= false then result.free(lockRes1) end
                    if lockRes2 and lockRes2 ~= false then result.free(lockRes2) end
                    db.query("ROLLBACK")
                end
            end, 0, STRESS_TABLE, runId, seqA, seqB, inc)
        end
    end

    local verifyDelay = REPORT_DELAY + 2000

    addEvent(function(playerId, rId, nRows, expected, dlBefore, lwBefore)
        local p = safePlayer(playerId)
        if not p then return end

        local actualTotal = 0
        local rowsFound   = 0

        for r = 1, nRows do
            local res = db.storeQuery(string.format(
                "SELECT `counter` FROM `%s` WHERE `run_id`=%d AND `phase`=4 AND `seq`=%d",
                STRESS_TABLE, rId, r
            ))
            if res and res ~= false and res ~= nil then
                actualTotal = actualTotal + result.getNumber(res, "counter")
                rowsFound   = rowsFound + 1
                result.free(res)
            end
        end

        -- Lê contadores InnoDB DEPOIS
        local deadlocksAfter = readStatusVar("Innodb_deadlocks")
        local lockWaitsAfter = readStatusVar("Innodb_row_lock_waits")
        local dlDelta  = deadlocksAfter  - dlBefore
        local lwDelta  = lockWaitsAfter  - lwBefore
        local lockAvg  = readStatusVar("Innodb_row_lock_time_avg")

        logInfo(p, string.format(
            "Phase 4 InnoDB: deadlocks delta=%d | lock_waits delta=%d | lock_time_avg=%dms",
            dlDelta, lwDelta, lockAvg
        ))

        if dlDelta == 0 and lwDelta == 0 then
            logInfo(p, "  -> Nenhum deadlock/wait detectado - teste pode nao ter gerado contencao real.")
            logInfo(p, "     Aumente ph4_update_bursts ou ph4_sentinel_rows para maior pressao.")
        end

        if rowsFound == nRows and actualTotal == expected then
            logPass(p, string.format(
                "Phase 4: %d linhas | counter=%d/%d - todos UPDATEs comitaram (retry OK)",
                rowsFound, actualTotal, expected
            ))
        elseif rowsFound == nRows then
            logFail(p, string.format(
                "Phase 4: counter=%d esperado=%d - %d UPDATEs perdidos!",
                actualTotal, expected, expected - actualTotal
            ))
            logFail(p, "  -> Verifique executeWithinTransactionRollbackOnFailure + lastQueryWasDeadlock().")
        else
            logFail(p, string.format("Phase 4: Apenas %d/%d linhas encontradas.", rowsFound, nRows))
        end

    end, verifyDelay, pid, runId, rows, totalExpected, deadlocksBefore, lockWaitsBefore)

    log(player, string.format(
        "Phase 4: %d UPDATEs (com FOR UPDATE) | expected counter=%d | resultado em ~%dms",
        burst * rows, totalExpected, verifyDelay
    ))
    return true
end

-- ============================================================================
-- PHASE 5 - Concurrent addEvent(0) burst (worker pool saturation)
-- ============================================================================
--[[
  NOVIDADE vs versão anterior: antes era um loop síncrono no dispatcher -
  todos os db.query() rodavam sequencialmente na thread do dispatcher.
  Agora dispara N addEvent(0) que caem no scheduler e são processados
  pelos workers DatabaseTasks com suas próprias ConnectionContexts (PR#69).
  Isso realmente exercita múltiplas ConnectionContexts simultâneas.

  Intercala SELECTs e INSERTs para:
    • Verificar que diferentes workers não interferem nas conexões uns dos outros.
    • Confirmar que result handles criados em workers são liberados corretamente.
    • Medir throughput real com concorrência (vs. sequencial do dispatcher).
--]]
local function runPhase5(player, runId)
    local n   = CFG.ph5_event_bursts
    local pid = player:getId()

    log(player, string.format("Phase 5: %d addEvent(0) bursts concorrentes...", n))

    local t0 = os.clock()

    for i = 1, n do
        local isWrite = (i % 2 == 0)
        addEvent(function(tbl, rId, seq, write)
            if write then
                local safeLabel = sqlString("burst_" .. seq)
                db.query(string.format(
                    "INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`ts`) VALUES (%d,5,%d,%s,%d)",
                    tbl, rId, seq, safeLabel, os.time()
                ))
            else
                -- SELECT + result:next() num worker - testa handle lifecycle fora do dispatcher
                local res = db.storeQuery(string.format(
                    "SELECT `seq`,`label` FROM `%s` WHERE `run_id`=%d AND `phase`=1 LIMIT 3",
                    tbl, rId
                ))
                if res and res ~= false and res ~= nil then
                    repeat
                        local _ = result.getNumber(res, "seq")
                    until not result.next(res)
                    result.free(res)
                end
            end
        end, 0, STRESS_TABLE, runId, i, isWrite)
    end

    -- Verifica após assentamento
    local verifyDelay = REPORT_DELAY + 500

    addEvent(function(playerId, rId, expectedWrites, wallStart)
        local p = safePlayer(playerId)
        if not p then return end

        local res = db.storeQuery(string.format(
            "SELECT COUNT(*) AS cnt FROM `%s` WHERE `run_id`=%d AND `phase`=5",
            STRESS_TABLE, rId
        ))
        local cnt = 0
        if res and res ~= false and res ~= nil then
            cnt = result.getNumber(res, "cnt")
            result.free(res)
        end

        local elapsed = (os.clock() - wallStart) * 1000

        if cnt == expectedWrites then
            logPass(p, string.format(
                "Phase 5: %d/%d writes chegaram | %.0fms wall | workers concorrentes OK",
                cnt, expectedWrites, elapsed
            ))
        else
            logFail(p, string.format(
                "Phase 5: Apenas %d/%d writes - %d perdidos em workers concorrentes!",
                cnt, expectedWrites, expectedWrites - cnt
            ))
        end
    end, verifyDelay, pid, runId, math.floor(n / 2), t0)

    log(player, string.format(
        "Phase 5: %d events disparados | resultado em ~%dms", n, verifyDelay
    ))
    return true
end

-- ============================================================================
-- PHASE 6 - Integridade: count + dup + gap + EXPLAIN + ANALYZE TABLE
-- ============================================================================
--[[
  NOVIDADES vs versão anterior:
    • EXPLAIN SELECT no índice da tabela - verifica que `idx_run_phase`
      está sendo usado. Se key=NULL, a query está fazendo full scan.
    • ANALYZE TABLE - força atualização das estatísticas de índice.
    • Agora cobre TODAS as fases (1-5, 7, 8, 9, 11) pelo run_id.
--]]
local function runPhase6(player, runId)
    log(player, "Phase 6: Integridade + EXPLAIN + ANALYZE TABLE...")
    local t0 = os.clock()

    -- ANALYZE TABLE antes de qualquer SELECT para estatísticas atualizadas
    db.query("ANALYZE TABLE `" .. STRESS_TABLE .. "`")

    local function countPhase(phase)
        local res = db.storeQuery(string.format(
            "SELECT COUNT(*) AS cnt FROM `%s` WHERE `run_id`=%d AND `phase`=%d",
            STRESS_TABLE, runId, phase
        ))
        if not res or res == false or res == nil then return -1 end
        local c = result.getNumber(res, "cnt")
        result.free(res)
        return c
    end

    local function hasDups(phase)
        local res = db.storeQuery(string.format(
            [[SELECT COUNT(*) AS dups FROM (
                SELECT `seq` FROM `%s`
                WHERE `run_id`=%d AND `phase`=%d
                GROUP BY `seq` HAVING COUNT(*) > 1
            ) AS t]],
            STRESS_TABLE, runId, phase
        ))
        if not res or res == false or res == nil then return -1 end
        local d = result.getNumber(res, "dups")
        result.free(res)
        return d
    end

    local function gapCount()
        local res = db.storeQuery(string.format(
            "SELECT MAX(`seq`) AS mx, COUNT(*) AS tot FROM `%s` WHERE `run_id`=%d AND `phase`=1",
            STRESS_TABLE, runId
        ))
        if not res or res == false or res == nil then return -1 end
        local mx  = result.getNumber(res, "mx")
        local tot = result.getNumber(res, "tot")
        result.free(res)
        return mx - tot  -- 0 = sem gaps
    end

    -- EXPLAIN para verificar uso do índice idx_run_phase
    local function checkIndexUsed()
        local res = db.storeQuery(string.format(
            "EXPLAIN SELECT * FROM `%s` WHERE `run_id`=%d AND `phase`=1 LIMIT 1",
            STRESS_TABLE, runId
        ))
        if not res or res == false or res == nil then return "N/A" end
        local keyUsed = result.getString(res, "key")
        local rows    = result.getNumber(res, "rows")
        result.free(res)
        if keyUsed and keyUsed ~= "" then
            return string.format("%s (%d rows estimadas)", keyUsed, rows)
        else
            return "NONE (full scan!)"
        end
    end

    -- Contagens esperadas
    local expPh1 = CFG.ph1_inserts + CFG.ph1_tx_batch + 1  -- auto-commit + tx + probe
    local expPh5 = math.floor(CFG.ph5_event_bursts / 2)
    local expPh7 = CFG.ph7_commit_rows + 1 -- COMMIT rows + 1 row antes do SAVEPOINT; rollback rows NAO devem aparecer
    local expPh8 = CFG.ph8_rows          -- apenas v2 rows (v1 deletadas)
    local expPh9 = CFG.ph9_batch_size
    local expPh11 = CFG.ph11_upsert_rows -- ON DUPLICATE = sem duplicatas

    local cPh1  = countPhase(1)
    local cPh5  = countPhase(5)
    local cPh7  = countPhase(7)
    local cPh8  = countPhase(8)
    local cPh9  = countPhase(9)
    local cPh11 = countPhase(11)
    local dPh1  = hasDups(1)
    local dPh9  = hasDups(9)
    local dPh11 = hasDups(11)
    local gaps  = gapCount()
    local idxInfo = checkIndexUsed()

    local elapsed = (os.clock() - t0) * 1000

    logInfo(player, string.format("Phase 6: EXPLAIN idx = %s", idxInfo))
    logInfo(player, string.format(
        "Phase 6: Ph1=%d/%d | Ph5=%d/%d | Ph7=%d/%d | Ph8=%d/%d | Ph9=%d/%d | Ph11=%d/%d",
        cPh1, expPh1, cPh5, expPh5, cPh7, expPh7, cPh8, expPh8, cPh9, expPh9, cPh11, expPh11
    ))

    local allOk = (cPh1 == expPh1) and (cPh5 == expPh5)
               and (cPh7 == expPh7) and (cPh8 == expPh8)
               and (cPh9 == expPh9) and (cPh11 == expPh11)
               and (dPh1 == 0) and (dPh9 == 0) and (dPh11 == 0)
               and (gaps == 0)
               and (not idxInfo:find("NONE"))

    if allOk then
        logPass(player, string.format(
            "Phase 6: ALL OK | dup1=%d | dup9=%d | dup11=%d | gaps=%d | %.1fms",
            dPh1, dPh9, dPh11, gaps, elapsed
        ))
    else
        logFail(player, string.format(
            "Phase 6: dup1=%d | dup9=%d | dup11=%d | gaps=%d | %.1fms",
            dPh1, dPh9, dPh11, gaps, elapsed
        ))
        if idxInfo:find("NONE") then
            logFail(player, "  -> EXPLAIN: indice nao usado - full table scan! Revise a UNIQUE KEY.")
        end
        if cPh7 ~= expPh7 then
            logFail(player, string.format(
                "  -> Ph7: %d/%d rows - ROLLBACK nao atomico ou COMMIT falhou!", cPh7, expPh7
            ))
        end
        if cPh8 ~= expPh8 then
            logFail(player, string.format(
                "  -> Ph8: %d/%d rows apos DELETE+re-INSERT - padrão player_storage corrompido!", cPh8, expPh8
            ))
        end
        if dPh11 > 0 then
            logFail(player, string.format(
                "  -> Ph11: %d duplicatas! ON DUPLICATE KEY UPDATE nao funcionou corretamente.", dPh11
            ))
        end
        if gaps > 0 then
            logFail(player, string.format("  -> %d gaps no seq da Ph1 - writes perdidos!", gaps))
        end
    end

    return allOk
end

-- ============================================================================
-- PHASE 7 - Atomicidade: START TRANSACTION / COMMIT / ROLLBACK
-- ============================================================================
--[[
  Testa explicitamente o modelo de transação do PR.
  flushPlayerSave() abre uma transação, replays todos os SQLs capturados e
  comita (ou faz rollback+retry em deadlock). Este é o core do PR.

  Sub-testes:
    7a (COMMIT): START TRANSACTION + ph7_commit_rows INSERTs + COMMIT
        → verifica que EXATAMENTE commit_rows estão no banco.
    7b (ROLLBACK): START TRANSACTION + ph7_rollback_rows INSERTs + ROLLBACK
        → verifica que ZERO rows dos rollback foram persistidas.
        → se aparecer qualquer row, atomicidade está quebrada.
    7c (Savepoint): START TRANSACTION + SAVEPOINT + INSERT + ROLLBACK TO SAVEPOINT
        → avançado: confirma granularidade de rollback parcial.
  FALHA ESPERADA SE: rows do ROLLBACK aparecem no banco (falha catastrófica
  de atomicidade), ou rows do COMMIT não aparecem (commit silencioso falhou).
--]]
local function runPhase7(player, runId)
    local nc = CFG.ph7_commit_rows
    local nr = CFG.ph7_rollback_rows

    log(player, string.format(
        "Phase 7: Atomicidade - COMMIT(%d rows) + ROLLBACK(%d rows) + SAVEPOINT...",
        nc, nr
    ))

    -- ── 7a: COMMIT path ───────────────────────────────────────────────────────
    local t0 = os.clock()
    local commitOk = true

    db.query("START TRANSACTION")
    for i = 1, nc do
        local safeLabel = sqlString("ph7_commit_" .. i)
        if not db.query(string.format(
            "INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`ts`) VALUES (%d,7,%d,%s,%d)",
            STRESS_TABLE, runId, i, safeLabel, os.time()
        )) then
            commitOk = false
        end
    end
    if commitOk then
        db.query("COMMIT")
    else
        db.query("ROLLBACK")
    end
    local elapsedCommit = (os.clock() - t0) * 1000

    -- ── 7b: ROLLBACK path ─────────────────────────────────────────────────────
    -- Usa seq offset nc+1000 para distinguir de 7a mesmo sem phase separado
    local rollbackSeqBase = nc + 1000
    db.query("START TRANSACTION")
    for i = 1, nr do
        local safeLabel = sqlString("ph7_rollback_" .. i)
        db.query(string.format(
            "INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`ts`) VALUES (%d,7,%d,%s,%d)",
            STRESS_TABLE, runId, rollbackSeqBase + i, safeLabel, os.time()
        ))
    end
    db.query("ROLLBACK")  -- Nada disso deve persistir

    -- ── 7c: SAVEPOINT ─────────────────────────────────────────────────────────
    local savepointSeq = rollbackSeqBase + nr + 500
    db.query("START TRANSACTION")
    local safeLabel3 = sqlString("ph7_before_savepoint")
    db.query(string.format(
        "INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`ts`) VALUES (%d,7,%d,%s,%d)",
        STRESS_TABLE, runId, savepointSeq, safeLabel3, os.time()
    ))
    db.query("SAVEPOINT sp_ph7")
    local safeLabel4 = sqlString("ph7_after_savepoint_rolled")
    db.query(string.format(
        "INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`ts`) VALUES (%d,7,%d,%s,%d)",
        STRESS_TABLE, runId, savepointSeq + 1, safeLabel4, os.time()
    ))
    db.query("ROLLBACK TO SAVEPOINT sp_ph7")  -- só o segundo INSERT é desfeito
    db.query("COMMIT")

    -- ── Verificações ──────────────────────────────────────────────────────────

    -- 7a: commit_rows devem estar presentes
    local res7a = db.storeQuery(string.format(
        "SELECT COUNT(*) AS cnt FROM `%s` WHERE `run_id`=%d AND `phase`=7 AND `seq` <= %d",
        STRESS_TABLE, runId, nc
    ))
    local cnt7a = -1
    if res7a and res7a ~= false and res7a ~= nil then 
        cnt7a = result.getNumber(res7a, "cnt")
        result.free(res7a)
    end

    -- 7b: rollback_rows NÃO devem estar presentes
    local res7b = db.storeQuery(string.format(
        "SELECT COUNT(*) AS cnt FROM `%s` WHERE `run_id`=%d AND `phase`=7 AND `seq` > %d AND `seq` <= %d",
        STRESS_TABLE, runId, rollbackSeqBase, rollbackSeqBase + nr
    ))
    local cnt7b = -1
    if res7b and res7b ~= false and res7b ~= nil then
        cnt7b = result.getNumber(res7b, "cnt")
        result.free(res7b)
    end

    -- 7c: apenas "before_savepoint" (seq=savepointSeq) deve existir; "after" não
    local res7c = db.storeQuery(string.format(
        "SELECT COUNT(*) AS cnt FROM `%s` WHERE `run_id`=%d AND `phase`=7 AND `seq` >= %d",
        STRESS_TABLE, runId, savepointSeq
    ))
    local cnt7c = -1
    if res7c and res7c ~= false and res7c ~= nil then
        cnt7c = result.getNumber(res7c, "cnt")
        result.free(res7c)
    end

    local ok7a = commitOk and (cnt7a == nc)
    local ok7b = (cnt7b == 0)   -- ROLLBACK: zero rows devem ter persistido
    local ok7c = (cnt7c == 1)   -- apenas a row antes do SAVEPOINT

    if ok7a then
        logPass(player, string.format(
            "Phase 7a COMMIT: %d/%d rows | %.1fms", cnt7a, nc, elapsedCommit
        ))
    else
        logFail(player, string.format(
            "Phase 7a COMMIT: %d/%d rows - COMMIT nao persistiu todos os dados!", cnt7a, nc
        ))
    end

    if ok7b then
        logPass(player, string.format(
            "Phase 7b ROLLBACK: 0 rows persistidas (esperado) - atomicidade OK"
        ))
    else
        logFail(player, string.format(
            "Phase 7b ROLLBACK: %d rows persistidas! - ROLLBACK nao foi atomico!", cnt7b
        ))
        logFail(player, "  -> Falha catastrofica: flushPlayerSave pode estar comitando parcialmente.")
    end

    if ok7c then
        logPass(player, "Phase 7c SAVEPOINT: 1 row pos-rollback-parcial - granularidade OK")
    else
        logFail(player, string.format(
            "Phase 7c SAVEPOINT: %d rows (esperado 1) - ROLLBACK TO SAVEPOINT com problema.", cnt7c
        ))
    end

    return ok7a and ok7b and ok7c
end

-- ============================================================================
-- PHASE 8 - DELETE + re-INSERT transacional (espelho do player_storage save)
-- ============================================================================
--[[
  Replica exatamente o padrão que IOLoginData::savePlayerQueries() usa:
    DELETE FROM player_storage WHERE player_id = ?
    INSERT INTO player_storage (player_id, key, value) VALUES (...) [× N]

  Este padrão inteiro corre dentro de uma única transação em flushPlayerSave.
  Se o COMMIT falhar no meio, a transação é retried. O estado do banco
  deve sempre ser ou "v1 completo" ou "v2 completo" - nunca híbrido.
  Teste:
    1. Insere ph8_rows linhas com label='v1_X' (simula save anterior)
    2. START TRANSACTION + DELETE + re-INSERT com label='v2_X' + COMMIT
    3. Verifica: COUNT = ph8_rows, TODAS labels começam com 'v2_', ZERO 'v1_'

  FALHA ESPERADA SE: sobram 'v1_' rows (DELETE não rodou),
  total != ph8_rows (INSERT parcial ou duplicata),
  ou mix de 'v1_' e 'v2_' (commit parcial - falha de atomicidade).
--]]
local function runPhase8(player, runId)
    local n = CFG.ph8_rows
    log(player, string.format("Phase 8: DELETE + re-INSERT (player_storage pattern) - %d rows...", n))

    -- ── Insere v1 (estado inicial, simula save anterior) ─────────────────────
    db.query("START TRANSACTION")
    for i = 1, n do
        local safeLabel = sqlString("v1_" .. i)
        db.query(string.format(
            "INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`ts`) VALUES (%d,8,%d,%s,%d)",
            STRESS_TABLE, runId, i, safeLabel, os.time()
        ))
    end
    db.query("COMMIT")

    -- ── Re-save: DELETE + INSERT v2 (dentro de transação) ───────────────────
    local t0 = os.clock()
    db.query("START TRANSACTION")
    db.query(string.format(
        "DELETE FROM `%s` WHERE `run_id`=%d AND `phase`=8",
        STRESS_TABLE, runId
    ))
    for i = 1, n do
        local safeLabel = sqlString("v2_" .. i)
        db.query(string.format(
            "INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`ts`) VALUES (%d,8,%d,%s,%d)",
            STRESS_TABLE, runId, i, safeLabel, os.time()
        ))
    end
    db.query("COMMIT")
    local elapsed = (os.clock() - t0) * 1000

    -- ── Verifica ──────────────────────────────────────────────────────────────
    local resCount = db.storeQuery(string.format(
        "SELECT COUNT(*) AS total FROM `%s` WHERE `run_id`=%d AND `phase`=8",
        STRESS_TABLE, runId
    ))
    local total = -1
    if resCount and resCount ~= false and resCount ~= nil then
        total = result.getNumber(resCount, "total")
        result.free(resCount)
    end

    -- Conta quantas são v1 (não devem existir) e v2 (devem ser todas)
    local resV1 = db.storeQuery(string.format(
        "SELECT COUNT(*) AS cnt FROM `%s` WHERE `run_id`=%d AND `phase`=8 AND `label` LIKE 'v1%%'",
        STRESS_TABLE, runId
    ))
    local v1count = -1
    if resV1 and resV1 ~= false and resV1 ~= nil then
        v1count = result.getNumber(resV1, "cnt")
        result.free(resV1)
    end

    local resV2 = db.storeQuery(string.format(
        "SELECT COUNT(*) AS cnt FROM `%s` WHERE `run_id`=%d AND `phase`=8 AND `label` LIKE 'v2%%'",
        STRESS_TABLE, runId
    ))
    local v2count = -1
    if resV2 and resV2 ~= false and resV2 ~= nil then
        v2count = result.getNumber(resV2, "cnt")
        result.free(resV2)
    end

    if total == n and v1count == 0 and v2count == n then
        logPass(player, string.format(
            "Phase 8: %d/%d rows | 0 v1 (deletadas) | %d v2 (atuais) | %.1fms - player_storage OK",
            total, n, v2count, elapsed
        ))
    else
        logFail(player, string.format(
            "Phase 8: total=%d/%d | v1=%d (deveria ser 0!) | v2=%d | %.1fms",
            total, n, v1count, v2count, elapsed
        ))
        if v1count > 0 then
            logFail(player, "  -> DELETE nao removeu rows v1 - transacao nao foi atomica!")
        end
        if total ~= n then
            logFail(player, string.format(
                "  -> Contagem errada: esperado %d, encontrado %d - INSERT parcial?", n, total
            ))
        end
    end

    return (total == n and v1count == 0 and v2count == n)
end

-- ============================================================================
-- PHASE 9 - Batch multi-row INSERT em transação única
-- ============================================================================
--[[
  flushPlayerSave() captura N queries no buildPlayerSave e as replays todas
  em sequência dentro de uma transação. Esta fase testa o caso extremo:
  um único INSERT com ph9_batch_size rows no VALUES() - a forma mais eficiente
  de INSERT em batch que o MySQL suporta. Compara:
    • ph9_batch_size INSERTs individuais em auto-commit (baseline)
    • 1 multi-row INSERT com ph9_batch_size rows em transação

  FALHA ESPERADA SE: multi-row INSERT insere duplicatas, falha no meio
  (atomicidade), ou a query excede max_allowed_packet (aumentar se necessário).
  O speedup do multi-row vs individual deve ser significativo (geralmente 5-20×).
--]]
local function runPhase9(player, runId)
    local n = CFG.ph9_batch_size
    log(player, string.format("Phase 9: Batch multi-row INSERT - %d rows numa query...", n))

    -- ── Baseline: N INSERTs individuais em auto-commit ────────────────────────
    -- Usa seq 90001+ para não colidir com o batch
    local t0 = os.clock()
    local baselineOk = 0
    for i = 1, n do
        local safeLabel = sqlString("ph9_individual_" .. i)
        if db.query(string.format(
            "INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`ts`) VALUES (%d,9,%d,%s,%d)",
            STRESS_TABLE, runId, 90000 + i, safeLabel, os.time()
        )) then
            baselineOk = baselineOk + 1
        end
    end
    local elapsedIndividual = (os.clock() - t0) * 1000

    -- Remove as rows individuais antes do batch (mesmo run_id+phase)
    db.query(string.format(
        "DELETE FROM `%s` WHERE `run_id`=%d AND `phase`=9 AND `seq` >= 90001",
        STRESS_TABLE, runId
    ))

    -- ── Batch: 1 multi-row INSERT dentro de transação ─────────────────────────
    local parts = {}
    for i = 1, n do
        local safeLabel = sqlString("ph9_batch_" .. i)
        parts[i] = string.format(
            "(%d,9,%d,%s,0,%d)",
            runId, i, safeLabel, os.time()
        )
    end
    local batchSQL = string.format(
        "INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`counter`,`ts`) VALUES %s",
        STRESS_TABLE, table.concat(parts, ",")
    )

    local t1 = os.clock()
    db.query("START TRANSACTION")
    local batchOk = db.query(batchSQL)
    if batchOk then db.query("COMMIT") else db.query("ROLLBACK") end
    local elapsedBatch = (os.clock() - t1) * 1000

    -- ── Verifica contagem pós-batch ───────────────────────────────────────────
    local res = db.storeQuery(string.format(
        "SELECT COUNT(*) AS cnt FROM `%s` WHERE `run_id`=%d AND `phase`=9 AND `seq` <= %d",
        STRESS_TABLE, runId, n
    ))
    local cnt = -1
    if res and res ~= false and res ~= nil then
        cnt = result.getNumber(res, "cnt")
        result.free(res)
    end

    local speedup = elapsedIndividual / (elapsedBatch + 1e-9)

    if batchOk and cnt == n then
        logPass(player, string.format(
            "Phase 9: %d/%d rows OK | individual=%.1fms | batch=%.1fms | speedup=%.1fx",
            cnt, n, elapsedIndividual, elapsedBatch, speedup
        ))
    else
        logFail(player, string.format(
            "Phase 9: %d/%d rows | batchOk=%s | individual=%.1fms | batch=%.1fms",
            cnt, n, tostring(batchOk), elapsedIndividual, elapsedBatch
        ))
        if not batchOk then
            logFail(player, "  -> Multi-row INSERT falhou - verifique max_allowed_packet ou syntax.")
        end
        if cnt ~= n then
            logFail(player, string.format("  -> Contagem errada: esperado %d, encontrado %d.", n, cnt))
        end
    end

    return batchOk and (cnt == n)
end

-- ============================================================================
-- PHASE 10 - InnoDB & INFORMATION_SCHEMA diagnostics
-- ============================================================================
--[[
  Fase de leitura pura - não escreve dados. Coleta métricas do MySQL que
  revelam o estado real da camada de banco após o stress:

    • SHOW STATUS: variáveis InnoDB (deadlocks, lock_waits, buffer hits) e
      globais (connections, threads, queries). Usa result:next() para
      iterar todas as linhas retornadas - verifica que o loop funciona.
    • SHOW ENGINE INNODB STATUS: texto completo do InnoDB status. O PR usa fluxo de worker/dispatcher - se houver transação aberta
      inesperadamente, aparece aqui em "TRANSACTIONS".
    • SHOW VARIABLES: verifica configuração relevante ao PR
      (innodb_lock_wait_timeout, max_connections, thread_stack).
    • SHOW FULL PROCESSLIST: lista threads ativas - verifica que workers
      do PR fecharam suas conexões corretamente após os flushes.
  FALHA ESPERADA SE: threads abertas sobraram dos workers (leak de conexão),
  innodb_lock_wait_timeout é muito baixo (explicaria falhas na Phase 4),
  ou buffer pool hit ratio < 90% (pressão de I/O excessiva).
--]]
local function runPhase10(player, runId)
    log(player, "Phase 10: InnoDB + INFORMATION_SCHEMA diagnostics...")

    -- ── SHOW STATUS: variáveis selecionadas ───────────────────────────────────
    local statusVars = {
        "Innodb_deadlocks",
        "Innodb_row_lock_waits",
        "Innodb_row_lock_time_avg",
        "Innodb_buffer_pool_reads",
        "Innodb_buffer_pool_read_requests",
        "Threads_connected",
        "Threads_running",
        "Com_insert",
        "Com_select",
        "Com_update",
        "Com_delete",
        "Com_commit",
        "Com_rollback",
    }

    -- Monta IN(list) com db.escapeString
    local inList = {}
    for _, v in ipairs(statusVars) do
        inList[#inList + 1] = sqlString(v)
    end

    local statusRes = db.storeQuery(
        "SHOW STATUS WHERE `Variable_name` IN (" .. table.concat(inList, ",") .. ")"
    )

    local statusMap = {}
    if statusRes and statusRes ~= false and statusRes ~= nil then
        repeat
            local name  = result.getString(statusRes, "Variable_name")
            local value = result.getString(statusRes, "Value")
            statusMap[name] = value
        until not result.next(statusRes)  -- ← result.next() itera as 13 variáveis
        result.free(statusRes)
    end

    -- Buffer pool hit ratio
    local bpReads    = tonumber(statusMap["Innodb_buffer_pool_reads"]) or 0
    local bpRequests = tonumber(statusMap["Innodb_buffer_pool_read_requests"]) or 1
    local hitRatio   = 100.0 * (1 - bpReads / bpRequests)

    logInfo(player, string.format(
        "Ph10 STATUS: deadlocks=%s | lock_waits=%s | lock_time_avg=%sms",
        statusMap["Innodb_deadlocks"] or "N/A",
        statusMap["Innodb_row_lock_waits"] or "N/A",
        statusMap["Innodb_row_lock_time_avg"] or "N/A"
    ))
    logInfo(player, string.format(
        "Ph10 STATUS: threads_conn=%s | threads_run=%s | buffer_hit=%.1f%%",
        statusMap["Threads_connected"] or "N/A",
        statusMap["Threads_running"]   or "N/A",
        hitRatio
    ))
    logInfo(player, string.format(
        "Ph10 STATUS: Com_insert=%s | Com_select=%s | Com_update=%s | Com_delete=%s | commit=%s | rollback=%s",
        statusMap["Com_insert"]   or "0",
        statusMap["Com_select"]   or "0",
        statusMap["Com_update"]   or "0",
        statusMap["Com_delete"]   or "0",
        statusMap["Com_commit"]   or "0",
        statusMap["Com_rollback"] or "0"
    ))

    -- ── SHOW VARIABLES relevantes ao PR ───────────────────────────────────────
    local varRes = db.storeQuery([[
        SHOW VARIABLES WHERE `Variable_name` IN (
            'innodb_lock_wait_timeout',
            'max_connections',
            'thread_stack',
            'innodb_deadlock_detect',
            'transaction_isolation'
        )
    ]])
    if varRes and varRes ~= false and varRes ~= nil then
        local varMap = {}
        repeat
            local name  = result.getString(varRes, "Variable_name")
            local value = result.getString(varRes, "Value")
            varMap[name] = value
        until not result.next(varRes)
        result.free(varRes)

        logInfo(player, string.format(
            "Ph10 VARS: lock_wait_timeout=%ss | max_conn=%s | isolation=%s | deadlock_detect=%s",
            varMap["innodb_lock_wait_timeout"] or "?",
            varMap["max_connections"] or "?",
            varMap["transaction_isolation"] or "?",
            varMap["innodb_deadlock_detect"] or "?"
        ))

        local lockTimeout = tonumber(varMap["innodb_lock_wait_timeout"]) or 50
        if lockTimeout < 5 then
            logFail(player, string.format(
                "Ph10: innodb_lock_wait_timeout=%ds e muito baixo - Phase 4 pode ter falsos negativos!", lockTimeout
            ))
        end
    end

    -- ── SHOW FULL PROCESSLIST: verifica conexões abertas de workers ────────────
    local procRes = db.storeQuery("SHOW FULL PROCESSLIST")
    local workerConns = 0
    if procRes and procRes ~= false and procRes ~= nil then
        repeat
            local cmd   = result.getString(procRes, "Command")
            local state = result.getString(procRes, "State")
            -- Conexões de worker TFS aparecem como "Sleep" ou "Query"
            if cmd == "Sleep" or cmd == "Query" then
                workerConns = workerConns + 1
            end
        until not result.next(procRes)
        result.free(procRes)
    end
    logInfo(player, string.format(
        "Ph10 PROCESSLIST: %d conexoes ativas (workers + dispatcher)", workerConns
    ))

    -- ── Buffer pool hit ratio: aviso se baixo ────────────────────────────────
    local diag_ok = true
    if hitRatio < 90 then
        logFail(player, string.format(
            "Ph10: Buffer pool hit=%.1f%% - pressao de I/O alta! Verifique innodb_buffer_pool_size.", hitRatio
        ))
        diag_ok = false
    else
        logPass(player, string.format("Ph10: Buffer pool hit=%.1f%% - OK", hitRatio))
    end

    return diag_ok
end

-- ============================================================================
-- PHASE 11 - ON DUPLICATE KEY UPDATE (upsert, padrão real do IOLoginData)
-- ============================================================================
--[[
  IOLoginData::savePlayerQueries() usa extensivamente:
    INSERT INTO player_storage (player_id, key, value)
    VALUES (X, Y, Z)
    ON DUPLICATE KEY UPDATE value = VALUES(value)

  Esta fase replica esse padrão na tabela de stress:
    1. INSERT ph11_upsert_rows rows com counter=0
    2. Re-INSERT das MESMAS rows com counter=99 + ON DUPLICATE KEY UPDATE
    3. Verifica: COUNT deve ser EXATAMENTE ph11_upsert_rows (sem duplicatas),
       todos os counters devem ser 99 (UPDATE executou, não INSERT duplicado).

  FALHA ESPERADA SE: count == ph11_upsert_rows * 2 (UNIQUE KEY ignorado),
  ou counter != 99 (UPDATE não executou - INSERT criou nova linha).
--]]
local function runPhase11(player, runId)
    local n = CFG.ph11_upsert_rows
    log(player, string.format("Phase 11: ON DUPLICATE KEY UPDATE - %d upserts...", n))

    -- ── INSERT inicial com counter=0 ──────────────────────────────────────────
    local t0 = os.clock()
    db.query("START TRANSACTION")
    for i = 1, n do
        local safeLabel = sqlString("upsert_" .. i)
        db.query(string.format(
            "INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`counter`,`ts`) VALUES (%d,11,%d,%s,0,%d)",
            STRESS_TABLE, runId, i, safeLabel, os.time()
        ))
    end
    db.query("COMMIT")

    -- ── Re-INSERT com ON DUPLICATE KEY UPDATE counter=99 ─────────────────────
    local t1 = os.clock()
    db.query("START TRANSACTION")
    for i = 1, n do
        local safeLabel = sqlString("upsert_" .. i)
        db.query(string.format(
            [[INSERT INTO `%s` (`run_id`,`phase`,`seq`,`label`,`counter`,`ts`)
              VALUES (%d,11,%d,%s,99,%d)
              ON DUPLICATE KEY UPDATE `counter`=VALUES(`counter`), `ts`=VALUES(`ts`)]],
            STRESS_TABLE, runId, i, safeLabel, os.time()
        ))
    end
    db.query("COMMIT")
    local elapsed = (os.clock() - t1) * 1000

    -- ── Verificação ───────────────────────────────────────────────────────────
    local resCount = db.storeQuery(string.format(
        "SELECT COUNT(*) AS cnt, SUM(`counter`) AS total_counter FROM `%s` WHERE `run_id`=%d AND `phase`=11",
        STRESS_TABLE, runId
    ))
    local cnt          = -1
    local totalCounter = -1
    if resCount and resCount ~= false and resCount ~= nil then
        cnt          = result.getNumber(resCount, "cnt")
        totalCounter = result.getNumber(resCount, "total_counter")
        result.free(resCount)
    end

    local expectedCounter = n * 99

    if cnt == n and totalCounter == expectedCounter then
        logPass(player, string.format(
            "Phase 11: %d/%d rows (sem duplicatas) | counter_sum=%d/%d | %.1fms - upsert OK",
            cnt, n, totalCounter, expectedCounter, elapsed
        ))
    else
        if cnt ~= n then
            logFail(player, string.format(
                "Phase 11: %d/%d rows - ON DUPLICATE KEY gerou duplicatas (esperado %d)!",
                cnt, n, n
            ))
        end
        if totalCounter ~= expectedCounter then
            logFail(player, string.format(
                "Phase 11: counter_sum=%d esperado=%d - UPDATE nao executou em %d rows!",
                totalCounter, expectedCounter, math.max(0, n - math.floor((totalCounter or 0) / 99))
            ))
        end
    end

    return (cnt == n and totalCounter == expectedCounter)
end

-- ============================================================================
-- REVSCRIPT - TalkAction
-- ============================================================================
local stressTalkAction = TalkAction("!stress")
stressTalkAction:separator(" ")
stressTalkAction:access(true)

function stressTalkAction.onSay(player, words, param)
    if not player:getGroup():getAccess() then
        return false
    end

    local cmd = (param or ""):lower():match("^%s*(.-)%s*$")

    -- ── INFO ─────────────────────────────────────────────────────────────────
    if cmd == "info" then
        local lines = {
            "=== Stress DB PR#69 | 11 fases ===",
            "Ph1  INSERT flood + escapeString + LAST_INSERT_ID + TX",
            "Ph2  Dirty snapshot: IN(...) + result:next() loop",
            "Ph3  Save flood: flushInFlight + pendingFlushes ordering",
            "Ph4  FOR UPDATE + deadlock retry + Innodb_deadlocks delta",
            "Ph5  addEvent(0) concurrent bursts (worker pool)",
            "Ph6  Integridade: ANALYZE + EXPLAIN + count/dup/gap",
            "Ph7  COMMIT + ROLLBACK + SAVEPOINT atomicidade",
            "Ph8  DELETE + re-INSERT transacional (player_storage pattern)",
            "Ph9  Batch multi-row INSERT - throughput comparison",
            "Ph10 SHOW STATUS + PROCESSLIST + SHOW VARIABLES (diagnostico)",
            "Ph11 ON DUPLICATE KEY UPDATE (upsert IOLoginData pattern)",
            "Uso: !stress [start|diag|1-11|clean|info]",
        }
        for _, l in ipairs(lines) do
            player:sendTextMessage(MSG_BLUE, l)
        end
        return false
    end

	-- ── CLEAN ────────────────────────────────────────────────────────────────
	if cmd == "clean" then
		if activeRun then
			log(player, "Stress em andamento - aguarde a conclusao antes de limpar.")
			return false
		end
		if teardownTable() then
            log(player, "Tabela stress_pr69 removida.")
        else
            logFail(player, "Falha ao remover tabela (talvez ja nao exista).")
        end
        return false
    end

    -- ── DIAG (apenas Phase 10, nao destrutivo) ────────────────────────────────
    if cmd == "diag" then
        runPhase10(player, 0)
        return false
    end

	local runId = os.time() % 65535

	-- ── FASE INDIVIDUAL ───────────────────────────────────────────────────────
	local args = {}
	for w in (param or ""):gmatch("%S+") do args[#args + 1] = w end
	local phaseNum = tonumber(args[1])
	local explicitRunId = tonumber(args[2])
	if explicitRunId then runId = explicitRunId end
	if phaseNum then
		if activeRun then
			log(player, "Stress em andamento - aguarde a conclusao antes de iniciar nova fase.")
			return false
		end
		if phaseNum ~= 6 and phaseNum ~= 10 then
            if not setupTable() then
                logFail(player, "Falha ao criar tabela de stress. Abortando.")
                activeRun = false
                return false
            end
        end
        if     phaseNum == 1  then runPhase1(player, runId)
        elseif phaseNum == 2  then runPhase2(player, runId)
        elseif phaseNum == 3  then runPhase3(player, runId)
        elseif phaseNum == 4  then runPhase4(player, runId)
        elseif phaseNum == 5  then runPhase5(player, runId)
        elseif phaseNum == 6  then runPhase6(player, runId)
        elseif phaseNum == 7  then runPhase7(player, runId)
        elseif phaseNum == 8  then runPhase8(player, runId)
        elseif phaseNum == 9  then runPhase9(player, runId)
        elseif phaseNum == 10 then runPhase10(player, runId)
        elseif phaseNum == 11 then runPhase11(player, runId)
        else
            player:sendTextMessage(MSG_BLUE,
                "Fase invalida. Use 1-11, start, diag, clean ou info.")
        end
        return false
    end

    -- ── START - todas as fases ────────────────────────────────────────────────
	if cmd == "" or cmd == "start" or cmd == "all" then
		if activeRun then
			log(player, "Stress ja em andamento - aguarde a conclusao.")
			return false
		end
		activeRun = true

		print(COLOR_BLUE .. "[StressDB]" .. COLOR_GREEN .. "[INFO]" .. COLOR_RESET .. " " .. 
			  COLOR_BLUE .. string.format("=== Stress DB PR#69 | run_id=%d | 11 fases | iniciando ===", runId) .. COLOR_RESET)
		player:sendTextMessage(MSG_BLUE, string.format(
            "[StressDB] === Stress DB PR#69 | run_id=%d | 11 fases | iniciando ===", runId
        ))

        if not setupTable() then
            logFail(player, "Falha ao criar tabela de stress. Abortando.")
            return false
        end

		print(COLOR_BLUE .. "[StressDB]" .. COLOR_GREEN .. "[INFO]" .. COLOR_RESET .. " " .. 
			  COLOR_BLUE .. "Tabela stress_pr69 criada." .. COLOR_RESET)
		player:sendTextMessage(MSG_BLUE, "[StressDB] Tabela stress_pr69 criada.")

        local wallStart = os.clock()
        local results   = {}

        -- Fases sincronas (dispatcher)
        results[1]  = runPhase1(player, runId)
        results[2]  = runPhase2(player, runId)
        results[3]  = runPhase3(player, runId)
        results[4]  = runPhase4(player, runId)
        results[5]  = runPhase5(player, runId)
        results[7]  = runPhase7(player, runId)
        results[8]  = runPhase8(player, runId)
        results[9]  = runPhase9(player, runId)
        results[10] = runPhase10(player, runId)
        results[11] = runPhase11(player, runId)

        -- Settle: aguarda flushes assíncronos das fases 2, 3, 4 e 5
        local settleTime = math.max(
            (CFG.ph3_save_count * CFG.ph3_stagger_ms) + REPORT_DELAY + 1200,
            REPORT_DELAY + 2200
        ) + 800

		addEvent(function(pid, rId, wStart, r)
			local p = safePlayer(pid)
			if not p then
				activeRun = false
				return
			end

            r[6] = runPhase6(p, rId)

            local passed = 0
            local total  = 0
            for _, v in pairs(r) do
                total = total + 1
                if v == true then passed = passed + 1 end
            end

            local wall   = (os.clock() - wStart) * 1000
            local status = passed == total
                and "ALL PASS"
                or string.format("%d/%d PASS", passed, total)

			if passed == total then
				print(COLOR_BLUE .. "[StressDB]" .. COLOR_GREEN .. "[INFO]" .. COLOR_RESET .. " " .. 
					  COLOR_BLUE .. string.format("=== STRESS COMPLETO: %s | wall ~%.0fms ===", status, wall) .. COLOR_RESET)
				p:sendTextMessage(MSG_BLUE, string.format(
					"[StressDB][PASS] === STRESS COMPLETO: %s | wall ~%.0fms ===", status, wall
				))
			else
				logFail(p, string.format(
					"=== STRESS COMPLETO: %s | wall ~%.0fms - revise os FAILs! ===",
					status, wall
				))
			end
			activeRun = false

        end, settleTime, player:getId(), runId, wallStart, results)

		local execMsg = string.format(
            "Fases 1-11 em execucao | Phase 6 (integridade final) em ~%.0fms", settleTime
        )
		print(COLOR_BLUE .. "[StressDB]" .. COLOR_RESET .. " " .. 
			  COLOR_ORANGE .. "Fases 1-11 em execucao" .. COLOR_RESET .. 
			  string.format(" | Phase 6 (integridade final) em ~%.0fms", settleTime))
		player:sendTextMessage(MSG_BLUE, "[StressDB] " .. execMsg)
        return false
    end

    player:sendTextMessage(MSG_BLUE,
        "Uso: !stress [start|diag|1-11|clean|info]")
    return false
end

stressTalkAction:accountType(6)
stressTalkAction:access(true)
stressTalkAction:register()
