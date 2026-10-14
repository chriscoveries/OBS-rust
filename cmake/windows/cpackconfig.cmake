# OBS CMake Windows CPack configuration module

include_guard(GLOBAL)

include(cpackconfig_common)

# Add GPLv2 license file to CPack
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/frontend/data/license/gplv2.txt")
set(CPACK_PACKAGE_VERSION "${OBS_VERSION_CANONICAL}")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-windows-${CMAKE_VS_PLATFORM_NAME}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY FALSE)
set(CPACK_GENERATOR ZIP)
set(CPACK_THREADS 0)

# Ship the Visual C++ runtime (vcruntime140*.dll, msvcp140*.dll, ...) next to
# obs64.exe so the zip starts on machines without the redistributable (#626).
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${OBS_EXECUTABLE_DESTINATION}")
set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT Runtime)
include(InstallRequiredSystemLibraries)

include(CPack)
