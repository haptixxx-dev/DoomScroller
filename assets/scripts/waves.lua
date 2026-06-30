-- DoomScroller wave + enemy tuning. Loaded by ScriptSystem at startup.
-- If this file is missing or errors, the engine falls back to its hardcoded
-- WaveConfig / enemy defaults (task 18), so editing here is always optional.
--
-- The `ds` table is provided by the engine (see ScriptSystem). Available calls:
--   ds.spawn_enemy(x, y, z [, type]) -> entity id
--   ds.get_field(entity, name) / ds.set_field(entity, name, value)
--   ds.emit_event(name [, number])
--   ds.set_wave_config(table)

-- Per-enemy stats. Read back via ScriptSystem::enemyStats().
ds.enemy_stats = {
    health = 100,
    speed  = 3.0,
    damage = 10,
}

-- Wave escalation. Read back via ScriptSystem::waveConfig().
ds.set_wave_config({
    base_enemies         = 3,
    enemies_per_wave     = 2,
    max_enemies_per_wave = 24,
    max_waves            = 8,
    intermission_time    = 3.0,
    kill_score           = 100,
})

-- Optional event callbacks. The engine invokes these by name if defined.
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
