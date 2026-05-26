vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO libsdl-org/SDL
    REF d4236dcd852d001df4cf6175067649572a3b461d
    SHA512 2f9beb03f81be6b6b89f9abde02e965684b584e1f6868e12369fffb5df3fcc32ac00a595384da6e8def4929d13dc8e52a45fec8240a47709998c82d1b39d524c
    HEAD_REF main
    PATCHES
        fix-freebsd.patch
        triton-imu.patch
)

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "static" SDL_STATIC)
string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" SDL_SHARED)
string(COMPARE EQUAL "${VCPKG_CRT_LINKAGE}" "static" FORCE_STATIC_VCRT)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        alsa     SDL_ALSA
        dbus     SDL_DBUS
        ibus     SDL_IBUS
        vulkan   SDL_VULKAN
        wayland  SDL_WAYLAND
        x11      SDL_X11
        libusb   SDL_HIDAPI_LIBUSB
)

if (VCPKG_TARGET_IS_EMSCRIPTEN)
    vcpkg_check_features(OUT_FEATURE_OPTIONS EMSCRIPTEN_FEATURE_OPTIONS
        FEATURES
            emscripten-pthreads     SDL_PTHREADS
    )
    vcpkg_list(APPEND FEATURE_OPTIONS "${EMSCRIPTEN_FEATURE_OPTIONS}")
endif()

if ("x11" IN_LIST FEATURES)
    message(WARNING "You will need to install Xorg dependencies to use feature x11:\nsudo apt install libx11-dev libxft-dev libxext-dev\n")
endif()
if ("wayland" IN_LIST FEATURES)
    message(WARNING "You will need to install Wayland dependencies to use feature wayland:\nsudo apt install libwayland-dev libxkbcommon-dev libegl1-mesa-dev\n")
endif()
if ("ibus" IN_LIST FEATURES)
    message(WARNING "You will need to install ibus dependencies to use feature ibus:\nsudo apt install libibus-1.0-dev\n")
endif()

if ("libusb" IN_LIST FEATURES)
    if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
        vcpkg_list(APPEND FEATURE_OPTIONS "-DSDL_HIDAPI_LIBUSB_SHARED=ON")
    else()
        if (WIN32)
            vcpkg_list(APPEND FEATURE_OPTIONS "-DSDL_HIDAPI_LIBUSB_SHARED=OFF")
        else()
            vcpkg_list(APPEND FEATURE_OPTIONS "-DCMAKE_DISABLE_FIND_PACKAGE_LibUSB=1")
        endif()
    endif()
endif()

# option for not need to show windows
list(APPEND FEATURE_OPTIONS -DSDL_UNIX_CONSOLE_BUILD=ON)
if (VCPKG_TARGET_IS_LINUX AND NOT "x11" IN_LIST FEATURES AND NOT "wayland" IN_LIST FEATURES)
    message(WARNING "The selected features don't allow sdl3 to create windows, which is usually unintentional. You can get windowing support by installing the x11 and/or wayland features.")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DSDL_STATIC=${SDL_STATIC}
        -DSDL_SHARED=${SDL_SHARED}
        -DSDL_FORCE_STATIC_VCRT=${FORCE_STATIC_VCRT}
        -DSDL_LIBC=ON
        -DSDL_TEST_LIBRARY=OFF
        -DSDL_TESTS=OFF
        -DSDL_X11_XSCRNSAVER=OFF
        -DSDL_INSTALL_CMAKEDIR_ROOT=share/${PORT}
        # Specifying the revision skips the need to use git to determine a version
        -DSDL_REVISION=vcpkg
        -DSDL_AUDIO=OFF
        -DSDL_CAMERA=OFF
        -DSDL_DIALOG=OFF
        -DSDL_GPU=OFF
        -DSDL_HAPTIC=OFF
        -DSDL_OFFSCREEN=OFF
        -DSDL_POWER=OFF
        -DSDL_RENDER=OFF
        -DSDL_SENSOR=OFF
        -DSDL_TRAY=OFF
        -DSDL_VIDEO=OFF
        -DSDL_VIRTUAL_JOYSTICK=OFF
    MAYBE_UNUSED_VARIABLES
        SDL_FORCE_STATIC_VCRT
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt"
    COMMENT "Some configurations may use code licensed under the MIT and Apache-2.0 licenses."
)
