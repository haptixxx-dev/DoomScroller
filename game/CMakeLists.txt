add_executable(DoomScroller
    src/main.cpp
)

target_link_libraries(DoomScroller PRIVATE engine)

# macOS: force-load mimalloc so its __interpose section is included in the
# executable. Without this the override/interpose object is dead-stripped from
# the static lib, leaving the malloc zone only partially hooked: libc++abi
# allocates per-thread __cxa_eh_globals via mimalloc but frees them through the
# system zone on thread exit -> "pointer being freed was not allocated" crash.
if(APPLE)
    target_link_options(DoomScroller PRIVATE
        -Wl,-force_load,$<TARGET_FILE:mimalloc-static>)
endif()

set_target_properties(DoomScroller PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ---------------------------------------------------------------------------
# macOS .app bundle (task 50).
# On macOS the game ships as DoomScroller.app: MACOSX_BUNDLE makes CMake lay the
# exe out at DoomScroller.app/Contents/MacOS/DoomScroller and synthesize the
# Contents/ skeleton from the Info.plist template below. The cooked data
# (assets/, compiled shaders incl. the .msl Metal bytecode) is staged under
# Contents/Resources by the install() rules further down; ds::paths
# (engine/src/Paths.cpp) detects the Contents/MacOS layout at runtime and
# resolves assets()/shaders() to that sibling Resources dir.
if(APPLE)
    set_target_properties(DoomScroller PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "DoomScroller"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.doomscroller.DoomScroller"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "DoomScroller"
    )
endif()

# ---------------------------------------------------------------------------
# Build-tree runnability: stage shaders + assets NEXT TO the exe after link.
#
# In non-DEV builds (release/profile — DS_DEV OFF) ds::paths::shaders()/assets()
# resolve BINARY-relative (resourceRoot()/shaders, resourceRoot()/assets — see
# engine/src/Paths.cpp). That layout is normally only produced by `cmake
# --install` (the install() rules below) / CPack, so a raw build-tree release
# binary crashes at launch ("ShaderLoader: cannot open .../bin/shaders/
# mesh.vertex.spv"). This POST_BUILD copy mirrors the compiled shaders
# (${CMAKE_BINARY_DIR}/shaders, produced by the ds_compile_slang targets) and
# the source assets/ into the exe's own dir so the build-tree binary finds them
# binary-relative, exactly like shipping mode — no extra install step needed.
#
# This is ADDITIVE and build-tree only: it does not touch the install()/CPack
# staging below, and it is harmless (just redundant) for DS_DEV/debug, which
# reads shaders from DS_BINARY_DIR/shaders and assets from the source tree.
#
# copy_directory (not _if_different) is used for portability down to the
# declared cmake_minimum_required (3.25); _if_different only exists since 3.26.
# The shader/asset trees are small, so the unconditional re-copy is negligible.
#
# Depend on the shader compile targets so the .spv/.msl/.dxil exist before we
# copy them (DS_SHADER_TARGETS is populated by ds_compile_slang in the root
# CMakeLists.txt).
add_dependencies(DoomScroller ${DS_SHADER_TARGETS})

if(APPLE)
    # macOS .app bundle: $<TARGET_FILE_DIR:DoomScroller> resolves inside
    # DoomScroller.app/Contents/MacOS, and resourceRoot() (Paths.cpp) redirects
    # a Contents/MacOS exe to the sibling Contents/Resources. Stage there so the
    # build-tree bundle is runnable in place too, matching the install() layout.
    set(_ds_stage_root "$<TARGET_FILE_DIR:DoomScroller>/../Resources")
else()
    # Windows / Linux flat layout: resourceRoot() == the exe's own dir.
    set(_ds_stage_root "$<TARGET_FILE_DIR:DoomScroller>")
endif()

add_custom_command(TARGET DoomScroller POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/shaders" "${_ds_stage_root}/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets" "${_ds_stage_root}/assets"
    COMMENT "Staging shaders/ + assets/ next to DoomScroller for build-tree runs"
    VERBATIM
)

# ---------------------------------------------------------------------------
# Install / packaging staging (task 50).
# Stages the exe + assets + compiled shaders into the binary-RELATIVE layout
# ds::paths expects in shipping mode (DS_DEV OFF): ds::paths::assets()/shaders()
# resolve to a resource root that is the exe's own dir on Windows/Linux (flat
# layout) and Contents/Resources inside the macOS .app bundle.
# Shaders are a build artifact (${CMAKE_BINARY_DIR}/shaders, produced by
# ds_compile_slang), so they are only present after a build — install(DIRECTORY
# ... OPTIONAL) tolerates a configure-time-only run. The cooked-asset story
# (BC7 .dstex) is layered on by the cook step; source assets/ ship as the
# DS_DEV-less fallback.
# All game staging goes in the `game` CPack component so packaging can ship ONLY
# it — the vendored deps (Lua, mimalloc) register component-less install rules
# that would otherwise bloat the archive with a dev-artifact include/ lib/
# share/ tree; the root CMakeLists restricts the package to this component
# (CPACK_COMPONENTS_ALL + per-generator *_COMPONENT_INSTALL ON).
if(APPLE)
    # Into the .app: the bundle install lands DoomScroller.app at the package
    # root, and the data goes inside Contents/Resources so ds::paths finds it.
    install(TARGETS DoomScroller
        BUNDLE DESTINATION . COMPONENT game)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/
        DESTINATION DoomScroller.app/Contents/Resources/assets COMPONENT game)
    install(DIRECTORY ${CMAKE_BINARY_DIR}/shaders/
        DESTINATION DoomScroller.app/Contents/Resources/shaders COMPONENT game OPTIONAL)
else()
    # Windows / Linux flat layout: exe at the root, assets/ and shaders/ next to
    # it (ds::paths resolves them relative to the exe's own dir).
    install(TARGETS DoomScroller RUNTIME DESTINATION . COMPONENT game)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION assets COMPONENT game)
    install(DIRECTORY ${CMAKE_BINARY_DIR}/shaders/ DESTINATION shaders COMPONENT game OPTIONAL)
endif()
