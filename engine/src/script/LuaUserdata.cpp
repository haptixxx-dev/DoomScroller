#include "engine/script/LuaUserdata.h"

namespace ds::lua {

namespace {

void setMetamethod(lua_State* L, const char* field, lua_CFunction fn) {
    if (!fn)
        return;
    lua_pushcfunction(L, fn);
    lua_setfield(L, -2, field);
}

} // namespace

void registerType(lua_State* L, const TypeSpec& spec) {
    luaL_newmetatable(L, spec.name); // pushes the (new) metatable

    setMetamethod(L, "__index", spec.index);
    setMetamethod(L, "__newindex", spec.newindex);
    setMetamethod(L, "__gc", spec.gc);
    setMetamethod(L, "__tostring", spec.tostring);
    setMetamethod(L, "__add", spec.add);
    setMetamethod(L, "__sub", spec.sub);
    setMetamethod(L, "__mul", spec.mul);
    setMetamethod(L, "__unm", spec.unm);
    setMetamethod(L, "__eq", spec.eq);

    if (spec.methods) {
        lua_newtable(L);
        luaL_setfuncs(L, spec.methods, 0);
        lua_setfield(L, -2, "__methods");
    }

    lua_pop(L, 1); // metatable
}

int methodFallback(lua_State* L, const char* typeName) {
    luaL_getmetatable(L, typeName); // [..., metatable]
    lua_getfield(L, -1, "__methods"); // [..., metatable, methods]
    lua_pushvalue(L, 2); // key
    lua_gettable(L, -2); // [..., metatable, methods, methods[key]]
    return 1; // Lua discards everything below the top result for us.
}

} // namespace ds::lua
