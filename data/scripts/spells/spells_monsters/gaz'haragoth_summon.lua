local gazFunctionsPath = "data/scripts/spells/monster/gaz_functions.lua"
if fileExists and fileExists(gazFunctionsPath) then
    dofile(gazFunctionsPath)
end

local spell = Spell("instant")

function spell.onCastSpell(creature, var)
    local spectators = Game.getSpectators(creature:getPosition(), false, false, 25, 25, 25, 25)
    local check = 0
    
    for _, spectator in ipairs(spectators) do
        if spectator:isMonster() and spectator:getName() == "Minion of Gaz'haragoth" then
            check = check + 1
        end
    end

    if not GazVariables then
        GazVariables = {MaxSummons = 20, MinionsNow = 5}
    end

    if check >= GazVariables.MaxSummons then
        return false
    end

    if check < GazVariables.MinionsNow then
        for i = 1, (GazVariables.MinionsNow - check) do
            local monster = Game.createMonster("Minion of Gaz'haragoth", creature:getPosition(), true, false,
                CONST_ME_NONE, creature:getInstanceId())
            if monster then
                if creature.hasBossDifficulty and creature:hasBossDifficulty() then
                    monster:applyBossDifficulty(creature:getBossDifficulty(), 1003)
                end
                monster:setMaster(creature)
            end
        end
        creature:say("Minions! Follow my call!", TALKTYPE_MONSTER_SAY)
        creature:getPosition():sendMagicEffect(CONST_ME_SOUND_RED)
    else
        if math.random(0, 100) < 25 then
            local monster = Game.createMonster("Minion of Gaz'haragoth", creature:getPosition(), true, false,
                CONST_ME_NONE, creature:getInstanceId())
            if monster then
                if creature.hasBossDifficulty and creature:hasBossDifficulty() then
                    monster:applyBossDifficulty(creature:getBossDifficulty(), 1003)
                end
                monster:setMaster(creature)
                GazVariables.MinionsNow = GazVariables.MinionsNow + 1
            end
            creature:say("Minions! Follow my call!", TALKTYPE_MONSTER_SAY)
            creature:getPosition():sendMagicEffect(CONST_ME_SOUND_RED)
        end
    end
    return true
end

spell:name("gaz'haragoth summon")
spell:words("###125")
spell:blockWalls(true)
spell:needLearn(true)
spell:register()
