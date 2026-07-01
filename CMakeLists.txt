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

# ---------------------------------------------------------------------------
# CPack per-OS packaging (task 50).
#
# Configures the packaging generators so `cpack` (after a build) emits a
# distributable per platform that ships ONLY the runnable game: the executable
# plus assets/ and shaders/ next to it, matching the binary-relative layout
# ds::paths expects in shipping mode (DS_DEV OFF — see engine/src/Paths.cpp:
# assets()=binDir/assets, shaders()=binDir/shaders, userDir=binDir/user). The
# install() rules that stage that layout live in game/CMakeLists.txt under the
# `game` CPack component; the macOS .app bundle is described there too.
#
# Dev-artifact trim: the vendored mimalloc + walterschell/Lua subprojects
# register unconditional, component-LESS install() rules (their static libs,
# headers, cmake package config, mimalloc.pc). Those land in CPack's implicit
# `Unspecified` component. We DON'T carry them: everything we ship uses
# `COMPONENT game`, and packaging is restricted to that one component via
# CPACK_COMPONENTS_ALL. The load-bearing switch is CPACK_*_COMPONENT_INSTALL ON
# per generator — without it the archive/DragNDrop generators do a MONOLITHIC
# install that ignores component filtering and re-drags the whole Unspecified
# dev tree back in (this is exactly why an earlier CPACK_COMPONENTS_ALL=game
# alone did not trim anything). CMAKE_SKIP_INSTALL_RULES on the deps is NOT a
# viable alternative here: it makes CMake omit the subdir cmake_install.cmake
# entirely while the parent still include()s it, which hard-errors cpack.
set(CPACK_PACKAGE_NAME "DoomScroller")
set(CPACK_PACKAGE_VENDOR "DoomScroller")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast 3D arena FPS")
set(CPACK_PACKAGE_EXECUTABLES "DoomScroller" "DoomScroller")

# Ship only the `game` component, packed into a single archive/installer.
set(CPACK_COMPONENTS_ALL game)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
# Enable per-component packaging for every generator we use, else the default
# monolithic install ignores CPACK_COMPONENTS_ALL and re-includes the deps.
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_ARCHIVE_DoomScroller_FILE_NAME "DoomScroller-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")

if(WIN32)
    # ZIP is the always-works portable drop; NSIS produces a Start-menu / Program
    # Files installer (config only — building it needs makensis on the runner).
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_NSIS_COMPONENT_INSTALL ON)
    set(CPACK_NSIS_PACKAGE_NAME "DoomScroller")
    set(CPACK_NSIS_DISPLAY_NAME "DoomScroller")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "DoomScroller")
elseif(APPLE)
    # macOS ships as a drag-install .dmg carrying the DoomScroller.app bundle
    # (MACOSX_BUNDLE, Info.plist, Metal shaders staged in Contents/Resources —
    # see game/CMakeLists.txt). TGZ is kept as a headless-verifiable fallback
    # and for CI/no-hdiutil environments.
    set(CPACK_GENERATOR "DragNDrop;TGZ")
    set(CPACK_DMG_COMPONENT_INSTALL ON)
    set(CPACK_DMG_VOLUME_NAME "DoomScroller")
else()
    # Linux: a relocatable TGZ (exe + assets/ + shaders/, runnable in place via
    # ds::paths' argv0-relative resolution). An AppImage is the nicer
    # single-file distributable but needs the external linuxdeploy/appimagetool
    # binaries fetched on the CI runner, so it's driven by a CI script rather
    # than pure CPack — the TGZ here is the self-contained, headless-verifiable
    # artifact and the AppImage's payload staging root.
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
