cmake_minimum_required(VERSION 3.24)

project(LumenPresenter
    VERSION 0.1.0
    DESCRIPTION "Free, offline, cross-platform worship presentation software"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LUMEN_BUILD_TESTS "Build the unit and integration tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick)
qt_standard_project_setup(REQUIRES 6.5)

# Shared warning settings for every Lumen target.
add_library(lumen_warnings INTERFACE)
if(MSVC)
    target_compile_options(lumen_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(lumen_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(render)

if(LUMEN_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
