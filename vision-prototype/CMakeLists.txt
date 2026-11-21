cmake_minimum_required(VERSION 3.21)
project(BSPVisionPrototype VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(CTest)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)
find_package(OpenCV 4.8 REQUIRED COMPONENTS core imgproc imgcodecs features2d calib3d)

qt_add_executable(bsp-vision
    src/main.cpp
    src/bsp_file.cpp
    src/bsp_map_view.cpp
    include/bsp_file.hpp
    include/bsp_map_view.hpp
)

target_include_directories(bsp-vision PRIVATE include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(bsp-vision PRIVATE Qt6::Widgets ${OpenCV_LIBS})

if(MSVC)
    target_compile_options(bsp-vision PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(bsp-vision PRIVATE -Wall -Wextra -Wpedantic)
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
