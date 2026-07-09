-- =============================================================================
-- Stages System -- Unified Stage Configuration (Level, Skill, Magic, Reset)
-- =============================================================================
-- This file replaces the old `experienceStages`, `skillStages`, `magicLevelStages`
-- tables from config.lua and unifies `stagesresets.lua`.
-- =============================================================================

StagesConfig = {}

-- =============================================================================
-- TOGGLES AND FALLBACK RATES
-- =============================================================================
-- For each stage type, set `enabled` and the `rate` fallback.
-- When `enabled = false`, a catch-all stage with `multiplier=<rate>` is registered
-- automatically (minlevel defaults to 1 for experience/skill, 0 for magic level).
StagesConfig.experienceEnabled = true
StagesConfig.rateExp            = 150

StagesConfig.skillEnabled       = true
StagesConfig.skillRate          = 3

StagesConfig.magicEnabled       = true
StagesConfig.magicRate          = 3

StagesConfig.resetEnabled       = false

-- =============================================================================
-- 1) EXPERIENCE STAGES (level-based)
-- =============================================================================
-- Format: { minlevel = <number>, maxlevel = <number|nil>, multiplier = <float> }
-- maxlevel = nil -> no upper limit (catch-all).
--
-- Example commented table:
--[[
Game.setExperienceStages({
    { minlevel = 1,   maxlevel = 8,   multiplier = 7 },
    { minlevel = 9,   maxlevel = 20,  multiplier = 6 },
    { minlevel = 21,  maxlevel = 50,  multiplier = 5 },
    { minlevel = 51,  maxlevel = 100, multiplier = 4 },
    { minlevel = 101,                 multiplier = 3 },  -- 101+ (open-ended)
})
--]]
if StagesConfig.experienceEnabled then
    Game.setExperienceStages({
        { minlevel = 1,   maxlevel = 50,   multiplier = 100 },
        { minlevel = 51,  maxlevel = 70,   multiplier = 80  },
        { minlevel = 71,  maxlevel = 80,   multiplier = 65  },
        { minlevel = 81,  maxlevel = 100,  multiplier = 45  },
        { minlevel = 101, maxlevel = 120,  multiplier = 25  },
        { minlevel = 121, maxlevel = 140,  multiplier = 10  },
        { minlevel = 141, maxlevel = 175,  multiplier = 8   },
        { minlevel = 176, maxlevel = 180,  multiplier = 2   },
        { minlevel = 181, maxlevel = 200,  multiplier = 1.5 },
        { minlevel = 201, maxlevel = 500,  multiplier = 1   },
        { minlevel = 501,                multiplier = 1   },
    })
else
    Game.setExperienceStages({
        { minlevel = 1, multiplier = StagesConfig.rateExp }
    })
end

-- =============================================================================
-- 2) SKILL STAGES (skill-level-based)
-- =============================================================================
-- Example commented table:
--[[
Game.setSkillStages({
    { minlevel = 1,  maxlevel = 30,  multiplier = 10 },
    { minlevel = 31, maxlevel = 60,  multiplier = 7  },
    { minlevel = 61, maxlevel = 90,  multiplier = 4  },
    { minlevel = 91,                multiplier = 2  },  -- 91+ (open-ended)
})
--]]
if StagesConfig.skillEnabled then
    Game.setSkillStages({
        { minlevel = 1,   maxlevel = 30,  multiplier = 113 },
        { minlevel = 31,  maxlevel = 40,  multiplier = 98  },
        { minlevel = 41,  maxlevel = 50,  multiplier = 83  },
        { minlevel = 51,  maxlevel = 60,  multiplier = 68  },
        { minlevel = 61,  maxlevel = 70,  multiplier = 60  },
        { minlevel = 71,  maxlevel = 80,  multiplier = 38  },
        { minlevel = 81,  maxlevel = 90,  multiplier = 23  },
        { minlevel = 91,  maxlevel = 100, multiplier = 15  },
        { minlevel = 101, maxlevel = 110, multiplier = 3   },
        { minlevel = 111,                multiplier = 1   },
    })
else
    Game.setSkillStages({
        { minlevel = 1, multiplier = StagesConfig.skillRate }
    })
end

-- =============================================================================
-- 3) MAGIC LEVEL STAGES
-- =============================================================================
-- Example commented table:
--[[
Game.setMagicLevelStages({
    { minlevel = 1,  maxlevel = 30,  multiplier = 8 },
    { minlevel = 31, maxlevel = 60,  multiplier = 5 },
    { minlevel = 61, maxlevel = 90,  multiplier = 3 },
    { minlevel = 91,                multiplier = 2 },  -- 91+ (open-ended)
})
--]]
if StagesConfig.magicEnabled then
    Game.setMagicLevelStages({
        { minlevel = 0,   maxlevel = 50,  multiplier = 38  },
        { minlevel = 51,  maxlevel = 70,  multiplier = 35  },
        { minlevel = 71,  maxlevel = 80,  multiplier = 23  },
        { minlevel = 81,  maxlevel = 100, multiplier = 15  },
        { minlevel = 101, maxlevel = 110, multiplier = 3   },
        { minlevel = 111,                multiplier = 1   },
    })
else
    Game.setMagicLevelStages({
        { minlevel = 0, multiplier = StagesConfig.magicRate }
    })
end

-- =============================================================================
-- 4) RESET STAGES (reset-count-based)
-- =============================================================================
-- configureResetStages(config) accepts 3 formats:
--
--   a) float     -> flat multiplier for all resets
--   b) table     -> list of stages [{minReset, maxReset, multiplier}, ...]
--                   maxReset = 0 -> no upper limit
--   c) function  -> function that returns a table of stages (dynamic generation)
--
-- Example: table format with progressive reduction.
-- The multiplier reduces XP gain as reset count increases (redutor progressivo).
-- maxReset = 0 means no upper limit (open-ended).
--[[
StagesConfig.configureResetStages({
    { minReset = 1,  maxReset = 5,  multiplier = 0.95 },
    { minReset = 6,  maxReset = 10, multiplier = 0.50 },
    { minReset = 11, maxReset = 0,  multiplier = 0.25 },  -- 11+ (open-ended)
})
-- Result:
--   resetCount     stage encontrado     XP final
--   ----------     ----------------     --------
--        3      ->    1-5   (0.95)   -> exp * 0.95
--        7      ->    6-10  (0.50)   -> exp * 0.50
--       50      ->   11+    (0.25)   -> exp * 0.25
--        0      ->   nenhum (1.0)    -> exp * 1.0   (fallback)
--]]

-- Other formats available:
--   a) Float:  StagesConfig.configureResetStages(0.5)
--   b) Table:  StagesConfig.configureResetStages({...})
--   c) Function: StagesConfig.configureResetStages(function() ... end)

function StagesConfig.configureResetStages(config)
    local configType = type(config)
    if configType == "number" then
        if config <= 0 then
            logger.error(">> Reset stages: multiplier must be a positive value, got %.2f", config)
            return false
        end
        local ok = Game.setResetStages({
            { minReset = 1, maxReset = 0, multiplier = config }
        })
        if ok then
            logger.info(">> Reset stages configured: flat multiplier %.2f", config)
            return true
        else
            logger.error(">> Reset stages configuration rejected: flat multiplier %.2f", config)
            return false
        end
    elseif configType == "table" then
        local ok = Game.setResetStages(config)
        if ok then
            logger.info(">> Reset stages configured: %d stage(s)", #config)
            return true
        else
            logger.error(">> Reset stages configuration rejected: %d stage(s)", #config)
            return false
        end
    elseif configType == "function" then
        local generated = config()
        if type(generated) == "table" then
            local ok = Game.setResetStages(generated)
            if ok then
                logger.info(">> Reset stages configured via formula: %d stage(s)", #generated)
                return true
            else
                logger.error(">> Reset stages configuration rejected via formula: %d stage(s)", #generated)
                return false
            end
        else
            logger.error(">> Reset stages formula must return a table, got %s", type(generated))
            return false
        end
    else
        logger.error(">> Invalid reset stages config type: %s", configType)
        return false
    end
end

function StagesConfig.getResetMultiplier(resetCount)
    return Game.getResetStage(resetCount)
end

if StagesConfig.resetEnabled then
    -- Call configureResetStages() with your desired format (see examples above)
    -- By default, without explicit configuration, returns 1.0 (no modification)
end

logger.info(">> Stages system loaded successfully.")
