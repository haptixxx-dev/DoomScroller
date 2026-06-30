-- Default procedural level. Mirrors the hardcoded buildArena()/arena.dslv
-- arena shape but demonstrates Lua-driven box/spawn/light placement instead
-- of a static binary file. ds.level.* calls accumulate into the engine's
-- level data as a side effect; this script returns nothing.

local W, H, D, T = 10, 5, 10, 0.1
local WHITE = Vec3.new(1, 1, 1)

ds.level.add_box(Vec3.new(0, -T, 0), Vec3.new(W, T, D), WHITE) -- floor
ds.level.add_box(Vec3.new(0, H + T, 0), Vec3.new(W, T, D), WHITE) -- ceiling
ds.level.add_box(Vec3.new(0, H / 2, -D - T), Vec3.new(W, H / 2, T), WHITE) -- north wall
ds.level.add_box(Vec3.new(0, H / 2, D + T), Vec3.new(W, H / 2, T), WHITE) -- south wall
ds.level.add_box(Vec3.new(-W - T, H / 2, 0), Vec3.new(T, H / 2, D), WHITE) -- west wall
ds.level.add_box(Vec3.new(W + T, H / 2, 0), Vec3.new(T, H / 2, D), WHITE) -- east wall

ds.level.add_spawn(Vec3.new(0, 1.7, 0), true) -- player start
ds.level.add_spawn(Vec3.new(-7, 1.5, -7), false)
ds.level.add_spawn(Vec3.new(7, 1.5, -7), false)
ds.level.add_spawn(Vec3.new(0, 1.5, 7), false)

ds.level.add_light(Vec3.new(0, H - 0.5, 0), WHITE, 20, 1)

-- Example mesh placement (commented out — no shipped default mesh asset yet):
-- ds.level.add_mesh("meshes/pillar.gltf", Vec3.new(3, 0, 3), Vec3.new(0, 45, 0))
