cmake_minimum_required(VERSION 3.28...3.30)

add_library(libobs-d3d11 MODULE)
add_library(OBS::libobs-d3d11 ALIAS libobs-d3d11)

# Unity group (BUILD.md): build/d3d11.core+.hpp.cpp #includes the
# implementation files (d3d11-foo.cpp.hpp) behind one precompiled header.
target_sources(libobs-d3d11 PRIVATE build/d3d11.core+.hpp.cpp d3d11-shaderprocessor.hpp d3d11-subsystem.hpp)

configure_file(cmake/windows/obs-module.rc.in libobs-d3d11.rc)
target_sources(libobs-d3d11 PRIVATE libobs-d3d11.rc)

target_compile_definitions(
  libobs-d3d11
  PRIVATE
    $<$<BOOL:${GPU_PRIORITY_VAL}>:USE_GPU_PRIORITY>
    "$<IF:$<BOOL:${GPU_PRIORITY_VAL}>,GPU_PRIORITY_VAL=${GPU_PRIORITY_VAL},GPU_PRIORITY_VAL=0>"
)

target_link_libraries(
  libobs-d3d11
  PRIVATE OBS::libobs d3d9 d3d11 d3dcompiler dxgi shcore
)

target_enable_feature(libobs "Direct3D 11 renderer")

set_target_properties_obs(
  libobs-d3d11
  PROPERTIES FOLDER core
             VERSION 0
             SOVERSION ${OBS_VERSION_MAJOR}
             COMPILE_WARNING_AS_ERROR FALSE
)
