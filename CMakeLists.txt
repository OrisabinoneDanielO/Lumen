cmake_minimum_required(VERSION 3.24)

project(LumenPresenter
    VERSION 0.2.1
    DESCRIPTION "Free, offline, cross-platform worship presentation software"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LUMEN_BUILD_TESTS "Build the unit and integration tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick Sql Network Multimedia Pdf Xml)
# Qt's zip reader (for .pptx files) is private API (see docs/decisions.md).
set(QT_NO_PRIVATE_MODULE_WARNING ON)
# Since Qt 6.9 private modules are packages of their own; before, the
# Qt6::GuiPrivate target came with Gui.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.9)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()
qt_standard_project_setup(REQUIRES 6.5)

# Every executable in one folder, so lumen finds lumen-render beside it.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Where an install puts things (packaging/README.md). Windows: everything in
# one folder, as windeployqt expects. macOS: everything inside the app
# bundle. Linux: the usual bin/ and share/ folders.
include(GNUInstallDirs)
set(LUMEN_BUNDLE_NAME "Lumen Presenter")
if(WIN32)
    set(LUMEN_INSTALL_BINDIR .)
    set(LUMEN_INSTALL_DATADIR .)
elseif(APPLE)
    set(LUMEN_INSTALL_BINDIR "${LUMEN_BUNDLE_NAME}.app/Contents/MacOS")
    set(LUMEN_INSTALL_DATADIR "${LUMEN_BUNDLE_NAME}.app/Contents/Resources")
else()
    set(LUMEN_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
    set(LUMEN_INSTALL_DATADIR ${CMAKE_INSTALL_DATADIR}/lumen)
endif()

# Shared warning settings for every Lumen target.
add_library(lumen_warnings INTERFACE)
if(MSVC)
    target_compile_options(lumen_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(lumen_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(thirdparty)
add_subdirectory(core)
add_subdirectory(tools)
add_subdirectory(app)
add_subdirectory(render)

add_subdirectory(bibles)
# Last, so its install steps (bundling Qt) run after everything is installed.
add_subdirectory(packaging)

if(LUMEN_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)
    add_subdirectory(tests)
endif()
