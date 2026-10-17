cmake_minimum_required(VERSION 3.28...3.30)

find_package(FFnvcodec 12 REQUIRED)
# std::thread and dlopen: before glibc 2.34 they live in libpthread and libdl, not libc
find_package(Threads REQUIRED)

add_executable(obs-nvenc-test)

target_sources(obs-nvenc-test PRIVATE obs-nvenc-test.cpp)
target_link_libraries(obs-nvenc-test FFnvcodec::FFnvcodec Threads::Threads ${CMAKE_DL_LIBS})

set_target_properties_obs(obs-nvenc-test PROPERTIES FOLDER plugins/obs-nvenc)
