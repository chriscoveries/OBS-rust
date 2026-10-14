# Tools -> Output Timer resets its button after the timer stops an output
# while the dialog is closed. Builds the real dialog
# (plugins/frontend-tools/output-timer.cpp and its .ui) against stubs of the
# frontend API and runs it on Qt's offscreen platform, so it needs Qt Widgets
# but no display and no frontend build.
if(OS_LINUX)
  find_package(Qt6 QUIET COMPONENTS Widgets)
  if(TARGET Qt6::Widgets)
    set(_output_timer_dir "${CMAKE_SOURCE_DIR}/plugins/frontend-tools")
    add_executable(
      test_output_timer
      test_output_timer.cpp
      "${_output_timer_dir}/output-timer.cpp"
      "${_output_timer_dir}/output-timer.hpp"
      "${_output_timer_dir}/forms/output-timer.ui"
    )
    set_target_properties(
      test_output_timer
      PROPERTIES AUTOMOC ON AUTOUIC ON AUTOUIC_SEARCH_PATHS "${_output_timer_dir}/forms"
    )
    target_include_directories(
      test_output_timer
      PRIVATE ${CMOCKA_INCLUDE_DIR} "${_output_timer_dir}" "${CMAKE_SOURCE_DIR}/frontend/api"
    )
    target_link_libraries(test_output_timer PRIVATE OBS::libobs Qt6::Widgets ${CMOCKA_LIBRARIES})
    add_dependencies(cmocka-tests test_output_timer)
    add_test(test_output_timer ${CMAKE_CURRENT_BINARY_DIR}/test_output_timer)
    unset(_output_timer_dir)
  endif()
endif()
