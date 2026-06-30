-- Per-enemy stat overrides (Grunt only today), read back via
-- ScriptSystem::enemyStats().
ds.enemy_stats = {
    health = 100,
    speed = 3.0,
    damage = 10,
}

-- Enemy AI FSM (port of engine/src/ecs/EnemySystem.cpp's per-archetype state
-- machine) + deterministic archetype selection for wave spawns (port of
-- Engine::archetypeForWave). EnemySystem.cpp keeps the EnTT/Jolt loop
-- (position read, dist/dir computation, the health<=0 destroy check) and
-- applies the decision this module returns (velocity/damage/projectile spawn
-- all stay C++-only, since they touch i-frames and the spawn callback Lua
-- can't see).
ds.enemy_ai = {}

-- archetype: 0=Grunt 1=Charger 2=Ranged (mirrors EnemyArchetype).
-- state: 0=Idle 1=Chase 2=Attack (mirrors EnemyComponent::State).
local ARCHETYPE_GRUNT, ARCHETYPE_CHARGER, ARCHETYPE_RANGED = 0, 1, 2
local STATE_IDLE, STATE_CHASE, STATE_ATTACK = 0, 1, 2

-- Returns: new_state, set_velocity(bool), move_intent(-1..1 along the
-- already-computed direction to the player; speed = chargeSpeed if lunge
-- else moveSpeed), lunge(bool), fire_projectile(bool), melee_attack(bool),
-- arm_windup(bool). attack_cooldown is the CALLER-decremented cooldown (the
-- dt subtraction already happened in EnemySystem.cpp, matching the original
-- C++ flow); this function only reads it, never decrements it further.
function ds.enemy_ai.tick(archetype, state, dist, attack_cooldown, move_speed, attack_range, detection_range,
                           attack_interval, charge_windup, charge_speed)
    local ranged = archetype == ARCHETYPE_RANGED
    local charger = archetype == ARCHETYPE_CHARGER

    if state == STATE_IDLE then
        if dist < detection_range then
            return STATE_CHASE, true, 0, false, false, false, false
        end
        return STATE_IDLE, true, 0, false, false, false, false
    end

    if state == STATE_CHASE then
        if dist < attack_range then
            -- Charger arms its windup the instant it enters attack range.
            return STATE_ATTACK, true, 0, false, false, false, charger
        elseif dist > detection_range then
            -- No velocity call here: the original leaves the body coasting
            -- on this transition rather than zeroing it.
            return STATE_IDLE, false, 0, false, false, false, false
        else
            return STATE_CHASE, true, 1, false, false, false, false
        end
    end

    -- STATE_ATTACK
    if ranged then
        if dist > attack_range then
            return STATE_CHASE, false, 0, false, false, false, false
        end
        local retreat = dist < attack_range * 0.5
        local move_intent = retreat and -1 or 0
        local fire = attack_cooldown <= 0
        return STATE_ATTACK, true, move_intent, false, fire, false, false
    elseif charger then
        if dist > attack_range then
            return STATE_CHASE, false, 0, false, false, false, false
        end
        if attack_cooldown > 0 then
            -- Telegraph: freeze horizontal movement during the windup.
            return STATE_ATTACK, true, 0, false, false, false, false
        end
        -- Lunge: burst toward the player and apply contact damage.
        return STATE_ATTACK, true, 1, true, false, false, false
    else
        -- Grunt: melee contact damage on the attack cooldown. Velocity is
        -- always zeroed here (even on the ->Chase transition), matching the
        -- original's unconditional setLinearVelocity at the end of this branch.
        if dist > attack_range then
            return STATE_CHASE, true, 0, false, false, false, false
        end
        local melee = attack_cooldown <= 0
        return STATE_ATTACK, true, 0, false, false, melee, false
    end
end

-- Deterministic archetype pick for a wave-spawned enemy: wave 1 is all
-- Grunts; from wave 2 mix in Chargers; from wave 3 add Ranged.
function ds.enemy_ai.archetype_for_wave(wave, spawn_index)
    if wave <= 1 then
        return ARCHETYPE_GRUNT
    end
    local sel = (wave + spawn_index) % 3
    if wave >= 3 and sel == 2 then
        return ARCHETYPE_RANGED
    end
    if sel == 1 then
        return ARCHETYPE_CHARGER
    end
    return ARCHETYPE_GRUNT
end
