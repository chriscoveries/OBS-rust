# The Cargo target triple for the Rust staticlib, so it matches the
# architecture CMake builds libobs for.

# obs_rust_cargo_target(<out> <is_macos> <osx_architectures>)
#
# Corrosion defaults to the host triple. On macOS a single
# CMAKE_OSX_ARCHITECTURES other than the host's (x86_64 cross-built on an
# arm64 runner, #628) needs the matching Apple triple. Empty means "keep
# Corrosion's default"; universal builds are not supported here.
function(obs_rust_cargo_target out is_macos osx_architectures)
  set(triple "")
  if(is_macos)
    if(osx_architectures STREQUAL "x86_64")
      set(triple x86_64-apple-darwin)
    elseif(osx_architectures STREQUAL "arm64")
      set(triple aarch64-apple-darwin)
    endif()
  endif()
  set(${out} "${triple}" PARENT_SCOPE)
endfunction()
