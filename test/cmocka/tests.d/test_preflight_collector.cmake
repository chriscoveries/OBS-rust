# Exercise the OBS stats sampler together with the real Rust decision engine.
# Core video needs a graphics module, so this test runs under xvfb-run on Linux.
if(OS_LINUX AND TARGET OBS::libobs-opengl)
  # obs_startup needs an X display. Follow the Xvfb launcher used by
  # test_audio_io.cmake, but require it here so collector coverage cannot skip.
  find_program(XVFB_RUN_EXECUTABLE xvfb-run REQUIRED)
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

  add_executable(
    test_preflight_collector
    test_preflight_collector.c
    "${CMAKE_SOURCE_DIR}/frontend/preflight/obs-preflight-collector.c"
  )
  target_compile_definitions(test_preflight_collector PRIVATE GRAPHICS_MODULE_PATH="$<TARGET_FILE:OBS::libobs-opengl>")
  target_include_directories(
    test_preflight_collector
    PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/frontend/preflight"
  )
  target_link_libraries(
    test_preflight_collector
    PRIVATE OBS::libobs "${_preflight_archive}" Threads::Threads ${CMAKE_DL_LIBS} ${CMOCKA_LIBRARIES}
  )
  if(NOT MSVC)
    target_link_libraries(test_preflight_collector PRIVATE m)
  endif()
  add_dependencies(test_preflight_collector libobs-opengl obs-preflight-rust)
  add_dependencies(cmocka-tests test_preflight_collector)
  add_test(
    NAME test_preflight_collector
    COMMAND "${XVFB_RUN_EXECUTABLE}" -a "$<TARGET_FILE:test_preflight_collector>"
    WORKING_DIRECTORY "${OBS_OUTPUT_DIR}/$<CONFIG>"
  )
  set_tests_properties(test_preflight_collector PROPERTIES TIMEOUT 60)
endif()
