cmake_minimum_required(VERSION 3.22)
project(screen-fg-gui CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
# 註：libadwaita 無 C++ (gtkmm) binding，本機只有 C 標頭；
# 故用純 gtkmm，GNOME 預設 GTK4 主題即 Adwaita，外觀相同。
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0)

add_executable(screen-fg-gui
    src/main.cpp
    src/process.cpp
)
target_include_directories(screen-fg-gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(screen-fg-gui
    PRIVATE
    PkgConfig::GTKMM
)

enable_testing()
# 純 ProtocolDecoder 測試：不需 gtkmm（只用 shared/protocol.hpp + ScreenFgStatus 純結構）
add_executable(screen-fg-gui-tests
    tests/test_protocol_decoder.cpp
)
target_include_directories(screen-fg-gui-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME unit COMMAND screen-fg-gui-tests)
