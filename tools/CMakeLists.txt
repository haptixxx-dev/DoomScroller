# Offline level converter. Links the engine library for LevelLoader::write.
add_executable(level_convert
    level_convert.cpp
)
target_link_libraries(level_convert PRIVATE engine)
