# Render process: owns every audience output. Built as a library (for tests)
# plus the lumen-render executable. It depends on Qt Quick (drawn through Qt
# RHI) and Qt Network (local socket), never on the operator UI or Qt SQL.

add_subdirectory(slides)

qt_add_library(lumen_render_lib STATIC
    screenchoice.h
    screenchoice.cpp
    processwatch.h
    processwatch.cpp
    keyfill.h
    keyfill.cpp
    ndisink.h
    ndisink.cpp
    cuerunner.h
    cuerunner.cpp
    outputwindow.h
    outputwindow.cpp
    renderserver.h
    renderserver.cpp
    renderclock.h
    scenemodel.h
    scenemodel.cpp
    videomonitor.h
    videomonitor.cpp
)
qt_add_qml_module(lumen_render_lib
    URI Lumen.Render
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Lumen/Render
    QML_FILES
        Output.qml
        Stage.qml
        LayerStack.qml
        StreamScene.qml
        IdentifyLabel.qml
)
# The stage display uses the operator UI's font (Inter, SIL OFL 1.1, see
# app/ui/fonts/OFL.txt), as in design/mockups/Stage.html.
qt_add_resources(lumen_render_lib "stage_fonts"
    PREFIX "/lumen/stage"
    BASE ${CMAKE_SOURCE_DIR}/app/ui
    FILES
        ${CMAKE_SOURCE_DIR}/app/ui/fonts/Inter-Regular.ttf
        ${CMAKE_SOURCE_DIR}/app/ui/fonts/Inter-Medium.ttf
        ${CMAKE_SOURCE_DIR}/app/ui/fonts/Inter-SemiBold.ttf
        ${CMAKE_SOURCE_DIR}/app/ui/fonts/Inter-Bold.ttf)
target_include_directories(lumen_render_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lumen_render_lib
    PUBLIC
        Lumen::Core
        Lumen::MtcInput
        lumen_render_libplugin
        lumen_slides
        lumen_slidesplugin
        Qt6::Gui
        Qt6::Quick
        Qt6::Network
        Qt6::Multimedia
    PRIVATE lumen_warnings)


qt_add_executable(lumen-render main.cpp)
target_link_libraries(lumen-render PRIVATE lumen_render_lib lumen_warnings)

# Beside lumen (inside the bundle on macOS), where RenderLink looks for it.
install(TARGETS lumen-render RUNTIME DESTINATION ${LUMEN_INSTALL_BINDIR})
