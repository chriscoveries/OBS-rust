if(NOT TARGET OBS::idian)
  add_subdirectory("${CMAKE_SOURCE_DIR}/shared/qt/idian" "${CMAKE_BINARY_DIR}/shared/qt/idian")
endif()

target_link_libraries(obs-studio PRIVATE OBS::idian)

if(NOT TARGET OBS::properties-view)
  add_subdirectory("${CMAKE_SOURCE_DIR}/shared/properties-view" "${CMAKE_BINARY_DIR}/shared/properties-view")
endif()

target_link_libraries(obs-studio PRIVATE OBS::properties-view)

target_sources(
  obs-studio
  PRIVATE
    dialogs/LogUploadDialog.cpp
    dialogs/LogUploadDialog.hpp
    dialogs/NameDialog.cpp
    dialogs/NameDialog.hpp
    dialogs/OAuthLogin.cpp
    dialogs/OAuthLogin.hpp
    dialogs/OBSAbout.cpp
    dialogs/OBSAbout.hpp
    dialogs/OBSBasicAdvAudio.cpp
    dialogs/OBSBasicAdvAudio.hpp
    dialogs/OBSBasicFilters.cpp
    dialogs/OBSBasicFilters.hpp
    dialogs/OBSBasicInteraction.cpp
    dialogs/OBSBasicInteraction.hpp
    dialogs/OBSBasicProperties.cpp
    dialogs/OBSBasicProperties.hpp
    dialogs/OBSBasicSourceSelect.cpp
    dialogs/OBSBasicSourceSelect.hpp
    dialogs/OBSBasicTransform.cpp
    dialogs/OBSBasicTransform.hpp
    dialogs/OBSBasicVCamConfig.cpp
    dialogs/OBSBasicVCamConfig.hpp
    dialogs/OBSLogViewer.cpp
    dialogs/OBSLogViewer.hpp
    dialogs/OBSMissingFiles.cpp
    dialogs/OBSMissingFiles.hpp
    dialogs/OBSRemux.cpp
    dialogs/OBSRemux.hpp
    dialogs/OBSWhatsNew.cpp
    dialogs/OBSWhatsNew.hpp
)

if(TARGET obs-preflight-rust)
  target_sources(
    obs-studio
    PRIVATE
      preflight/preflight-bench-data.cpp
      preflight/preflight-bench-data-obs.cpp
      preflight/preflight-bench-data.hpp
      preflight/preflight-apply-plan.cpp
      preflight/preflight-apply-plan.hpp
      preflight/obs-preflight-collector.c
      preflight/preflight-bench-window.cpp
      preflight/preflight-bench-window.hpp
      preflight/preflight-cloudflare-speed-test.cpp
      preflight/preflight-cloudflare-speed-test.hpp
      preflight/preflight-loupe-capture.cpp
      preflight/preflight-loupe-capture.hpp
      preflight/preflight-loupe-core.cpp
      preflight/preflight-loupe-core.hpp
      preflight/preflight-test-mode.cpp
      preflight/preflight-test-mode.hpp
  )
endif()
