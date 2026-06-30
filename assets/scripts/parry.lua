-- Parry state machine (port of engine/include/engine/ParryTech.h). ds.parry
-- holds the live window/cooldown timers; ds.parry.tuning holds the tunable
-- constants. Engine.cpp drives this through ScriptSystem::parry*() instead of
-- calling ParryTech.h's free functions directly.

ds.parry = {
    window = 0,
    cooldown = 0,
}

ds.parry.tuning = {
    window_duration = 0.3,
    cooldown = 0.6,
    dash_refund = 1,
}

function ds.parry.reset()
    ds.parry.window = 0
    ds.parry.cooldown = 0
end

-- Start a parry: only allowed when off cooldown. A trigger while still
-- cooling down is ignored (no-op).
function ds.parry.trigger()
    if ds.parry.cooldown > 0 then
        return
    end
    ds.parry.window = ds.parry.tuning.window_duration
    ds.parry.cooldown = ds.parry.tuning.cooldown
end

-- Advance both timers by dt, clamped at 0. Call once per frame.
function ds.parry.tick(dt)
    ds.parry.window = math.max(0, ds.parry.window - dt)
    ds.parry.cooldown = math.max(0, ds.parry.cooldown - dt)
end

-- True while the parry window is open (this frame's incoming damage is negated).
function ds.parry.active()
    return ds.parry.window > 0
end

-- Compute the velocity for a reflected projectile: flip the incoming velocity
-- and scale its magnitude by speed_boost (default 1.5x faster on the way back).
function ds.parry.reflect_velocity(incoming, speed_boost)
    speed_boost = speed_boost or 1.5
    return Vec3.new(-incoming.x * speed_boost, -incoming.y * speed_boost, -incoming.z * speed_boost)
end
