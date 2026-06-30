--- @file
-- Wave progression + scoring (port of engine/src/WaveSystem.cpp's five pure
-- functions, plus the intermission-arming/spawn-bookkeeping orchestration
-- that used to live inline in Engine::updateWaves). ds.wave.state is the live
-- run state; ds.wave.config is the tunable escalation/scoring constants.
-- Engine.cpp drives this through ScriptSystem::wave*() wrappers and refreshes
-- its WaveState read-back cache (m_wave) after every mutating call.

ds.wave = {
    state = {
        wave = 0,
        alive_enemies = 0,
        intermission = 0,
        intermission_armed = false,
        spawn_pending = false,
        cleared = false,
        kills = 0,
        score = 0,
        time_survived = 0,
        combo = 0,
        best_combo = 0,
        combo_timer = 0,
    },
    config = {
        base_enemies = 3,
        enemies_per_wave = 2,
        max_enemies_per_wave = 24,
        max_waves = 8,
        intermission_time = 3.0,
        kill_score = 100,
        combo_window = 3.0,
    },
}

--- Enemy count for a given 1-based wave, escalating by enemies_per_wave each
-- wave and clamped to max_enemies_per_wave. Out-of-range waves spawn nothing.
-- @param wave 1-based wave number
-- @return integer enemy count for this wave (0 if wave < 1)
function ds.wave.enemies_for_wave(wave)
    if wave < 1 then
        return 0
    end
    local count = ds.wave.config.base_enemies + (wave - 1) * ds.wave.config.enemies_per_wave
    return math.min(count, ds.wave.config.max_enemies_per_wave)
end

--- Points for a single kill given the combo count *after* the kill is
-- registered (combo == 1 for the first kill in a chain).
-- @param combo combo count after this kill
-- @return integer points awarded for this kill
function ds.wave.score_for_kill(combo)
    local mult = combo > 0 and combo or 1
    return ds.wave.config.kill_score * mult
end

--- Registers one enemy kill: advances kill count, combo (resetting the
-- window), best combo, total score, and decrements alive_enemies.
function ds.wave.register_kill()
    local s = ds.wave.state
    s.kills = s.kills + 1
    s.combo = s.combo_timer > 0 and s.combo + 1 or 1
    s.combo_timer = ds.wave.config.combo_window
    s.best_combo = math.max(s.best_combo, s.combo)
    s.score = s.score + ds.wave.score_for_kill(s.combo)
    if s.alive_enemies > 0 then
        s.alive_enemies = s.alive_enemies - 1
    end
end

--- Advances combo/intermission/survival timers by dt.
-- @param dt frame delta time in seconds
function ds.wave.tick(dt)
    local s = ds.wave.state
    s.time_survived = s.time_survived + dt

    if s.combo_timer > 0 then
        s.combo_timer = s.combo_timer - dt
        if s.combo_timer <= 0 then
            s.combo_timer = 0
            s.combo = 0
        end
    end

    if s.intermission > 0 then
        s.intermission = s.intermission - dt
        if s.intermission < 0 then
            s.intermission = 0
        end
    end
end

--- Begins the next wave: increments the wave number and flags spawn_pending,
-- or marks the run cleared once max_waves is exceeded. Always clears the
-- intermission-armed state (advancing always means the wait is over).
function ds.wave.advance()
    local s = ds.wave.state
    s.intermission_armed = false
    s.intermission = 0
    if s.wave >= ds.wave.config.max_waves then
        s.cleared = true
        return
    end
    s.wave = s.wave + 1
    s.spawn_pending = true
end

--- Resets all wave + score state for a fresh run.
function ds.wave.reset()
    ds.wave.state = {
        wave = 0,
        alive_enemies = 0,
        intermission = 0,
        intermission_armed = false,
        spawn_pending = false,
        cleared = false,
        kills = 0,
        score = 0,
        time_survived = 0,
        combo = 0,
        best_combo = 0,
        combo_timer = 0,
    }
end

--- Keeps alive_enemies in sync with the world's live EnemyComponent count
-- (kills are also decremented in register_kill; this guards against any
-- external destruction, e.g. the boss-clear path).
-- @param n live EnemyComponent count this frame
function ds.wave.set_alive(n)
    ds.wave.state.alive_enemies = n
end

--- Arms the intermission countdown to the next wave (first clear frame).
function ds.wave.arm_intermission()
    ds.wave.state.intermission = ds.wave.config.intermission_time
    ds.wave.state.intermission_armed = true
end

--- Records that the just-spawned wave's enemies are live and clears
-- spawn_pending. Called right after the engine spawns enemies_for_wave(wave)
-- enemies.
-- @param alive_count enemy count just spawned for this wave
function ds.wave.mark_spawned(alive_count)
    ds.wave.state.alive_enemies = alive_count
    ds.wave.state.spawn_pending = false
end
