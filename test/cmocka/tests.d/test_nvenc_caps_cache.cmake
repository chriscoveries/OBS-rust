# obs-nvenc's saved obs-nvenc-test answer: the cache file and the key read
# from a fake sysfs (obs-rust/obs-studio#1170). No GPU, CUDA or NVENC is needed.
if(OS_LINUX)
  add_executable(
    test_nvenc_caps_cache
    test_nvenc_caps_cache.c
    "${CMAKE_SOURCE_DIR}/plugins/obs-nvenc/nvenc-caps-cache.c.h"
    "${CMAKE_SOURCE_DIR}/plugins/obs-nvenc/nvenc-caps-cache-linux.c.h"
  )
  target_include_directories(
    test_nvenc_caps_cache
    PRIVATE ${CMOCKA_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/plugins/obs-nvenc"
  )
  target_link_libraries(test_nvenc_caps_cache PRIVATE OBS::libobs ${CMOCKA_LIBRARIES})
  add_dependencies(cmocka-tests test_nvenc_caps_cache)
  add_test(test_nvenc_caps_cache ${CMAKE_CURRENT_BINARY_DIR}/test_nvenc_caps_cache)
endif()
