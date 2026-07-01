# Offline level converter. Links the engine library for LevelLoader::write.
add_executable(level_convert
    level_convert.cpp
)
target_link_libraries(level_convert PRIVATE engine)

# Offline asset cooker: reads source images and writes .dstex containers.
# Links bc7enc for the default BC7-compressed cook path.
add_executable(asset_cook
    asset_cook.cpp
)
target_link_libraries(asset_cook PRIVATE engine bc7enc)
