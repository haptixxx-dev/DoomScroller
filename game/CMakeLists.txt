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
# Install / packaging staging (task 50 pre-work).
# Stages the exe + assets + compiled shaders into the binary-RELATIVE layout
# ds::paths expects in shipping mode (DS_DEV OFF): the exe at the install root,
# assets/ and shaders/ next to it (ds::paths::assets()/shaders() resolve to
# <binDir>/assets and <binDir>/shaders). Shaders are a build artifact
# (${CMAKE_BINARY_DIR}/shaders, produced by ds_compile_slang), so they are only
# present after a build — install(DIRECTORY ... OPTIONAL) tolerates a
# configure-time-only run. The cooked-asset story (BC7 .dstex) is layered on by
# the cook step; source assets/ ship as the DS_DEV-less fallback.
# All game staging goes in the `game` CPack component so packaging can ship ONLY
# it — the vendored deps (Lua, mimalloc) install their own headers/libs/cmake
# files unconditionally, which would otherwise bloat the archive with dev
# artifacts. CPACK_COMPONENTS_ALL (root CMakeLists) restricts the package to
# this component.
install(TARGETS DoomScroller RUNTIME DESTINATION . COMPONENT game)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION assets COMPONENT game)
install(DIRECTORY ${CMAKE_BINARY_DIR}/shaders/ DESTINATION shaders COMPONENT game OPTIONAL)
