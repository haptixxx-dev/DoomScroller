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
# Engine + Game
# ---------------------------------------------------------------------------
add_subdirectory(engine)
add_subdirectory(game)
