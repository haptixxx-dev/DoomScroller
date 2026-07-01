add_executable(ds_tests
    test_rhi_types.cpp
    test_level_format.cpp
    test_level_parser.cpp
    test_gltf_extract.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/GltfExtract.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/cgltf_impl.cpp
)
target_link_libraries(ds_tests PRIVATE engine_headers cgltf Catch2::Catch2WithMain)
# Lets test_gltf_extract.cpp parse the real fixture files under tests/fixtures/
# instead of embedding glTF JSON as a giant string literal.
target_compile_definitions(ds_tests PRIVATE DS_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
add_test(NAME ds_tests COMMAND ds_tests)

# Pure math + component tests: no SDL3/Tracy/mimalloc/Jolt in test process
add_executable(ds_engine_tests
    test_transform.cpp
    test_camera.cpp
    test_components.cpp
    test_particles.cpp
    test_lights.cpp
    test_movement.cpp
    test_projectile.cpp
    test_frustum.cpp
    test_gamefeel.cpp
    test_style.cpp
    test_settings.cpp
    test_input.cpp
    test_save.cpp
    test_tonemap.cpp
    test_bloom.cpp
    test_pbr.cpp
    test_shadow.cpp
    test_rendergraph.cpp
    test_shaderwatcher.cpp
    test_archetype.cpp
    test_parry.cpp
    test_upgrade.cpp
    test_combat_feedback.cpp
    test_instancing.cpp
    test_dstex.cpp
    test_quality.cpp
    test_userstorage.cpp
    test_boss.cpp
    test_procedural_textures.cpp
    test_golden_image.cpp
    test_metaprogression.cpp
    test_economy.cpp
)
target_link_libraries(ds_engine_tests PRIVATE engine_math engine_headers Catch2::Catch2WithMain)
add_test(NAME ds_engine_tests COMMAND ds_engine_tests)

# BC7 cook round-trip test (task 56): links the vendored bc7enc encoder +
# bc7decomp decoder and the pure Bc7Cook.h layout helper, so it truth-tests the
# CPU compression path (encode -> decode -> PSNR) without a GPU. No SDL3/Jolt.
add_executable(ds_bc7_tests test_bc7.cpp)
target_link_libraries(ds_bc7_tests PRIVATE engine_headers bc7enc Catch2::Catch2WithMain)
add_test(NAME ds_bc7_tests COMMAND ds_bc7_tests)

# Headless smoke target (PLAN task 46): NOT a Catch2 test. Links the FULL
# `engine` lib and constructs every device-independent subsystem (physics,
# audio, Lua scripts, particles, ECS) without creating a GPU device/window, so
# CI surfaces link / static-init-order / PIMPL-ctor regressions the GPU-gated
# build cannot in a headless sandbox. Returns non-zero on any failed check.
add_executable(ds_smoke smoke_main.cpp)
target_link_libraries(ds_smoke PRIVATE engine)
# Locate the shipped Lua scripts via the source tree regardless of preset. CI
# runs the release preset (DS_DEV OFF) where ds::paths::assets() resolves to a
# binary-relative dir with no assets staged next to the exe; baking the source
# assets/ dir keeps the script-load smoke checks green there (mirrors the
# DS_ASSETS_DIR injection used by ds_script_tests below).
target_compile_definitions(ds_smoke PRIVATE DS_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
# macOS: force-load mimalloc so its __interpose section survives dead-stripping
# from the static lib (same rationale as the DoomScroller exe; see
# game/CMakeLists.txt). Without it a thread-exit free crosses allocator zones.
if(APPLE)
    target_link_options(ds_smoke PRIVATE -Wl,-force_load,$<TARGET_FILE:mimalloc-static>)
endif()
add_test(NAME ds_smoke COMMAND ds_smoke)

# Lua scripting test: validates the lua link + binding plumbing. Compiles
# ScriptSystem.cpp directly and links lua_static (+ SDL3 for SDL_Log* only) so
# the test process avoids the full engine's Jolt/mimalloc global state.
add_executable(ds_script_tests
    test_scripting.cpp
    test_lua_vec3.cpp
    test_lua_global.cpp
    test_parry_lua.cpp
    test_pickups_lua.cpp
    test_wave_lua.cpp
    test_boss_lua.cpp
    test_enemy_ai_lua.cpp
    test_level_lua.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/ScriptSystem.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/ScriptSystemGameplay.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/script/LuaUserdata.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/script/LuaVec3.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/script/LuaGlobal.cpp
)
target_link_libraries(ds_script_tests PRIVATE engine_headers lua_static SDL3::SDL3-static Catch2::Catch2WithMain)
if(DS_DEV)
    target_compile_definitions(ds_script_tests PRIVATE DS_DEV)
endif()
# Lets Lua-side gameplay-module tests (test_*_lua.cpp) loadFile() the real
# shipped scripts under assets/scripts/ instead of duplicating their source
# inline, so a syntax/logic error in the actual file fails the build's tests.
target_compile_definitions(ds_script_tests PRIVATE DS_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME ds_script_tests COMMAND ds_script_tests)
