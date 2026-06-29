add_executable(DoomScroller
    src/main.cpp
)

target_link_libraries(DoomScroller PRIVATE engine)

set_target_properties(DoomScroller PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
