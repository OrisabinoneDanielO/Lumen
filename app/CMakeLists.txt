# Operator UI: the Lumen.Ui QML module (ui/), its C++ glue (operator/) and
# the lumen executable.
add_subdirectory(ui)
add_subdirectory(operator)

# The program's icon: lumen.rc on Windows, an .icns in the macOS bundle.
set(lumen_icon)
if(WIN32)
    set(lumen_icon ${CMAKE_SOURCE_DIR}/packaging/windows/lumen.rc)
elseif(APPLE)
    set(lumen_icon ${CMAKE_SOURCE_DIR}/packaging/icons/lumen-presenter.icns)
    set_source_files_properties(${lumen_icon} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

qt_add_executable(lumen main.cpp ${lumen_icon})
# On macOS the bundle is "Lumen Presenter.app"; elsewhere the program is lumen.
if(APPLE)
    set(lumen_output_name "${LUMEN_BUNDLE_NAME}")
else()
    set(lumen_output_name lumen)
endif()
set_target_properties(lumen PROPERTIES
    OUTPUT_NAME "${lumen_output_name}"
    MACOSX_BUNDLE_ICON_FILE lumen-presenter.icns
    MACOSX_BUNDLE_COPYRIGHT "GPL-3.0"
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Lumen Presenter"
    MACOSX_BUNDLE_GUI_IDENTIFIER org.lumenpresenter.app
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
target_link_libraries(lumen PRIVATE
    Lumen::Core
    Lumen::Show
    Lumen::Operator
    Lumen::Songs
    Lumen::Scripture
    lumen_ui
    lumen_uiplugin
    Qt6::Gui
    Qt6::Quick
    lumen_warnings)

install(TARGETS lumen
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${LUMEN_INSTALL_BINDIR})
add_dependencies(lumen bibles)
