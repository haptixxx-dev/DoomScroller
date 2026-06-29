add_executable(ds_tests
    test_rhi_types.cpp
)
target_link_libraries(ds_tests PRIVATE engine_headers Catch2::Catch2WithMain)
add_test(NAME ds_tests COMMAND ds_tests)

# Pure math + component tests: no SDL3/Tracy/mimalloc/Jolt in test process
add_executable(ds_engine_tests
    test_transform.cpp
    test_camera.cpp
    test_components.cpp
)
target_link_libraries(ds_engine_tests PRIVATE engine_math engine_headers Catch2::Catch2WithMain)
add_test(NAME ds_engine_tests COMMAND ds_engine_tests)
