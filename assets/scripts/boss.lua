-- Boss phase/attack-pattern logic (port of engine/include/engine/BossLogic.h's
-- bossPhaseForHealth plus the inline attack-pattern selection/cadence that
-- used to live in Engine::bossSystem). ds.boss is global, single-instance
-- state (only one boss is ever alive at a time); Engine.cpp keeps the EnTT/
-- Jolt position sync and the entire death-handling block (VFX/audio/hitstop/
-- Victory transition).

ds.boss = {
    phase = 0,
    vulnerable_timer = 0,
    attack_timer = 2.0, -- brief grace before the first volley (mirrors spawnBoss())
    pattern = 0, -- 0 = fan volley, 1 = charge burst (alternates each shot)
}

local thresholds = { 0.66, 0.33, 0.0 }

function ds.boss.reset()
    ds.boss.phase = 0
    ds.boss.vulnerable_timer = 0
    ds.boss.attack_timer = 2.0
    ds.boss.pattern = 0
end

-- Count of thresholds the current health fraction is at or below, clamped to
-- [0, #thresholds]. Port of bossPhaseForHealth.
local function phase_for_health(current, max_health)
    if max_health <= 0 then
        return #thresholds
    end
    local c = current < 0 and 0 or current
    local frac = c / max_health
    local phase = 0
    for _, t in ipairs(thresholds) do
        if frac <= t then
            phase = phase + 1
        end
    end
    return phase
end

-- boss_pos, player_pos: Vec3 userdata. Returns new_phase, new_vulnerable_timer,
-- new_pattern so the caller can detect a phase transition (phase > previous,
-- for its own VFX/audio cue) and a fire event (pattern changed, since pattern
-- only increments when a volley/burst actually fires) to play the weapon-fire
-- cue. Gates 2x damage during the vulnerable window. Fires pellets itself via
-- ds.spawn_projectile(origin, velocity, damage, owner_body_id).
function ds.boss.tick(health, max_health, dt, boss_pos, player_pos, boss_body_id)
    -- Phase transition: opens a brief parryable vulnerable window and bumps
    -- the attack cadence. Falls through (not an early return) so a freshly
    -- opened window still ticks down by dt this same frame, matching the
    -- original inline C++ control flow.
    local new_phase = phase_for_health(health, max_health)
    if new_phase > ds.boss.phase then
        ds.boss.phase = new_phase
        ds.boss.vulnerable_timer = 2.0
        ds.boss.attack_timer = 1.0
    end

    if ds.boss.vulnerable_timer > 0 then
        -- Telegraphing: holds fire and glows while vulnerable.
        ds.boss.vulnerable_timer = ds.boss.vulnerable_timer - dt
        return ds.boss.phase, ds.boss.vulnerable_timer, ds.boss.pattern
    end

    ds.boss.attack_timer = ds.boss.attack_timer - dt
    if ds.boss.attack_timer > 0 then
        return ds.boss.phase, ds.boss.vulnerable_timer, ds.boss.pattern
    end

    local to_player = player_pos - boss_pos
    local dist = to_player:length()
    local dir
    if dist > 1e-4 then
        dir = Vec3.new(to_player.x / dist, to_player.y / dist, to_player.z / dist)
    else
        dir = Vec3.new(0, 0, 1)
    end
    local muzzle = boss_pos + dir * 1.6 + Vec3.new(0, 0.5, 0)

    -- Pattern escalates with the phase: a wider, faster volley each phase.
    local phase = ds.boss.phase
    local pellets = 3 + phase * 2 -- 3,5,7,...
    local speed = 14.0 + phase * 3.0
    local damage = 12 + phase * 4

    if ds.boss.pattern % 2 == 0 then
        -- Fan volley: spread pellets in a horizontal arc toward the player.
        local right = dir:cross(Vec3.new(0, 1, 0)):normalize()
        local half_arc = 0.35 + phase * 0.1
        for i = 0, pellets - 1 do
            local t = 0
            if pellets > 1 then
                t = (i / (pellets - 1)) * 2 - 1
            end
            local d = (dir + right * (t * half_arc)):normalize()
            ds.spawn_projectile(muzzle, d * speed, damage, boss_body_id)
        end
    else
        -- Charge burst: a tight, fast straight stream the player must dodge.
        for i = 0, pellets - 1 do
            ds.spawn_projectile(muzzle + dir * (i * 0.4), dir * (speed * 1.4), damage, boss_body_id)
        end
    end

    ds.boss.pattern = ds.boss.pattern + 1
    -- Faster cadence in later phases, floored at 0.6s.
    ds.boss.attack_timer = 2.2 - phase * 0.4
    if ds.boss.attack_timer < 0.6 then
        ds.boss.attack_timer = 0.6
    end

    return ds.boss.phase, ds.boss.vulnerable_timer, ds.boss.pattern
end
