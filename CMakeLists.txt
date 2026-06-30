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

ds_compile_slang(
    SOURCES   ${CMAKE_SOURCE_DIR}/shaders/triangle.slang
              ${CMAKE_SOURCE_DIR}/shaders/mesh.slang
              ${CMAKE_SOURCE_DIR}/shaders/particle.slang
              ${CMAKE_SOURCE_DIR}/shaders/ui.slang
    STAGES    vertex fragment
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
