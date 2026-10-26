# USB diagnostic probe (see main.cpp). Configure with PacBrew's toolchain file, as
# scripts/build-usb-probe.sh does.
cmake_minimum_required(VERSION 3.13)
project(dolphinps4_usb_probe C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

include(${CMAKE_CURRENT_LIST_DIR}/../toolchain/ps4-love-style.cmake)

add_executable(usb_probe main.cpp ${PS4_RUNTIME_SOURCES})
# orbis/Usbd.h includes <libusb.h>, which the toolchain keeps in include/orbis/_types.
# It must come after the system headers (-idirafter): _types also has a pthread.h that would
# shadow the real one.
set_source_files_properties(main.cpp PROPERTIES COMPILE_OPTIONS "-idirafter;${OPENORBIS}/include/orbis/_types")
target_compile_options(usb_probe PRIVATE -Wall -Wno-format)
# SceUsbd is the libSceUsbd.sprx stub from orbis-lib-gen; the sprx itself is loaded at run
# time with sceSysmoduleLoadModule(ORBIS_SYSMODULE_USBD).
target_link_libraries(usb_probe PRIVATE SceSysmodule SceUsbd pthread)
ps4_add_eboot(usb_probe)
