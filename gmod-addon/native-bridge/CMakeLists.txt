cmake_minimum_required(VERSION 3.20)
project(sbgl_bridge LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "SBGL Bridge currently targets Windows Garry's Mod clients.")
endif()

include(FetchContent)
FetchContent_Declare(
    gmod_module_base
    GIT_REPOSITORY https://github.com/Facepunch/gmod-module-base.git
    GIT_TAG development
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(gmod_module_base)
if(NOT gmod_module_base_POPULATED)
    FetchContent_Populate(gmod_module_base)
endif()
add_subdirectory(${gmod_module_base_SOURCE_DIR}/include ${gmod_module_base_BINARY_DIR}/include)

add_library(sbgl_bridge SHARED module.cpp)
target_compile_features(sbgl_bridge PRIVATE cxx_std_17)
target_link_libraries(sbgl_bridge PRIVATE gmod-module-base)
set_gmod_suffix_prefix(sbgl_bridge)
# This is a client module loaded by the local player's Lua realm, not a server module.
set_target_properties(sbgl_bridge PROPERTIES PREFIX "gmcl_")
