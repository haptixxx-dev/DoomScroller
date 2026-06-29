add_library(engine STATIC
    src/Engine.cpp
    src/rhi/sdl3/SDL3Device.cpp
)

target_include_directories(engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(engine PUBLIC SDL3::SDL3-static)
