# Build the iPhone/iPad host and SDL around the statically compiled guest image.
cmake_minimum_required(VERSION 3.24)
project(HaloCE LANGUAGES C CXX OBJC ASM)
set(HALO_BUNDLE_IDENTIFIER "org.haloce.ios" CACHE STRING "App bundle identifier")
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/build/third_party/SDL3" sdl3 EXCLUDE_FROM_ALL)
if(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
    # SDL's CMake forces SDL_OPENGLES off for tvOS, but its UIKit GL ES code is
    # shared with iOS and tvOS still ships OpenGLES.framework (SDL's own Xcode
    # config, SDL_build_config_ios.h, enables it). The sources are always
    # compiled; only these macros gate them.
    target_compile_definitions(SDL3-static PRIVATE SDL_VIDEO_OPENGL_ES=1 SDL_VIDEO_OPENGL_ES2=1)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
    # Controllers only, no Files app: maps are bundled (HALO_MAPS), see host/host_tv.m.
    set(HALO_TVOS ON)
    set(HALO_MAPS "" CACHE PATH "Folder of .map files extracted from the user's XISO")
    set(HALO_PLATFORM_SOURCES host/host_tv.m)
    set(HALO_INFO_PLIST Info-tvOS.plist)
    set(HALO_DEVICE_FAMILY 3)
else()
    set(HALO_PLATFORM_SOURCES Assets.xcassets host/host_import.m host/host_orientation.m host/host_touch.m)
    set(HALO_INFO_PLIST Info.plist)
    set(HALO_DEVICE_FAMILY "1,2")
endif()
add_executable(HaloCE MACOSX_BUNDLE
    ${HALO_PLATFORM_SOURCES} "${ROOT}/port/runtime/xiso.c"
    host/host_main.m host/host_memory.c host/host_loader.c host/host_syscall.c host/host_gl.c host/guest_call.S
    host/host_thread.c host/host_sdl.c
    "${ROOT}/port/linux/src/posix_files.c" host/posix_net.c
    "${ROOT}/build/ios/host/bridges.c" "${ROOT}/build/ios/embedded/guest_image.S")
target_include_directories(HaloCE PRIVATE host "${ROOT}/port/runtime" "${ROOT}/port/runtime/include"
    "${ROOT}/port/runtime/guest/runtime" "${ROOT}/port/linux/src" "${ROOT}/build/ios/host"
    "${ROOT}/build/ios/gl_include" "${ROOT}/build/ios/embedded")
target_compile_definitions(HaloCE PRIVATE HALO_IOS=1 st_mtim=st_mtimespec st_atim=st_atimespec st_ctim=st_ctimespec)
target_compile_options(HaloCE PRIVATE -Wno-deprecated-declarations)
target_compile_options(HaloCE PRIVATE "$<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>")
target_link_libraries(HaloCE PRIVATE SDL3::SDL3-static "-framework Foundation" "-framework UIKit" "-framework OpenGLES")
if(NOT HALO_TVOS)
    target_link_libraries(HaloCE PRIVATE "-framework UniformTypeIdentifiers")
    set_source_files_properties(Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(HaloCE PROPERTIES XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
endif()
file(GLOB HALO_LICENSES "${ROOT}/build/ios/licenses/*.txt")
target_sources(HaloCE PRIVATE ${HALO_LICENSES})
set_source_files_properties(${HALO_LICENSES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Licenses)
set_target_properties(HaloCE PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/${HALO_INFO_PLIST}"
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${HALO_BUNDLE_IDENTIFIER}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "${HALO_DEVICE_FAMILY}"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic)
if(HALO_DEVELOPMENT_TEAM)
    set_target_properties(HaloCE PROPERTIES XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${HALO_DEVELOPMENT_TEAM}")
endif()
if(HALO_TVOS AND HALO_MAPS)
    # APFS clone (cp -c): copying the ~1.7 GB of maps costs no time or space.
    # Xcode's own variables name the bundle; the generator path contains an unexpanded ${EFFECTIVE_PLATFORM_NAME}.
    add_custom_command(TARGET HaloCE POST_BUILD
        COMMAND sh -c "rm -rf \"$TARGET_BUILD_DIR/$WRAPPER_NAME/maps\" && cp -Rc \"$0\" \"$TARGET_BUILD_DIR/$WRAPPER_NAME/maps\"" "${HALO_MAPS}"
        VERBATIM)
endif()
