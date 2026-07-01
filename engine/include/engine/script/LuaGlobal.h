#pragma once

struct lua_State;

namespace ds::lua {

/** Builds the global `ds.Global` table: organized sub-tables whose fields
 *  read and write live Engine state through ScriptSystem's
 *  CameraCallbacks/PlayerCallbacks/TimeCallbacks (engine/ScriptSystem.h). Each
 *  sub-table is an otherwise-empty Lua table with one __index/__newindex C
 *  dispatcher pair, so every access goes straight to the C++ side with no
 *  per-frame snapshot (read `ds.Global.player.health` and you get this
 *  instant's HP, not a cached copy).
 *
 *  Lua-side surface:
 *  - `ds.Global.camera.position` (ds.Vec3, read/write)
 *  - `ds.Global.camera.yaw`, `.pitch`, `.fovY` (numbers, read/write)
 *  - `ds.Global.player.health` (read/write); `.maxHealth`, `.dashCharges`,
 *    `.sliding`, `.iFrames`, `.eyePosition` (read-only)
 *  - `ds.Global.time.dt`, `.elapsed` (read-only)
 */
void registerGlobalTable(lua_State* L);

} // namespace ds::lua
