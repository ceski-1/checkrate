cmake_minimum_required(VERSION 3.16)
project("checkrate" VERSION 1.0.4 LANGUAGES C)
set(CMAKE_C_STANDARD 99)
add_executable(${PROJECT_NAME} main.c)
configure_file(res/config.h.in config.h @ONLY)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if(WIN32)
    set(PROJECT_COMPANY "ceski")
    set(PROJECT_COPYRIGHT "Copyright (C) 2026")
    set(PROJECT_FILE_VERSION "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
    configure_file(res/version.rc.in version.rc)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
endif()
find_package(SDL3 3.4.4 REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)

string(TIMESTAMP PROJECT_DATE "%Y-%m-%d" UTC)
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR ${PROJECT_COMPANY})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_RESOURCE_FILE_LICENSE ${PROJECT_SOURCE_DIR}/LICENSE)
if(WIN32)
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    set(CPACK_GENERATOR ZIP)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.3)
        set(CPACK_COMPRESSION_LEVEL 9)
    endif()
    set(CPACK_BIN_DIR ".")
    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION .)
    install(FILES LICENSE DESTINATION . RENAME LICENSE.txt)
else()
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/appimage_generator.cmake"
        CONTENT [[
            find_program(LINUXDEPLOY_EXECUTABLE
                NAMES linuxdeploy linuxdeploy-x86_64.AppImage
                PATHS ${CPACK_PACKAGE_DIRECTORY}/linuxdeploy)

            if(NOT LINUXDEPLOY_EXECUTABLE)
                set(LINUXDEPLOY_EXECUTABLE
                    ${CPACK_PACKAGE_DIRECTORY}/linuxdeploy/linuxdeploy)
                file(DOWNLOAD
                    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
                    ${LINUXDEPLOY_EXECUTABLE}
                    INACTIVITY_TIMEOUT 10)
                execute_process(COMMAND chmod +x ${LINUXDEPLOY_EXECUTABLE} COMMAND_ECHO STDOUT)
            endif()

            execute_process(COMMAND
                    ${CMAKE_COMMAND} -E env LDAI_OUTPUT=${CPACK_PACKAGE_FILE_NAME}.AppImage
                    LINUXDEPLOY_OUTPUT_VERSION=${CPACK_PACKAGE_VERSION} NO_STRIP=1
                    ${LINUXDEPLOY_EXECUTABLE} --appimage-extract-and-run
                    --appdir=${CPACK_TEMPORARY_DIRECTORY}
                    --executable=${CPACK_TEMPORARY_DIRECTORY}/${CPACK_PACKAGING_INSTALL_PREFIX}/${CPACK_BIN_DIR}/checkrate
                    --desktop-file=${CPACK_TEMPORARY_DIRECTORY}/${CPACK_PACKAGING_INSTALL_PREFIX}/share/applications/checkrate.desktop
                    --icon-file=${CPACK_TEMPORARY_DIRECTORY}/${CPACK_PACKAGING_INSTALL_PREFIX}/share/icons/hicolor/128x128/apps/checkrate.png
                    --output=appimage)
        ]]
    )
    set(CPACK_GENERATOR External)
    set(CPACK_EXTERNAL_ENABLE_STAGING ON)
    set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/appimage_generator.cmake")
    set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
    include(GNUInstallDirs)
    set(CPACK_BIN_DIR "${CMAKE_INSTALL_BINDIR}")
    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES LICENSE DESTINATION "share/doc/${PROJECT_NAME}")
    install(FILES res/checkrate.desktop DESTINATION share/applications)
    install(FILES res/checkrate.png DESTINATION share/icons/hicolor/128x128/apps)
endif()
include(CPack)
