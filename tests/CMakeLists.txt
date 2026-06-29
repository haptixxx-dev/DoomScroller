add_executable(ds_tests
    test_rhi_types.cpp
)

# Link engine_headers (types only) not engine (pulls SDL3 + mimalloc global overrides)
target_link_libraries(ds_tests PRIVATE engine_headers Catch2::Catch2WithMain)

add_test(NAME ds_tests COMMAND ds_tests)
