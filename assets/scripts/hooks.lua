--- @file
-- Optional, cross-cutting event hooks the engine invokes by name if defined
-- (ScriptSystem::onWaveStart/onEnemyDeath/onPlayerDeath). Each is a graceful
-- no-op when undefined, so this file itself is optional.

--- Called when a new wave begins.
-- @param wave the wave number that just started
function onWaveStart(wave)
    -- Example hook: announce the wave. Replace with custom spawn logic, e.g.
    -- ds.spawn_enemy(0, 1.5, 0) to inject a scripted enemy.
    print("[lua] wave " .. tostring(wave) .. " starting")
end

--- Called when an enemy dies, at its death position.
-- @param entity the killed enemy's entity id
-- @param x world-space death position x
-- @param y world-space death position y
-- @param z world-space death position z
function onEnemyDeath(entity, x, y, z)
    -- ds.emit_event("enemy_death", entity)
end

--- Called once when the player dies, ending the run.
-- @param score final run score
function onPlayerDeath(score)
    print("[lua] player died with score " .. tostring(score))
end
