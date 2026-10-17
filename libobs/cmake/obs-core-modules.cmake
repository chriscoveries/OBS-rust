add_library(libobs-core-modules OBJECT)
add_library(OBS::libobs-core-modules ALIAS libobs-core-modules)

set(core_module_list_source "${CMAKE_CURRENT_BINARY_DIR}/obs-core-modules.c")
set_source_files_properties("${core_module_list_source}" PROPERTIES GENERATED TRUE)
target_sources(libobs-core-modules PRIVATE "${core_module_list_source}" PUBLIC obs-core-modules.h)

target_include_directories(libobs-core-modules PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")

set_target_properties(libobs-core-modules PROPERTIES FOLDER core POSITION_INDEPENDENT_CODE TRUE)
