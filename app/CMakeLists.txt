# Operator UI: the Lumen.Ui QML module (ui/) and the lumen executable.
add_subdirectory(ui)

qt_add_executable(lumen main.cpp)
set_target_properties(lumen PROPERTIES
    OUTPUT_NAME lumen
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Lumen Presenter"
    MACOSX_BUNDLE_GUI_IDENTIFIER org.lumenpresenter.app
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
target_link_libraries(lumen PRIVATE
    Lumen::Core
    lumen_ui
    lumen_uiplugin
    Qt6::Gui
    Qt6::Quick
    lumen_warnings)

include(GNUInstallDirs)
install(TARGETS lumen
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
