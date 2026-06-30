#pragma once

#include <lua.hpp>

#include <new>

// Generic Lua userdata/metatable glue. A type T gets Lua-side userdata support
// by specializing UserdataTraits<T> (just a registry metatable name) and calling
// registerType() once with a TypeSpec of C-function metamethods/methods. push/
// check/test<T> are then reusable by any binding code that needs to move T in
// and out of the Lua stack. Vec3 (LuaVec3.h) is the first consumer; future types
// (Vec2, Quat, an entity handle) follow the same pattern.
namespace ds::lua {

template <typename T>
struct UserdataTraits; // specialize per T: static constexpr const char* name;

// Constructs a T in-place inside a new full userdata and tags it with T's
// metatable. Returns a pointer to the live object (still owned by the Lua GC).
template <typename T>
T* pushUserdata(lua_State* L, const T& value) {
    void* mem = lua_newuserdatauv(L, sizeof(T), 0);
    T* obj    = new (mem) T(value);
    luaL_setmetatable(L, UserdataTraits<T>::name);
    return obj;
}

// Like luaL_checkudata: errors (longjmp) if the stack value at idx isn't a T.
template <typename T>
T* checkUserdata(lua_State* L, int idx) {
    return static_cast<T*>(luaL_checkudata(L, idx, UserdataTraits<T>::name));
}

// Like luaL_testudata: returns nullptr (no error) if the stack value at idx
// isn't a T. Used where a binding accepts T in either operand position.
template <typename T>
T* testUserdata(lua_State* L, int idx) {
    return static_cast<T*>(luaL_testudata(L, idx, UserdataTraits<T>::name));
}

// Generic __gc: runs T's destructor over the userdata's storage. Registered by
// types whose T isn't trivially destructible (harmless to register always).
template <typename T>
int gcUserdata(lua_State* L) {
    T* obj = checkUserdata<T>(L, 1);
    obj->~T();
    return 0;
}

// Metamethods/methods for one userdata type. Any function pointer left null is
// simply not installed on the metatable (Lua's default behavior applies).
struct TypeSpec {
    const char* name        = nullptr; // registry metatable name, e.g. "ds.Vec3"
    const luaL_Reg* methods = nullptr; // null-terminated; stashed under metatable.__methods
    lua_CFunction index     = nullptr;
    lua_CFunction newindex  = nullptr;
    lua_CFunction gc        = nullptr;
    lua_CFunction tostring  = nullptr;
    lua_CFunction add       = nullptr;
    lua_CFunction sub       = nullptr;
    lua_CFunction mul       = nullptr;
    lua_CFunction unm       = nullptr;
    lua_CFunction eq        = nullptr;
};

// Builds (or rebuilds) the named metatable in the registry from spec. Call once
// per type during ScriptSystem init.
void registerType(lua_State* L, const TypeSpec& spec);

// Shared __index tail: looks `key` (stack slot 2 of the calling __index) up in
// typeName's metatable.__methods table and returns it (or nil). Every hand-
// written per-type __index calls this after its own named-field checks fail.
int methodFallback(lua_State* L, const char* typeName);

} // namespace ds::lua
