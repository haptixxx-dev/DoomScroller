-- Pickup drop cadence + collection check (port of the logic that used to live
-- inline in Engine::handleEnemyDeaths / Engine::pickupSystem, plus
-- engine/include/engine/PickupSystem.h's withinPickupRange/
-- pickupEffectMagnitude). Engine.cpp still owns spawning the pickup entity
-- and applying the granted HP/ammo/dash-charge.

-- kind: 0 = Health, 1 = Ammo, 2 = DashCharge (mirrors PickupComponent::Kind).
local KIND_HEALTH      = 0
local KIND_AMMO        = 1
local KIND_DASH_CHARGE = 2

ds.pickups = {
    kill_count = 0,
}

function ds.pickups.reset()
    ds.pickups.kill_count = 0
end

-- Deterministic cadence: every 3rd kill drops an orb, cycling kind so the
-- player sees all three over a run. No RNG state. Returns should_drop(bool),
-- kind(int), value(int).
function ds.pickups.register_kill()
    ds.pickups.kill_count = ds.pickups.kill_count + 1
    if ds.pickups.kill_count % 3 ~= 0 then
        return false, KIND_HEALTH, 0
    end
    local cycle = math.floor(ds.pickups.kill_count / 3) % 3
    if cycle == 0 then
        return true, KIND_HEALTH, 25
    elseif cycle == 1 then
        return true, KIND_AMMO, 30
    else
        return true, KIND_DASH_CHARGE, 1
    end
end

-- True when the player is within `radius` of the pickup (sphere test on full
-- 3D distance). Negative/zero radius never collects.
local function within_range(player_pos, pickup_pos, radius)
    if radius <= 0 then
        return false
    end
    local d = player_pos - pickup_pos
    return d:dot(d) <= radius * radius
end

-- Effect magnitude clamp: never grants more than `headroom` (e.g. missing
-- HP), never negative.
local function effect_magnitude(value, headroom)
    local v = value < 0 and 0 or value
    local h = headroom < 0 and 0 or headroom
    return v < h and v or h
end

-- Combined range + magnitude decision: returns collected(bool), grant(int).
function ds.pickups.collect_check(player_pos, pickup_pos, radius, value, headroom)
    if not within_range(player_pos, pickup_pos, radius) then
        return false, 0
    end
    return true, effect_magnitude(value, headroom)
end
