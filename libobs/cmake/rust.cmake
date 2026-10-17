# Links the Rust ports (rust/libobs-rust) into libobs-with-rust, libobs's
# never-installed twin, in place of the C sources they replace (#1065). See
# docs/rust-port/testing-policy.md.

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

# Whole-archive so every exported C symbol ends up in libobs-with-rust, including ones
# libobs itself never calls (e.g. bitstream_reader_r16, used by plugins).
target_link_libraries(libobs-with-rust PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,libobs_rust-static>")

# Keep libobs-with-rust exporting the same symbol set as libobs:
# - ELF: Rust staticlib internals have default visibility; hide them with a
#   version script.
# - Windows: Rust no_mangle symbols in a staticlib are not dllexport, so export
#   the listed shims explicitly (otherwise its obs.dll and import lib miss them).
# - macOS: hide Rust internals with ld64 -unexported_symbols_list.
if(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD)
  target_link_options(
    libobs-with-rust
    PRIVATE "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/rust-exports.map"
  )
  set_property(
    TARGET libobs-with-rust
    APPEND
    PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rust-exports.map"
  )
  # The obs-data glue calls json_delete from inside the staticlib.
  # jansson is first linked earlier, in libobs/CMakeLists.txt. On
  # Ubuntu, GNU ld --as-needed drops that first occurrence because
  # nothing references jansson yet, and every cmocka executable then
  # fails to link. Link it again so a later occurrence follows the
  # archive.
  if(OS_LINUX)
    target_link_libraries(libobs-with-rust PRIVATE jansson::jansson)
  endif()
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
      target_link_options(libobs-with-rust PRIVATE "LINKER:/EXPORT:${sym}")
    endforeach()
    set_property(TARGET libobs-with-rust APPEND PROPERTY LINK_DEPENDS "${fragment}")
  endforeach()
elseif(OS_MACOS)
  # Corrosion 0.5.2 injects the CLT SDK's libraries ahead of CMake's selected
  # sysroot. If CLT is newer than Xcode, its .tbd files may name architectures
  # Xcode's linker cannot read (#777). Let the linker use the selected SDK;
  # preserve any other native library search paths on the imported target.
  get_target_property(_obs_rust_link_dirs libobs_rust-static INTERFACE_LINK_DIRECTORIES)
  if(_obs_rust_link_dirs)
    list(REMOVE_ITEM _obs_rust_link_dirs "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib")
    set_property(TARGET libobs_rust-static PROPERTY INTERFACE_LINK_DIRECTORIES "${_obs_rust_link_dirs}")
  endif()

  target_link_options(
    libobs-with-rust
    PRIVATE "LINKER:-unexported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/cmake/rust-unexports-macos.txt"
  )
  set_property(
    TARGET libobs-with-rust
    APPEND
    PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rust-unexports-macos.txt"
  )
endif()

# Its own directory (file name and SONAME are copied from libobs at the end of
# libobs/CMakeLists.txt), so Tier 2 can load it in libobs's place (LD_LIBRARY_PATH, DYLD_FRAMEWORK_PATH, PATH). Never
# installed or copied to the rundir: nothing links against it.
set_target_properties(
  libobs-with-rust
  PROPERTIES
    FOLDER core
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libobs-with-rust"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libobs-with-rust"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libobs-with-rust"
)
