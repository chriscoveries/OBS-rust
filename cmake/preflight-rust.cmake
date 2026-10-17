option(OBS_PREFLIGHT_ENABLE "Build the OBS Preflight bench and Rust engine" ON)

# The Flatpak sandbox does not provision soldr or its Rust toolchain; that
# package disables this demo until those build tools are available in-sandbox.
if(OBS_PREFLIGHT_ENABLE AND (OS_LINUX OR OS_MACOS))
  find_program(SOLDR_EXECUTABLE soldr REQUIRED)
  find_package(Threads REQUIRED)

  set(OBS_PREFLIGHT_CARGO_TARGET_DIR "${CMAKE_BINARY_DIR}/obs-preflight-cargo")
  set(OBS_PREFLIGHT_MANIFEST "${CMAKE_SOURCE_DIR}/rust/obs-preflight-ffi/Cargo.toml")
  set(
    OBS_PREFLIGHT_ARCHIVE
    "${OBS_PREFLIGHT_CARGO_TARGET_DIR}/release/${CMAKE_STATIC_LIBRARY_PREFIX}obs_preflight_ffi${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )
  file(
    GLOB_RECURSE OBS_PREFLIGHT_RUST_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/rust/obs-preflight/src/*.rs"
    "${CMAKE_SOURCE_DIR}/rust/obs-preflight-ffi/src/*.rs"
  )

  add_custom_command(
    OUTPUT "${OBS_PREFLIGHT_ARCHIVE}"
    COMMAND
      "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${OBS_PREFLIGHT_CARGO_TARGET_DIR}" "${SOLDR_EXECUTABLE}" cargo build
      --manifest-path "${CMAKE_SOURCE_DIR}/Cargo.toml" --package obs-preflight-ffi --lib --release --locked
    DEPENDS
      "${OBS_PREFLIGHT_MANIFEST}"
      "${CMAKE_SOURCE_DIR}/rust/obs-preflight/Cargo.toml"
      "${CMAKE_SOURCE_DIR}/Cargo.toml"
      "${CMAKE_SOURCE_DIR}/Cargo.lock"
      ${OBS_PREFLIGHT_RUST_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Building obs-preflight Rust C ABI"
    VERBATIM
  )
  add_custom_target(obs-preflight-rust DEPENDS "${OBS_PREFLIGHT_ARCHIVE}")
endif()
