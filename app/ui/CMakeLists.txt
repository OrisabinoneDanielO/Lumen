# Lumen.Ui QML module. A static library so the app and the tests load the same
# window. Files sit at the module root so their implicit import sees the qmldir
# (needed for the Theme singleton).

set_source_files_properties(Theme.qml SampleData.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(lumen_ui STATIC)
qt_add_qml_module(lumen_ui
    URI Lumen.Ui
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Lumen/Ui
    QML_FILES
        Main.qml
        Theme.qml
        SampleData.qml
        LText.qml
        SectionLabel.qml
        LButton.qml
        LumenLogo.qml
        SearchField.qml
        Toolbar.qml
        ServicePlanPanel.qml
        SlideThumb.qml
        SlideGridPanel.qml
        SlideFrame.qml
        PreviewLivePanel.qml
        LibraryPanel.qml
)
target_link_libraries(lumen_ui
    PUBLIC Qt6::Quick
    PRIVATE lumen_warnings)
