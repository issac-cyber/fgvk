cmake_minimum_required(VERSION 3.28)
project(screen-fg CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(PIW REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)

find_library(VULKAN_LIB vulkan)
find_path(VULKAN_INCLUDE vulkan/vulkan.h)

add_executable(screen-fg
    src/main.cpp
    src/config.cpp
    src/capture.cpp
    src/dedup.cpp
    src/present.cpp
    src/reexec.cpp
    src/synthetic.cpp
)
target_include_directories(screen-fg PRIVATE
    ${VULKAN_INCLUDE}
    ${PIW_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(screen-fg PRIVATE
    PkgConfig::SDL3
    PkgConfig::PIW
    PkgConfig::GIO
    ${VULKAN_LIB}
)

enable_testing()
# 純模組測試：不 link PW/SDL/Vulkan（frame.hpp / reexec / config / protocol 皆純）
add_executable(screen-fg-tests
    tests/test_main.cpp
    tests/test_config.cpp
    tests/test_config_resolve.cpp
    tests/test_dedup.cpp
    tests/test_clamp.cpp
    tests/test_reexec.cpp
    tests/test_frame_source.cpp
    src/config.cpp
    src/dedup.cpp
    src/reexec.cpp
    src/synthetic.cpp
)
target_include_directories(screen-fg-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME unit COMMAND screen-fg-tests)
