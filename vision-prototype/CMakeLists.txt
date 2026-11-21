cmake_minimum_required(VERSION 3.21)
project(BSPVisionPrototype VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
find_package(OpenCV 4.8 REQUIRED COMPONENTS core imgproc imgcodecs features2d)

qt_add_executable(bsp-vision src/main.cpp)
target_link_libraries(bsp-vision PRIVATE Qt6::Widgets ${OpenCV_LIBS})
target_include_directories(bsp-vision PRIVATE ${OpenCV_INCLUDE_DIRS})

if(MSVC)
    target_compile_options(bsp-vision PRIVATE /W4 /permissive-)
else()
    target_compile_options(bsp-vision PRIVATE -Wall -Wextra -Wpedantic)
endif()
