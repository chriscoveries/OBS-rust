if(OS_LINUX OR OS_MACOS)
  # Windows is excluded: linking a Rust static library there needs extra system
  # libraries that this test does not set up.
  find_program(SOLDR_EXECUTABLE soldr REQUIRED)

  set(_preflight_manifest "${CMAKE_SOURCE_DIR}/rust/obs-preflight-ffi/Cargo.toml")
  set(_preflight_target_dir "${CMAKE_BINARY_DIR}/obs-preflight-cargo")
  file(
    GLOB_RECURSE _preflight_rust_sources
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/rust/obs-preflight/src/*.rs"
    "${CMAKE_SOURCE_DIR}/rust/obs-preflight-ffi/src/*.rs"
  )
  set(
    _preflight_archive
    "${_preflight_target_dir}/release/${CMAKE_STATIC_LIBRARY_PREFIX}obs_preflight_ffi${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )
  add_custom_command(
    OUTPUT "${_preflight_archive}"
    COMMAND
      "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${_preflight_target_dir}" "${SOLDR_EXECUTABLE}" cargo build
      --manifest-path "${CMAKE_SOURCE_DIR}/Cargo.toml" --package obs-preflight-ffi --lib --release --locked
    DEPENDS
      "${_preflight_manifest}"
      "${CMAKE_SOURCE_DIR}/rust/obs-preflight/Cargo.toml"
      "${CMAKE_SOURCE_DIR}/Cargo.toml"
      "${CMAKE_SOURCE_DIR}/Cargo.lock"
      ${_preflight_rust_sources}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Building obs-preflight Rust C ABI"
    VERBATIM
  )
  add_custom_target(obs-preflight-rust DEPENDS "${_preflight_archive}")

  add_executable(test_preflight_abi test_preflight_abi.c)
  target_include_directories(test_preflight_abi PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/frontend/preflight")
  target_link_libraries(
    test_preflight_abi
    PRIVATE "${_preflight_archive}" ${CMOCKA_LIBRARIES} Threads::Threads ${CMAKE_DL_LIBS}
  )
  add_dependencies(test_preflight_abi obs-preflight-rust)
  add_dependencies(cmocka-tests test_preflight_abi)
  add_test(NAME test_preflight_abi COMMAND test_preflight_abi)
  set_tests_properties(test_preflight_abi PROPERTIES TIMEOUT 60)
endif()
