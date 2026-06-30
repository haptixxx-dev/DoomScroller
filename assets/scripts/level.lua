-- Procedural level generation: randomizes room size, scatters pillar
-- obstacles, and randomizes enemy spawn placement every run. Lua 5.4 auto-
-- seeds math.random per-state (time + address based) without needing
-- os.time(), so this varies across runs even in shipping builds where io/os
-- are sandboxed outside DS_DEV.

local H, T = 5, 0.1
local W = 8 + math.random(0, 6) -- room half-width: 8..14
local D = 8 + math.random(0, 6) -- room half-depth: 8..14
local WHITE = Vec3.new(1, 1, 1)

-- Floor / ceiling / four walls.
ds.level.add_box(Vec3.new(0, -T, 0), Vec3.new(W, T, D), WHITE)
ds.level.add_box(Vec3.new(0, H + T, 0), Vec3.new(W, T, D), WHITE)
ds.level.add_box(Vec3.new(0, H / 2, -D - T), Vec3.new(W, H / 2, T), WHITE)
ds.level.add_box(Vec3.new(0, H / 2, D + T), Vec3.new(W, H / 2, T), WHITE)
ds.level.add_box(Vec3.new(-W - T, H / 2, 0), Vec3.new(T, H / 2, D), WHITE)
ds.level.add_box(Vec3.new(W + T, H / 2, 0), Vec3.new(T, H / 2, D), WHITE)

-- Player always starts at the center; pillars keep a clear ring around it so
-- the spawn is never blocked.
local PLAYER_CLEARANCE = 2.5
ds.level.add_spawn(Vec3.new(0, 1.7, 0), true)

-- Scatter pillar obstacles, away from the walls and the player.
local PILLAR_COUNT = 4 + math.random(0, 4) -- 4..8
local PILLAR_COLOR = Vec3.new(0.6, 0.6, 0.65)
local MARGIN = 1.5
for _ = 1, PILLAR_COUNT do
    local x, z
    repeat
        x = (math.random() * 2 - 1) * (W - MARGIN)
        z = (math.random() * 2 - 1) * (D - MARGIN)
    until (x * x + z * z) > (PLAYER_CLEARANCE * PLAYER_CLEARANCE)
    local hh = 1.0 + math.random() * 1.5 -- pillar half-height 1..2.5
    ds.level.add_box(Vec3.new(x, hh, z), Vec3.new(0.5, hh, 0.5), PILLAR_COLOR)
end

-- Enemy spawns scattered around the perimeter, evenly spread with a little
-- per-spawn jitter so it's not a perfectly regular ring.
local SPAWN_COUNT = 3 + math.random(0, 3) -- 3..6
for i = 1, SPAWN_COUNT do
    local angle = (i / SPAWN_COUNT) * math.pi * 2 + math.random() * 0.5
    local radius = math.min(W, D) - 1.5
    local x = math.cos(angle) * radius
    local z = math.sin(angle) * radius
    ds.level.add_spawn(Vec3.new(x, 1.5, z), false)
end

ds.level.add_light(Vec3.new(0, H - 0.5, 0), WHITE, math.max(W, D) * 1.8, 1)

-- Example mesh placement (commented out — no shipped default mesh asset yet):
-- ds.level.add_mesh("meshes/pillar.gltf", Vec3.new(3, 0, 3), Vec3.new(0, 45, 0))
