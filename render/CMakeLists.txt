# Render process: owns every audience output. Built as a library (for tests)
# plus the lumen-render executable. It depends on Qt Quick (drawn through Qt
# RHI) and Qt Network (local socket), never on the operator UI or Qt SQL.

qt_add_library(lumen_render_lib STATIC
    screenchoice.h
    screenchoice.cpp
    outputwindow.h
    outputwindow.cpp
    renderserver.h
    renderserver.cpp
)
qt_add_qml_module(lumen_render_lib
    URI Lumen.Render
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Lumen/Render
    QML_FILES
        Output.qml
)
target_include_directories(lumen_render_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lumen_render_lib
    PUBLIC
        Lumen::Core
        lumen_render_libplugin
        Qt6::Gui
        Qt6::Quick
        Qt6::Network
    PRIVATE lumen_warnings)

qt_add_executable(lumen-render main.cpp)
target_link_libraries(lumen-render PRIVATE lumen_render_lib lumen_warnings)

include(GNUInstallDirs)
install(TARGETS lumen-render RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
