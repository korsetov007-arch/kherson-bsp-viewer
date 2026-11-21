cmake_minimum_required(VERSION 3.21)
project(BSPGeometryTools VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(CTest)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

qt_add_executable(bsp-geometry
    src/main.cpp
    src/bsp_file.cpp
    src/bsp_map_view.cpp
    src/bsp_3d_view.cpp
    src/ballistics_view.cpp
    src/live_overlay.cpp
    src/raw_mouse_tracker.cpp
    include/app_language.hpp
    include/bsp_file.hpp
    include/bsp_map_view.hpp
    include/bsp_3d_view.hpp
    include/ballistics_view.hpp
    include/live_overlay.hpp
    include/raw_mouse_tracker.hpp
)

target_include_directories(bsp-geometry PRIVATE include)
target_link_libraries(bsp-geometry PRIVATE Qt6::Widgets)

if(MSVC)
    target_compile_options(bsp-geometry PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(bsp-geometry PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(bsp-parser-tests
        tests/bsp_file_tests.cpp
        src/bsp_file.cpp
        include/bsp_file.hpp
    )
    target_include_directories(bsp-parser-tests PRIVATE include)
    target_link_libraries(bsp-parser-tests PRIVATE Qt6::Core)
    if(MSVC)
        target_compile_options(bsp-parser-tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(bsp-parser-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME bsp-parser-tests COMMAND bsp-parser-tests)
endif()
