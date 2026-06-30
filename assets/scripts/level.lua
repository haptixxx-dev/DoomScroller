-- Procedural multi-room level: 2-4 rooms in a row, connected by open
-- doorways, each room favoring a different enemy archetype. Randomized every
-- run (Lua 5.4 auto-seeds math.random per-state, so this varies in shipping
-- builds too, no os.time() needed).

local H, T = 5, 0.1
local D = 7 + math.random(0, 4) -- shared room depth (half-extent): every room
                                 -- uses the same D so the shared end-wall
                                 -- between two rooms fully covers both rooms'
                                 -- Z extent (only the intended doorway gap is
                                 -- open) -- a per-room D would leave stray
                                 -- gaps wherever rooms differ in depth.
local DOOR_WIDTH = 3.5
local ROOM_COUNT = 2 + math.random(0, 2) -- 2..4 rooms

local WHITE = Vec3.new(1, 1, 1)
local PILLAR_COLOR = Vec3.new(0.6, 0.6, 0.65)
-- Per-archetype light tint so a room's mood hints at its enemy mix:
-- Grunt=warm white, Charger=hot red, Ranged=cool blue.
local ARCHETYPE_LIGHT_COLOR = { Vec3.new(1, 0.95, 0.85), Vec3.new(1, 0.4, 0.3), Vec3.new(0.4, 0.7, 1) }

-- Lay rooms out left-to-right along X, each room's left edge exactly
-- touching the previous room's right edge (no gap), each with a randomized
-- width and a favored enemy archetype (0=Grunt 1=Charger 2=Ranged).
local rooms = {}
local cursorX = 0
for i = 1, ROOM_COUNT do
    local hw = 6 + math.random(0, 4) -- room half-width: 6..10
    rooms[i] = { hw = hw, cx = cursorX + hw, archetype = math.random(0, 2) }
    cursorX = cursorX + hw * 2
end

for i, room in ipairs(rooms) do
    local hw, cx, archetype = room.hw, room.cx, room.archetype

    -- Floor / ceiling / north / south walls (no doors needed on these; the
    -- only openings are the shared east/west walls between rooms).
    ds.level.add_box(Vec3.new(cx, -T, 0), Vec3.new(hw, T, D), WHITE)
    ds.level.add_box(Vec3.new(cx, H + T, 0), Vec3.new(hw, T, D), WHITE)
    ds.level.add_box(Vec3.new(cx, H / 2, -D - T), Vec3.new(hw, H / 2, T), WHITE)
    ds.level.add_box(Vec3.new(cx, H / 2, D + T), Vec3.new(hw, H / 2, T), WHITE)

    -- West wall: solid only for the first room (every other room shares its
    -- west wall with the previous room's doorway-gapped east wall).
    if i == 1 then
        ds.level.add_box(Vec3.new(cx - hw - T, H / 2, 0), Vec3.new(T, H / 2, D), WHITE)
    end

    -- East wall: solid for the last room; an open doorway (two wall segments
    -- with a gap between them) for every room with a neighbor to its right.
    if i == ROOM_COUNT then
        ds.level.add_box(Vec3.new(cx + hw + T, H / 2, 0), Vec3.new(T, H / 2, D), WHITE)
    else
        local segDepth = D - DOOR_WIDTH / 2
        if segDepth > 0.5 then
            local segCenter = DOOR_WIDTH / 2 + segDepth / 2
            ds.level.add_box(Vec3.new(cx + hw + T, H / 2, -segCenter), Vec3.new(T, H / 2, segDepth / 2), WHITE)
            ds.level.add_box(Vec3.new(cx + hw + T, H / 2, segCenter), Vec3.new(T, H / 2, segDepth / 2), WHITE)
        end
        -- else: the room is narrow enough that the doorway spans the whole
        -- wall -- leave it fully open, more interesting than a corridor pinch.
    end

    ds.level.add_light(Vec3.new(cx, H - 0.5, 0), ARCHETYPE_LIGHT_COLOR[archetype + 1], math.max(hw, D) * 1.8, 1)

    -- A couple of pillars for cover/interest, kept off the room's center
    -- line so the lanes between doorways stay open.
    for _ = 1, math.random(0, 2) do
        local px = cx + (math.random() * 2 - 1) * (hw - 2)
        local pz = (math.random() < 0.5 and -1 or 1) * (1.5 + math.random() * (D - 3))
        local hh = 1.0 + math.random() * 1.5
        ds.level.add_box(Vec3.new(px, hh, pz), Vec3.new(0.5, hh, 0.5), PILLAR_COLOR)
    end

    -- This room's enemy spawns favor its archetype.
    for _ = 1, 2 + math.random(0, 2) do
        local sx = cx + (math.random() * 2 - 1) * (hw - 2)
        local sz = (math.random() * 2 - 1) * (D - 2)
        ds.level.add_spawn(Vec3.new(sx, 1.5, sz), false, archetype)
    end
end

-- Player always starts centered in the first room.
ds.level.add_spawn(Vec3.new(rooms[1].cx, 1.7, 0), true)

-- Example mesh placement (commented out — no shipped default mesh asset yet):
-- ds.level.add_mesh("meshes/pillar.gltf", Vec3.new(3, 0, 3), Vec3.new(0, 45, 0))
