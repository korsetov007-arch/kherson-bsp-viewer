cmake_minimum_required(VERSION 3.24)
project(TFC VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
qt_add_executable(tfc WIN32 src/main.cpp src/file_bridge.cpp src/ballistics.cpp src/topdown_picker.cpp src/bsp_file.cpp src/bsp_3d_view.cpp include/file_bridge.hpp include/ballistics.hpp include/topdown_picker.hpp include/bsp_file.hpp include/bsp_3d_view.hpp include/app_language.hpp resources.qrc)
target_include_directories(tfc PRIVATE include)
target_link_libraries(tfc PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
if(WIN32)
 find_program(TFC_WINDEPLOYQT_EXECUTABLE
  NAMES windeployqt.exe windeployqt
  HINTS "${Qt6_DIR}/../../../bin"
 )
 if(TFC_WINDEPLOYQT_EXECUTABLE)
  add_custom_command(TARGET tfc POST_BUILD
   COMMAND "${TFC_WINDEPLOYQT_EXECUTABLE}"
    "$<IF:$<CONFIG:Debug>,--debug,--release>"
    --no-translations "$<TARGET_FILE:tfc>"
   COMMENT "Deploying Qt runtime libraries"
   VERBATIM
  )
 else()
  message(WARNING "windeployqt was not found; the Qt runtime will not be copied next to tfc.exe.")
 endif()
endif()
if(WIN32)
 target_link_libraries(tfc PRIVATE user32 shell32)
endif()
if(MSVC)
 target_compile_options(tfc PRIVATE /W4 /permissive- /utf-8)
else()
 target_compile_options(tfc PRIVATE -Wall -Wextra -Wpedantic)
endif()
include(CTest)
if(BUILD_TESTING)
 add_executable(tfc-ballistics-tests tests/ballistics_tests.cpp src/ballistics.cpp)
 target_include_directories(tfc-ballistics-tests PRIVATE include)
 add_test(NAME tfc-ballistics-tests COMMAND tfc-ballistics-tests)
endif()
