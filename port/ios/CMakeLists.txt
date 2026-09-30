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
add_executable(HaloCE MACOSX_BUNDLE
    Assets.xcassets "${ROOT}/port/runtime/xiso.c"
    host/host_main.m host/host_matchmaking.m host/host_import.m host/host_orientation.m host/host_touch.m host/host_memory.c host/host_loader.c host/host_syscall.c host/host_gl.c host/guest_call.S
    host/host_thread.c host/host_sdl.c
    "${ROOT}/port/linux/src/posix_files.c" host/posix_net.c
    "${ROOT}/build/ios/host/bridges.c" "${ROOT}/build/ios/embedded/guest_image.S")
target_include_directories(HaloCE PRIVATE host "${ROOT}/port/runtime" "${ROOT}/port/runtime/include"
    "${ROOT}/port/runtime/guest/runtime" "${ROOT}/port/linux/src" "${ROOT}/build/ios/host"
    "${ROOT}/build/ios/gl_include" "${ROOT}/build/ios/embedded")
target_compile_definitions(HaloCE PRIVATE HALO_IOS=1 st_mtim=st_mtimespec st_atim=st_atimespec st_ctim=st_ctimespec)
target_compile_options(HaloCE PRIVATE -Wno-deprecated-declarations)
target_compile_options(HaloCE PRIVATE "$<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>")
target_link_libraries(HaloCE PRIVATE SDL3::SDL3-static "-framework Foundation" "-framework UIKit" "-framework UniformTypeIdentifiers" "-framework OpenGLES")
set_source_files_properties(Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
file(GLOB HALO_LICENSES "${ROOT}/build/ios/licenses/*.txt")
target_sources(HaloCE PRIVATE ${HALO_LICENSES})
set_source_files_properties(${HALO_LICENSES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Licenses)
set_target_properties(HaloCE PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${HALO_BUNDLE_IDENTIFIER}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic)
if(HALO_DEVELOPMENT_TEAM)
    set_target_properties(HaloCE PROPERTIES XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${HALO_DEVELOPMENT_TEAM}")
endif()
