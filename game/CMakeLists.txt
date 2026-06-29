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
