# Links the Rust ports (rust/libobs-rust) into libobs in place of the C
# sources they replace. See docs/rust-port/testing-policy.md.

include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/rust-target.cmake")

# Build the staticlib for the architecture libobs targets, not the host's.
if(NOT Rust_CARGO_TARGET)
  obs_rust_cargo_target(_obs_rust_target "${OS_MACOS}" "${CMAKE_OSX_ARCHITECTURES}")
  if(_obs_rust_target)
    set(Rust_CARGO_TARGET "${_obs_rust_target}" CACHE STRING "Rust target triple for libobs-rust")
  endif()
endif()

FetchContent_Declare(Corrosion GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git GIT_TAG v0.5.2)
FetchContent_MakeAvailable(Corrosion)

# Pin the release profile so the staticlib always builds with
# panic=abort (set only for [profile.release] in Cargo.toml): without
# PROFILE, Corrosion picks dev for Debug or unset CMAKE_BUILD_TYPE, and a
# panic in a no_mangle shim would then unwind across the C ABI (UB).
corrosion_import_crate(
  MANIFEST_PATH "${CMAKE_SOURCE_DIR}/Cargo.toml"
  CRATES libobs-rust
  CRATE_TYPES staticlib
  PROFILE release
)

# Whole-archive so every exported C symbol ends up in libobs, including ones
# libobs itself never calls (e.g. bitstream_reader_r16, used by plugins).
target_link_libraries(libobs PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,libobs_rust-static>")

# Keep libobs exporting the same symbol set as the C build:
# - ELF: Rust staticlib internals have default visibility; hide them with a
#   version script.
# - Windows: Rust no_mangle symbols in a staticlib are not dllexport, so export
#   the listed shims explicitly (otherwise obs.dll and its import lib miss them).
# - macOS: hide Rust internals with ld64 -unexported_symbols_list.
if(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD)
  target_link_options(libobs PRIVATE "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/rust-exports.map")
  set_property(TARGET libobs APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rust-exports.map")
elseif(OS_WINDOWS)
  # One fragment per FFI module (cmake/rust-exports.d/README.md).
  file(GLOB _obs_rust_export_fragments CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rust-exports.d/*.txt")
  list(SORT _obs_rust_export_fragments)
  foreach(fragment IN LISTS _obs_rust_export_fragments)
    file(STRINGS "${fragment}" _obs_rust_exports REGEX "^[A-Za-z_]")
    foreach(sym IN LISTS _obs_rust_exports)
      # "name unix" shims are not compiled on Windows; the C file exports them.
      if(sym MATCHES " unix$")
        continue()
      endif()
      target_link_options(libobs PRIVATE "LINKER:/EXPORT:${sym}")
    endforeach()
    set_property(TARGET libobs APPEND PROPERTY LINK_DEPENDS "${fragment}")
  endforeach()
elseif(OS_MACOS)
  target_link_options(
    libobs
    PRIVATE "LINKER:-unexported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/cmake/rust-unexports-macos.txt"
  )
  set_property(TARGET libobs APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rust-unexports-macos.txt")
endif()
