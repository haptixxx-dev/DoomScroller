-- Optional, cross-cutting event hooks the engine invokes by name if defined
-- (ScriptSystem::onWaveStart/onEnemyDeath/onPlayerDeath). Each is a graceful
-- no-op when undefined, so this file itself is optional.
function onWaveStart(wave)
    -- Example hook: announce the wave. Replace with custom spawn logic, e.g.
    -- ds.spawn_enemy(0, 1.5, 0) to inject a scripted enemy.
    print("[lua] wave " .. tostring(wave) .. " starting")
end

function onEnemyDeath(entity, x, y, z)
    -- ds.emit_event("enemy_death", entity)
end

function onPlayerDeath(score)
    print("[lua] player died with score " .. tostring(score))
end
