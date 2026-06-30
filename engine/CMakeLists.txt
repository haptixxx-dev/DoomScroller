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
target_link_libraries(engine_headers INTERFACE glm::glm EnTT::EnTT)

# Pure-math subset for headless unit tests (Camera, no SDL3/Tracy/Jolt/mimalloc)
add_library(engine_math STATIC
    src/Camera.cpp
    src/ParticleSystem.cpp
    src/ShaderWatcher.cpp
)
target_include_directories(engine_math PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)
target_link_libraries(engine_math PUBLIC glm::glm)
target_compile_definitions(engine_math PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)

add_library(engine STATIC
    src/Engine.cpp
    src/Camera.cpp
    src/Paths.cpp
    src/ShaderLoader.cpp
    src/ShaderWatcher.cpp
    src/TextureManager.cpp
    src/MeshLoader.cpp
    src/GltfExtract.cpp
    src/PhysicsWorld.cpp
    src/LevelLoader.cpp
    src/LevelGen.cpp
    src/PlayerController.cpp
    src/ParticleSystem.cpp
    src/HighScore.cpp
    src/UISystem.cpp
    src/AudioSystem.cpp
    src/ScriptSystem.cpp
    src/ScriptSystemGameplay.cpp
    src/script/LuaUserdata.cpp
    src/script/LuaVec3.cpp
    src/script/LuaGlobal.cpp
    src/miniaudio_impl.cpp
    src/cgltf_impl.cpp
    src/stb_impl.cpp
    src/ecs/RenderSystem.cpp
    src/ecs/EnemySystem.cpp
    src/ecs/ProjectileSystem.cpp
    src/rhi/sdl3/SDL3Device.cpp
)

target_include_directories(engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/include   # generated BuildConfig.h
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(engine PUBLIC SDL3::SDL3-static TracyClient mimalloc-static glm::glm EnTT::EnTT stb cgltf Jolt miniaudio lua_static)

target_compile_definitions(engine PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)

if(DS_DEV)
    target_compile_definitions(engine PUBLIC DS_DEV)
endif()

if(DS_PROFILE)
    target_compile_definitions(engine PUBLIC DS_PROFILE)
endif()
