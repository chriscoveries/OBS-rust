# cmocka test fragments

Add a new cmocka test as `<test>.cmake` here instead of appending to
`../CMakeLists.txt` (obs-rust/obs-studio#137). Each fragment is included in
`test/cmocka`'s scope, so write it exactly like a block in that file:

```cmake
add_executable(test_example test_example.c)
target_include_directories(test_example PRIVATE ${CMOCKA_INCLUDE_DIR})
target_link_libraries(test_example PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
add_dependencies(cmocka-tests test_example)
add_test(test_example ${CMAKE_CURRENT_BINARY_DIR}/test_example)
```

Wrap plugin- or platform-specific tests in their own `if(...)`/`endif()`.
