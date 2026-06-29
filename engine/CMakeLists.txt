# Generate BuildConfig.h from .in template
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/engine/BuildConfig.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/engine/BuildConfig.h
)

# Header-only interface for tests that only need type definitions (no SDL3/mimalloc deps)
add_library(engine_headers INTERFACE)
target_include_directories(engine_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

add_library(engine STATIC
    src/Engine.cpp
    src/Camera.cpp
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

target_link_libraries(engine PUBLIC SDL3::SDL3-static TracyClient mimalloc-static glm::glm)

target_compile_definitions(engine PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)

if(DS_DEV)
    target_compile_definitions(engine PUBLIC DS_DEV)
endif()

if(DS_PROFILE)
    target_compile_definitions(engine PUBLIC DS_PROFILE)
endif()
