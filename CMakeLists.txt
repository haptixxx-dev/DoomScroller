cmake_minimum_required(VERSION 3.25)
project(DoomScroller VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# SDL3
# ---------------------------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/SDL)

# ---------------------------------------------------------------------------
# GLM
# ---------------------------------------------------------------------------
add_subdirectory(extern/glm)

# ---------------------------------------------------------------------------
# Header-only interface targets
# ---------------------------------------------------------------------------
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE extern/stb)

add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE extern/cgltf)

add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE extern/miniaudio)

# BC7 texture encoder + decoder (bc7enc_rdo). Only the two standalone TUs are
# compiled — the RDO/ispc machinery is not pulled in. Used by the asset_cook
# tool (encode) and the ds_bc7_tests round-trip (encode+decode). The decoder
# guards its SSE2 intrinsics behind __SSE2__/_M_X64 with scalar fallbacks, so it
# compiles on arm64 macOS too.
add_library(bc7enc STATIC
    extern/bc7enc_rdo/bc7enc.cpp
    extern/bc7enc_rdo/bc7decomp.cpp
)
target_include_directories(bc7enc PUBLIC extern/bc7enc_rdo)

# ---------------------------------------------------------------------------
# EnTT (ECS)
# ---------------------------------------------------------------------------
add_subdirectory(extern/entt)

# ---------------------------------------------------------------------------
# Jolt Physics
# ---------------------------------------------------------------------------
set(TARGET_UNIT_TESTS    OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD   OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES       OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER        OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL       OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS   OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS  OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
# Jolt defaults to the static MSVC runtime (/MT); every other target (SDL3,
# mimalloc, the executables) uses CMake's default dynamic runtime (/MD). The
# mismatch fails linking on Windows (LNK2038). Align Jolt to the dynamic runtime.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/JoltPhysics/Build Jolt)

# ---------------------------------------------------------------------------
# mimalloc
# ---------------------------------------------------------------------------
set(MI_BUILD_SHARED  OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT  OFF CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/mimalloc)

# ---------------------------------------------------------------------------
# Lua 5.4
# ---------------------------------------------------------------------------
add_subdirectory(extern/lua/lua-5.4.7)

# ---------------------------------------------------------------------------
# Tracy profiler (opt-in via DS_PROFILE)
# Embedded directly via public/TracyClient.cpp to avoid CMake preset conflicts
# ---------------------------------------------------------------------------
option(DS_PROFILE "Enable Tracy frame profiler" OFF)
add_library(TracyClient STATIC)
target_sources(TracyClient PRIVATE extern/tracy/public/TracyClient.cpp)
target_include_directories(TracyClient PUBLIC extern/tracy/public)
if(DS_PROFILE)
    target_compile_definitions(TracyClient PUBLIC TRACY_ENABLE TRACY_ON_DEMAND)
endif()

# ---------------------------------------------------------------------------
# DS_DEV flag - enables hot-reload, runtime asset loading, extra logging
# ---------------------------------------------------------------------------
option(DS_DEV "Enable developer mode" OFF)

# ---------------------------------------------------------------------------
# Slang shader compiler (prebuilt binary download)
# ---------------------------------------------------------------------------
include(cmake/Slang.cmake)
include(cmake/CompileSlang.cmake)

set(_shader_out "${CMAKE_BINARY_DIR}/shaders")
if(APPLE)
    set(_shader_targets spirv metal)
elseif(WIN32)
    set(_shader_targets spirv dxil)
else()
    set(_shader_targets spirv)
endif()

# Primary shader target format, baked into BuildConfig.h for DS_DEV runtime
# shader recompiles (ShaderWatcher). The runtime ShaderLoader picks whichever
# of spirv/msl/dxil the GPU supports; on each platform the first listed target
# is the one that GPU will use, so recompile to that format.
list(GET _shader_targets 0 DS_PRIMARY_SHADER_TARGET)

ds_compile_slang(
    SOURCES   ${CMAKE_SOURCE_DIR}/shaders/triangle.slang
              ${CMAKE_SOURCE_DIR}/shaders/mesh.slang
              ${CMAKE_SOURCE_DIR}/shaders/particle.slang
              ${CMAKE_SOURCE_DIR}/shaders/ui.slang
              ${CMAKE_SOURCE_DIR}/shaders/tonemap.slang
              ${CMAKE_SOURCE_DIR}/shaders/shadow_depth.slang
              ${CMAKE_SOURCE_DIR}/shaders/point_shadow_depth.slang
              ${CMAKE_SOURCE_DIR}/shaders/post.slang
              ${CMAKE_SOURCE_DIR}/shaders/blur.slang
    STAGES    vertex fragment
    TARGETS   ${_shader_targets}
    OUT_DIR   "${_shader_out}"
)

# Compute shaders (task 39): a separate invocation with the compute stage
# (entry compMain). Shares the same output dir; CompileSlang.cmake tags the
# custom-target name with the stage list so this does not collide with the
# vertex/fragment batch above.
ds_compile_slang(
    SOURCES   ${CMAKE_SOURCE_DIR}/shaders/particle_sim.slang
    STAGES    compute
    TARGETS   ${_shader_targets}
    OUT_DIR   "${_shader_out}"
)

# ---------------------------------------------------------------------------
# Catch2 (unit tests)
# ---------------------------------------------------------------------------
set(CATCH_INSTALL_DOCS    OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS  OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/catch2)

# ---------------------------------------------------------------------------
# Engine + Game + Tests
# ---------------------------------------------------------------------------
add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(tools)

enable_testing()
add_subdirectory(tests)

# ---------------------------------------------------------------------------
# Docs (optional): the ds.* Lua API reference, docs/Doxyfile. Not built by
# default or by `all`; only exists when Doxygen is found, so a missing local
# install never breaks a normal build.
# ---------------------------------------------------------------------------
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/docs/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating ds.* Lua API docs (docs/generated/html/index.html)"
        VERBATIM
    )
endif()
