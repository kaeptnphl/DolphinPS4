# usbpad: USB game controllers for PS4 homebrew through sceUsbd (see include/usbpad.h).
# Library target "usbpad" (static) for use via add_subdirectory(); the demo app is built by
# scripts/build-usbpad-demo.sh. Configure with a PacBrew-based toolchain file (ps4-love-modern.cmake).
cmake_minimum_required(VERSION 3.13)
project(usbpad C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

add_library(usbpad STATIC src/usbpad.cpp src/drv_xinput.cpp)
target_include_directories(usbpad PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_options(usbpad PRIVATE -Wall)
# orbis/Usbd.h includes <libusb.h>, which the toolchain keeps in include/orbis/_types. _types
# also has a pthread.h, so it must come after the system headers (-idirafter).
set_source_files_properties(src/usbpad.cpp PROPERTIES
    COMPILE_OPTIONS "-idirafter;${OPENORBIS}/include/orbis/_types")
target_link_libraries(usbpad PUBLIC SceSysmodule SceUsbd pthread)

if (CMAKE_PROJECT_NAME STREQUAL "usbpad")
    include(${CMAKE_CURRENT_LIST_DIR}/../toolchain/ps4-love-style.cmake)
    add_executable(usbpad_demo demo/main.cpp ${PS4_RUNTIME_SOURCES})
    target_link_libraries(usbpad_demo PRIVATE usbpad)
    ps4_add_eboot(usbpad_demo)
endif ()
