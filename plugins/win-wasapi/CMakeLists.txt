cmake_minimum_required(VERSION 3.28...3.30)

add_library(win-wasapi MODULE)
add_library(OBS::wasapi ALIAS win-wasapi)

target_sources(win-wasapi PRIVATE wasapi-notify.hpp build/win-wasapi.windows+.hpp.cpp enum-wasapi.hpp)

configure_file(cmake/windows/obs-module.rc.in win-wasapi.rc)
target_sources(win-wasapi PRIVATE win-wasapi.rc)

target_link_libraries(win-wasapi PRIVATE OBS::libobs Avrt)

set_target_properties_obs(win-wasapi PROPERTIES FOLDER plugins PREFIX "")
