#pragma once

struct lua_State;

namespace ds::lua {

// Builds the global ds.Global table: organized sub-tables (camera/player/time)
// whose fields read and write live Engine state through ScriptSystem's
// CameraCallbacks/PlayerCallbacks/TimeCallbacks (engine/ScriptSystem.h). Each
// sub-table is an otherwise-empty Lua table with one __index/__newindex C
// dispatcher pair, so every access goes straight to the C++ side with no
// per-frame snapshot.
void registerGlobalTable(lua_State* L);

} // namespace ds::lua
