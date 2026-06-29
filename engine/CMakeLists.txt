# Generate BuildConfig.h from .in template
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/engine/BuildConfig.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/engine/BuildConfig.h
)

add_library(engine STATIC
    src/Engine.cpp
    src/Paths.cpp
    src/ShaderLoader.cpp
    src/rhi/sdl3/SDL3Device.cpp
)

target_include_directories(engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/include   # generated BuildConfig.h
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(engine PUBLIC SDL3::SDL3-static TracyClient mimalloc-static)

if(DS_DEV)
    target_compile_definitions(engine PUBLIC DS_DEV)
endif()

if(DS_PROFILE)
    target_compile_definitions(engine PUBLIC DS_PROFILE)
endif()
