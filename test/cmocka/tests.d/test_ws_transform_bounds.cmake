# obs-websocket SetSceneItemTransform must accept bounds of 0 while the bounds
# type is OBS_BOUNDS_NONE (obs-rust/obs-studio#540). Compiles the plugin's
# scene item requests from the submodule against libobs; needs no video and no
# display. Skipped when the submodule is not checked out or nlohmann_json is
# missing.
set(_ws "${CMAKE_SOURCE_DIR}/plugins/obs-websocket")
if(OS_LINUX AND EXISTS "${_ws}/src/requesthandler/RequestHandler_SceneItems.cpp")
  find_package(nlohmann_json 3.11 QUIET)
  if(nlohmann_json_FOUND)
    # plugin-macros.generated.h, as the plugin's own CMakeLists makes it.
    set(obs-websocket_VERSION "0.0.0")
    set(OBS_WEBSOCKET_RPC_VERSION 1)
    configure_file(
      "${_ws}/src/plugin-macros.h.in"
      "${CMAKE_CURRENT_BINARY_DIR}/ws_transform_bounds/plugin-macros.generated.h"
    )
    add_executable(
      test_ws_transform_bounds
      test_ws_transform_bounds.cpp
      "${_ws}/src/requesthandler/RequestHandler_SceneItems.cpp"
      "${_ws}/src/requesthandler/rpc/Request.cpp"
      "${_ws}/src/requesthandler/rpc/RequestResult.cpp"
      "${_ws}/src/utils/Json.cpp"
      "${_ws}/src/utils/Obs_ActionHelper.cpp"
      "${_ws}/src/utils/Obs_ArrayHelper.cpp"
      "${_ws}/src/utils/Obs_NumberHelper.cpp"
      "${_ws}/src/utils/Obs_ObjectHelper.cpp"
      "${_ws}/src/utils/Obs_SearchHelper.cpp"
    )
    target_include_directories(
      test_ws_transform_bounds
      PRIVATE
        ${CMOCKA_INCLUDE_DIR}
        "${_ws}/src"
        "${CMAKE_CURRENT_BINARY_DIR}/ws_transform_bounds"
        "${CMAKE_SOURCE_DIR}/frontend/api"
    )
    # The plugin still calls APIs libobs has deprecated.
    target_compile_options(test_ws_transform_bounds PRIVATE -Wno-error=deprecated-declarations)
    target_compile_features(test_ws_transform_bounds PRIVATE cxx_std_17)
    target_link_libraries(test_ws_transform_bounds PRIVATE OBS::libobs nlohmann_json::nlohmann_json ${CMOCKA_LIBRARIES})
    add_dependencies(cmocka-tests test_ws_transform_bounds)
    # obs_startup() needs a display on Linux; the cases skip without one.
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
      add_test(
        NAME test_ws_transform_bounds
        COMMAND ${XVFB_RUN} -a ${CMAKE_CURRENT_BINARY_DIR}/test_ws_transform_bounds
      )
    else()
      add_test(test_ws_transform_bounds ${CMAKE_CURRENT_BINARY_DIR}/test_ws_transform_bounds)
    endif()
  endif()
endif()
