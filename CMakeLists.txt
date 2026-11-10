# Optional CMake build (MSVC is the best supported path; build.bat is the
# primary entry point).  Output: build/xinput1_4.dll
cmake_minimum_required(VERSION 3.16)
project(SprintSplit LANGUAGES CXX)

add_library(xinput1_4 SHARED
    src/xinput_proxy.cpp
    src/config.cpp
    src/proxy.def
)

set_target_properties(xinput1_4 PROPERTIES
    OUTPUT_NAME "xinput1_4"
    PREFIX ""
    SUFFIX ".dll"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(xinput1_4 PRIVATE src)

if (MSVC)
    target_compile_options(xinput1_4 PRIVATE /MT /W3 /D_CRT_SECURE_NO_WARNINGS)
    target_link_options(xinput1_4 PRIVATE
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/proxy.def")
else()
    target_link_options(xinput1_4 PRIVATE -static)
endif()
