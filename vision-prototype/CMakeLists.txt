cmake_minimum_required(VERSION 3.21)
project(BSPGeometryTools VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(CTest)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network)

qt_add_executable(bsp-geometry
    src/main.cpp
    src/bsp_file.cpp
    src/bsp_map_view.cpp
    src/bsp_3d_view.cpp
    src/ballistics_view.cpp
    src/live_overlay.cpp
    src/cl_showpos_reader.cpp
    src/heightfield.cpp
    src/triangle_bvh.cpp
    include/app_language.hpp
    include/bsp_file.hpp
    include/bsp_map_view.hpp
    include/bsp_3d_view.hpp
    include/ballistics_view.hpp
    include/live_overlay.hpp
    include/cl_showpos_reader.hpp
    include/heightfield.hpp
    include/triangle_bvh.hpp
)

target_include_directories(bsp-geometry PRIVATE include)
target_link_libraries(bsp-geometry PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
if(WIN32)
    target_link_libraries(bsp-geometry PRIVATE user32 d3d11 dxgi)
endif()

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

    add_executable(cl-showpos-tests
        tests/cl_showpos_reader_tests.cpp
        src/cl_showpos_reader.cpp
        include/cl_showpos_reader.hpp
    )
    target_include_directories(cl-showpos-tests PRIVATE include)
    target_link_libraries(cl-showpos-tests PRIVATE Qt6::Core Qt6::Gui)
    if(WIN32)
        target_link_libraries(cl-showpos-tests PRIVATE d3d11 dxgi)
    endif()
    if(MSVC)
        target_compile_options(cl-showpos-tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(cl-showpos-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME cl-showpos-tests COMMAND cl-showpos-tests)

    add_executable(heightfield-tests
        tests/heightfield_tests.cpp
        src/heightfield.cpp
        include/heightfield.hpp
        include/bsp_file.hpp
    )
    target_include_directories(heightfield-tests PRIVATE include)
    target_link_libraries(heightfield-tests PRIVATE Qt6::Core Qt6::Gui)
    if(MSVC)
        target_compile_options(heightfield-tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(heightfield-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME heightfield-tests COMMAND heightfield-tests)

    add_executable(triangle-bvh-tests
        tests/triangle_bvh_tests.cpp
        src/triangle_bvh.cpp
        include/triangle_bvh.hpp
        include/bsp_file.hpp
    )
    target_include_directories(triangle-bvh-tests PRIVATE include)
    target_link_libraries(triangle-bvh-tests PRIVATE Qt6::Core Qt6::Gui)
    if(MSVC)
        target_compile_options(triangle-bvh-tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(triangle-bvh-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME triangle-bvh-tests COMMAND triangle-bvh-tests)
endif()
